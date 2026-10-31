// Standard Library
#include <iostream>
#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <fstream>

// Vulkan
#include <vulkan/vulkan.h>
#ifndef VULKAN_TARGET_API_VERSION
#error "VULKAN_TARGET_API_VERSION is not defined"
#endif

// Core app components
VkInstance                              g_pInstance                  { VK_NULL_HANDLE };
VkPhysicalDevice                        g_pPhysicalDevice            { VK_NULL_HANDLE };
VkDevice                                g_pDevice                    { VK_NULL_HANDLE };
uint32_t                                g_QueueFamilyID              {};
VkQueue                                 g_pQueue                     { VK_NULL_HANDLE };
VkCommandPool                           g_pCmdPool                   { VK_NULL_HANDLE };
VkCommandBuffer                         g_pCmdBuffer                 { VK_NULL_HANDLE };
struct Image                                                  
{                                                                    
    VkImage                             pImage                       { VK_NULL_HANDLE };
    VkDeviceMemory                      pMemory                      { VK_NULL_HANDLE };
}                                       g_Image                      {};
struct DescriptorHeap 
{                     
    VkBuffer                            pBuffer                      { VK_NULL_HANDLE };
    VkDeviceMemory                      pMemory                      { VK_NULL_HANDLE };
    uint8_t*                            pHeapStartHostMemoryAddress  { nullptr };
    uint64_t                            HeapStartDeviceMemoryAddress { 0ull };
    uint64_t                            HeapSizeInBytes              { 0ull };
    uint64_t                            ReservedRangeOffsetInBytes   { 0ull };
    uint64_t                            ReservedRangeSizeInBytes     { 0ull };
    uint64_t                            ImageDescRangeOffsetInBytes  { 0ull };
    uint64_t                            ImageDescSizeInBytes         { 0ull };
    uint32_t                            PushDataOffset               { 0u };
    VkDescriptorSetAndBindingMappingEXT SetAndBindingMapping         {};
}                                       g_DescriptorHeap             {};
VkPipeline                              g_pPipeline                  { VK_NULL_HANDLE };

// Instance extension functions
PFN_vkSetDebugUtilsObjectNameEXT    g_vkSetDebugUtilsObjectNameEXT    { VK_NULL_HANDLE };
#if defined(DEBUG) | defined(_DEBUG)
PFN_vkCreateDebugUtilsMessengerEXT  g_vkCreateDebugUtilsMessengerEXT  { VK_NULL_HANDLE };
PFN_vkDestroyDebugUtilsMessengerEXT g_vkDestroyDebugUtilsMessengerEXT { VK_NULL_HANDLE };
#endif

// Device extension functions
PFN_vkCmdBindResourceHeapEXT      g_vkCmdBindResourceHeapEXT      { VK_NULL_HANDLE };
PFN_vkWriteResourceDescriptorsEXT g_vkWriteResourceDescriptorsEXT { VK_NULL_HANDLE };
PFN_vkCmdPushDataEXT              g_vkCmdPushDataEXT              { VK_NULL_HANDLE };

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

// Memory type helper
uint32_t FindMemoryTypeID(
    uint32_t allowedMemoryTypeBits,
    VkMemoryPropertyFlags requiredMemoryPropertyFlags,
    VkMemoryPropertyFlags preferredMemoryPropertyFlags,
    VkMemoryPropertyFlags preferredNotMemoryPropertyFlags,
    VkPhysicalDevice pPhysicalDevice)
{
    constexpr uint32_t MAX_UINT32 { std::numeric_limits<uint32_t>().max() };

    // Query the device memory properties just once
    static VkPhysicalDeviceMemoryProperties memProperties = [pPhysicalDevice]()
        {
            // Query
            VkPhysicalDeviceMemoryProperties2 memProperties2{};
            memProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            vkGetPhysicalDeviceMemoryProperties2(pPhysicalDevice, &memProperties2);

            // Return the memory properties
            return memProperties2.memoryProperties;
        }();

    // ID of the best-fitting memory type
    uint32_t bestMemTypeID {  MAX_UINT32 };
	// Best memory type score
	int      bestScore     { -1 };
	
	// Iterate over the memory type IDs
	for (uint32_t memTypeID = 0u; memTypeID < memProperties.memoryTypeCount; ++memTypeID)
	{
		// Check if this memory type ID is allowed for the allocation
		// -> If the bit at position i is set, the memory type at ID i
		//    is allowed.
		if ((allowedMemoryTypeBits & (1u << memTypeID)) != 0)
		{
			// Get the property flags of this memory type.
			const VkMemoryPropertyFlags propertyFlags{
				memProperties.memoryTypes[memTypeID].propertyFlags
			};

			// Check the type's properties against our required properties.
			// -> Check that all bits are set!
			if ((propertyFlags & requiredMemoryPropertyFlags) != requiredMemoryPropertyFlags)
				// If the required properties are not met, continue searching.
				continue;

			// Init the score
			int score { 0 };

			// Check the type's properties against our preferred properties.
			// -> Check that all bits are set!
			if ((propertyFlags & preferredMemoryPropertyFlags) == preferredMemoryPropertyFlags)
				// Add to the score
				// -> Preferred weighs higher than preferred-not.
				score += 2;

			// Check the type's properties against our preferred-not properties.
			// -> Check that all bits are NOT set!
			if ((propertyFlags & preferredNotMemoryPropertyFlags) == 0)
				// Add to the score
				score += 1;
			
			// Check the score and update the type ID if
			// surpassed.
			if (score > bestScore)
			{
				bestScore     = score;
				bestMemTypeID = memTypeID;
			}
		}
	}

    // If our requirements were not met, throw
    if (bestMemTypeID == MAX_UINT32)
        throw std::runtime_error(
            "Unable to find memory heap with required properties."
        );

    return bestMemTypeID;
}

