// (Bindful) Storage Image
#ifdef __spirv__
[[vk::image_format("r32ui")]]
#endif
RWTexture2D<uint> gStorageImage : register(u0, space0);

// Shader
[numthreads(8, 8, 1)]
void Main(uint2 dispatchID : SV_DispatchThreadID)
{
    // Query dimensions
    // -> Demonstrates correct descriptor mapping on
    //    the device.
    uint width, height;
    gStorageImage.GetDimensions(width, height);
    
    // Bounds check
    if (dispatchID.x >= width || dispatchID.y >= height)
        return;
    
    // Write value
    // -> width * height
    gStorageImage[dispatchID.xy] = width * height;
}
