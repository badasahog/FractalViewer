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
    uint maxiter = (uint) (MyConstantBuffer.MaxIterations.z * 6);
    uint iter = 0;
    
    float2 z = coord;
    float2 c = MyConstantBuffer.JuliaPos.xy;
    float2 prev = float2(0.0, 0.0);
    
    const float phi = 1.6180339887;

    while (iter < maxiter && dot(z, z) < 16.0)
    {
        float x2 = z.x * z.x;
        float y2 = z.y * z.y;
        float2 z2 = float2(x2 - y2, 2.0 * z.x * z.y);
        
        float r = length(z);
        float theta = atan2(z.y, z.x);
        float spiral = sin(phi * theta - phi * r * 0.3);
        
        z = z2 + c + 0.15 * prev * spiral;
        prev = z2;
        iter++;
    }

    float it = (float) iter;
    float r2 = dot(z, z);
    float smooth = it;
    
    if (r2 > 4.0 && iter < maxiter)
    {
        float log_zn = 0.5 * log(r2);
        float nu = log(log_zn / log(2.0)) / log(2.0);
        smooth = it + 1.0 - nu;
    }
    
    float angle = atan2(z.y, z.x);
    float color_t = frac(smooth / 6.0 + 0.2 * angle / 6.28318);
    
    return color_t;
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
    float2 Coord = WindowLocal.xy * MyConstantBuffer.WindowPos.xy + MyConstantBuffer.WindowPos.zw;

    float ColorIndex = Julia(Coord);
    
    Framebuffer[DTid.xy] = float4(frac(ColorIndex * 1), frac(ColorIndex * 3), frac(ColorIndex * 5), 0);
}