// Object naming helper
void SetObjectName(
    uint64_t hObject,
    VkObjectType objectType,
    const std::string& name,
    VkDevice pDevice)
{
    // Name info
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType   = objectType;
    nameInfo.objectHandle = hObject;
    nameInfo.pObjectName  = name.c_str();
    nameInfo.pNext        = VK_NULL_HANDLE;

    // Set name
    ThrowIfFailed(
        g_vkSetDebugUtilsObjectNameEXT(
            pDevice,
            &nameInfo
        )
    );
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
	// - Extensions
    std::vector<const char*> enabledExtensionNames{
        "VK_EXT_debug_utils"
    };
    CheckInstanceExtensionsSupport(enabledExtensionNames);
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensionNames.size());
    createInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
#if defined(DEBUG) | defined(_DEBUG)
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
//
void LoadInstanceExtensionFunctions()
{
    // API object/handling naming
    g_vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(g_pInstance, "vkSetDebugUtilsObjectNameEXT")
        );
    if (!g_vkSetDebugUtilsObjectNameEXT)
        throw std::runtime_error("Unable to load vkSetDebugUtilsObjectNameEXT");

#if defined(DEBUG) | defined(_DEBUG)
    // Create debug utils messenger
    g_vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(g_pInstance, "vkCreateDebugUtilsMessengerEXT")
        );
    if (!g_vkCreateDebugUtilsMessengerEXT)
        throw std::runtime_error("Unable to load vkCreateDebugUtilsMessengerEXT");
    
    // Destroy debug utils messenger
    g_vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(g_pInstance, "vkDestroyDebugUtilsMessengerEXT")
        );
    if (!g_vkDestroyDebugUtilsMessengerEXT)
        throw std::runtime_error("Unable to load vkDestroyDebugUtilsMessengerEXT");
