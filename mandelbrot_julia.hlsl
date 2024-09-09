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
ConstantBuffer<ConstantBufferData> cb : register(b0, space0);

float julia(float2 coord)
{
    uint maxiter = (uint) cb.MaxIterations.z * 4;
    uint iter = 0;
    
    float2 z = coord;
    float2 c = cb.JuliaPos.xy;
    
    while (iter < maxiter && dot(z, z) < 4.0)
    {
        z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        iter++;
    }
    
    return frac((float) iter / cb.MaxIterations.z);
}

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 WindowLocal = ((float2) DTid.xy / cb.MaxIterations.xy) * float2(1, -1) + float2(-0.5f, 0.5f);
    float2 coord = WindowLocal.xy * cb.WindowPos.xy + cb.WindowPos.zw;

    float colorIndex = julia(coord);
    
    framebuffer[DTid.xy] = float4(frac(colorIndex * 1), frac(colorIndex * 3), frac(colorIndex * 5), 0);
    
}
