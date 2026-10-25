// Standard Library
#include <iostream>
#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>

// Vulkan
#include <vulkan/vulkan.h>
#ifndef VULKAN_TARGET_API_VERSION
#error "VULKAN_TARGET_API_VERSION is not defined"
#endif

// Core app components
VkInstance            pInstance       { VK_NULL_HANDLE };
VkPhysicalDevice      pPhysicalDevice { VK_NULL_HANDLE };
VkDevice              pDevice         { VK_NULL_HANDLE };
VkQueue               pQueue          { VK_NULL_HANDLE };
VkCommandPool         pCmdPool        { VK_NULL_HANDLE };
VkCommandBuffer       pCmdBuffer      { VK_NULL_HANDLE };
struct StorageImage   
{                     
    VkImage           pImage          { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory         { VK_NULL_HANDLE };
}                     storageImage    {};
struct DescriptorHeap 
{                     
    VkBuffer          pBuffer         { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory         { VK_NULL_HANDLE };
}                     descriptorHeap  {};
VkPipeline            pPipeline       { VK_NULL_HANDLE };

// Debug/validation components
VkDebugUtilsMessengerEXT pDebugMessenger { VK_NULL_HANDLE };
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Convert the severity and type to a string
    std::string severity;
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:	severity = "VERBOSE"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:      severity = "INFO";    break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:   severity = "WARNING"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:	    severity = "ERROR";   break;
    default:                                                severity = "UNKOWN";  break;
    }
    std::string type;
    switch (messageType)
    {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:       type = "GENERAL";     break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:    type = "VALIDATION";  break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:   type = "PERFORMANCE"; break;
    default:                                                type = "UNKOWN";      break;
    }

    std::cout << "\nVulkan Debug Message\n"
        << "[Severity] " << severity << '\n'
        << "[Type]     " << type << '\n'
        << "[Message]  " << pCallbackData->pMessage << "\n";

    // Boolean indicates if Vulkan call that triggered the validation layer callback
    // should be aborted. VK_TRUE is normally only used to test the validation layers
    // themselves.
    return VK_FALSE;
}

