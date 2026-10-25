// Standard Library
#include <iostream>
#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <unordered_map>

// Vulkan
#include <vulkan/vulkan.h>
#ifndef VULKAN_TARGET_API_VERSION
#error "VULKAN_TARGET_API_VERSION is not defined"
#endif

// Core app components
VkInstance            g_pInstance       { VK_NULL_HANDLE };
VkPhysicalDevice      g_pPhysicalDevice { VK_NULL_HANDLE };
VkDevice              g_pDevice         { VK_NULL_HANDLE };
uint32_t              g_QueueFamilyID   {};
VkQueue               g_pQueue          { VK_NULL_HANDLE };
VkCommandPool         g_pCmdPool        { VK_NULL_HANDLE };
VkCommandBuffer       g_pCmdBuffer      { VK_NULL_HANDLE };
struct StorageImage   
{                     
    VkImage           pImage          { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory         { VK_NULL_HANDLE };
}                     g_StorageImage    {};
struct DescriptorHeap 
{                     
    VkBuffer          pBuffer         { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory         { VK_NULL_HANDLE };
}                     g_DescriptorHeap  {};
VkPipeline            g_pPipeline       { VK_NULL_HANDLE };

// Debug/validation components
VkDebugUtilsMessengerEXT g_pDebugMessenger { VK_NULL_HANDLE };
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
            &g_pInstance
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
        vkGetInstanceProcAddr(g_pInstance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (!CreateDebugUtilsMessengerEXT)
        throw std::runtime_error("Unable to load vkCreateDebugUtilsMessengerEXT");

    // Creation
    ThrowIfFailed(
        CreateDebugUtilsMessengerEXT(
            g_pInstance,
            &createInfo,
            VK_NULL_HANDLE,
            &g_pDebugMessenger
        )
    );
}
void PickPhysicalDevice()
{
	// Get the physical devices
	uint32_t deviceCount;
	ThrowIfFailed(vkEnumeratePhysicalDevices(g_pInstance, &deviceCount, VK_NULL_HANDLE));
	std::vector<VkPhysicalDevice> availableDevices;
    availableDevices.resize(deviceCount);
	ThrowIfFailed(vkEnumeratePhysicalDevices(g_pInstance, &deviceCount, availableDevices.data()));

    // Device infos
    struct DeviceInfo
    {
        VkPhysicalDevice pPhysicalDevice { VK_NULL_HANDLE };
        std::string      Name;
    };
    std::unordered_map<uint32_t, DeviceInfo> deviceIDToInfo;
    deviceIDToInfo.reserve(deviceCount);

    // Helpers
    auto CheckDeviceExtensionSupport = [](
        const char* extensionName,
        const std::vector<VkExtensionProperties>& supportedExtensionProperties) -> bool
        {
            for (const VkExtensionProperties& property : supportedExtensionProperties)
                if (strcmp(property.extensionName, extensionName) == 0)
                    return true;

            return false;
        };

	// Iterate the devices
	for (uint32_t devID = 0; devID < deviceCount; ++devID)
	{
        // Init device info
        DeviceInfo deviceInfo{};

		// Get the physical device
        deviceInfo.pPhysicalDevice = availableDevices[devID];

		// Get the physical device properties
		VkPhysicalDeviceProperties devProps;
		vkGetPhysicalDeviceProperties(deviceInfo.pPhysicalDevice, &devProps);

        // Set the name
        deviceInfo.Name = devProps.deviceName;

		// Check if device reports API version >= target version
        if (devProps.apiVersion < VULKAN_TARGET_API_VERSION)
            continue;

        // Query the device extensions this physical device supports
        uint32_t supportedExtensionCount;
        ThrowIfFailed(
            vkEnumerateDeviceExtensionProperties(
                deviceInfo.pPhysicalDevice,
                VK_NULL_HANDLE,
                &supportedExtensionCount,
                VK_NULL_HANDLE
            )
        );
        std::vector<VkExtensionProperties> supportedExtensionProperties(supportedExtensionCount);
        ThrowIfFailed(
            vkEnumerateDeviceExtensionProperties(
                deviceInfo.pPhysicalDevice,
                VK_NULL_HANDLE,
                &supportedExtensionCount,
                supportedExtensionProperties.data()
            )
        );

        // Query the physical device features 2 the device supports
        // - Buffer device address
        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures {};
        supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        // - Descriptor heap
        VkPhysicalDeviceDescriptorHeapFeaturesEXT supportedDescriptorHeapFeatures {};
        supportedDescriptorHeapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
        supportedDescriptorHeapFeatures.pNext = &supportedBufferDeviceAddressFeatures;
        // - Base
        VkPhysicalDeviceFeatures2 supportedPhysicalDeviceFeatures2 {};
        supportedPhysicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedPhysicalDeviceFeatures2.pNext = &supportedDescriptorHeapFeatures;
        // - Query
        vkGetPhysicalDeviceFeatures2(deviceInfo.pPhysicalDevice, &supportedPhysicalDeviceFeatures2);

        // Check if device supports descriptor heap extension
        {
            // Check extensions
            // 
            // - VK_EXT_descriptor_heap
            if (!CheckDeviceExtensionSupport("VK_EXT_descriptor_heap", supportedExtensionProperties))
                continue;
            //
            // - VK_KHR_shader_untyped_pointers
            //   -> This one must be supported alongside VK_EXT_descriptor_heap
            //      but it does not have to be enabled.
            if (!CheckDeviceExtensionSupport("VK_KHR_shader_untyped_pointers", supportedExtensionProperties))
                continue;

            // Check features
            if (!supportedDescriptorHeapFeatures.descriptorHeap ||
                !supportedDescriptorHeapFeatures.descriptorHeapCaptureReplay ||
                !supportedBufferDeviceAddressFeatures.bufferDeviceAddress ||
                !supportedBufferDeviceAddressFeatures.bufferDeviceAddressCaptureReplay)
                continue;
        }

        // Add device info to map
        deviceIDToInfo.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(devID),
            std::forward_as_tuple(deviceInfo)
        );
	}

