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

struct ConstantBufferData
{
    float4 MaxIterations;
    float4 WindowPos;
    float4 JuliaPos;
};

RWTexture2D<float4> framebuffer : register(u0);
Texture2D texture : register(t0);
ConstantBuffer<ConstantBufferData> cb : register(b0, space0);
SamplerState mySampler : register(s0);

float mandelbrot(float2 coord)
{
    uint maxiter = (uint) cb.MaxIterations.z * 4;
    uint iter = 0;
    float2 constant = coord;
    float2 sq;
    do
    {
        float2 newvalue;
        sq = coord * coord;
        newvalue.x = sq.x - sq.y;
        newvalue.y = 2 * coord.y * coord.x;
        coord = newvalue + constant;
        iter++;
    } while (iter < maxiter && (sq.x + sq.y) < 4.0);

    return frac((float) iter / cb.MaxIterations.z);
}

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    bool pixelInMinimap = (((float) DTid.x / cb.MaxIterations.x) > .8) && (((float) DTid.y / cb.MaxIterations.y) < .2);
    //draw the minimap
    
    if (WaveActiveAnyTrue(pixelInMinimap))
    {
        float2 minimapScaleTo1 = float2((((float) DTid.x / cb.MaxIterations.x) - .8) * 5, ((float) DTid.y / cb.MaxIterations.y) * 5);
        
        framebuffer[DTid.xy] = texture.Sample(mySampler, minimapScaleTo1);
    }
    else
    {
        float2 WindowLocal = ((float2) DTid.xy / cb.MaxIterations.xy) * float2(1, -1) + float2(-0.5f, 0.5f);
        float2 coord = WindowLocal.xy * cb.WindowPos.xy + cb.WindowPos.zw;
        
        float colorIndex = mandelbrot(coord);
        framebuffer[DTid.xy] = float4(frac(colorIndex * 1), frac(colorIndex * 3), frac(colorIndex * 5), 0);
    }
}
