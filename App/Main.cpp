// Standard Library
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <iomanip>

// Vulkan
#include <vulkan/vulkan.h>
#ifndef VULKAN_TARGET_API_VERSION
#error "VULKAN_TARGET_API_VERSION is not defined"
#endif

// Literals
constexpr uint64_t operator"" _KiB(uint64_t x) {
    return x << 10;
}
constexpr uint64_t operator"" _MiB(uint64_t x) {
    return x << 20;
}
constexpr uint64_t operator"" _GiB(uint64_t x) {
    return x << 30;
}

// Command line variables
// - If not provided, we as much memory as required by
//   the image.
uint64_t g_ImageMemoryAllocationByteSize { 0ull  };
// - If not provided, we use capture replay for the
//   image memory.
bool     g_ImageMemoryNoCaptureReplay    { false };

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
    uint64_t                            StorageDescHeapOffsetInBytes { 0u };
    static constexpr uint32_t           Width                        { 1280u };
    static constexpr uint32_t           Height                       { 720u  };
}                                       g_Image                      {};
struct ReadBackBuffer                                                  
{                                                                    
    VkBuffer                            pBuffer                      { VK_NULL_HANDLE };
    VkDeviceMemory                      pMemory                      { VK_NULL_HANDLE };
    uint8_t*                            pHostMemoryAddress           { nullptr };
}                                       g_ReadBackBuffer             {};
struct DescriptorHeap 
{   
    // Backing buffer
    VkBuffer                            pBuffer                      { VK_NULL_HANDLE };
    VkDeviceMemory                      pMemory                      { VK_NULL_HANDLE };

    // Heap start memory address
    // - Host
    uint8_t*                            pHeapStartHostMemoryAddress  { nullptr };
    // - Device
    uint64_t                            HeapStartDeviceMemoryAddress { 0ull };

    // Heap size
    uint64_t                            HeapSizeInBytes              { 0ull };

    // Reserved range
    uint64_t                            ReservedRangeOffsetInBytes   { 0ull };
    uint64_t                            ReservedRangeSizeInBytes     { 0ull };

    // Image descriptor range in heap
    uint64_t                            ImageDescRangeOffsetInBytes  { 0ull };
    uint64_t                            ImageDescSizeInBytes         { 0ull };
    // -> Single descriptor
    uint32_t                            ImageDescCount               { 1u };

    // Binding
    VkDescriptorSetAndBindingMappingEXT SetAndBindingMapping         {};
    uint32_t                            PushDataOffset               { 0u };
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
//
// Debug callbacks
// -> We need two distinct routines since we want to check for
//    VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT
//    which belongs to a device extension. Thus, we should not check
//    for it in the instance's debug callback.
//
// - For VkInstance
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallbackInstance(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Convert the severity and type to a string
    std::string severity;
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: severity = "VERBOSE"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    severity = "INFO";    break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: severity = "WARNING"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:	  severity = "ERROR";   break;
    default:                                              severity = "UNKOWN";  break;
    }
    std::string type;
    switch (messageType)
    {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:     type = "GENERAL";         break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:  type = "VALIDATION";      break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: type = "PERFORMANCE";     break;
    default:                                              type = "UNKOWN";          break;
    }

    std::cout << "\nVulkan Debug Message\n"
        << "[Severity] " << severity << '\n'
        << "[Type]     " << type << '\n'
        << "[Message]  " << (pCallbackData->pMessage ? pCallbackData->pMessage : "None") << '\n';

