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

struct ConstantBufferData
{
	float4 MaxIterations;
	float4 WindowPos;
	float4 JuliaPos;
};

RWTexture2D<float4> Framebuffer : register(u0);
ConstantBuffer<ConstantBufferData> MyConstantBuffer : register(b0, space0);

float Julia(float2 coord)
{
    uint maxiter = (uint) MyConstantBuffer.MaxIterations.z * 4;
    uint iter = 0;

    float2 z = coord;
    float2 c = MyConstantBuffer.JuliaPos.xy;

    while (iter < maxiter && dot(z, z) < 4.0)
    {
        // Burning-ship style folding
        z = abs(z);

        // z^2
        float x2 = z.x * z.x;
        float y2 = z.y * z.y;

        // z^3
        float2 z3;
        z3.x = z.x * (x2 - 3.0 * y2);
        z3.y = z.y * (3.0 * x2 - y2);

        // Same skew as base
        z3.x += 0.2 * z3.y;

        z = z3 + c;
        iter++;
    }

    return frac((float) iter / MyConstantBuffer.MaxIterations.z);
}

struct BroadcastPayload
{
	uint2 DispatchGrid : SV_DispatchGrid;
};

[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("thread")]
[NodeId("Entry")]
void main(
	[MaxRecords(1)]
	[NodeId("MyConsumer")]
	NodeOutput<BroadcastPayload> MyConsumer
)
{
	ThreadNodeOutputRecords<BroadcastPayload> outputRecord = MyConsumer.GetThreadNodeOutputRecords(1);
	outputRecord.Get().DispatchGrid = float2(MyConstantBuffer.MaxIterations.x, MyConstantBuffer.MaxIterations.y) / 8;
	outputRecord.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1024, 1024, 1)]
[NumThreads(8, 8, 1)]
[NodeID("MyConsumer")]
void myConsumer(
	uint3 DTid : SV_DispatchThreadID,
	DispatchNodeInputRecord<BroadcastPayload> InputRecord
)
{
	float2 WindowLocal = ((float2) DTid.xy / MyConstantBuffer.MaxIterations.xy) * float2(1, -1) + float2(-0.5f, 0.5f);
	float2 coord = WindowLocal.xy * MyConstantBuffer.WindowPos.xy + MyConstantBuffer.WindowPos.zw;

	float colorIndex = Julia(coord);
	
	Framebuffer[DTid.xy] = float4(frac(colorIndex * 1), frac(colorIndex * 3), frac(colorIndex * 5), 0);
}
