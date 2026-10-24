// (Bindful) storage image
#ifdef __spirv__
[[vk::image_format("r8")]]
#endif
RWTexture2D<float> gStorageImage : register(u0, space0);

// Shader
[numthreads(1, 1, 1)]
void Main(uint2 dispatchID : SV_DispatchThreadID)
{    
    // Write value
    gStorageImage[dispatchID.xy] = 0.5f;
}