    // Boolean indicates if Vulkan call that triggered the validation layer callback
    // should be aborted. VK_TRUE is normally only used to test the validation layers
    // themselves.
    return VK_FALSE;
}
//
// - For VkDevice
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallbackDevice(
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
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:                type = "GENERAL";         break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:             type = "VALIDATION";      break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:            type = "PERFORMANCE";     break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT: type = "ADDRESS BINDING"; break;
    default:                                                         type = "UNKOWN";          break;
    }

    std::cout << "\nVulkan Debug Message\n"
        << "[Severity] " << severity << '\n'
        << "[Type]     " << type << '\n'
        << "[Message]  " << (pCallbackData->pMessage ? pCallbackData->pMessage : "None") << '\n';

    // Handle address binding message
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT)
	{
		// Walk the pNext chain, identifying structures by sType.
        const auto* pNext =
            static_cast<const VkBaseInStructure*>(pCallbackData->pNext);

        while (pNext)
        {
            if (pNext->sType ==
                VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT)
            {
                const auto* pBinding =
                    reinterpret_cast<
                        const VkDeviceAddressBindingCallbackDataEXT*>(pNext);

				std::cout
					<< "[Address binding] "
					<< (pBinding->bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT
						? "BIND"
						: "UNBIND")
					<< "\n"
					<< "  Base address: 0x"
					<< std::hex << std::setfill('0') << std::setw(16)
					<< pBinding->baseAddress
					<< "\n"
					<< "  Size:         0x"
					<< pBinding->size
					<< "\n"
					<< "  Flags:        0x"
					<< pBinding->flags
					<< std::dec << std::setfill(' ')
					<< "\n";

				for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
				{
					const auto& object = pCallbackData->pObjects[i];

					std::cout
						<< "  Object type:  "
						<< static_cast<int>(object.objectType)
						<< "\n"
						<< "  Handle:       0x"
						<< std::hex << std::setfill('0') << std::setw(16)
						<< object.objectHandle
						<< std::dec << std::setfill(' ')
						<< "\n"
						<< "  Name:         "
						<< (object.pObjectName ? object.pObjectName : "(unnamed)")
						<< "\n";
				}
            }

			pNext = pNext->pNext;
        }
	}

    // Boolean indicates if Vulkan call that triggered the validation layer callback
    // should be aborted. VK_TRUE is normally only used to test the validation layers
    // themselves.
    return VK_FALSE;
}
//
// Debug messenger create infos
// - Instance
VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfoInstance()
{
	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		  VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	createInfo.messageType     =
		  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallbackInstance;
	createInfo.pUserData       = VK_NULL_HANDLE;
	createInfo.pNext           = VK_NULL_HANDLE;
	createInfo.flags           = 0;

	return createInfo;
}
// - Device
VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfoDevice()
{
	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		  VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	createInfo.messageType     =
		  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallbackDevice;
	createInfo.pUserData       = VK_NULL_HANDLE;
	createInfo.pNext           = VK_NULL_HANDLE;
	createInfo.flags           = 0;

	return createInfo;
}

// Error helper
void ThrowIfFailed(VkResult result, int line)
{
    // Error message
    std::ostringstream sstream;
    sstream << "\nVulkan failed with VkResult "
        << std::to_string(result)
        << " at line "
        << line
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
        ),
        __LINE__
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
                vkEnumerateInstanceExtensionProperties(NULL, &supportedExtensionCount, VK_NULL_HANDLE),
                __LINE__
            );
            std::vector<VkExtensionProperties> supportedExtensions(supportedExtensionCount);
            ThrowIfFailed(
                vkEnumerateInstanceExtensionProperties(NULL, &supportedExtensionCount, supportedExtensions.data()),
                __LINE__
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
                vkEnumerateInstanceLayerProperties(&supportedLayerCount, VK_NULL_HANDLE),
                __LINE__
            );
            std::vector<VkLayerProperties> supportedLayers(supportedLayerCount);
            ThrowIfFailed(
                vkEnumerateInstanceLayerProperties(&supportedLayerCount, supportedLayers.data()),
                __LINE__
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
    const VkDebugUtilsMessengerCreateInfoEXT debMsgCreateInfo { MakeDebugMessengerCreateInfoInstance() };
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
        ),
        __LINE__
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
#if defined(DEBUG) | defined(_DEBUG)
    // Create info
    const VkDebugUtilsMessengerCreateInfoEXT createInfo { MakeDebugMessengerCreateInfoDevice() };

    // Creation
    ThrowIfFailed(
        g_vkCreateDebugUtilsMessengerEXT(
            g_pInstance,
            &createInfo,
            VK_NULL_HANDLE,
            &g_pDebugMessenger
        ),
        __LINE__
    );
#endif
}
//
void PickPhysicalDevice()
{
	// Get the physical devices
	uint32_t deviceCount;
	ThrowIfFailed(vkEnumeratePhysicalDevices(g_pInstance, &deviceCount, VK_NULL_HANDLE), __LINE__);
	std::vector<VkPhysicalDevice> availableDevices;
    availableDevices.resize(deviceCount);
	ThrowIfFailed(vkEnumeratePhysicalDevices(g_pInstance, &deviceCount, availableDevices.data()), __LINE__);

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
            ),
            __LINE__
        );
        std::vector<VkExtensionProperties> supportedExtensionProperties(supportedExtensionCount);
        ThrowIfFailed(
            vkEnumerateDeviceExtensionProperties(
                deviceInfo.pPhysicalDevice,
                VK_NULL_HANDLE,
                &supportedExtensionCount,
                supportedExtensionProperties.data()
            ),
            __LINE__
        );

        // Query the physical device features 2 the device supports