// Debug messenger create info
// -> Required in different places
VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    // This function is necessary because we need two debug messengers,
	// one in general and another one to handle the validation layers for
	// instance creation and destruction.

	// Available severities:
	// VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: Diagnostic message
	// VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: Informational message
	//     like the creation of a resource
	// VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT : Message about
	//     behavior that is not necessarily an error but very likely a bug in the
	//     application
	// VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT : Message about
	//     behavior that is invalid and may cause crashes
	// 
    // Available types:
    // VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: Some event has happened
    //     that is unrelated to the specification or performance
    // VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT : Something has
    //     happened that violates the specification or indicates a possible mistake
    // VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT : Potential nonoptimal
    //     use of Vulkan

	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		  VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType     =
		  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallback;
	createInfo.pUserData       = VK_NULL_HANDLE;
	createInfo.pNext           = VK_NULL_HANDLE;
	createInfo.flags           = 0;

	return createInfo;
}

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
    // Helpers
    auto CheckInstanceExtensionsSupport = [](
        const std::vector<const char*>& enabledExtensionNames)
        {
            // Query the supported extensions
            uint32_t supportedExtensionCount;
            ThrowIfFailed(
                vkEnumerateInstanceExtensionProperties(NULL, &supportedExtensionCount, VK_NULL_HANDLE)
            );
            std::vector<VkExtensionProperties> supportedExtensions(supportedExtensionCount);
            ThrowIfFailed(
                vkEnumerateInstanceExtensionProperties(NULL, &supportedExtensionCount, supportedExtensions.data())
            );

            // Check them against the ones we require
            for (const char* extensionName : enabledExtensionNames)
            {
                // Check
                bool found { false };
                for (const VkExtensionProperties& supportedExtension : supportedExtensions)
                    // String compare
                    if (strcmp(extensionName, supportedExtension.extensionName) == 0)
                    {
                        found = true;
                        break;
                    }

                // Check if the extension was found
                if (!found)
                {
                    std::ostringstream sstream;
                    sstream << "VkInstance extension "
                        << extensionName << " not supported!";
                    throw std::runtime_error(sstream.str());
                }
            }
        };
    auto CheckInstanceLayersSupport = [](
        const std::vector<const char*>& enabledLayerNames)
        {
            // Query the supported layers
            uint32_t supportedLayerCount;
            ThrowIfFailed(
                vkEnumerateInstanceLayerProperties(&supportedLayerCount, VK_NULL_HANDLE)
            );
            std::vector<VkLayerProperties> supportedLayers(supportedLayerCount);
            ThrowIfFailed(
                vkEnumerateInstanceLayerProperties(&supportedLayerCount, supportedLayers.data())
            );

            // Check them against the ones we require
            for (const char* layerName : enabledLayerNames)
            {
                // Check
                bool found{ false };
                for (const VkLayerProperties& supportedLayer : supportedLayers)
                    // String compare
                    if (strcmp(layerName, supportedLayer.layerName) == 0)
                    {
                        found = true;
                        break;
                    }

                // Check if the layer was found
                if (!found)
                {
                    std::ostringstream sstream;
                    sstream << "VkInstance layer "
                        << layerName << " not supported!";
                    throw std::runtime_error(sstream.str());
                }
            }
        };

	// Application info
	VkApplicationInfo appInfo{};
	appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.apiVersion         = VULKAN_TARGET_API_VERSION; // Macro set from target API version set in CMake root
	appInfo.pApplicationName   = "VkDescriptorHeapStorageImageAccess";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName;
	appInfo.engineVersion;
	appInfo.pNext              = VK_NULL_HANDLE;

	// Create info
	VkInstanceCreateInfo createInfo{};
	createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(DEBUG) | defined(_DEBUG)
	// - Extensions
    std::vector<const char*> enabledExtensionNames{
        "VK_EXT_debug_utils"
    };
    CheckInstanceExtensionsSupport(enabledExtensionNames);
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensionNames.size());
    createInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
	// - Layers
    std::vector<const char*> enabledLayerNames{
        "VK_LAYER_KHRONOS_validation"
    };
    CheckInstanceLayersSupport(enabledLayerNames);
    createInfo.enabledLayerCount       = static_cast<uint32_t>(enabledLayerNames.size());
    createInfo.ppEnabledLayerNames     = enabledLayerNames.data();
    // - Next
    //   In debug mode, we also want an additional
    //   messenger that outputs validation layer
    //   messages about the instance creation and
    //   destruction.
    const VkDebugUtilsMessengerCreateInfoEXT debMsgCreateInfo { MakeDebugMessengerCreateInfo() };
    createInfo.pNext                   = reinterpret_cast<const void*>(&debMsgCreateInfo);
#endif
	// - App info
	createInfo.pApplicationInfo        = &appInfo;
	// - Flags
	createInfo.flags = 0u;

	// Creation
	ThrowIfFailed(
        vkCreateInstance(
            &createInfo,
            VK_NULL_HANDLE,
            &pInstance
        )
    );
}
void CreateDebugMessenger()
{
    // Create info
    const VkDebugUtilsMessengerCreateInfoEXT createInfo { MakeDebugMessengerCreateInfo() };

    // Load extension function
    PFN_vkCreateDebugUtilsMessengerEXT CreateDebugUtilsMessengerEXT { VK_NULL_HANDLE };
    CreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(pInstance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (!CreateDebugUtilsMessengerEXT)
        throw std::runtime_error("Unable to load vkCreateDebugUtilsMessengerEXT");

    // Creation
    ThrowIfFailed(
        CreateDebugUtilsMessengerEXT(
            pInstance,
            &createInfo,
            VK_NULL_HANDLE,
            &pDebugMessenger
        )
    );
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
#if defined(DEBUG) | defined(_DEBUG)
    CreateDebugMessenger();
#endif
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
#if defined(DEBUG) | defined(_DEBUG)
    if (pDebugMessenger)
    {
        // Load extension function
        PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT { VK_NULL_HANDLE };
        DestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(pInstance, "vkDestroyDebugUtilsMessengerEXT")
		);
		if (!DestroyDebugUtilsMessengerEXT)
			throw std::runtime_error("Unable to load vkDestroyDebugUtilsMessengerEXT");

        // Destroy
        DestroyDebugUtilsMessengerEXT(
            pInstance,
            pDebugMessenger,
            VK_NULL_HANDLE
        );
    }
#endif
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
    catch (const std::runtime_error& e)
    {
        std::cout << e.what() << '\n';
    }
    catch (...)
    {
        std::cout << "An unknown error occured\n";
    }

    return 0;
}
