/*
* (C) 2024-2026 badasahog. All Rights Reserved
* 
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define COBJMACROS
#include <d3d12.h>
#include <dxgi1_6.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#include <d3d12sdklayers.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:dxguid.lib")

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) UINT D3D12SDKVersion = 612;
__declspec(dllexport) char* D3D12SDKPath = ".\\D3D12\\";

HANDLE ConsoleHandle;
ID3D12Device10* Device;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (hr == 0x887A0005)//device removed
	{
		THROW_ON_FAIL_IMPL(ID3D12Device10_GetDeviceRemovedReason(Device), line);
	}

	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			NULL
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, NULL, NULL);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, NULL, NULL);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, NULL, NULL);
			WriteConsoleA(ConsoleHandle, "\n", 1, NULL, NULL);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == NULL || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

inline void MEMCPY_VERIFY_IMPL(errno_t error, int line)
{
	if (error != 0)
	{
		char buffer[28];
		int stringlength = _snprintf_s(buffer, 28, _TRUNCATE, "memcpy failed on line %i\n", line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);
		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define MEMCPY_VERIFY(x) MEMCPY_VERIFY_IMPL(x, __LINE__)

static const bool bWarp = false;

#define BUFFER_COUNT 3u
#define WM_INIT (WM_USER + 1)

struct ConstantBufferData
{
	float MaxIterations[4];
	float WindowPos[4];
	float JuliaPos[4];
};

static const int ConstantBufferDataAlignedSize = (sizeof(struct ConstantBufferData) + 255) & ~255;

enum FractalSet
{
	FRACTAL_SET_MANDELBROT,
	FRACTAL_SET_TRICORN,
	FRACTAL_SET_BURNINGSHIP,
	FRACTAL_SET_COUNT
};

enum FractalType
{
	FRACTAL_TYPE_JULIA,
	FRACTAL_TYPE_BASE,
	FRACTAL_TYPE_COUNT
};

enum RenderMode
{
	RENDER_MODE_JULIA,
	RENDER_MODE_BASE,
	RENDER_MODE_COUNT
};

static const LPCWSTR ShaderFileNames[FRACTAL_SET_COUNT][FRACTAL_TYPE_COUNT] = {
	{ L"mandelbrot_julia.cso",	L"mandelbrot.cso" },
	{ L"tricorn_julia.cso",		L"tricorn.cso" },
	{ L"burningship_julia.cso",	L"burningship.cso" }
};

struct DxObjects
{
	ID3D12PipelineState* PipelineStates[FRACTAL_SET_COUNT][FRACTAL_TYPE_COUNT];
	ID3D12RootSignature* RootSignatures[RENDER_MODE_COUNT];
	IDXGISwapChain4* SwapChain;
	ID3D12Resource* SwapchainBuffers[BUFFER_COUNT];

	ID3D12CommandQueue* DirectCommandQueue;
	ID3D12CommandAllocator* DirectCommandAllocator;
	ID3D12GraphicsCommandList7* DirectCommandList;

	ID3D12CommandQueue* ComputeCommandQueue;
	ID3D12CommandAllocator* ComputeCommandAllocator;
	ID3D12GraphicsCommandList7* ComputeCommandList;

	ID3D12Resource* MinimapFrameBuffer;
	ID3D12Resource* MainFrameBuffer;

	ID3D12DescriptorHeap* DescriptorHeap;
	D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle;
	D3D12_GPU_VIRTUAL_ADDRESS StationaryConstantBufferPtr[BUFFER_COUNT];
	D3D12_GPU_VIRTUAL_ADDRESS MovableConstantBufferPtr[BUFFER_COUNT];
	
	UINT cbvDescriptorSize;

	UINT8* StationaryCbCpuPtr[BUFFER_COUNT];
	UINT8* MovableCbCpuPtr[BUFFER_COUNT];


	ID3D12Fence* ComputeFinishedFence[BUFFER_COUNT];
	ID3D12Fence* AllClearFence[BUFFER_COUNT];
	UINT64 FenceValue[BUFFER_COUNT];
	HANDLE FenceEvent;
	int FrameIndex;
};

LRESULT CALLBACK PreInitProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);
inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects);

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	const HINSTANCE Instance = GetModuleHandleW(NULL);

	int WindowWidth;
	int WindowHeight;

	{
		RECT WindowRect;
		THROW_ON_FALSE(SystemParametersInfoW(SPI_GETWORKAREA, 0, &WindowRect, 0));
		WindowWidth = WindowRect.right - WindowRect.left;
		WindowHeight = WindowRect.bottom - WindowRect.top;
	}

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	HICON Icon = LoadIconW(NULL, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(NULL, IDC_ARROW);
	static const LPCTSTR WindowClassName = L"ComputeShaders";

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = Instance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = NULL;
	WindowClass.lpszClassName = WindowClassName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	HWND Window = CreateWindowExW(
		0UL,
		WindowClassName,
		L"D3D Compute Shader",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowWidth,
		WindowHeight,
		NULL,
		NULL,
		Instance,
		NULL);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));

#ifdef _DEBUG
	ID3D12Debug6* DebugController;

	{
		ID3D12Debug* DebugControllerV1;
		THROW_ON_FAIL(D3D12GetDebugInterface(&IID_ID3D12Debug, &DebugControllerV1));
		THROW_ON_FAIL(ID3D12Debug_QueryInterface(DebugControllerV1, &IID_ID3D12Debug6, &DebugController));
		ID3D12Debug_Release(DebugControllerV1);
	}

	ID3D12Debug6_EnableDebugLayer(DebugController);
	ID3D12Debug6_SetEnableSynchronizedCommandQueueValidation(DebugController, TRUE);
	ID3D12Debug6_SetGPUBasedValidationFlags(DebugController, D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);
	ID3D12Debug6_SetEnableGPUBasedValidation(DebugController, TRUE);
#endif

	IDXGIFactory6* Factory;
#ifdef _DEBUG
	THROW_ON_FAIL(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, &IID_IDXGIFactory6, &Factory));
#else
	THROW_ON_FAIL(CreateDXGIFactory2(0, &IID_IDXGIFactory6, &Factory));
#endif

	IDXGIAdapter1* Adapter;
	if (bWarp)
	{
		THROW_ON_FAIL(IDXGIFactory6_EnumWarpAdapter(Factory, &IID_IDXGIAdapter1, &Adapter));
	}
	else
	{
		THROW_ON_FAIL(IDXGIFactory6_EnumAdapterByGpuPreference(Factory, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, &Adapter));
	}

	THROW_ON_FAIL(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device10, &Device));

	struct DxObjects DxObjects = { 0 };

	DxObjects.cbvDescriptorSize = ID3D12Device10_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

#ifdef _DEBUG
	ID3D12InfoQueue* InfoQueue;
	THROW_ON_FAIL(ID3D12Device10_QueryInterface(Device, &IID_ID3D12InfoQueue, &InfoQueue));

	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
#endif

	{
		D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = { 0 };
		DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		DescriptorHeapDesc.NumDescriptors = 2 + 1;//two frame buffers (main + minimap) and constant buffer
		DescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &DescriptorHeapDesc, &IID_ID3D12DescriptorHeap, &DxObjects.DescriptorHeap));
	}

	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DxObjects.DescriptorHeap, &DxObjects.GpuDescriptorHandle);

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &DxObjects.DirectCommandQueue));
	}

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12CommandQueue_SetName(DxObjects.DirectCommandQueue, L"Direct Command Queue"));
#endif

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &DxObjects.ComputeCommandQueue));
	}

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12CommandQueue_SetName(DxObjects.ComputeCommandQueue, L"Compute Command Queue"));
#endif

	THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &DxObjects.DirectCommandAllocator));
	THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, &DxObjects.ComputeCommandAllocator));

	THROW_ON_FAIL(ID3D12Device10_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, DxObjects.DirectCommandAllocator, NULL, &IID_ID3D12GraphicsCommandList7, &DxObjects.DirectCommandList));
	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects.DirectCommandList));

	THROW_ON_FAIL(ID3D12Device10_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, DxObjects.ComputeCommandAllocator, NULL, &IID_ID3D12GraphicsCommandList7, &DxObjects.ComputeCommandList));
	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects.ComputeCommandList));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		DxObjects.FenceValue[i] = 0;
		THROW_ON_FAIL(ID3D12Device10_CreateFence(Device, DxObjects.FenceValue[i], D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &DxObjects.ComputeFinishedFence[i]));

#ifdef _DEBUG
		wchar_t buffer[17];
		_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Compute Fence %i", i);
		THROW_ON_FAIL(ID3D12Fence_SetName(DxObjects.ComputeFinishedFence[i], buffer));
#endif
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device10_CreateFence(Device, DxObjects.FenceValue[i], D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &DxObjects.AllClearFence[i]));

#ifdef _DEBUG
		wchar_t buffer[9];
		_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Fence %i", i);
		THROW_ON_FAIL(ID3D12Fence_SetName(DxObjects.AllClearFence[i], buffer));
#endif
	}

	DxObjects.FenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	VALIDATE_HANDLE(DxObjects.FenceEvent);

	{
		DXGI_SWAP_CHAIN_DESC SwapchainDesc = { 0 };
		SwapchainDesc.BufferDesc.Width = 1;
		SwapchainDesc.BufferDesc.Height = 1;
		SwapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		SwapchainDesc.SampleDesc.Count = 1;
		SwapchainDesc.SampleDesc.Quality = 0;
		SwapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapchainDesc.BufferCount = BUFFER_COUNT;
		SwapchainDesc.OutputWindow = Window;
		SwapchainDesc.Windowed = TRUE;
		SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		IDXGISwapChain* TempSwapChain;
		THROW_ON_FAIL(IDXGIFactory6_CreateSwapChain(Factory, DxObjects.DirectCommandQueue, &SwapchainDesc, &TempSwapChain));
		THROW_ON_FAIL(IDXGISwapChain_QueryInterface(TempSwapChain, &IID_IDXGISwapChain4, &DxObjects.SwapChain));
		THROW_ON_FAIL(IDXGISwapChain_Release(TempSwapChain));
	}

	THROW_ON_FAIL(IDXGIFactory6_MakeWindowAssociation(Factory, Window, DXGI_MWA_NO_ALT_ENTER));
	THROW_ON_FAIL(IDXGIFactory6_Release(Factory));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(IDXGISwapChain4_GetBuffer(DxObjects.SwapChain, i, &IID_ID3D12Resource, &DxObjects.SwapchainBuffers[i]));

#ifdef _DEBUG
		wchar_t buffer[20];
		_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Swapchain Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(DxObjects.SwapchainBuffers[i], buffer));
#endif
	}

	{
		D3D12_DESCRIPTOR_RANGE1 DescRange = { 0 };
		DescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;// u0
		DescRange.NumDescriptors = 1;
		DescRange.BaseShaderRegister = 0;
		DescRange.RegisterSpace = 0;
		DescRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		DescRange.OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER1 RootParameters[2] = { 0 };
		RootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[0].DescriptorTable.pDescriptorRanges = &DescRange;
		RootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		RootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		RootParameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		RootParameters[1].Descriptor.ShaderRegister = 0;
		RootParameters[1].Descriptor.RegisterSpace = 0;
		RootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootSignatureDescription = { 0 };
		RootSignatureDescription.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		RootSignatureDescription.Desc_1_1.NumParameters = ARRAYSIZE(RootParameters);
		RootSignatureDescription.Desc_1_1.pParameters = RootParameters;
		RootSignatureDescription.Desc_1_1.NumStaticSamplers = 0;
		RootSignatureDescription.Desc_1_1.pStaticSamplers = NULL;
		RootSignatureDescription.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3D10Blob* Rootsig;
		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&RootSignatureDescription, &Rootsig, NULL));
		THROW_ON_FAIL(ID3D12Device10_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(Rootsig), ID3D10Blob_GetBufferSize(Rootsig), &IID_ID3D12RootSignature, &DxObjects.RootSignatures[RENDER_MODE_JULIA]));
		THROW_ON_FAIL(ID3D10Blob_Release(Rootsig));
	}

	{
		D3D12_DESCRIPTOR_RANGE1 DescRange[2] = { 0 };
		DescRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;// u0
		DescRange[0].NumDescriptors = 1;
		DescRange[0].BaseShaderRegister = 0;
		DescRange[0].RegisterSpace = 0;
		DescRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		DescRange[0].OffsetInDescriptorsFromTableStart = 0;

		DescRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;// t0
		DescRange[1].NumDescriptors = 1;
		DescRange[1].BaseShaderRegister = 0;
		DescRange[1].RegisterSpace = 0;
		DescRange[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		DescRange[1].OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER1 RootParameters[3] = { 0 };
		RootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[0].DescriptorTable.pDescriptorRanges = &DescRange[0];
		RootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		RootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		RootParameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		RootParameters[1].Descriptor.ShaderRegister = 0;
		RootParameters[1].Descriptor.RegisterSpace = 0;
		RootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		RootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[2].DescriptorTable.pDescriptorRanges = &DescRange[1];
		RootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_STATIC_SAMPLER_DESC Sampler = { 0 };
		Sampler.Filter = D3D12_FILTER_ANISOTROPIC;
		Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Sampler.MipLODBias = 0;
		Sampler.MaxAnisotropy = 16;
		Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		Sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		Sampler.MinLOD = 0.0f;
		Sampler.MaxLOD = D3D12_FLOAT32_MAX;
		Sampler.ShaderRegister = 0;
		Sampler.RegisterSpace = 0;
		Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootSignatureDescription = { 0 };
		RootSignatureDescription.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		RootSignatureDescription.Desc_1_1.NumParameters = ARRAYSIZE(RootParameters);
		RootSignatureDescription.Desc_1_1.pParameters = RootParameters;
		RootSignatureDescription.Desc_1_1.NumStaticSamplers = 1;
		RootSignatureDescription.Desc_1_1.pStaticSamplers = &Sampler;
		RootSignatureDescription.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3D10Blob* RootSig;
		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&RootSignatureDescription, &RootSig, NULL));
		THROW_ON_FAIL(ID3D12Device10_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(RootSig), ID3D10Blob_GetBufferSize(RootSig), &IID_ID3D12RootSignature, &DxObjects.RootSignatures[RENDER_MODE_BASE]));
		THROW_ON_FAIL(ID3D10Blob_Release(RootSig));
	}

	for (int i = 0; i < FRACTAL_SET_COUNT; i++)
	{
		for (int j = 0; j < FRACTAL_TYPE_COUNT; j++)
		{
			HANDLE ShaderFileHandle = CreateFileW(ShaderFileNames[i][j], GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			VALIDATE_HANDLE(ShaderFileHandle);

			LONGLONG ShaderFileSize;
			THROW_ON_FALSE(GetFileSizeEx(ShaderFileHandle, &ShaderFileSize));

			HANDLE ShaderFileMap = CreateFileMappingW(ShaderFileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(ShaderFileMap);

			const void* ShaderFilePtr = MapViewOfFile(ShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			{
				D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineStateDescription = { 0 };
				PipelineStateDescription.pRootSignature = DxObjects.RootSignatures[j];
				PipelineStateDescription.CS.pShaderBytecode = ShaderFilePtr;
				PipelineStateDescription.CS.BytecodeLength = ShaderFileSize;
				PipelineStateDescription.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
				THROW_ON_FAIL(ID3D12Device10_CreateComputePipelineState(Device, &PipelineStateDescription, &IID_ID3D12PipelineState, &DxObjects.PipelineStates[i][j]));
			}
			
			THROW_ON_FALSE(UnmapViewOfFile(ShaderFilePtr));
			THROW_ON_FALSE(CloseHandle(ShaderFileMap));
			THROW_ON_FALSE(CloseHandle(ShaderFileHandle));
		}
	}

	ID3D12Resource* StationaryConstantBuffer[BUFFER_COUNT];
	ID3D12Resource* MovableConstantBuffer[BUFFER_COUNT];

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC ResourceDescription = { 0 };
		ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDescription.Alignment = 0;
		ResourceDescription.Width = ConstantBufferDataAlignedSize;
		ResourceDescription.Height = 1;
		ResourceDescription.DepthOrArraySize = 1;
		ResourceDescription.MipLevels = 1;
		ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDescription.SampleDesc.Count = 1;
		ResourceDescription.SampleDesc.Quality = 0;
		ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, &StationaryConstantBuffer[i]));

#ifdef _DEBUG
		wchar_t buffer[29];
		_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Stationary Constant Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(StationaryConstantBuffer[i], buffer));
#endif

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, &MovableConstantBuffer[i]));

#ifdef _DEBUG
		_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Movable Constant Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(MovableConstantBuffer[i], buffer));
#endif

		DxObjects.StationaryConstantBufferPtr[i] = ID3D12Resource_GetGPUVirtualAddress(StationaryConstantBuffer[i]);
		DxObjects.MovableConstantBufferPtr[i] = ID3D12Resource_GetGPUVirtualAddress(MovableConstantBuffer[i]);

		THROW_ON_FAIL(ID3D12Resource_Map(StationaryConstantBuffer[i], 0, NULL, &DxObjects.StationaryCbCpuPtr[i]));
		THROW_ON_FAIL(ID3D12Resource_Map(MovableConstantBuffer[i], 0, NULL, &DxObjects.MovableCbCpuPtr[i]));
	}

	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
	
	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_INIT,
		.wParam = (WPARAM)&DxObjects,
		.lParam = 0
	});

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_SIZE,
		.wParam = SIZE_RESTORED,
		.lParam = MAKELONG(WindowWidth, WindowHeight)
	});

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	WaitForPreviousFrame(&DxObjects);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		ID3D12Resource_Unmap(StationaryConstantBuffer[i], 0, NULL);
		ID3D12Resource_Unmap(MovableConstantBuffer[i], 0, NULL);

		THROW_ON_FAIL(ID3D12Resource_Release(StationaryConstantBuffer[i]));
		THROW_ON_FAIL(ID3D12Resource_Release(MovableConstantBuffer[i]));
	}

	for (int i = 0; i < FRACTAL_SET_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12PipelineState_Release(DxObjects.PipelineStates[i][FRACTAL_TYPE_BASE]));
		THROW_ON_FAIL(ID3D12PipelineState_Release(DxObjects.PipelineStates[i][FRACTAL_TYPE_JULIA]));
	}

	THROW_ON_FAIL(ID3D12RootSignature_Release(DxObjects.RootSignatures[RENDER_MODE_BASE]));
	THROW_ON_FAIL(ID3D12RootSignature_Release(DxObjects.RootSignatures[RENDER_MODE_JULIA]));

	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.MinimapFrameBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.MainFrameBuffer));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.SwapchainBuffers[i]));
	}

	THROW_ON_FAIL(IDXGISwapChain4_Release(DxObjects.SwapChain));

	THROW_ON_FALSE(CloseHandle(DxObjects.FenceEvent));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(DxObjects.ComputeFinishedFence[i]));
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(DxObjects.AllClearFence[i]));
	}

	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Release(DxObjects.DirectCommandList));
	THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.DirectCommandAllocator));
	THROW_ON_FAIL(ID3D12CommandQueue_Release(DxObjects.DirectCommandQueue));

	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Release(DxObjects.ComputeCommandList));
	THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.ComputeCommandAllocator));
	THROW_ON_FAIL(ID3D12CommandQueue_Release(DxObjects.ComputeCommandQueue));

	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DxObjects.DescriptorHeap));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12InfoQueue_Release(InfoQueue));
#endif

	THROW_ON_FAIL(ID3D12Device10_Release(Device));

	THROW_ON_FAIL(IDXGIAdapter1_Release(Adapter));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Debug6_Release(DebugController));
#endif

	THROW_ON_FALSE(UnregisterClassW(WindowClassName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

#ifdef _DEBUG
	{
		IDXGIDebug1* dxgiDebug;
		THROW_ON_FAIL(DXGIGetDebugInterface1(0, &IID_IDXGIDebug1, &dxgiDebug));
		THROW_ON_FAIL(IDXGIDebug1_ReportLiveObjects(dxgiDebug, DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
#endif
	return 0;
}

LRESULT CALLBACK PreInitProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, Message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (wParam == SIZE_RESTORED)
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	static bool up = false;
	static bool down = false;
	static bool left = false;
	static bool right = false;
	static bool in = false;
	static bool out = false;
	static bool mouseClicked = false;

	static uint32_t WindowWidth;
	static uint32_t WindowHeight;

	static LARGE_INTEGER ProcessorFrequency;
	static LONGLONG TickCount = 0;

	static bool bFullScreen = false;
	static bool bVsync = false;

	static enum RenderMode CurrentRenderMode = RENDER_MODE_BASE;
	static enum FractalSet CurrentFractalSet = FRACTAL_SET_MANDELBROT;
	
	static struct DxObjects *restrict DxObjects;

	static struct ConstantBufferData DefaultCbData = { 0 };
	static struct ConstantBufferData CbData = { 0 };

	switch (Message)
	{
	case WM_INIT:
		QueryPerformanceFrequency(&ProcessorFrequency);

		DefaultCbData.WindowPos[0] = 4.f;
		DefaultCbData.WindowPos[1] = 2.25;
		DefaultCbData.WindowPos[2] = -.65;
		DefaultCbData.WindowPos[3] = 0;

		DefaultCbData.MaxIterations[2] = 700;
		DefaultCbData.MaxIterations[3] = 0;

		MEMCPY_VERIFY(memcpy_s(&CbData, sizeof(struct ConstantBufferData), &DefaultCbData, sizeof(struct ConstantBufferData)));
		
		DxObjects = ((struct DxObjects*)wParam);
		break;
	case WM_LBUTTONDOWN:
		if (CurrentRenderMode == RENDER_MODE_JULIA)
		{
			MEMCPY_VERIFY(memcpy_s(&CbData, sizeof(struct ConstantBufferData), &DefaultCbData, sizeof(struct ConstantBufferData)));
		}
		mouseClicked = true;
		break;
	case WM_LBUTTONUP:
		mouseClicked = false;
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case '1':
			CurrentFractalSet = FRACTAL_SET_MANDELBROT;
			break;
		case '2':
			CurrentFractalSet = FRACTAL_SET_TRICORN;
			break;
		case '3':
			CurrentFractalSet = FRACTAL_SET_BURNINGSHIP;
			break;
		case 'W':
			up = true;
			break;
		case 'A':
			left = true;
			break;
		case 'S':
			down = true;
			break;
		case 'D':
			right = true;
			break;
		case 'E':
			in = true;
			break;
		case 'Q':
			out = true;
			break;
		case 'V':
			bVsync = !bVsync;
			break;
		case VK_SPACE:
			if (CurrentRenderMode == RENDER_MODE_BASE)
			{
				CurrentRenderMode = RENDER_MODE_JULIA;
				MEMCPY_VERIFY(memcpy_s(
					&CbData,
					sizeof(struct ConstantBufferData),
					&DefaultCbData,
					sizeof(struct ConstantBufferData) - sizeof(CbData.JuliaPos)
				));

				THROW_ON_FAIL(ID3D12Device10_Evict(Device, 1, &DxObjects->MinimapFrameBuffer));
			}
			else
			{
				CurrentRenderMode = RENDER_MODE_BASE;
				THROW_ON_FAIL(ID3D12Device10_MakeResident(Device, 1, &DxObjects->MinimapFrameBuffer));
			}
			break;
		case VK_ESCAPE:
			THROW_ON_FALSE(DestroyWindow(Window));
			PostQuitMessage(0);
			break;
		}
		break;
	case WM_KEYUP:
		switch (wParam)
		{
		case 'W':
			up = false;
			break;
		case 'A':
			left = false;
			break;
		case 'S':
			down = false;
			break;
		case 'D':
			right = false;
			break;
		case 'E':
			in = false;
			break;
		case 'Q':
			out = false;
			break;
		}
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
		{
			bFullScreen = !bFullScreen;

			if (bFullScreen)
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, WS_EX_TOPMOST) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
			else
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, WS_OVERLAPPEDWINDOW) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
		}
		break;
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 300;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		break;
	case WM_SIZE:
	{
		WaitForPreviousFrame(DxObjects);
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		DefaultCbData.MaxIterations[0] = CbData.MaxIterations[0] = WindowWidth = LOWORD(lParam);
		DefaultCbData.MaxIterations[1] = CbData.MaxIterations[1] = WindowHeight = HIWORD(lParam);

		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			MEMCPY_VERIFY(memcpy_s(DxObjects->StationaryCbCpuPtr[i], sizeof(struct ConstantBufferData), &CbData, sizeof(struct ConstantBufferData)));
			MEMCPY_VERIFY(memcpy_s(DxObjects->MovableCbCpuPtr[i], sizeof(struct ConstantBufferData), &CbData, sizeof(struct ConstantBufferData)));
		}
		
		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->SwapchainBuffers[i]));
		}

		THROW_ON_FAIL(IDXGISwapChain4_ResizeBuffers(
			DxObjects->SwapChain,
			BUFFER_COUNT,
			WindowWidth,
			WindowHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		));

		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(IDXGISwapChain4_GetBuffer(DxObjects->SwapChain, i, &IID_ID3D12Resource, &DxObjects->SwapchainBuffers[i]));

#ifdef _DEBUG
			wchar_t buffer[20];
			_snwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, L"Swapchain Buffer %i", i);
			THROW_ON_FAIL(ID3D12Resource_SetName(DxObjects->SwapchainBuffers[i], buffer));
#endif
		}

		if (DxObjects->MinimapFrameBuffer)
		{
			THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->MinimapFrameBuffer));
			THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->MainFrameBuffer));
		}

		{
			D3D12_HEAP_PROPERTIES DefaultHeap = { 0 };
			DefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
			DefaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			DefaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			D3D12_RESOURCE_DESC1 BufferDesc = { 0 };
			BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			BufferDesc.Alignment = 0;
			BufferDesc.Width = WindowWidth;
			BufferDesc.Height = WindowHeight;
			BufferDesc.DepthOrArraySize = 1;
			BufferDesc.MipLevels = 1;
			BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			BufferDesc.SampleDesc.Count = 1;
			BufferDesc.SampleDesc.Quality = 0;
			BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			BufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&DefaultHeap,
				D3D12_HEAP_FLAG_NONE,
				&BufferDesc,
				D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&DxObjects->MinimapFrameBuffer));

#ifdef _DEBUG
			THROW_ON_FAIL(ID3D12Resource_SetName(DxObjects->MinimapFrameBuffer, L"Minimap Frame Buffer"));
#endif

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&DefaultHeap,
				D3D12_HEAP_FLAG_NONE,
				&BufferDesc,
				D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&DxObjects->MainFrameBuffer));

#ifdef _DEBUG
			THROW_ON_FAIL(ID3D12Resource_SetName(DxObjects->MainFrameBuffer, L"Main Frame Buffer"));
#endif
		}

		D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DxObjects->DescriptorHeap, &CpuDescriptorHandle);

		ID3D12Device10_CreateUnorderedAccessView(Device, DxObjects->MinimapFrameBuffer, NULL, NULL, CpuDescriptorHandle);
		CpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;

		ID3D12Device10_CreateUnorderedAccessView(Device, DxObjects->MainFrameBuffer, NULL, NULL, CpuDescriptorHandle);
		CpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;

		//minimap
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { 0 };
			SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
			SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			SrvDesc.Texture2D.MipLevels = 1;

			ID3D12Device10_CreateShaderResourceView(
				Device,
				DxObjects->MinimapFrameBuffer,
				&SrvDesc,
				CpuDescriptorHandle
			);
		}
		CpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;
	}
	break;
	case WM_PAINT:
		WaitForPreviousFrame(DxObjects);
		DxObjects->FenceValue[DxObjects->FrameIndex]++;

		LONGLONG TickCountNow;
		QueryPerformanceCounter(&TickCountNow);
		ULONGLONG TickCountDelta = TickCountNow - TickCount;

		TickCount = TickCountNow;

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(DxObjects->DirectCommandAllocator));
		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Reset(DxObjects->DirectCommandList, DxObjects->DirectCommandAllocator, NULL));

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(DxObjects->ComputeCommandAllocator));
		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Reset(DxObjects->ComputeCommandList, DxObjects->ComputeCommandAllocator, NULL));

		if (up || down || left || right || in || out || mouseClicked)
		{
			const float ElapsedTime = (TickCountDelta / ((double)ProcessorFrequency.QuadPart)) * .5f;
			const float ScaleSpeed = 1.f;

			const float Zoom = out ? 1.f : (in ? -1.f : 0.f);
			const float x = right ? 1.f : (left ? -1.f : 0.f);
			const float y = up ? 1.f : (down ? -1.f : 0.f);

			const float WindowScale = 1.0f + Zoom * ScaleSpeed * ElapsedTime;
			CbData.WindowPos[0] *= WindowScale;
			CbData.WindowPos[1] *= WindowScale;
			CbData.WindowPos[2] += CbData.WindowPos[0] * x * ElapsedTime * 0.5f;
			CbData.WindowPos[3] += CbData.WindowPos[1] * y * ElapsedTime * 0.5f;

			CbData.WindowPos[2] = fmax(fmin(2.0f, CbData.WindowPos[2]), -3.0f);
			CbData.WindowPos[3] = fmax(fmin(1.8f, CbData.WindowPos[3]), -1.8f);
		}

		if (mouseClicked)
		{
			POINT NewCursorPos;
			THROW_ON_FALSE(GetCursorPos(&NewCursorPos));
			THROW_ON_FALSE(ScreenToClient(Window, &NewCursorPos));

			float cx = ((float)NewCursorPos.x / CbData.MaxIterations[0]) * 1 + -0.5f;
			cx = cx * CbData.WindowPos[0] + CbData.WindowPos[2];

			float cy = ((float)NewCursorPos.y / CbData.MaxIterations[1]) * -1 + 0.5f;
			cy = cy * CbData.WindowPos[1] + CbData.WindowPos[3];

			CbData.JuliaPos[0] = cx;
			CbData.JuliaPos[1] = cy;
		}

		//update the primary screen location
		MEMCPY_VERIFY(memcpy_s(DxObjects->MovableCbCpuPtr[DxObjects->FrameIndex], sizeof(struct ConstantBufferData), &CbData, sizeof(struct ConstantBufferData)));

		if (CurrentRenderMode == RENDER_MODE_BASE)
		{
			MEMCPY_VERIFY(memcpy_s(
				&((struct ConstantBufferData*)DxObjects->StationaryCbCpuPtr[DxObjects->FrameIndex])->JuliaPos,
				sizeof(CbData.JuliaPos),
				&CbData.JuliaPos,
				sizeof(CbData.JuliaPos)
			));
		}

		D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle = DxObjects->GpuDescriptorHandle;
		if (CurrentRenderMode == RENDER_MODE_BASE)
		{
			ID3D12GraphicsCommandList7_SetComputeRootSignature(DxObjects->ComputeCommandList, DxObjects->RootSignatures[RENDER_MODE_JULIA]);
			ID3D12GraphicsCommandList7_SetDescriptorHeaps(DxObjects->ComputeCommandList, 1, &DxObjects->DescriptorHeap);

			ID3D12GraphicsCommandList7_SetComputeRootConstantBufferView(DxObjects->ComputeCommandList, 1, DxObjects->StationaryConstantBufferPtr[DxObjects->FrameIndex]);
			
			//render julia to Minimap
			ID3D12GraphicsCommandList7_SetPipelineState(DxObjects->ComputeCommandList, DxObjects->PipelineStates[CurrentFractalSet][FRACTAL_TYPE_JULIA]);

			ID3D12GraphicsCommandList7_SetComputeRootDescriptorTable(DxObjects->ComputeCommandList, 0, GpuDescriptorHandle);

			ID3D12GraphicsCommandList7_Dispatch(DxObjects->ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);

			{
				D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
				TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
				TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
				TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
				TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
				TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE;
				TextureBarrier.pResource = DxObjects->MinimapFrameBuffer;
				TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

				D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
				ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
				ResourceBarrier.NumBarriers = 1;
				ResourceBarrier.pTextureBarriers = &TextureBarrier;
				ID3D12GraphicsCommandList7_Barrier(DxObjects->ComputeCommandList, 1, &ResourceBarrier);
			}

			//render mandelbrot
			ID3D12GraphicsCommandList7_SetComputeRootSignature(DxObjects->ComputeCommandList, DxObjects->RootSignatures[RENDER_MODE_BASE]);

			ID3D12GraphicsCommandList7_SetComputeRootConstantBufferView(DxObjects->ComputeCommandList, 1, DxObjects->MovableConstantBufferPtr[DxObjects->FrameIndex]);

			ID3D12GraphicsCommandList7_SetDescriptorHeaps(DxObjects->ComputeCommandList, 1, &DxObjects->DescriptorHeap);
			ID3D12GraphicsCommandList7_SetPipelineState(DxObjects->ComputeCommandList, DxObjects->PipelineStates[CurrentFractalSet][FRACTAL_TYPE_BASE]);
			GpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;
			ID3D12GraphicsCommandList7_SetComputeRootDescriptorTable(DxObjects->ComputeCommandList, 0, GpuDescriptorHandle);

			//set the original julia as a texture input
			GpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;
			ID3D12GraphicsCommandList7_SetComputeRootDescriptorTable(DxObjects->ComputeCommandList, 2, GpuDescriptorHandle);

			ID3D12GraphicsCommandList7_Dispatch(DxObjects->ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);

			{
				D3D12_TEXTURE_BARRIER TextureBarriers[2] = { 0 };
				TextureBarriers[0].SyncBefore = D3D12_BARRIER_SYNC_ALL;
				TextureBarriers[0].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
				TextureBarriers[0].AccessBefore = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
				TextureBarriers[0].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				TextureBarriers[0].LayoutBefore = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE;
				TextureBarriers[0].LayoutAfter = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
				TextureBarriers[0].pResource = DxObjects->MinimapFrameBuffer;
				TextureBarriers[0].Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD;

				TextureBarriers[1].SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
				TextureBarriers[1].SyncAfter = D3D12_BARRIER_SYNC_COPY;
				TextureBarriers[1].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				TextureBarriers[1].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
				TextureBarriers[1].LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
				TextureBarriers[1].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
				TextureBarriers[1].pResource = DxObjects->MainFrameBuffer;
				TextureBarriers[1].Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

				D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
				ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
				ResourceBarrier.NumBarriers = 2;
				ResourceBarrier.pTextureBarriers = TextureBarriers;
				ID3D12GraphicsCommandList7_Barrier(DxObjects->ComputeCommandList, 1, &ResourceBarrier);
			}
		}
		else//render mode julia (no minimap)
		{
			ID3D12GraphicsCommandList7_SetComputeRootSignature(DxObjects->ComputeCommandList, DxObjects->RootSignatures[RENDER_MODE_JULIA]);
			ID3D12GraphicsCommandList7_SetDescriptorHeaps(DxObjects->ComputeCommandList, 1, &DxObjects->DescriptorHeap);

			ID3D12GraphicsCommandList7_SetComputeRootConstantBufferView(DxObjects->ComputeCommandList, 1, DxObjects->MovableConstantBufferPtr[DxObjects->FrameIndex]);

			GpuDescriptorHandle.ptr += DxObjects->cbvDescriptorSize;

			ID3D12GraphicsCommandList7_SetPipelineState(DxObjects->ComputeCommandList, DxObjects->PipelineStates[CurrentFractalSet][FRACTAL_TYPE_JULIA]);

			ID3D12GraphicsCommandList7_SetComputeRootDescriptorTable(DxObjects->ComputeCommandList, 0, GpuDescriptorHandle);

			ID3D12GraphicsCommandList7_Dispatch(DxObjects->ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);
			
			{
				D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
				TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
				TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
				TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
				TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
				TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
				TextureBarrier.pResource = DxObjects->MainFrameBuffer;
				TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

				D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
				ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
				ResourceBarrier.NumBarriers = 1;
				ResourceBarrier.pTextureBarriers = &TextureBarrier;
				ID3D12GraphicsCommandList7_Barrier(DxObjects->ComputeCommandList, 1, &ResourceBarrier);
			}
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects->ComputeCommandList));

		ID3D12CommandQueue_ExecuteCommandLists(DxObjects->ComputeCommandQueue, 1, &DxObjects->ComputeCommandList);
		
		THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->ComputeCommandQueue, DxObjects->ComputeFinishedFence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex]));
		THROW_ON_FAIL(ID3D12CommandQueue_Wait(DxObjects->DirectCommandQueue, DxObjects->ComputeFinishedFence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex]));

		{
			D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
			TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
			TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
			TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
			TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST;
			TextureBarrier.pResource = DxObjects->SwapchainBuffers[DxObjects->FrameIndex];
			TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 1;
			ResourceBarrier.pTextureBarriers = &TextureBarrier;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->DirectCommandList, 1, &ResourceBarrier);
		}

		ID3D12GraphicsCommandList7_CopyResource(DxObjects->DirectCommandList, DxObjects->SwapchainBuffers[DxObjects->FrameIndex], DxObjects->MainFrameBuffer);

		{
			D3D12_TEXTURE_BARRIER TextureBarriers[2] = { 0 };
			TextureBarriers[0].SyncBefore = D3D12_BARRIER_SYNC_COPY;
			TextureBarriers[0].SyncAfter = D3D12_BARRIER_SYNC_ALL;
			TextureBarriers[0].AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
			TextureBarriers[0].AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarriers[0].LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
			TextureBarriers[0].LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarriers[0].pResource = DxObjects->SwapchainBuffers[DxObjects->FrameIndex];
			TextureBarriers[0].Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			TextureBarriers[1].SyncBefore = D3D12_BARRIER_SYNC_COPY;
			TextureBarriers[1].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
			TextureBarriers[1].AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
			TextureBarriers[1].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
			TextureBarriers[1].LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
			TextureBarriers[1].LayoutAfter = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
			TextureBarriers[1].pResource = DxObjects->MainFrameBuffer;
			TextureBarriers[1].Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 2;
			ResourceBarrier.pTextureBarriers = TextureBarriers;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->DirectCommandList, 1, &ResourceBarrier);
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects->DirectCommandList));

		ID3D12CommandQueue_ExecuteCommandLists(DxObjects->DirectCommandQueue, 1, &DxObjects->DirectCommandList);

		THROW_ON_FAIL(IDXGISwapChain4_Present(DxObjects->SwapChain, bVsync ? 1 : 0, !bVsync ? DXGI_PRESENT_ALLOW_TEARING : 0));

		THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->DirectCommandQueue, DxObjects->AllClearFence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex]));

		THROW_ON_FAIL(ID3D12CommandQueue_Wait(DxObjects->ComputeCommandQueue, DxObjects->AllClearFence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex]));
		break;

	case WM_DESTROY:
		THROW_ON_FALSE(DestroyWindow(Window));
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, Message, wParam, lParam);
	}
	return 0;
}

inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects)
{
	DxObjects->FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects->SwapChain);
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->DirectCommandQueue, DxObjects->AllClearFence[DxObjects->FrameIndex], ++DxObjects->FenceValue[DxObjects->FrameIndex]));

	if (ID3D12Fence_GetCompletedValue(DxObjects->AllClearFence[DxObjects->FrameIndex]) < DxObjects->FenceValue[DxObjects->FrameIndex])
	{
		THROW_ON_FAIL(ID3D12Fence_SetEventOnCompletion(DxObjects->AllClearFence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex], DxObjects->FenceEvent));
		THROW_ON_FALSE(WaitForSingleObject(DxObjects->FenceEvent, INFINITE) == WAIT_OBJECT_0);
	}
}