#endif
}
//
void CreateDebugMessenger()
{
    // Create info
    const VkDebugUtilsMessengerCreateInfoEXT createInfo { MakeDebugMessengerCreateInfo() };

    // Creation
    ThrowIfFailed(
        g_vkCreateDebugUtilsMessengerEXT(
            g_pInstance,
            &createInfo,
            VK_NULL_HANDLE,
            &g_pDebugMessenger
        )
    );
}
//
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
        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures{};
        supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        // - Descriptor heap
        VkPhysicalDeviceDescriptorHeapFeaturesEXT supportedDescriptorHeapFeatures{};
        supportedDescriptorHeapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
        supportedDescriptorHeapFeatures.pNext = &supportedBufferDeviceAddressFeatures;
        // - Synchronization 2
        VkPhysicalDeviceSynchronization2Features supportedSynchronization2Features{};
        supportedSynchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        supportedSynchronization2Features.pNext = &supportedDescriptorHeapFeatures;
        // - Base
        VkPhysicalDeviceFeatures2 supportedPhysicalDeviceFeatures2{};
        supportedPhysicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedPhysicalDeviceFeatures2.pNext = &supportedSynchronization2Features;
        // - Query
        vkGetPhysicalDeviceFeatures2(deviceInfo.pPhysicalDevice, &supportedPhysicalDeviceFeatures2);

        // Check if device supports descriptor heap extension and features
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

        // Check if device supports synchronization 2 features
        {
            // -> No extension required, part of core since Vulkan 1.3
            // Check features
            if (!supportedSynchronization2Features.synchronization2)
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
        std::cout << "\n";
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
//
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
//
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
    // -- Synchronization 2
    VkPhysicalDeviceSynchronization2Features enabledSynchronization2Features{};
    enabledSynchronization2Features.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    enabledSynchronization2Features.synchronization2 = VK_TRUE;
    enabledSynchronization2Features.pNext            = &enabledDescriptorHeapFeatures;
    // -- Base
    VkPhysicalDeviceFeatures2 enabledPhysicalDeviceFeatures2{};
    enabledPhysicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabledPhysicalDeviceFeatures2.pNext = &enabledSynchronization2Features;
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
//
void LoadDeviceExtensionFunctions()
{
    // Bind resource heap
    g_vkCmdBindResourceHeapEXT = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
        vkGetDeviceProcAddr(g_pDevice, "vkCmdBindResourceHeapEXT")
        );
    if (!g_vkCmdBindResourceHeapEXT)
        throw std::runtime_error("Unable to load vkCmdBindResourceHeapEXT");
    
    // Write resource descriptors
    g_vkWriteResourceDescriptorsEXT = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(g_pDevice, "vkWriteResourceDescriptorsEXT")
        );
    if (!g_vkWriteResourceDescriptorsEXT)
        throw std::runtime_error("Unable to load vkWriteResourceDescriptorsEXT");
    
    // Push data
    g_vkCmdPushDataEXT = reinterpret_cast<PFN_vkCmdPushDataEXT>(
        vkGetDeviceProcAddr(g_pDevice, "vkCmdPushDataEXT")
        );
    if (!g_vkCmdPushDataEXT)
        throw std::runtime_error("Unable to load vkCmdPushDataEXT");
}
//
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
//
void CreateDescriptorHeap()
{
    // Alignment helper
    auto AlignUp = [](uint64_t value, uint64_t alignment)
        {
            return alignment ? ((value + alignment - 1) / alignment) * alignment : value;
        };

    // Keep track of the current heap offset, and thereby size
    uint64_t currentHeapOffsetInBytes { 0ull };

    VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptorHeapProperties{};
    descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

    // Account for reserved range
    {
        // Query VkPhysicalDeviceDescriptorHeapPropertiesEXT
	    VkPhysicalDeviceProperties2 physicalDeviceProperties2{};
	    physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	    physicalDeviceProperties2.pNext = &descriptorHeapProperties;
	    vkGetPhysicalDeviceProperties2(
	    	g_pPhysicalDevice,
	    	&physicalDeviceProperties2
	    );

	    // Memory heap alignment
        // -> We do not need it since we allocate a dedicated memory page
        //    for the buffer, hence, the alignment is satisfied.
        // -> This alignment must be satisfied for the device memory address
        //    that is bound for the descriptor heap.
	    descriptorHeapProperties.resourceHeapAlignment;

	    // Account for the reserved range
	    // -> The driver/implementation requires a slice of the bound heap
	    //    memory for its own internal state tracking, embedded descriptors,
	    //    or hardware constraints.
	    // -> For a resource heap, reservedRangeOffset during binding has to be
	    //    aligned to both:
	    //    - bufferDescriptorAlignment
	    //    - imageDescriptorAlignment
	    //    -> Aligning is not necessary if we place the range at the start of
	    //       the heap but we keep this logic in case we ever move the range.
	    currentHeapOffsetInBytes = AlignUp(
	    	currentHeapOffsetInBytes,
	    	std::max(
	    		descriptorHeapProperties.bufferDescriptorAlignment,
	    		descriptorHeapProperties.imageDescriptorAlignment
	    	)
	    );
	    g_DescriptorHeap.ReservedRangeOffsetInBytes = currentHeapOffsetInBytes;
	    g_DescriptorHeap.ReservedRangeSizeInBytes   = descriptorHeapProperties.minResourceHeapReservedRange;
	    //
	    // - Offset the current heap offset
	    currentHeapOffsetInBytes += g_DescriptorHeap.ReservedRangeSizeInBytes;
    }

    // Account for descriptor
    {
        // -> We need a resource descriptor heap with only a single
        //    image descriptor

        // Align current offset to image descriptor alignment
        currentHeapOffsetInBytes = AlignUp(
            currentHeapOffsetInBytes,
            descriptorHeapProperties.imageDescriptorAlignment
        );

        // Set image descriptor range offset
        // -> This is where we write the descriptor
        g_DescriptorHeap.ImageDescRangeOffsetInBytes = currentHeapOffsetInBytes;

        // Set image descriptor size
        g_DescriptorHeap.ImageDescSizeInBytes = descriptorHeapProperties.imageDescriptorSize;

        // -> We do not need the imageDescriptorAlignment an longer since
        //    we only write a single descriptor in this app.

        // Adjust the current heap offset for the descriptor size
        currentHeapOffsetInBytes += g_DescriptorHeap.ImageDescSizeInBytes;
    }

    // Set descriptor heap size
    g_DescriptorHeap.HeapSizeInBytes = currentHeapOffsetInBytes;

    // Create buffer
    {
        // Create info
        VkBufferCreateInfo createInfo{};
	    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	    createInfo.size                  = g_DescriptorHeap.HeapSizeInBytes;
	    createInfo.usage                 = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;;
	    createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
	    createInfo.queueFamilyIndexCount = 0;
	    createInfo.pQueueFamilyIndices   = VK_NULL_HANDLE;
	    createInfo.flags                 = 0u;
	    createInfo.pNext                 = VK_NULL_HANDLE;

        // Creation
        ThrowIfFailed(
            vkCreateBuffer(
                g_pDevice,
                &createInfo,
                VK_NULL_HANDLE,
                &g_DescriptorHeap.pBuffer
            )
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_DescriptorHeap.pBuffer),
            VK_OBJECT_TYPE_BUFFER,
            "Buffer: Descriptor Heap",
            g_pDevice
        );
    }

    // Allocate memory
    {
        // We allocate a dedicated memory block for a single resource
        // -> Alignment is satisfied

        // Get the memory requirements of the image
        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(
            g_pDevice,
            g_DescriptorHeap.pBuffer,
            &memoryRequirements
        );

        // Find memory type ID
        // -> We require host-visible + host-coherent memory.
        //    In addition, we prefer this memory to be device-local,
        //    i.e., on discrete GPUs to be GPU upload memory.
        const uint32_t memoryTypeID{
             FindMemoryTypeID(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0u,
                g_pPhysicalDevice
            )
        };

        // Allocate info
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.memoryTypeIndex = memoryTypeID;
        allocInfo.allocationSize  = memoryRequirements.size;
        // - Allocate flags info
	    //   -> We have to set the VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT flag to query a buffer's device address
	    //      via vkGetBufferDeviceAddress and use that address inside shaders.
	    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
	    allocFlagsInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	    allocFlagsInfo.flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
	    allocFlagsInfo.deviceMask = 1u; // Single device -> Only one device in "group";
	    allocFlagsInfo.pNext      = VK_NULL_HANDLE;
	    // - Add to pNext chain
	    allocInfo.pNext           = &allocFlagsInfo;

        // Allocation
        ThrowIfFailed(
            vkAllocateMemory(
                g_pDevice,
                &allocInfo,
                VK_NULL_HANDLE,
                &g_DescriptorHeap.pMemory
            )
        );

        // Map memory to host
        ThrowIfFailed(
            vkMapMemory(
                g_pDevice,
                g_DescriptorHeap.pMemory,
                0ull,
                g_DescriptorHeap.HeapSizeInBytes,
                0u,
                reinterpret_cast<void**>(& g_DescriptorHeap.pHeapStartHostMemoryAddress)
            )
        );
    }

    // Bind memory
    {
        // Bind info
        VkBindBufferMemoryInfo bindInfo{};
        bindInfo.sType        = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
        bindInfo.buffer       = g_DescriptorHeap.pBuffer;
        bindInfo.memory       = g_DescriptorHeap.pMemory;
        bindInfo.memoryOffset = 0ull;
        bindInfo.pNext        = VK_NULL_HANDLE;

        // Bind
        ThrowIfFailed(
            vkBindBufferMemory2(
                g_pDevice,
                1u,
                &bindInfo
            )
        );
    }

    // Get the heap start device address
    {
        // Address info
        VkBufferDeviceAddressInfo deviceAddressInfo{};
        deviceAddressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        deviceAddressInfo.buffer = g_DescriptorHeap.pBuffer;
        deviceAddressInfo.pNext  = VK_NULL_HANDLE;

        // Query
        g_DescriptorHeap.HeapStartDeviceMemoryAddress = vkGetBufferDeviceAddress(
            g_pDevice,
            &deviceAddressInfo
        );

        // Make sure alignment is satisfied
        // -> We only add this here for completeness.
        //    This check will get passed no matter what since we allocated
        //    a dedicated page and bound the buffer at offset 0.
        if (g_DescriptorHeap.HeapStartDeviceMemoryAddress % descriptorHeapProperties.resourceHeapAlignment)
            throw std::runtime_error(
                "Descriptor heap start's device memory address "
                "does not adhere to resourceHeapAlignment."
            );
    }

    // Set up descriptor set and binding mapping + push data
    {
        g_DescriptorHeap.SetAndBindingMapping.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
        g_DescriptorHeap.SetAndBindingMapping.descriptorSet = 0u;
        g_DescriptorHeap.SetAndBindingMapping.bindingCount  = 1u;
        g_DescriptorHeap.SetAndBindingMapping.firstBinding  = 0u;
        g_DescriptorHeap.SetAndBindingMapping.resourceMask  = VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT;
        g_DescriptorHeap.SetAndBindingMapping.pNext         = VK_NULL_HANDLE;

#if defined(CONSTANT_OFFSET_SET_AND_BINDING_MAPPING)
        // CONSTANT OFFSET

        g_DescriptorHeap.SetAndBindingMapping.source                                    = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
        // -> Instead of pushing the offset, we set it statically
        g_DescriptorHeap.SetAndBindingMapping.sourceData.constantOffset.heapOffset      = g_DescriptorHeap.ImageDescRangeOffsetInBytes;
        // - Byte stride between descriptors, but we only have single descriptor anyway
        g_DescriptorHeap.SetAndBindingMapping.sourceData.constantOffset.heapArrayStride = g_DescriptorHeap.ImageDescSizeInBytes;
#else
        // PUSH INDEX

        g_DescriptorHeap.SetAndBindingMapping.source                               = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
        // -> In general, we have to use byte offsets here since each semantic
        //    can have different byte strides, so there is no universal byte
        //    stride for a descriptor.
		//    The final offset is computed as follows:
		//        pushIndex   = ((uint32_t*)pPushData)[pushOffset / 4]
		//        shaderIndex = (Binding - firstBinding) + arrayIndex
		//        offset      = heapOffset + pushIndex * heapIndexStride + shaderIndex * heapArrayStride
        //
        // - 0: There is only a single entry in our push data
        g_DescriptorHeap.SetAndBindingMapping.sourceData.pushIndex.pushOffset      = g_DescriptorHeap.PushDataOffset;
        // - We use byte strides
        g_DescriptorHeap.SetAndBindingMapping.sourceData.pushIndex.heapIndexStride = 1u;
        // - Byte stride between descriptors, but we only have single descriptor anyway
        g_DescriptorHeap.SetAndBindingMapping.sourceData.pushIndex.heapArrayStride = g_DescriptorHeap.ImageDescSizeInBytes;
        // - No additional offset to the pushed offset
        g_DescriptorHeap.SetAndBindingMapping.sourceData.pushIndex.heapOffset      = 0u;
#endif

        // Max push data size
        // -> We only have set a single 4-byte value
        if (descriptorHeapProperties.maxPushDataSize < 4u)
            throw std::runtime_error("Application push data exceeds maxPushDataSize.");
    }
}
//
void CreateImage()
{
    // Create the image
    {
        // -> 1x1 2D image of R8_UNORM format with storage usage

        // Create info
        VkImageCreateInfo createInfo{};
	    createInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	    createInfo.imageType             = VK_IMAGE_TYPE_2D;
	    createInfo.extent.width          = 1u;
	    createInfo.extent.height         = 1u;
	    createInfo.extent.depth          = 1u;
	    createInfo.arrayLayers           = 1u;
	    createInfo.mipLevels             = 1u;
	    createInfo.format                = VK_FORMAT_R8_UNORM;
	    createInfo.usage                 = VK_IMAGE_USAGE_STORAGE_BIT;
	    createInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;
	    createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
	    createInfo.queueFamilyIndexCount = 0;
	    createInfo.pQueueFamilyIndices   = VK_NULL_HANDLE;
	    createInfo.samples               = VK_SAMPLE_COUNT_1_BIT;
	    createInfo.tiling                = VK_IMAGE_TILING_OPTIMAL;
	    createInfo.flags                 = 0u;
	    createInfo.pNext                 = VK_NULL_HANDLE;

        // Creation
        ThrowIfFailed(
            vkCreateImage(
                g_pDevice,
                &createInfo,
                VK_NULL_HANDLE,
                &g_Image.pImage
            )
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_Image.pImage),
            VK_OBJECT_TYPE_IMAGE,
            "Image: Test Image",
            g_pDevice
        );
    }

    // Allocate memory
    {
        // We allocate a dedicated memory block for a single resource
        // -> Alignment is satisfied

        // Get the memory requirements of the image
        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(
            g_pDevice,
            g_Image.pImage,
            &memoryRequirements
        );

        // Find memory type ID
        // -> We require device-local memory and prefer that
        //    it is not host-visible.
        const uint32_t memoryTypeID{
             FindMemoryTypeID(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0u,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                g_pPhysicalDevice
            )
        };

        // Allocate info
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.memoryTypeIndex = memoryTypeID;
        allocInfo.allocationSize  = memoryRequirements.size;
        allocInfo.pNext           = VK_NULL_HANDLE;

        // Allocation
        ThrowIfFailed(
            vkAllocateMemory(
                g_pDevice,
                &allocInfo,
                VK_NULL_HANDLE,
                &g_Image.pMemory
            )
        );
    }

    // Bind memory
    {
        // Bind info
        VkBindImageMemoryInfo bindInfo{};
        bindInfo.sType        = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bindInfo.image        = g_Image.pImage;
        bindInfo.memory       = g_Image.pMemory;
        bindInfo.memoryOffset = 0ull;
        bindInfo.pNext        = VK_NULL_HANDLE;

        // Bind
        ThrowIfFailed(
            vkBindImageMemory2(
                g_pDevice,
                1u,
                &bindInfo
            )
        );
    }

    // Write descriptor
    {
        // Image view create info
		VkImageViewCreateInfo viewCreateInfo{};
		viewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image                           = g_Image.pImage;
		viewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format                          = VK_FORMAT_R8_UNORM;
		viewCreateInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
		// - Subresource
		viewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;
		viewCreateInfo.subresourceRange.baseArrayLayer = 0u;
		viewCreateInfo.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
		viewCreateInfo.subresourceRange.baseMipLevel   = 0u;
		viewCreateInfo.flags                           = 0u;
        // - Usage create info
        VkImageViewUsageCreateInfo usageCreateInfo{};
        usageCreateInfo.sType                          = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        usageCreateInfo.usage                          = VK_IMAGE_USAGE_STORAGE_BIT;
        usageCreateInfo.pNext                          = VK_NULL_HANDLE;
		viewCreateInfo.pNext                           = &usageCreateInfo;

		// Image descriptor info
		VkImageDescriptorInfoEXT imageDescriptorInfo{};
		imageDescriptorInfo.sType  = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
		imageDescriptorInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
		imageDescriptorInfo.pView  = &viewCreateInfo;
		imageDescriptorInfo.pNext  = VK_NULL_HANDLE;;

		// Descriptor info
		VkResourceDescriptorInfoEXT resourceDescriptorInfo{};
		resourceDescriptorInfo.sType       = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
		resourceDescriptorInfo.type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		resourceDescriptorInfo.data.pImage = &imageDescriptorInfo;
		resourceDescriptorInfo.pNext       = VK_NULL_HANDLE;

		// Host address range
		VkHostAddressRangeEXT hostAddressRange{};
		hostAddressRange.address = g_DescriptorHeap.pHeapStartHostMemoryAddress + g_DescriptorHeap.ImageDescRangeOffsetInBytes;
		hostAddressRange.size    = g_DescriptorHeap.ImageDescSizeInBytes;

        // Write
        ThrowIfFailed(
            g_vkWriteResourceDescriptorsEXT(
                g_pDevice,
                1u,
                &resourceDescriptorInfo,
                &hostAddressRange
            )
        );
    }
}
//
void CreatePipeline()
{
    // Helper
    auto LoadBinaryFile = [](
        const std::string& filePath) -> std::vector<char>
        {
            // Open as binary file and advance the pointer to the end.
            std::ifstream ifstream(filePath, std::ios::binary | std::ios::ate);

            // Check if file is open.
            if (!ifstream.is_open())
            {
                std::ostringstream sstream;
                sstream << "Failed to open binary file \"" << filePath << "\"!";
                throw std::runtime_error(sstream.str());
            }

            // Get the file size.
            const uint64_t fileSize{
                static_cast<uint64_t>(ifstream.tellg())
            };

            // Array to hold the binary data
            std::vector<char> binaryData;
            binaryData.resize(fileSize);

            // Reset the file pointer.
            ifstream.seekg(0u);

            // Read the binary data into the array.
            ifstream.read(binaryData.data(), fileSize);

            // Close the file.
            ifstream.close();

            // Return the binary data.
            return binaryData;
        };

    // Shader module
    VkShaderModule pShaderModule { VK_NULL_HANDLE };
    {
        // Load binary
        std::vector<char> shaderBinary{
            LoadBinaryFile(SHADER_BIN_FILE_PATH)
        };

        // Create info
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = shaderBinary.size();
		createInfo.pCode    = reinterpret_cast<const uint32_t*>(shaderBinary.data());
		createInfo.flags    = 0u;
		createInfo.pNext    = VK_NULL_HANDLE;

		// Creation
		ThrowIfFailed(
			vkCreateShaderModule(
				g_pDevice,
				&createInfo,
				VK_NULL_HANDLE,
				&pShaderModule
			)
		);
    }

    // Shader stage
    VkPipelineShaderStageCreateInfo               shaderStageCreateInfo    {};
    VkShaderDescriptorSetAndBindingMappingInfoEXT setAndBindingMappingInfo {};
    {
        // Set and binding mapping info
	    setAndBindingMappingInfo.sType        = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
	    setAndBindingMappingInfo.mappingCount = 1u;
	    setAndBindingMappingInfo.pMappings    = &g_DescriptorHeap.SetAndBindingMapping;
	    setAndBindingMappingInfo.pNext        = VK_NULL_HANDLE;

        // Shader stage
        shaderStageCreateInfo.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStageCreateInfo.pName               = "Main";
		shaderStageCreateInfo.module              = pShaderModule;
		shaderStageCreateInfo.pSpecializationInfo = VK_NULL_HANDLE;
		shaderStageCreateInfo.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
		shaderStageCreateInfo.flags               = 0u;
		// -> Add set and binding mapping info to pNext chain.
		//    It replaces pipeline layout when descriptor heaps
		//    are used.
		shaderStageCreateInfo.pNext               = &setAndBindingMappingInfo;
    }

    // Compute pipeline create info
    VkComputePipelineCreateInfo pipelineCreateInfo{};
    {
        pipelineCreateInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	    // - No pipeline layout with VK_EXT_descriptor_heap
	    //   Instead, set and binding mapping is added to pNext chain
	    //   of shader stages.
        pipelineCreateInfo.layout             = VK_NULL_HANDLE;
        pipelineCreateInfo.stage              = shaderStageCreateInfo;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex  = 0u;
        pipelineCreateInfo.flags              = 0u;
	    // - Create flags 2
        VkPipelineCreateFlags2CreateInfo createFlags2{};
        createFlags2.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
        createFlags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
        createFlags2.pNext = VK_NULL_HANDLE;
	    pipelineCreateInfo.pNext              = &createFlags2;
    }

    // Create
    ThrowIfFailed(
        vkCreateComputePipelines(
            g_pDevice,
            VK_NULL_HANDLE,
            1u,
            &pipelineCreateInfo,
            VK_NULL_HANDLE,
            &g_pPipeline
        )
    );

    // Destroy shader module
    if (pShaderModule)
        vkDestroyShaderModule(
            g_pDevice,
            pShaderModule,
            VK_NULL_HANDLE
        );
}

