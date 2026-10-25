// Standard Library
#include <iostream>
#include <string>
#include <stdexcept>
#include <sstream>

// Vulkan
#include <vulkan/vulkan.h>

// Components
VkInstance         pInstance       { VK_NULL_HANDLE };
VkPhysicalDevice   pPhysicalDevice { VK_NULL_HANDLE };
VkDevice           pDevice         { VK_NULL_HANDLE };
VkQueue            pQueue          { VK_NULL_HANDLE };
VkCommandPool      pCmdPool        { VK_NULL_HANDLE };
VkCommandBuffer    pCmdBuffer      { VK_NULL_HANDLE };
struct StorageImage
{
    VkImage        pImage          { VK_NULL_HANDLE };
    VkDeviceMemory pMemory         { VK_NULL_HANDLE };
}                  storageImage    {};
struct DescriptorHeap
{
    VkBuffer       pBuffer         { VK_NULL_HANDLE };
    VkDeviceMemory pMemory         { VK_NULL_HANDLE };
}                  descriptorHeap  {};
VkPipeline         pPipeline       { VK_NULL_HANDLE };

// Error helper
void ThrowIfFailed(VkResult result)
{
    // Error message
    std::ostringstream sstream;
    sstream << "Vulkan failed with VkResult "
        << std::to_string(result)
        << " at line "
        << __LINE__
        << '\n';

    if (result != VK_SUCCESS)
        throw std::runtime_error(sstream.str());
}

// Init helpers
void CreateInstance()
{

}
void PickPhysicalDevice()
{

}
void CreateDevice()
{
    // Create device

    // Retrieve the queue handle
}
void CreateCommandObjects()
{

}
void CreateStorageImage()
{

}
void CreateDescriptorHeap()
{

}
void CreatePipeline()
{

}

// Init wrapper
void Init()
{
    CreateInstance();
    PickPhysicalDevice();
    CreateDevice();
    CreateCommandObjects();
    CreateStorageImage();
    CreateDescriptorHeap();
    CreatePipeline();
}

// Dispatch
void Dispatch()
{

}

// Clean up
void CleanUp()
{
    if (pPipeline)
        vkDestroyPipeline(pDevice, pPipeline, VK_NULL_HANDLE);
    if (descriptorHeap.pMemory)
        vkFreeMemory(pDevice, descriptorHeap.pMemory, VK_NULL_HANDLE);
    if (descriptorHeap.pBuffer)
        vkDestroyBuffer(pDevice, descriptorHeap.pBuffer, VK_NULL_HANDLE);
    if (storageImage.pMemory)
        vkFreeMemory(pDevice, storageImage.pMemory, VK_NULL_HANDLE);
    if (storageImage.pImage)
        vkDestroyImage(pDevice, storageImage.pImage, VK_NULL_HANDLE);
    if (pCmdPool)
        vkDestroyCommandPool(pDevice, pCmdPool, VK_NULL_HANDLE);
    if (pDevice)
        vkDestroyDevice(pDevice, VK_NULL_HANDLE);
    if (pInstance)
        vkDestroyInstance(pInstance, VK_NULL_HANDLE);
}

int main()
{
    try
    {
        // Init
        Init();

        // Dispatch
        Dispatch();

        // Clean up
        CleanUp();
    }
    catch (const std::exception& e)
    {
        std::cout << e.what() << '\n';
    }
    catch (...)
    {
        std::cout << "An unknown error occured\n";
    }

    return 0;
}
