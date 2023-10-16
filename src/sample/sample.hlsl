Texture2DArray<float> _luminance : register(t0, space0);     // SRV: R8_UNORM, plane 0, 2 array slices.
Texture2DArray<float2> _chrominance : register(t1, space0);  // SRV: R8G8_UNORM, plane 1, 2 array slices.
RWStructuredBuffer<float> _output : register(u2, space0);    // 2.880.000 elements for 2 images with 3 channels of size 800*600 each.
SamplerState _sampler : register(s0, space1);                // Static sampler, nothing fancy.

[numthreads(8, 8, 1)]
void kernel_sampleYUV(uint3 id : SV_DispatchThreadId)
{
    const uint width = 800;
    const uint height = 600;
    const uint stride = 800 * 600;
    const uint baseOffset = id.z * stride * 3; // Image 0 outputs at element 0, image 1 outputs at element 1.440.000

    float3 sampleCoords = float3((float)id.x / (float)width, (float)id.y / (float)height, id.z);
    float3 yuv = float3(
        _luminance.SampleLevel(_sampler, sampleCoords, 0),
        _chrominance.SampleLevel(_sampler, sampleCoords, 0)
    );

    _output[(baseOffset + stride * 0) + (id.y * width) + id.x] = yuv.x;
    _output[(baseOffset + stride * 1) + (id.y * width) + id.x] = yuv.y;
    _output[(baseOffset + stride * 2) + (id.y * width) + id.x] = yuv.z;
}