#if defined(DEBUG) | defined(_DEBUG)
        // - Address binding report
        VkPhysicalDeviceAddressBindingReportFeaturesEXT supportedAddressBindingReportFeatures {};
        supportedAddressBindingReportFeatures.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT;
        supportedAddressBindingReportFeatures.reportAddressBinding = VK_TRUE;
        supportedAddressBindingReportFeatures.pNext                = VK_NULL_HANDLE;
#endif
        // - Buffer device address
        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures{};
        supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
#if defined(DEBUG) | defined(_DEBUG)
        supportedBufferDeviceAddressFeatures.pNext = &supportedAddressBindingReportFeatures;
#endif
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

#if defined(DEBUG) | defined(_DEBUG)
        // Check if device supports address binding report
        {
            // Check support of VK_EXT_device_address_binding_report extension
            if (!CheckDeviceExtensionSupport("VK_EXT_device_address_binding_report", supportedExtensionProperties))
                continue;

            // Check support of associated feature
            if (!supportedAddressBindingReportFeatures.reportAddressBinding)
                continue;
        }
#endif

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
        std::cout << "\nFound a single suitable device: " << devInfo.Name << "\n";

        // Set the physical device
        g_pPhysicalDevice = devInfo.pPhysicalDevice;
        return;
    }

    // If there are multiple suitable devices
    {
        // List the suitable devices
        std::cout << "\nFound multiple suitable devices:\n";
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
        "VK_EXT_descriptor_heap",
#if defined(DEBUG) | defined(_DEBUG)
        "VK_EXT_device_address_binding_report"
#endif
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
#if defined(DEBUG) | defined(_DEBUG)
    // -- Address binding report
    VkPhysicalDeviceAddressBindingReportFeaturesEXT enabledAddressBindingReportFeatures {};
    enabledAddressBindingReportFeatures.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT;
    enabledAddressBindingReportFeatures.reportAddressBinding = VK_TRUE;
    enabledAddressBindingReportFeatures.pNext                = VK_NULL_HANDLE;
#endif
    // -- Buffer device address
    VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddressFeatures{};
    enabledBufferDeviceAddressFeatures.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    enabledBufferDeviceAddressFeatures.bufferDeviceAddress              = VK_TRUE;
    enabledBufferDeviceAddressFeatures.bufferDeviceAddressCaptureReplay = VK_TRUE;
#if defined(DEBUG) | defined(_DEBUG)
    enabledBufferDeviceAddressFeatures.pNext                            = &enabledAddressBindingReportFeatures;
#endif
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
		),
        __LINE__
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
            ),
            __LINE__
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
            ),
            __LINE__
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

    // Account for image descriptors
    {
        // Align current offset to image descriptor alignment
        currentHeapOffsetInBytes = AlignUp(
            currentHeapOffsetInBytes,
            descriptorHeapProperties.imageDescriptorAlignment
        );

        // Set image descriptor range offset
        // -> This is where we write the descriptors
        g_DescriptorHeap.ImageDescRangeOffsetInBytes = currentHeapOffsetInBytes;

        // Set image descriptor size
        g_DescriptorHeap.ImageDescSizeInBytes = descriptorHeapProperties.imageDescriptorSize;

        // -> We do not need the imageDescriptorAlignment since it is a power of two
        //    and <= imageDescriptorSize, i.e., we can make the descritpor slots
        //    imageDescriptorSize-wide.

        // Adjust the current heap offset for the descriptors
        currentHeapOffsetInBytes += g_DescriptorHeap.ImageDescSizeInBytes * g_DescriptorHeap.ImageDescCount;
    }

    // Set descriptor heap size
    g_DescriptorHeap.HeapSizeInBytes = currentHeapOffsetInBytes;

    // Create buffer
    {
        // Create info
        VkBufferCreateInfo createInfo{};
	    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	    createInfo.size                  = g_DescriptorHeap.HeapSizeInBytes;
	    createInfo.usage                 = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
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
            ),
            __LINE__
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
            ),
            __LINE__
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_DescriptorHeap.pMemory),
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            "Memory: Descriptor Heap",
            g_pDevice
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
            ),
            __LINE__
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
            ),
            __LINE__
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

        // Push index configuration
        g_DescriptorHeap.SetAndBindingMapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
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
	    createInfo.extent.width          = g_Image.Width;
	    createInfo.extent.height         = g_Image.Height;
	    createInfo.extent.depth          = 1u;
	    createInfo.arrayLayers           = 1u;
	    createInfo.mipLevels             = 1u;
	    createInfo.format                = VK_FORMAT_R32_UINT;
	    createInfo.usage                 = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
            ),
            __LINE__
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
        // - Use the parsed value but default to the required memory size
        allocInfo.allocationSize  = std::max(memoryRequirements.size, g_ImageMemoryAllocationByteSize);
	    // - Allocate flags info
	    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
	    allocFlagsInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        // - Set the capture replay flag only if not wished otherwise
        if (!g_ImageMemoryNoCaptureReplay)
            allocFlagsInfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
	    allocFlagsInfo.deviceMask = 1u; // Single device -> Only one device in "group";
	    allocFlagsInfo.pNext      = VK_NULL_HANDLE;
	    // - Add to pNext chain
        allocInfo.pNext = &allocFlagsInfo;

        // Allocation
        ThrowIfFailed(
            vkAllocateMemory(
                g_pDevice,
                &allocInfo,
                VK_NULL_HANDLE,
                &g_Image.pMemory
            ),
            __LINE__
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_Image.pMemory),
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            "Memory: Test Image",
            g_pDevice
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
            ),
            __LINE__
        );
    }

    // Write descriptor
    // -> Currently only a single descriptor
    {
        // Data passed to descriptor creation
        std::vector<VkHostAddressRangeEXT>       hostAddressRanges;
        std::vector<VkResourceDescriptorInfoEXT> resourceDescriptorInfos;
        // - Associated data referenced by descriptor infos
        std::vector<VkImageDescriptorInfoEXT>    imageDescriptorInfos;
        std::vector<VkImageViewCreateInfo>       imageViewCreateInfos;
        std::vector<VkImageViewUsageCreateInfo>  imageViewUsageCreateInfos;

        // Reserve
        hostAddressRanges.reserve(g_DescriptorHeap.ImageDescCount);
        resourceDescriptorInfos.reserve(g_DescriptorHeap.ImageDescCount);
        imageDescriptorInfos.reserve(g_DescriptorHeap.ImageDescCount);
        imageViewCreateInfos.reserve(g_DescriptorHeap.ImageDescCount);
        imageViewUsageCreateInfos.reserve(g_DescriptorHeap.ImageDescCount);

        // Keep track of current byte offset in heap
        uint64_t currentHeapByteOffset { g_DescriptorHeap.ImageDescRangeOffsetInBytes };

        // Helper function
        auto AddDescriptor = [
            &hostAddressRanges,
            &resourceDescriptorInfos,
            &imageDescriptorInfos,
            &imageViewCreateInfos,
            &imageViewUsageCreateInfos](
                VkImageUsageFlags imageUsage,
                VkDescriptorType descriptorType,
                uint64_t currentHeapByteOffset)
            {
                // Usage create info
                VkImageViewUsageCreateInfo usageCreateInfo{};
                usageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
                usageCreateInfo.usage = imageUsage;
                usageCreateInfo.pNext = VK_NULL_HANDLE;
                // - Add to array
                imageViewUsageCreateInfos.push_back(usageCreateInfo);

                // Image view create info
		        VkImageViewCreateInfo viewCreateInfo{};
		        viewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		        viewCreateInfo.image                           = g_Image.pImage;
		        viewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		        viewCreateInfo.format                          = VK_FORMAT_R32_UINT;
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
		        viewCreateInfo.pNext                           = &(*imageViewUsageCreateInfos.rbegin());
                // - Add to array
                imageViewCreateInfos.push_back(viewCreateInfo);

		        // Image descriptor info
		        VkImageDescriptorInfoEXT imageDescriptorInfo{};
		        imageDescriptorInfo.sType  = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
		        imageDescriptorInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
		        imageDescriptorInfo.pView  = &(*imageViewCreateInfos.rbegin());
		        imageDescriptorInfo.pNext  = VK_NULL_HANDLE;
                // - Add to array
                imageDescriptorInfos.push_back(imageDescriptorInfo);

		        // Descriptor info
		        VkResourceDescriptorInfoEXT resourceDescriptorInfo{};
		        resourceDescriptorInfo.sType       = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
		        resourceDescriptorInfo.type        = descriptorType;
		        resourceDescriptorInfo.data.pImage = &(*imageDescriptorInfos.rbegin());
		        resourceDescriptorInfo.pNext       = VK_NULL_HANDLE;
                // - Add to array
                resourceDescriptorInfos.push_back(resourceDescriptorInfo);

		        // Host address range
		        VkHostAddressRangeEXT hostAddressRange{};
		        hostAddressRange.address = g_DescriptorHeap.pHeapStartHostMemoryAddress + currentHeapByteOffset;
		        hostAddressRange.size    = g_DescriptorHeap.ImageDescSizeInBytes;
                // - Add to array
                hostAddressRanges.push_back(hostAddressRange);
            };

        // Add storage image descriptor
        AddDescriptor(
            VK_IMAGE_USAGE_STORAGE_BIT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            currentHeapByteOffset
        );

        // Set the offset to the storage descriptor
        g_Image.StorageDescHeapOffsetInBytes = currentHeapByteOffset;

        // Write
        ThrowIfFailed(
            g_vkWriteResourceDescriptorsEXT(
                g_pDevice,
                g_DescriptorHeap.ImageDescCount,
                resourceDescriptorInfos.data(),
                hostAddressRanges.data()
            ),
            __LINE__
        );
    }
}
//
void CreateReadBackBuffer()
{
    // Create buffer
    {
        // Create info
        VkBufferCreateInfo createInfo{};
	    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	    createInfo.size                  = g_Image.Width * g_Image.Height * 4u;
	    createInfo.usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
                &g_ReadBackBuffer.pBuffer
            ),
            __LINE__
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_ReadBackBuffer.pBuffer),
            VK_OBJECT_TYPE_BUFFER,
            "Buffer: Readback Buffer",
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
            g_ReadBackBuffer.pBuffer,
            &memoryRequirements
        );

        // Find memory type ID
        // -> We require host-visible + host-coherent memory.
        const uint32_t memoryTypeID{
             FindMemoryTypeID(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0u,
                0u,
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
                &g_ReadBackBuffer.pMemory
            ),
            __LINE__
        );

        // Name
        SetObjectName(
            reinterpret_cast<uint64_t>(g_ReadBackBuffer.pMemory),
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            "Memory: Readback Buffer",
            g_pDevice
        );

        // Map memory to host
        ThrowIfFailed(
            vkMapMemory(
                g_pDevice,
                g_ReadBackBuffer.pMemory,
                0ull,
                memoryRequirements.size,
                0u,
                reinterpret_cast<void**>(&g_ReadBackBuffer.pHostMemoryAddress)
            ),
            __LINE__
        );
    }

    // Bind memory
    {
        // Bind info
        VkBindBufferMemoryInfo bindInfo{};
        bindInfo.sType        = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
        bindInfo.buffer       = g_ReadBackBuffer.pBuffer;
        bindInfo.memory       = g_ReadBackBuffer.pMemory;
        bindInfo.memoryOffset = 0ull;
        bindInfo.pNext        = VK_NULL_HANDLE;

        // Bind
        ThrowIfFailed(
            vkBindBufferMemory2(
                g_pDevice,
                1u,
                &bindInfo
            ),
            __LINE__
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
			),
            __LINE__
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
    VkComputePipelineCreateInfo      pipelineCreateInfo {};
    VkPipelineCreateFlags2CreateInfo createFlags2       {};
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
        ),
        __LINE__
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
    CreateReadBackBuffer();
    CreatePipeline();

    std::cout << "\nFinished app init\n";
}