// Init wrapper
void Init()
{
    CreateInstance();
    LoadInstanceExtensionFunctions();
#if defined(DEBUG) | defined(_DEBUG)
    CreateDebugMessenger();
#endif
    PickPhysicalDevice();
    PickQueueFamilyID();
    CreateDevice();
    LoadDeviceExtensionFunctions();
    CreateCommandObjects();
    CreateDescriptorHeap();
    CreateImage();
    CreatePipeline();

    std::cout << "Finished app init\n\n";
}

// Dispatch
void Dispatch()
{
    std::cout << "Dispatching...\n";

    // Reset command pool
    ThrowIfFailed(
        vkResetCommandPool(
            g_pDevice,
            g_pCmdPool,
            0u
        )
    );

    // Begin command buffer
    {
        // Begin info
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = VK_NULL_HANDLE;
        beginInfo.pNext            = VK_NULL_HANDLE;

        // Begin
        ThrowIfFailed(
            vkBeginCommandBuffer(
                g_pCmdBuffer,
                &beginInfo
            )
        );
    }

    // Bind descriptor heap
    {
	    // Bind info
	    VkBindHeapInfoEXT bindInfo{};
	    bindInfo.sType               = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
	    bindInfo.heapRange.address   = g_DescriptorHeap.HeapStartDeviceMemoryAddress;
	    bindInfo.heapRange.size      = g_DescriptorHeap.HeapSizeInBytes;
	    bindInfo.reservedRangeOffset = g_DescriptorHeap.ReservedRangeOffsetInBytes;
	    bindInfo.reservedRangeSize   = g_DescriptorHeap.ReservedRangeSizeInBytes;
	    bindInfo.pNext               = VK_NULL_HANDLE;

	    // Bind
	    g_vkCmdBindResourceHeapEXT(
	    	g_pCmdBuffer,
	    	&bindInfo
	    );
    }

    // Bind pipeline
    vkCmdBindPipeline(
        g_pCmdBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        g_pPipeline
    );

    // Push descriptor index
    // -> Only if we did not set it as constant offset
#if !defined(CONSTANT_OFFSET_SET_AND_BINDING_MAPPING)
    {
        // Push data info
		VkPushDataInfoEXT pushDataInfo{};
		pushDataInfo.sType        = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
        // -> Push the offset to the descriptor range
        const uint32_t descriptorRangeOffset {
            static_cast<uint32_t>(g_DescriptorHeap.ImageDescRangeOffsetInBytes)
        };
		pushDataInfo.data.address = &descriptorRangeOffset;          // Push the offset to the descriptor range 
		pushDataInfo.data.size    = 4u;
		pushDataInfo.offset       = g_DescriptorHeap.PushDataOffset; // 0: There is only a single entry in our push data
		pushDataInfo.pNext        = VK_NULL_HANDLE;

		// Push data
		g_vkCmdPushDataEXT(
			g_pCmdBuffer,
			&pushDataInfo
		);
    }
#endif

    // Transition image to storage
    {
        // Barrier
		VkImageMemoryBarrier2 imageMemBarrier2{};
		imageMemBarrier2.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		imageMemBarrier2.image                           = g_Image.pImage;
		imageMemBarrier2.pNext                           = VK_NULL_HANDLE;
		// - Subresource
		imageMemBarrier2.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		imageMemBarrier2.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;
		imageMemBarrier2.subresourceRange.baseArrayLayer = 0u;
		imageMemBarrier2.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
		imageMemBarrier2.subresourceRange.baseMipLevel   = 0u;
		// - State transition
		//   - Src
		imageMemBarrier2.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemBarrier2.srcAccessMask                   = VK_ACCESS_2_NONE;
		imageMemBarrier2.srcStageMask                    = VK_PIPELINE_STAGE_2_NONE;
		//   - Dst
		imageMemBarrier2.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
		imageMemBarrier2.dstAccessMask                   = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
		imageMemBarrier2.dstStageMask                    = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		// - Queue family ownership transfer
        imageMemBarrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemBarrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        // Dependency info
        VkDependencyInfo dependencyInfo{};
	    dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	    dependencyInfo.imageMemoryBarrierCount  = 1u;
	    dependencyInfo.pImageMemoryBarriers     = &imageMemBarrier2;
	    dependencyInfo.bufferMemoryBarrierCount = 0u;
	    dependencyInfo.pBufferMemoryBarriers    = VK_NULL_HANDLE;
	    dependencyInfo.memoryBarrierCount       = 0u;
	    dependencyInfo.pMemoryBarriers          = VK_NULL_HANDLE;
	    dependencyInfo.dependencyFlags          = 0u;
	    dependencyInfo.pNext                    = VK_NULL_HANDLE;

		// Schedule
        vkCmdPipelineBarrier2(
            g_pCmdBuffer,
            &dependencyInfo
        );
    }

    // Dispatch
    vkCmdDispatch(
        g_pCmdBuffer,
        1u,
        1u,
        1u
    );

    // End command buffer
    ThrowIfFailed(
        vkEndCommandBuffer(g_pCmdBuffer)
    );

    // Submit
    {
        // Command buffer submit info
        VkCommandBufferSubmitInfo cmdBufferSubmitInfo{};
        cmdBufferSubmitInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.commandBuffer = g_pCmdBuffer;
        cmdBufferSubmitInfo.deviceMask    = 1u; // Single device -> Only one device in "group"
        cmdBufferSubmitInfo.pNext         = VK_NULL_HANDLE;

        // Submit info
		VkSubmitInfo2 submitInfo2{};
		submitInfo2.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo2.waitSemaphoreInfoCount   = 0u;
		submitInfo2.pWaitSemaphoreInfos      = VK_NULL_HANDLE;
		submitInfo2.commandBufferInfoCount   = 1u;
		submitInfo2.pCommandBufferInfos      = &cmdBufferSubmitInfo;
		submitInfo2.signalSemaphoreInfoCount = 0u;
		submitInfo2.pSignalSemaphoreInfos    = VK_NULL_HANDLE;
		submitInfo2.flags                    = 0u;
		submitInfo2.pNext                    = VK_NULL_HANDLE;

		// Submit
		ThrowIfFailed(
			vkQueueSubmit2(
				g_pQueue,
				1u,
				&submitInfo2,
				VK_NULL_HANDLE
			)
		);
    }

    // Sync
    vkDeviceWaitIdle(g_pDevice);

    std::cout << "...Success!\n\n";
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
    if (g_Image.pMemory)
        vkFreeMemory(g_pDevice, g_Image.pMemory, VK_NULL_HANDLE);
    if (g_Image.pImage)
        vkDestroyImage(g_pDevice, g_Image.pImage, VK_NULL_HANDLE);
    if (g_pCmdPool)
        vkDestroyCommandPool(g_pDevice, g_pCmdPool, VK_NULL_HANDLE);
    if (g_pDevice)
        vkDestroyDevice(g_pDevice, VK_NULL_HANDLE);
#if defined(DEBUG) | defined(_DEBUG)
    if (g_pDebugMessenger)
        g_vkDestroyDebugUtilsMessengerEXT(
            g_pInstance,
            g_pDebugMessenger,
            VK_NULL_HANDLE
        );
#endif
    if (g_pInstance)
        vkDestroyInstance(g_pInstance, VK_NULL_HANDLE);

    std::cout << "Cleaned up app\n";
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