    // If there is no suitable device
    if (deviceIDToInfo.empty())
        throw std::runtime_error("No suitable device found!");

    // If there is just only one suitable device
    if (deviceIDToInfo.size() == 1u)
    {
        // Get the device info
        const auto& [devID, devInfo] { *deviceIDToInfo.begin() };

        // Message
        std::cout << "Found a single suitable device: " << devInfo.Name << "\n";

        // Set the physical device
        g_pPhysicalDevice = devInfo.pPhysicalDevice;

        return;
    }

    // If there are multiple suitable devices
    {
        // List the suitable devices
        std::cout << "Found multiple suitable devices:\n";
        for (const auto& [devID, devInfo] : deviceIDToInfo)
        {
            std::cout << "- ID: " << devID << " / Name: " << devInfo.Name << '\n';
        }
        std::cout << "\n";

        // Prompt the user to pick
        while (true)
        {
            // Prompt the user to pick the desired device
            std::cout << "Enter the ID of the desired device: ";

            // Get ID
            uint32_t devID;
            
            // 1. Check if the extraction itself failed (e.g., user typed text)
            if (!(std::cin >> devID))
            {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Invalid input! Please enter a valid ID.\n";
                continue;
            }

            // 2. Clear any leftover trailing whitespace/newline from a successful read
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            // 3. Now check if the successfully parsed number exists in the map
            const auto it { deviceIDToInfo.find(devID) };
            if (it == deviceIDToInfo.end())
            {
                std::cout << "Invalid ID! Device not found.\n";
                continue;
            }

            // Set the physical device
            g_pPhysicalDevice = it->second.pPhysicalDevice;
            std::cout << "\n";
            break;
        }
    }
}
void PickQueueFamilyID()
{
	// Query the queue family properties
	uint32_t propertiesCount;
	vkGetPhysicalDeviceQueueFamilyProperties(
		g_pPhysicalDevice,
		&propertiesCount,
		VK_NULL_HANDLE
	);
	std::vector<VkQueueFamilyProperties> queueFamilyProperties(propertiesCount);
	vkGetPhysicalDeviceQueueFamilyProperties(
		g_pPhysicalDevice,
		&propertiesCount,
		queueFamilyProperties.data()
	);

	// Determine the queue family types
	uint32_t id { 0u };
	for (const VkQueueFamilyProperties& properties : queueFamilyProperties)
	{
		// Check for a queue family that supports compute
		if (properties.queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
            g_QueueFamilyID = id;
            return;
		}

		// Increment queue family index
		++id;
	}

    // If we passed the loop, we found no suitable queue family
    throw std::runtime_error("No suitable queue family found.");
}
void CreateDevice()
{
    // Create info
	VkDeviceCreateInfo createInfo{};
	createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	// - Enabled features
	createInfo.pEnabledFeatures        = VK_NULL_HANDLE; // We use VkPhysicalDeviceFeatures2 in pNext, therefore we have to set nullptr here!	
	// - Enabled extensions
    std::vector<const char*> enabledExtensionNames{
        "VK_EXT_descriptor_heap"
    };
	createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensionNames.size());
	createInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
	// - Enabled layers
	//   DEPRECATED: Handled by VkInstance only
	createInfo.enabledLayerCount       = 0;
	createInfo.ppEnabledLayerNames     = VK_NULL_HANDLE;
	// - Queue
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = g_QueueFamilyID;
    queueCreateInfo.queueCount       = 1u;
    const float queuePriority { 1.0f };
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfo.flags            = 0u;
    queueCreateInfo.pNext            = VK_NULL_HANDLE;
    createInfo.queueCreateInfoCount  = 1u;
    createInfo.pQueueCreateInfos     = &queueCreateInfo;
	// - Flags
	createInfo.flags                   = 0;
    // - Physical device features 2
    // -- Buffer device address
    VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddressFeatures{};
    enabledBufferDeviceAddressFeatures.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    enabledBufferDeviceAddressFeatures.bufferDeviceAddress              = VK_TRUE;
    enabledBufferDeviceAddressFeatures.bufferDeviceAddressCaptureReplay = VK_TRUE;
    // -- Descriptor heap
    VkPhysicalDeviceDescriptorHeapFeaturesEXT enabledDescriptorHeapFeatures{};
    enabledDescriptorHeapFeatures.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
    enabledDescriptorHeapFeatures.descriptorHeap              = VK_TRUE;
    enabledDescriptorHeapFeatures.descriptorHeapCaptureReplay = VK_TRUE;
    enabledDescriptorHeapFeatures.pNext                       = &enabledBufferDeviceAddressFeatures;
    // -- Base
    VkPhysicalDeviceFeatures2 enabledPhysicalDeviceFeatures2{};
    enabledPhysicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabledPhysicalDeviceFeatures2.pNext = &enabledDescriptorHeapFeatures;
    // -- Set in pNext chain
	createInfo.pNext                   = &enabledPhysicalDeviceFeatures2;

