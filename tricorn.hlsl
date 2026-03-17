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
Texture2D Texture : register(t0);
ConstantBuffer<ConstantBufferData> MyConstantBuffer : register(b0, space0);
SamplerState MySampler : register(s0);

float Tricorn(float2 Coord)
{
    uint MaxIterations = (uint)MyConstantBuffer.MaxIterations.z * 4;
    uint iter = 0;
    
    float2 z = Coord;
    float2 c = Coord;
    
    while (iter < MaxIterations && dot(z, z) < 4.0f)
    {
        float x = z.x * z.x - z.y * z.y;
        float y = -2.0f * z.x * z.y;
        z = float2(x, y) + c;
        iter++;
    }
    
    return frac((float)iter / MyConstantBuffer.MaxIterations.z);
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
    ThreadNodeOutputRecords<BroadcastPayload> OutputRecord = MyConsumer.GetThreadNodeOutputRecords(1);
	OutputRecord.Get().DispatchGrid = float2(MyConstantBuffer.MaxIterations.x, MyConstantBuffer.MaxIterations.y) / 8;
	OutputRecord.OutputComplete();
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
	bool pixelInMinimap = (((float) DTid.x / MyConstantBuffer.MaxIterations.x) > .8) && (((float) DTid.y / MyConstantBuffer.MaxIterations.y) < .2);
	//draw the minimap
	
	if (WaveActiveAnyTrue(pixelInMinimap))
	{
		float2 MinimapScaleTo1 = float2((((float) DTid.x / MyConstantBuffer.MaxIterations.x) - .8) * 5, ((float) DTid.y / MyConstantBuffer.MaxIterations.y) * 5);
		
		Framebuffer[DTid.xy] = Texture.Sample(MySampler, MinimapScaleTo1);
	}
	else
	{
		float2 WindowLocal = ((float2) DTid.xy / MyConstantBuffer.MaxIterations.xy) * float2(1, -1) + float2(-0.5f, 0.5f);
		float2 Coord = WindowLocal.xy * MyConstantBuffer.WindowPos.xy + MyConstantBuffer.WindowPos.zw;
        Coord *= float2(1, -1);
		
		float ColorIndex = Tricorn(Coord);
		Framebuffer[DTid.xy] = float4(frac(ColorIndex * 1), frac(ColorIndex * 3), frac(ColorIndex * 5), 0);
	}
}