// Dispatch
void Dispatch()
{
    std::cout << "\nExecuting...\n";

    // Helper
    auto ScheduleImageBarrier = [](
        VkImageLayout srcLayout,
        VkAccessFlags2 srcAccessFlags2,
        VkPipelineStageFlags2 srcPipelineStageFlags,
        VkImageLayout dstLayout,
        VkAccessFlags2 dstAccessFlags2,
        VkPipelineStageFlags2 dstPipelineStageFlags)
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
		    imageMemBarrier2.oldLayout                       = srcLayout;
		    imageMemBarrier2.srcAccessMask                   = srcAccessFlags2;
		    imageMemBarrier2.srcStageMask                    = srcPipelineStageFlags;
		    //   - Dst
		    imageMemBarrier2.newLayout                       = dstLayout;
		    imageMemBarrier2.dstAccessMask                   = dstAccessFlags2;
		    imageMemBarrier2.dstStageMask                    = dstPipelineStageFlags;
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
        };

    // Reset command pool
    ThrowIfFailed(
        vkResetCommandPool(
            g_pDevice,
            g_pCmdPool,
            0u
        ),
        __LINE__
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
            ),
            __LINE__
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
    {
        // Push data info
		VkPushDataInfoEXT pushDataInfo{};
		pushDataInfo.sType        = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
        // -> Push the offset to the descriptor range
        const uint32_t descriptorRangeOffset {
            static_cast<uint32_t>(g_DescriptorHeap.ImageDescRangeOffsetInBytes)
        };
		pushDataInfo.data.address = &g_Image.StorageDescHeapOffsetInBytes; // Push the offset to the storage descriptor
		pushDataInfo.data.size    = 4u;
		pushDataInfo.offset       = g_DescriptorHeap.PushDataOffset;       // 0: There is only a single entry in our push data
		pushDataInfo.pNext        = VK_NULL_HANDLE;

		// Push data
		g_vkCmdPushDataEXT(
			g_pCmdBuffer,
			&pushDataInfo
		);
    }

    // Transition image to storage
    ScheduleImageBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_NONE,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
    );

    // Dispatch
    // -> Local threads: (8, 8, 1)
    vkCmdDispatch(
        g_pCmdBuffer,
        static_cast<uint32_t>(ceil(g_Image.Width  / 8u)),
        static_cast<uint32_t>(ceil(g_Image.Height / 8u)),
        1u
    );

    // Transition image to copy src
    ScheduleImageBarrier(
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    // Write back the image data
    {
        // Region
        VkBufferImageCopy2 region{};
        region.sType                           = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.bufferOffset                    = 0u;
        region.bufferRowLength                 = 0u; // Image data tightly packed
        region.bufferImageHeight               = 0u; // Image data tightly packed
        region.imageExtent                     = { g_Image.Width, g_Image.Height, 1u };
        region.imageOffset                     = { 0, 0, 0 };
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.baseArrayLayer = 0u;
        region.imageSubresource.layerCount     = 1u;
        region.imageSubresource.mipLevel       = 0u;
        region.pNext                           = VK_NULL_HANDLE;

        // Copy info
        VkCopyImageToBufferInfo2 imageToBufferInfo2{};
        imageToBufferInfo2.sType          = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        imageToBufferInfo2.srcImage       = g_Image.pImage;
        imageToBufferInfo2.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageToBufferInfo2.dstBuffer      = g_ReadBackBuffer.pBuffer;
        imageToBufferInfo2.pRegions       = &region;
        imageToBufferInfo2.regionCount    = 1u;
        imageToBufferInfo2.pNext          = VK_NULL_HANDLE;

        // Copy
        vkCmdCopyImageToBuffer2(
            g_pCmdBuffer,
            &imageToBufferInfo2
        );
    }

    // End command buffer
    ThrowIfFailed(
        vkEndCommandBuffer(g_pCmdBuffer),
        __LINE__
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
			),
            __LINE__
		);
    }

    // Sync
    vkDeviceWaitIdle(g_pDevice);

    std::cout << "\n...finished execution.\n";

    // Print the values
    const uint32_t value { *reinterpret_cast<uint32_t*>(g_ReadBackBuffer.pHostMemoryAddress) };
    std::cout << "\nWritten value of texel (0, 0)                : " << value << '\n';
    std::cout << "Intended to equal image width * image height : " << g_Image.Width * g_Image.Height << "\n";
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
    if (g_ReadBackBuffer.pMemory)
        vkFreeMemory(g_pDevice, g_ReadBackBuffer.pMemory, VK_NULL_HANDLE);
    if (g_ReadBackBuffer.pBuffer)
        vkDestroyBuffer(g_pDevice, g_ReadBackBuffer.pBuffer, VK_NULL_HANDLE);
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

    std::cout << "\nCleaned up app\n";
}

int main(int argc, char* argv[])
{
    // Retrieve the command line args
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        // Image memory allocation size in MiB
        if (arg == "--image-mem-alloc-mib" && i + 1 < argc)
        {
            // Get the parsed MiB value
            // -> stoull converts string to unsigned long long (uint64_t)
            const uint64_t memAllocSizeMiB { std::stoull(argv[++i]) };

            // Convert the value to bytes
            g_ImageMemoryAllocationByteSize = memAllocSizeMiB << 20;
        }
        // Image memory no capture replay
        else if (arg == "--image-mem-no-capture-replay")
            g_ImageMemoryNoCaptureReplay = true;
    }

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