	// Creation
	ThrowIfFailed(
		vkCreateDevice(
            g_pPhysicalDevice,
			&createInfo,
			VK_NULL_HANDLE,
			&g_pDevice
		)
	);
}
void CreateCommandObjects()
{
    // Retrieve the queue handle
    vkGetDeviceQueue(
        g_pDevice,
        g_QueueFamilyID,
        0u,
        &g_pQueue
    );

    // Create the command pool
    {
        // Create info
		VkCommandPoolCreateInfo createInfo{};
		createInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.queueFamilyIndex = g_QueueFamilyID;
		createInfo.flags            = 0u;
		createInfo.pNext            = VK_NULL_HANDLE;

		// Create
		ThrowIfFailed(
			vkCreateCommandPool(
                g_pDevice,
                &createInfo,
                VK_NULL_HANDLE,
                &g_pCmdPool
            )
		);
    }

    // Allocate the command buffer
    {
		// Allocate info
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool        = g_pCmdPool;
		allocInfo.commandBufferCount = 1u;
		allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.pNext              = VK_NULL_HANDLE;

		// Allocate
		ThrowIfFailed(
			vkAllocateCommandBuffers(
                g_pDevice,
                &allocInfo,
                &g_pCmdBuffer
            )
		);
    }
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
    PickQueueFamilyID();
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
    if (g_pPipeline)
        vkDestroyPipeline(g_pDevice, g_pPipeline, VK_NULL_HANDLE);
    if (g_DescriptorHeap.pMemory)
        vkFreeMemory(g_pDevice, g_DescriptorHeap.pMemory, VK_NULL_HANDLE);
    if (g_DescriptorHeap.pBuffer)
        vkDestroyBuffer(g_pDevice, g_DescriptorHeap.pBuffer, VK_NULL_HANDLE);
    if (g_StorageImage.pMemory)
        vkFreeMemory(g_pDevice, g_StorageImage.pMemory, VK_NULL_HANDLE);
    if (g_StorageImage.pImage)
        vkDestroyImage(g_pDevice, g_StorageImage.pImage, VK_NULL_HANDLE);
    if (g_pCmdPool)
        vkDestroyCommandPool(g_pDevice, g_pCmdPool, VK_NULL_HANDLE);
    if (g_pDevice)
        vkDestroyDevice(g_pDevice, VK_NULL_HANDLE);
#if defined(DEBUG) | defined(_DEBUG)
    if (g_pDebugMessenger)
    {
        // Load extension function
        PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT { VK_NULL_HANDLE };
        DestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(g_pInstance, "vkDestroyDebugUtilsMessengerEXT")
		);
		if (!DestroyDebugUtilsMessengerEXT)
			throw std::runtime_error("Unable to load vkDestroyDebugUtilsMessengerEXT");

        // Destroy
        DestroyDebugUtilsMessengerEXT(
            g_pInstance,
            g_pDebugMessenger,
            VK_NULL_HANDLE
        );
    }
#endif
    if (g_pInstance)
        vkDestroyInstance(g_pInstance, VK_NULL_HANDLE);
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
