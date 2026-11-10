// (Bindful) Storage Image
#ifdef __spirv__
[[vk::image_format("r8")]]
#endif
RWTexture2D<float> gStorageImage : register(u0, space0);

// Shader
[numthreads(8, 8, 1)]
void Main(uint2 dispatchID : SV_DispatchThreadID)
{
    // Bounds check
    // TODO: Push image dimensions
    if (dispatchID.x >= 1280 || dispatchID.y >= 720)
        return;
    
    // Write value
    gStorageImage[dispatchID.xy] = 0.5f;
}
