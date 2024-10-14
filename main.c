/*
* (C) 2024 badasahog. All Rights Reserved
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
#include <stdio.h>
#include <math.h>

//c23 compatibility stuff:
#include <stdbool.h>
#define nullptr ((void*)0)

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:dxguid.lib")

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

inline void WaitForPreviousFrame(uint32_t SwapchainBufferIndex);

ID3D12Device* Device;

HANDLE ConsoleHandle;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (hr == 0x887A0005)//device removed
	{
		THROW_ON_FAIL_IMPL(ID3D12Device_GetDeviceRemovedReason(Device), line);
	}

	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			nullptr
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, nullptr, nullptr);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, nullptr, nullptr);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, nullptr, nullptr);
			WriteConsoleA(ConsoleHandle, "\n", 1, nullptr, nullptr);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, nullptr, nullptr);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == nullptr || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

inline void MEMCPY_VERIFY_IMPL(errno_t error, int line)
{
	if (error != 0)
	{
		char buffer[28];
		int stringlength = _snprintf_s(buffer, 28, _TRUNCATE, "memcpy failed on line %i\n", line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, nullptr, nullptr);
		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	}
}

#define MEMCPY_VERIFY(x) MEMCPY_VERIFY_IMPL(x, __LINE__)

#define countof(x) (sizeof(x) / sizeof(x[0]))

uint32_t WindowWidth;
uint32_t WindowHeight;

bool bFullScreen = false;
const bool bWarp = false;
bool bTearingSupport = false;
bool bVsync = false;

LARGE_INTEGER ProcessorFrequency;

#ifdef _DEBUG
ID3D12Debug6* DebugController;
#endif

IDXGISwapChain4* Swapchain;
#define BUFFER_COUNT ((uint32_t)3)
ID3D12Resource* SwapchainBuffers[BUFFER_COUNT];

ID3D12CommandQueue* DirectCommandQueue;
ID3D12CommandAllocator* DirectCommondAllocator;
ID3D12GraphicsCommandList* DirectCommandList;

ID3D12CommandQueue* ComputeCommandQueue;
ID3D12CommandAllocator* ComputeCommondAllocator;
ID3D12GraphicsCommandList* ComputeCommandList;

ID3D12Fence* ComputeFinishedFence[BUFFER_COUNT];
ID3D12Fence* AllClearFence[BUFFER_COUNT];
UINT64 FenceValue[BUFFER_COUNT];
HANDLE FenceEvent;

#define JULIA 0
#define BASE 1

ID3D12Resource* MinimapFrameBuffer;
ID3D12Resource* MainFrameBuffer;

ID3D12RootSignature* RootSignatures[2];

ID3D12DescriptorHeap* DescriptorHeap;

D3D_ROOT_SIGNATURE_VERSION RootSignatureVersion;

SIZE_T cbvDescriptorSize;

struct ConstantBufferData
{
	float MaxIterations[4];
	float WindowPos[4];
	float JuliaPos[4];
};

const int ConstantBufferDataAlignedSize = (sizeof(struct ConstantBufferData) + 255) & ~255;

struct ConstantBufferData defaultCbData = { 0 };
struct ConstantBufferData cbData = { 0 };

ID3D12Resource* StationaryConstantBuffer[BUFFER_COUNT];
ID3D12Resource* MovableConstantBuffer[BUFFER_COUNT];

UINT8* StationaryCbCpuPtr[BUFFER_COUNT];
UINT8* MovableCbCpuPtr[BUFFER_COUNT];

#define FRACTAL_SET_COUNT 3

struct RenderMode
{
	LPWSTR ShaderFileName;
	HANDLE ShaderFileHandle;
	SIZE_T ShaderFileSize;
	HANDLE ShaderFileMap;
	void* ShaderFilePtr;
	ID3D12PipelineState* PipelineState;
};

struct FractalSet
{
	struct RenderMode RenderModes[2];
};

struct FractalSet FractalSets[FRACTAL_SET_COUNT] = { 0 };

#define RENDER_MODE_JULIA 0
#define RENDER_MODE_BASE 1

uint32_t CurrentRenderMode = RENDER_MODE_BASE;

uint32_t CurrentFractalSet = 0;

LRESULT CALLBACK PreInitProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	const HINSTANCE Instance = GetModuleHandleW(nullptr);

	FractalSets[0].RenderModes[JULIA].ShaderFileName = L"mandelbrot_julia.cso";
	FractalSets[0].RenderModes[BASE].ShaderFileName = L"mandelbrot.cso";

	FractalSets[1].RenderModes[JULIA].ShaderFileName = L"tricorn_julia.cso";
	FractalSets[1].RenderModes[BASE].ShaderFileName = L"tricorn.cso";

	FractalSets[2].RenderModes[JULIA].ShaderFileName = L"burningship_julia.cso";
	FractalSets[2].RenderModes[BASE].ShaderFileName = L"burningship.cso";

	{
		RECT WorkArea;
		THROW_ON_FALSE(SystemParametersInfoW(SPI_GETWORKAREA, 0, &WorkArea, 0));
		WindowWidth = WorkArea.right - WorkArea.left;
		WindowHeight = WorkArea.bottom - WorkArea.top;
	}

	defaultCbData.WindowPos[0] = 4.f;
	defaultCbData.WindowPos[1] = 2.25;
	defaultCbData.WindowPos[2] = -.65;
	defaultCbData.WindowPos[3] = 0;

	defaultCbData.MaxIterations[0] = WindowWidth;
	defaultCbData.MaxIterations[1] = WindowHeight;
	defaultCbData.MaxIterations[2] = 700;
	defaultCbData.MaxIterations[3] = 0;

	MEMCPY_VERIFY(memcpy_s(&cbData, sizeof(struct ConstantBufferData), &defaultCbData, sizeof(struct ConstantBufferData)));

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	THROW_ON_FALSE(QueryPerformanceFrequency(&ProcessorFrequency));


	HICON Icon = LoadIconW(nullptr, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(nullptr, IDC_ARROW);
	const LPCTSTR WindowClassName = L"ComputeShaders";

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = &PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = Instance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = nullptr;
	WindowClass.lpszClassName = WindowClassName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	HWND Window = CreateWindowExW(
		0,
		WindowClassName,
		L"D3D Compute Shader",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowWidth,
		WindowHeight,
		nullptr,
		nullptr,
		Instance,
		nullptr);

	VALIDATE_HANDLE(Window);

	if (bFullScreen)
	{
		HMONITOR hmon = MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { 0 };
		mi.cbSize = sizeof(MONITORINFO);
		THROW_ON_FALSE(GetMonitorInfoW(hmon, &mi));

		WindowWidth = mi.rcMonitor.right - mi.rcMonitor.left;
		WindowHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

		defaultCbData.MaxIterations[0] = WindowWidth;
		defaultCbData.MaxIterations[1] = WindowHeight;

		cbData.MaxIterations[0] = WindowWidth;
		cbData.MaxIterations[1] = WindowHeight;

		THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, 0));
		THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, WS_EX_TOPMOST));

		THROW_ON_FALSE(SetWindowPos(Window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED));
	}

	IDXGIFactory6* Factory;

#ifdef _DEBUG
	THROW_ON_FAIL(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, &IID_IDXGIFactory6, &Factory));
#else
	THROW_ON_FAIL(CreateDXGIFactory2(0, &IID_IDXGIFactory6, &Factory));
#endif

	BOOL allowTearing = FALSE;

	THROW_ON_FAIL(IDXGIFactory6_CheckFeatureSupport(
		Factory,
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&allowTearing,
		sizeof(allowTearing)));

	bTearingSupport = (allowTearing == TRUE);

#ifdef _DEBUG
	ID3D12Debug* DebugControllerV1;
	THROW_ON_FAIL(D3D12GetDebugInterface(&IID_ID3D12Debug, &DebugControllerV1));
	THROW_ON_FAIL(ID3D12Debug_QueryInterface(DebugControllerV1, &IID_ID3D12Debug6, &DebugController));
	ID3D12Debug6_SetEnableSynchronizedCommandQueueValidation(DebugController, true);
	ID3D12Debug6_SetForceLegacyBarrierValidation(DebugController, true);
	ID3D12Debug6_SetEnableAutoName(DebugController, true);
	ID3D12Debug6_EnableDebugLayer(DebugController);
	ID3D12Debug6_SetEnableGPUBasedValidation(DebugController, true);
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

	THROW_ON_FAIL(D3D12CreateDevice((IUnknown*)Adapter, D3D_FEATURE_LEVEL_12_1, &IID_ID3D12Device, &Device));

	cbvDescriptorSize = ID3D12Device_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = { 0 };
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		RootSignatureVersion = featureData.HighestVersion;
	}

#ifdef _DEBUG
	ID3D12InfoQueue* InfoQueue;
	THROW_ON_FAIL(ID3D12Device_QueryInterface(Device, &IID_ID3D12InfoQueue, &InfoQueue));

	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE));

	D3D12_MESSAGE_ID MessageIDs[] = {
		D3D12_MESSAGE_ID_DEVICE_CLEARVIEW_EMPTYRECT
	};

	D3D12_INFO_QUEUE_FILTER NewFilter = { 0 };
	NewFilter.DenyList.NumSeverities = 0;
	NewFilter.DenyList.pSeverityList = nullptr;
	NewFilter.DenyList.NumIDs = countof(MessageIDs);
	NewFilter.DenyList.pIDList = MessageIDs;

	THROW_ON_FAIL(ID3D12InfoQueue_PushStorageFilter(InfoQueue, &NewFilter));
#endif

	{
		D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = { 0 };
		DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		DescriptorHeapDesc.NumDescriptors = 2 + 1;//two frame buffers (main + minimap) and constant buffer
		DescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		THROW_ON_FAIL(ID3D12Device_CreateDescriptorHeap(Device, &DescriptorHeapDesc, &IID_ID3D12DescriptorHeap, &DescriptorHeap));
	}

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		CommandQueueDesc.NodeMask = 0;

		THROW_ON_FAIL(ID3D12Device_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &DirectCommandQueue));

#ifdef _DEBUG
		THROW_ON_FAIL(ID3D12CommandQueue_SetName(DirectCommandQueue, L"Direct Command Queue"));
#endif
	}

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		CommandQueueDesc.NodeMask = 0;

		THROW_ON_FAIL(ID3D12Device_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &ComputeCommandQueue));

#ifdef _DEBUG
		THROW_ON_FAIL(ID3D12CommandQueue_SetName(ComputeCommandQueue, L"Compute Command Queue"));
#endif
	}

	THROW_ON_FAIL(ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &DirectCommondAllocator));
	THROW_ON_FAIL(ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, &ComputeCommondAllocator));

	THROW_ON_FAIL(ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, DirectCommondAllocator, nullptr, &IID_ID3D12GraphicsCommandList, &DirectCommandList));
	THROW_ON_FAIL(ID3D12GraphicsCommandList_Close(DirectCommandList));

	THROW_ON_FAIL(ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, ComputeCommondAllocator, nullptr, &IID_ID3D12GraphicsCommandList, &ComputeCommandList));
	THROW_ON_FAIL(ID3D12GraphicsCommandList_Close(ComputeCommandList));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		FenceValue[i] = 0;
		THROW_ON_FAIL(ID3D12Device_CreateFence(Device, FenceValue[i], D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &ComputeFinishedFence[i]));

#ifdef _DEBUG
		wchar_t buffer[17];
		_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Compute Fence %i", i);
		THROW_ON_FAIL(ID3D12Fence_SetName(ComputeFinishedFence[i], buffer));
#endif
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device_CreateFence(Device, FenceValue[i], D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &AllClearFence[i]));

#ifdef _DEBUG
		wchar_t buffer[9];
		_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Fence %i", i);
		THROW_ON_FAIL(ID3D12Fence_SetName(AllClearFence[i], buffer));
#endif
	}

	FenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	VALIDATE_HANDLE(FenceEvent);

	{
		DXGI_MODE_DESC BackBufferDesc = { 0 };
		BackBufferDesc.Width = WindowWidth;
		BackBufferDesc.Height = WindowHeight;
		BackBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		DXGI_SWAP_CHAIN_DESC SwapchainDesc = { 0 };
		SwapchainDesc.BufferCount = BUFFER_COUNT;
		SwapchainDesc.BufferDesc = BackBufferDesc;
		SwapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapchainDesc.OutputWindow = Window;
		SwapchainDesc.SampleDesc.Count = 1;
		SwapchainDesc.SampleDesc.Quality = 0;
		SwapchainDesc.Windowed = TRUE;
		SwapchainDesc.Flags = bTearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		IDXGISwapChain* tempSwapChain;

		THROW_ON_FAIL(IDXGIFactory6_CreateSwapChain(Factory, (IUnknown*)DirectCommandQueue, &SwapchainDesc, &tempSwapChain));

		THROW_ON_FAIL(IDXGISwapChain_QueryInterface(tempSwapChain, &IID_IDXGISwapChain4, &Swapchain));

		THROW_ON_FAIL(IDXGISwapChain_Release(tempSwapChain));

		THROW_ON_FAIL(IDXGIFactory6_MakeWindowAssociation(Factory, Window, DXGI_MWA_NO_ALT_ENTER));
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(IDXGISwapChain4_GetBuffer(Swapchain, i, &IID_ID3D12Resource, &SwapchainBuffers[i]));

#ifdef _DEBUG
		wchar_t buffer[20];
		_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Swapchain Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(SwapchainBuffers[i], buffer));
#endif
	}

	D3D12_RESOURCE_DESC BufferDesc = { 0 };
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

	D3D12_HEAP_PROPERTIES DefaultHeap = { 0 };
	DefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(
		Device,
		&DefaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&BufferDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		&IID_ID3D12Resource,
		&MinimapFrameBuffer));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Resource_SetName(MinimapFrameBuffer, L"Minimap Frame Buffer"));
#endif

	THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(
		Device,
		&DefaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&BufferDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		&IID_ID3D12Resource,
		&MainFrameBuffer));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Resource_SetName(MainFrameBuffer, L"Main Frame Buffer"));
#endif

	D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DescriptorHeap, &CpuDescriptorHandle);

	ID3D12Device_CreateUnorderedAccessView(Device, MinimapFrameBuffer, nullptr, nullptr, CpuDescriptorHandle);
	CpuDescriptorHandle.ptr += cbvDescriptorSize;

	ID3D12Device_CreateUnorderedAccessView(Device, MainFrameBuffer, nullptr, nullptr, CpuDescriptorHandle);
	CpuDescriptorHandle.ptr += cbvDescriptorSize;

	//minimap
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { 0 };
		SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SrvDesc.Texture2D.MipLevels = 1;

		ID3D12Device_CreateShaderResourceView(
			Device,
			MinimapFrameBuffer,
			&SrvDesc,
			CpuDescriptorHandle
		);
		CpuDescriptorHandle.ptr += cbvDescriptorSize;
	}

	for (int i = 0; i < FRACTAL_SET_COUNT; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			FractalSets[i].RenderModes[j].ShaderFileHandle = CreateFileW(
				FractalSets[i].RenderModes[j].ShaderFileName,
				GENERIC_READ,
				0,
				nullptr,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);

			VALIDATE_HANDLE(FractalSets[i].RenderModes[j].ShaderFileHandle);

			LARGE_INTEGER tempLongInteger;

			THROW_ON_FALSE(GetFileSizeEx(FractalSets[i].RenderModes[j].ShaderFileHandle, &tempLongInteger));

			FractalSets[i].RenderModes[j].ShaderFileSize = tempLongInteger.QuadPart;

			FractalSets[i].RenderModes[j].ShaderFileMap = CreateFileMappingW(
				FractalSets[i].RenderModes[j].ShaderFileHandle,
				nullptr,
				PAGE_READONLY,
				0,
				0,
				nullptr);

			VALIDATE_HANDLE(FractalSets[i].RenderModes[j].ShaderFileMap);

			FractalSets[i].RenderModes[j].ShaderFilePtr = MapViewOfFile(FractalSets[i].RenderModes[j].ShaderFileMap, FILE_MAP_READ, 0, 0, 0);
		}
	}

	{
		D3D12_DESCRIPTOR_RANGE1 descRange[1] = { 0 };
		descRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;// u0
		descRange[0].NumDescriptors = 1;
		descRange[0].BaseShaderRegister = 0;
		descRange[0].RegisterSpace = 0;
		descRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
		descRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[2] = { 0 };
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &descRange[0];
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		rootParameters[1].Descriptor.ShaderRegister = 0;
		rootParameters[1].Descriptor.RegisterSpace = 0;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription = { 0 };
		rootSignatureDescription.Version = RootSignatureVersion;
		rootSignatureDescription.Desc_1_1.NumParameters = countof(rootParameters);
		rootSignatureDescription.Desc_1_1.pParameters = rootParameters;
		rootSignatureDescription.Desc_1_1.NumStaticSamplers = 0;
		rootSignatureDescription.Desc_1_1.pStaticSamplers = nullptr;
		rootSignatureDescription.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3D10Blob* rootsig;
		ID3D10Blob* error;

		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&rootSignatureDescription, &rootsig, &error));

		if (rootsig == nullptr)
		{
			void* errorMessage = ID3D10Blob_GetBufferPointer(error);
			int errorMessageLength = ID3D10Blob_GetBufferSize(error);
			WriteConsoleA(ConsoleHandle, errorMessage, errorMessageLength, nullptr, nullptr);
		}

		THROW_ON_FAIL(ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(rootsig), ID3D10Blob_GetBufferSize(rootsig), &IID_ID3D12RootSignature, &RootSignatures[JULIA]));

		for (int i = 0; i < FRACTAL_SET_COUNT; i++)
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineStateDescription = { 0 };
			PipelineStateDescription.pRootSignature = RootSignatures[JULIA];
			PipelineStateDescription.CS.BytecodeLength = FractalSets[i].RenderModes[JULIA].ShaderFileSize;
			PipelineStateDescription.CS.pShaderBytecode = FractalSets[i].RenderModes[JULIA].ShaderFilePtr;
			THROW_ON_FAIL(ID3D12Device_CreateComputePipelineState(Device, &PipelineStateDescription, &IID_ID3D12PipelineState, &FractalSets[i].RenderModes[JULIA].PipelineState));

			THROW_ON_FALSE(UnmapViewOfFile(FractalSets[i].RenderModes[JULIA].ShaderFilePtr));
			THROW_ON_FALSE(CloseHandle(FractalSets[i].RenderModes[JULIA].ShaderFileMap));
			THROW_ON_FALSE(CloseHandle(FractalSets[i].RenderModes[JULIA].ShaderFileHandle));
		}
	}

	{
		D3D12_DESCRIPTOR_RANGE1 descRange[2] = { 0 };
		descRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;// u0
		descRange[0].NumDescriptors = 1;
		descRange[0].BaseShaderRegister = 0;
		descRange[0].RegisterSpace = 0;
		descRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
		descRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		descRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;// t0
		descRange[1].NumDescriptors = 1;
		descRange[1].BaseShaderRegister = 0;
		descRange[1].RegisterSpace = 0;
		descRange[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		descRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[3] = { 0 };
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &descRange[0];
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		rootParameters[1].Descriptor.ShaderRegister = 0;
		rootParameters[1].Descriptor.RegisterSpace = 0;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[2].DescriptorTable.pDescriptorRanges = &descRange[1];
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription = { 0 };
		rootSignatureDescription.Version = RootSignatureVersion;
		rootSignatureDescription.Desc_1_1.NumParameters = countof(rootParameters);
		rootSignatureDescription.Desc_1_1.pParameters = rootParameters;
		rootSignatureDescription.Desc_1_1.NumStaticSamplers = 1;
		rootSignatureDescription.Desc_1_1.pStaticSamplers = &Sampler;
		rootSignatureDescription.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3D10Blob* rootsig;
		ID3D10Blob* error;

		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&rootSignatureDescription, &rootsig, &error));

		if (rootsig == nullptr)
		{
			void* errorMessage = ID3D10Blob_GetBufferPointer(error);
			int errorMessageLength = ID3D10Blob_GetBufferSize(error);
			WriteConsoleA(ConsoleHandle, errorMessage, errorMessageLength, nullptr, nullptr);
			RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}

		THROW_ON_FAIL(ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(rootsig), ID3D10Blob_GetBufferSize(rootsig), &IID_ID3D12RootSignature, &RootSignatures[BASE]));

		for (int i = 0; i < FRACTAL_SET_COUNT; i++)
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineStateDescription = { 0 };
			PipelineStateDescription.pRootSignature = RootSignatures[BASE];
			PipelineStateDescription.CS.BytecodeLength = FractalSets[i].RenderModes[BASE].ShaderFileSize;
			PipelineStateDescription.CS.pShaderBytecode = FractalSets[i].RenderModes[BASE].ShaderFilePtr;
			THROW_ON_FAIL(ID3D12Device_CreateComputePipelineState(Device, &PipelineStateDescription, &IID_ID3D12PipelineState, &FractalSets[i].RenderModes[BASE].PipelineState));
			
			THROW_ON_FALSE(UnmapViewOfFile(FractalSets[i].RenderModes[BASE].ShaderFilePtr));
			THROW_ON_FALSE(CloseHandle(FractalSets[i].RenderModes[BASE].ShaderFileMap));
			THROW_ON_FALSE(CloseHandle(FractalSets[i].RenderModes[BASE].ShaderFileHandle));
		}
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

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

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &StationaryConstantBuffer[i]));

#ifdef _DEBUG
		wchar_t buffer[29];
		_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Stationary Constant Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(StationaryConstantBuffer[i], buffer));
#endif

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &MovableConstantBuffer[i]));

#ifdef _DEBUG
		_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Movable Constant Buffer %i", i);
		THROW_ON_FAIL(ID3D12Resource_SetName(MovableConstantBuffer[i], buffer));
#endif

		D3D12_RANGE ReadRange = { 0 };
		ReadRange.Begin = 0;
		ReadRange.End = 0;

		THROW_ON_FAIL(ID3D12Resource_Map(StationaryConstantBuffer[i], 0, &ReadRange, &StationaryCbCpuPtr[i]));
		THROW_ON_FAIL(ID3D12Resource_Map(MovableConstantBuffer[i], 0, &ReadRange, &MovableCbCpuPtr[i]));

		MEMCPY_VERIFY(memcpy_s(StationaryCbCpuPtr[i], sizeof(struct ConstantBufferData), &cbData, sizeof(struct ConstantBufferData)));
		MEMCPY_VERIFY(memcpy_s(MovableCbCpuPtr[i], sizeof(struct ConstantBufferData), &cbData, sizeof(struct ConstantBufferData)));
	}

	ShowWindow(Window, SW_SHOWMAXIMIZED);

	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	WaitForPreviousFrame(IDXGISwapChain4_GetCurrentBackBufferIndex(Swapchain));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		ID3D12Resource_Unmap(StationaryConstantBuffer[i], 0, nullptr);
		ID3D12Resource_Unmap(MovableConstantBuffer[i], 0, nullptr);

		THROW_ON_FAIL(ID3D12Resource_Release(StationaryConstantBuffer[i]));
		THROW_ON_FAIL(ID3D12Resource_Release(MovableConstantBuffer[i]));
	}

	for (int i = 0; i < FRACTAL_SET_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12PipelineState_Release(FractalSets[i].RenderModes[BASE].PipelineState));
		THROW_ON_FAIL(ID3D12PipelineState_Release(FractalSets[i].RenderModes[JULIA].PipelineState));
	}

	THROW_ON_FAIL(ID3D12RootSignature_Release(RootSignatures[BASE]));
	THROW_ON_FAIL(ID3D12RootSignature_Release(RootSignatures[JULIA]));

	THROW_ON_FAIL(ID3D12Resource_Release(MinimapFrameBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(MainFrameBuffer));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(SwapchainBuffers[i]));
	}

	THROW_ON_FAIL(IDXGISwapChain4_Release(Swapchain));

	THROW_ON_FALSE(CloseHandle(FenceEvent));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(ComputeFinishedFence[i]));
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(AllClearFence[i]));
	}

	THROW_ON_FAIL(ID3D12GraphicsCommandList_Release(DirectCommandList));
	THROW_ON_FAIL(ID3D12CommandAllocator_Release(DirectCommondAllocator));
	THROW_ON_FAIL(ID3D12CommandQueue_Release(DirectCommandQueue));

	THROW_ON_FAIL(ID3D12GraphicsCommandList_Release(ComputeCommandList));
	THROW_ON_FAIL(ID3D12CommandAllocator_Release(ComputeCommondAllocator));
	THROW_ON_FAIL(ID3D12CommandQueue_Release(ComputeCommandQueue));

	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DescriptorHeap));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12InfoQueue_Release(InfoQueue));
#endif

	THROW_ON_FAIL(ID3D12Device_Release(Device));

	THROW_ON_FAIL(IDXGIAdapter1_Release(Adapter));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Debug6_Release(DebugController));
#endif

	THROW_ON_FAIL(IDXGIFactory6_Release(Factory));

	THROW_ON_FALSE(UnregisterClassW(WindowClassName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

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

LRESULT CALLBACK IdleProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
	case WM_ACTIVATE:
		THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
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

LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	static LARGE_INTEGER tickCount = { 0 };

	static bool up = false;
	static bool down = false;
	static bool left = false;
	static bool right = false;
	static bool in = false;
	static bool out = false;
	static bool mouseClicked = false;
	switch (Message)
	{
	case WM_LBUTTONDOWN:
		if (CurrentRenderMode == RENDER_MODE_JULIA)
		{
			MEMCPY_VERIFY(memcpy_s(&cbData, sizeof(struct ConstantBufferData), &defaultCbData, sizeof(struct ConstantBufferData)));
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
			CurrentFractalSet = 0;
			break;
		case '2':
			CurrentFractalSet = 1;
			break;
		case '3':
			CurrentFractalSet = 2;
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
					&cbData,
					sizeof(struct ConstantBufferData),
					&defaultCbData,
					sizeof(struct ConstantBufferData) - sizeof(cbData.JuliaPos)
				));

				THROW_ON_FAIL(ID3D12Device_Evict(Device, 1, &MinimapFrameBuffer));
			}
			else
			{
				CurrentRenderMode = RENDER_MODE_BASE;
				THROW_ON_FAIL(ID3D12Device_MakeResident(Device, 1, &MinimapFrameBuffer));
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
		WaitForPreviousFrame(IDXGISwapChain4_GetCurrentBackBufferIndex(Swapchain));
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		WindowWidth = LOWORD(lParam);
		WindowHeight = HIWORD(lParam);

		defaultCbData.MaxIterations[0] = WindowWidth;
		defaultCbData.MaxIterations[1] = WindowHeight;

		cbData.MaxIterations[0] = WindowWidth;
		cbData.MaxIterations[1] = WindowHeight;

		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(ID3D12Resource_Release(SwapchainBuffers[i]));
		}

		THROW_ON_FAIL(IDXGISwapChain4_ResizeBuffers(
			Swapchain,
			BUFFER_COUNT,
			WindowWidth,
			WindowHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			bTearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
		));

		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(IDXGISwapChain4_GetBuffer(Swapchain, i, &IID_ID3D12Resource, &SwapchainBuffers[i]));

#ifdef _DEBUG
			wchar_t buffer[20];
			_snwprintf_s(buffer, countof(buffer), _TRUNCATE, L"Swapchain Buffer %i", i);
			THROW_ON_FAIL(ID3D12Resource_SetName(SwapchainBuffers[i], buffer));
#endif
		}

		THROW_ON_FAIL(ID3D12Resource_Release(MinimapFrameBuffer));

		D3D12_RESOURCE_DESC BufferDesc = { 0 };
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

		D3D12_HEAP_PROPERTIES DefaultHeap = { 0 };
		DefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(
			Device,
			&DefaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&BufferDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			&IID_ID3D12Resource,
			&MinimapFrameBuffer));

#ifdef _DEBUG
		THROW_ON_FAIL(ID3D12Resource_SetName(MinimapFrameBuffer, L"Minimap Frame Buffer"));
#endif

		THROW_ON_FAIL(ID3D12Resource_Release(MainFrameBuffer));

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(
			Device,
			&DefaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&BufferDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			&IID_ID3D12Resource,
			&MainFrameBuffer));

#ifdef _DEBUG
		THROW_ON_FAIL(ID3D12Resource_SetName(MainFrameBuffer, L"Main Frame Buffer"));
#endif

		D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DescriptorHeap, &CpuDescriptorHandle);

		ID3D12Device_CreateUnorderedAccessView(Device, MinimapFrameBuffer, nullptr, nullptr, CpuDescriptorHandle);
		CpuDescriptorHandle.ptr += cbvDescriptorSize;

		ID3D12Device_CreateUnorderedAccessView(Device, MainFrameBuffer, nullptr, nullptr, CpuDescriptorHandle);
		CpuDescriptorHandle.ptr += cbvDescriptorSize;

		//minimap
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { 0 };
			SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
			SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			SrvDesc.Texture2D.MipLevels = 1;

			ID3D12Device_CreateShaderResourceView(
				Device,
				MinimapFrameBuffer,
				&SrvDesc,
				CpuDescriptorHandle
			);
			CpuDescriptorHandle.ptr += cbvDescriptorSize;
		}
	}
	break;
	case WM_PAINT:
		uint32_t SwapchainBufferIndex = IDXGISwapChain4_GetCurrentBackBufferIndex(Swapchain);
		FenceValue[SwapchainBufferIndex]++;

		LARGE_INTEGER tickCountNow;
		QueryPerformanceCounter(&tickCountNow);
		ULONGLONG tickCountDelta = tickCountNow.QuadPart - tickCount.QuadPart;

		tickCount.QuadPart = tickCountNow.QuadPart;

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(DirectCommondAllocator));
		THROW_ON_FAIL(ID3D12GraphicsCommandList_Reset(DirectCommandList, DirectCommondAllocator, nullptr));

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(ComputeCommondAllocator));
		THROW_ON_FAIL(ID3D12GraphicsCommandList_Reset(ComputeCommandList, ComputeCommondAllocator, nullptr));

		if (up || down || left || right || in || out || mouseClicked)
		{
			const float elapsedTime = (tickCountDelta / ((double)ProcessorFrequency.QuadPart)) * .5f;
			const float ScaleSpeed = 1.f;

			const float zoom = out ? 1.f : (in ? -1.f : 0.f);
			const float x = right ? 1.f : (left ? -1.f : 0.f);
			const float y = up ? 1.f : (down ? -1.f : 0.f);

			const float WindowScale = 1.0f + zoom * ScaleSpeed * elapsedTime;
			cbData.WindowPos[0] *= WindowScale;
			cbData.WindowPos[1] *= WindowScale;
			cbData.WindowPos[2] += cbData.WindowPos[0] * x * elapsedTime * 0.5f;
			cbData.WindowPos[3] += cbData.WindowPos[1] * y * elapsedTime * 0.5f;

			cbData.WindowPos[2] = fmax(fmin(2.0f, cbData.WindowPos[2]), -3.0f);
			cbData.WindowPos[3] = fmax(fmin(1.8f, cbData.WindowPos[3]), -1.8f);
		}

		if (mouseClicked)
		{
			POINT newCursorPos;
			THROW_ON_FALSE(GetCursorPos(&newCursorPos));
			THROW_ON_FALSE(ScreenToClient(Window, &newCursorPos));

			float cx = ((float)newCursorPos.x / cbData.MaxIterations[0]) * 1 + -0.5f;
			cx = cx * cbData.WindowPos[0] + cbData.WindowPos[2];

			float cy = ((float)newCursorPos.y / cbData.MaxIterations[1]) * -1 + 0.5f;
			cy = cy * cbData.WindowPos[1] + cbData.WindowPos[3];

			cbData.JuliaPos[0] = cx;
			cbData.JuliaPos[1] = cy;
		}

		//update the primary screen location
		MEMCPY_VERIFY(memcpy_s(MovableCbCpuPtr[SwapchainBufferIndex], sizeof(struct ConstantBufferData), &cbData, sizeof(struct ConstantBufferData)));

		if (CurrentRenderMode == RENDER_MODE_BASE)
		{
			MEMCPY_VERIFY(memcpy_s(
				&((struct ConstantBufferData*)StationaryCbCpuPtr[SwapchainBufferIndex])->JuliaPos,
				sizeof(cbData.JuliaPos),
				&cbData.JuliaPos,
				sizeof(cbData.JuliaPos)
			));
		}

		if (CurrentRenderMode == RENDER_MODE_BASE)
		{
			ID3D12GraphicsCommandList_SetComputeRootSignature(ComputeCommandList, RootSignatures[JULIA]);
			ID3D12GraphicsCommandList_SetDescriptorHeaps(ComputeCommandList, 1, &DescriptorHeap);

			ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(
				ComputeCommandList,
				1,
				ID3D12Resource_GetGPUVirtualAddress(StationaryConstantBuffer[SwapchainBufferIndex])
			);

			D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle;
			ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DescriptorHeap, &GpuDescriptorHandle);
			
			//render julia to Minimap
			ID3D12GraphicsCommandList_SetPipelineState(ComputeCommandList, FractalSets[CurrentFractalSet].RenderModes[JULIA].PipelineState);

			ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ComputeCommandList, 0, GpuDescriptorHandle);

			ID3D12GraphicsCommandList_Dispatch(ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);

			{
				D3D12_RESOURCE_BARRIER FrameBufferPrecopyBarrier = { 0 };
				FrameBufferPrecopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				FrameBufferPrecopyBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				FrameBufferPrecopyBarrier.Transition.pResource = MinimapFrameBuffer;
				FrameBufferPrecopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				FrameBufferPrecopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				FrameBufferPrecopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				ID3D12GraphicsCommandList_ResourceBarrier(ComputeCommandList, 1, &FrameBufferPrecopyBarrier);
			}

			//render mandelbrot
			ID3D12GraphicsCommandList_SetComputeRootSignature(ComputeCommandList, RootSignatures[BASE]);

			ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(
				ComputeCommandList,
				1,
				ID3D12Resource_GetGPUVirtualAddress(MovableConstantBuffer[SwapchainBufferIndex])
			);

			ID3D12GraphicsCommandList_SetDescriptorHeaps(ComputeCommandList, 1, &DescriptorHeap);
			ID3D12GraphicsCommandList_SetPipelineState(ComputeCommandList, FractalSets[CurrentFractalSet].RenderModes[BASE].PipelineState);
			GpuDescriptorHandle.ptr += cbvDescriptorSize;
			ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ComputeCommandList, 0, GpuDescriptorHandle);

			//set the original julia as a texture input
			GpuDescriptorHandle.ptr += cbvDescriptorSize;
			ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ComputeCommandList, 2, GpuDescriptorHandle);

			ID3D12GraphicsCommandList_Dispatch(ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);

			{
				D3D12_RESOURCE_BARRIER ResourceBarriers[2] = { 0 };
				ResourceBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				ResourceBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				ResourceBarriers[0].Transition.pResource = MinimapFrameBuffer;
				ResourceBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				ResourceBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				ResourceBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				ResourceBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				ResourceBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				ResourceBarriers[1].Transition.pResource = MainFrameBuffer;
				ResourceBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				ResourceBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
				ResourceBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				ID3D12GraphicsCommandList_ResourceBarrier(ComputeCommandList, 2, ResourceBarriers);
			}
		}
		else//render mode julia (no minimap)
		{
			ID3D12GraphicsCommandList_SetComputeRootSignature(ComputeCommandList, RootSignatures[JULIA]);
			ID3D12GraphicsCommandList_SetDescriptorHeaps(ComputeCommandList, 1, &DescriptorHeap);

			ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(
				ComputeCommandList,
				1,
				ID3D12Resource_GetGPUVirtualAddress(MovableConstantBuffer[SwapchainBufferIndex])
			);

			D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle;
			ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DescriptorHeap, &GpuDescriptorHandle);
			GpuDescriptorHandle.ptr += cbvDescriptorSize;

			ID3D12GraphicsCommandList_SetPipelineState(ComputeCommandList, FractalSets[CurrentFractalSet].RenderModes[JULIA].PipelineState);

			ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ComputeCommandList, 0, GpuDescriptorHandle);

			ID3D12GraphicsCommandList_Dispatch(ComputeCommandList, (WindowWidth + 32 - 1) / 32, (WindowHeight + 32 - 1) / 32, 1);

			{
				D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
				ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				ResourceBarrier.Transition.pResource = MainFrameBuffer;
				ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
				ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				ID3D12GraphicsCommandList_ResourceBarrier(ComputeCommandList, 1, &ResourceBarrier);
			}
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList_Close(ComputeCommandList));

		ID3D12CommandQueue_ExecuteCommandLists(ComputeCommandQueue, 1, &ComputeCommandList);
		
		THROW_ON_FAIL(ID3D12CommandQueue_Signal(ComputeCommandQueue, ComputeFinishedFence[SwapchainBufferIndex], FenceValue[SwapchainBufferIndex]));
		THROW_ON_FAIL(ID3D12CommandQueue_Wait(DirectCommandQueue, ComputeFinishedFence[SwapchainBufferIndex], FenceValue[SwapchainBufferIndex]));

		{
			D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			ResourceBarrier.Transition.pResource = SwapchainBuffers[SwapchainBufferIndex];
			ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			ID3D12GraphicsCommandList_ResourceBarrier(DirectCommandList, 1, &ResourceBarrier);
		}

		ID3D12GraphicsCommandList_CopyResource(DirectCommandList, SwapchainBuffers[SwapchainBufferIndex], MainFrameBuffer);

		{
			D3D12_RESOURCE_BARRIER ResourceBarriers[2] = { 0 };
			ResourceBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			ResourceBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			ResourceBarriers[0].Transition.pResource = MainFrameBuffer;
			ResourceBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
			ResourceBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			ResourceBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			ResourceBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			ResourceBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			ResourceBarriers[1].Transition.pResource = SwapchainBuffers[SwapchainBufferIndex];
			ResourceBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			ResourceBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			ResourceBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			ID3D12GraphicsCommandList_ResourceBarrier(DirectCommandList, 2, ResourceBarriers);
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList_Close(DirectCommandList));

		ID3D12CommandQueue_ExecuteCommandLists(DirectCommandQueue, 1, &DirectCommandList);

		THROW_ON_FAIL(IDXGISwapChain4_Present(Swapchain, bVsync ? 1 : 0, (bTearingSupport && !bVsync) ? DXGI_PRESENT_ALLOW_TEARING : 0));

		THROW_ON_FAIL(ID3D12CommandQueue_Signal(DirectCommandQueue, AllClearFence[SwapchainBufferIndex], FenceValue[SwapchainBufferIndex]));

		THROW_ON_FAIL(ID3D12CommandQueue_Wait(ComputeCommandQueue, AllClearFence[SwapchainBufferIndex], FenceValue[SwapchainBufferIndex]));
		WaitForPreviousFrame(SwapchainBufferIndex);

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

inline void WaitForPreviousFrame(const uint32_t SwapchainBufferIndex)
{
	if (ID3D12Fence_GetCompletedValue(AllClearFence[SwapchainBufferIndex]) < FenceValue[SwapchainBufferIndex])
	{
		THROW_ON_FAIL(ID3D12Fence_SetEventOnCompletion(AllClearFence[SwapchainBufferIndex], FenceValue[SwapchainBufferIndex], FenceEvent));
		THROW_ON_FALSE(WaitForSingleObject(FenceEvent, INFINITE) == WAIT_OBJECT_0);
	}
}
