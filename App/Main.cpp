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
VkInstance            g_pInstance                  { VK_NULL_HANDLE };
VkPhysicalDevice      g_pPhysicalDevice            { VK_NULL_HANDLE };
VkDevice              g_pDevice                    { VK_NULL_HANDLE };
uint32_t              g_QueueFamilyID              {};
VkQueue               g_pQueue                     { VK_NULL_HANDLE };
VkCommandPool         g_pCmdPool                   { VK_NULL_HANDLE };
VkCommandBuffer       g_pCmdBuffer                 { VK_NULL_HANDLE };
struct StorageImage                                
{                                                  
    VkImage           pImage                       { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory                      { VK_NULL_HANDLE };
}                     g_StorageImage               {};
struct DescriptorHeap 
{                     
    VkBuffer          pBuffer                      { VK_NULL_HANDLE };
    VkDeviceMemory    pMemory                      { VK_NULL_HANDLE };
    uint64_t          HeapStartDeviceMemoryAddress { 0ull };
    uint64_t          HeapSizeInBytes              { 0ull };
    uint64_t          ReservedRangeOffsetInBytes   { 0ull };
    uint64_t          ReservedRangeSizeInBytes     { 0ull };
    uint64_t          ImageDescRangeOffsetInBytes  { 0ull };
    uint64_t          ImageDescSizeInBytes         { 0ull };
}                     g_DescriptorHeap             {};
VkPipeline            g_pPipeline                  { VK_NULL_HANDLE };

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
    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT { VK_NULL_HANDLE };
    pfnCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(g_pInstance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (!pfnCreateDebugUtilsMessengerEXT)
        throw std::runtime_error("Unable to load vkCreateDebugUtilsMessengerEXT");

    // Creation
    ThrowIfFailed(
        pfnCreateDebugUtilsMessengerEXT(
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

    // Create buffer
    {
        // Create info
        VkBufferCreateInfo createInfo{};
	    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	    createInfo.size                  = currentHeapOffsetInBytes;
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
        // Max push data size
        descriptorHeapProperties.maxPushDataSize;
    }
}
void CreateStorageImage()
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
                &g_StorageImage.pImage
            )
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
            g_StorageImage.pImage,
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
                &g_StorageImage.pMemory
            )
        );
    }

    // Bind memory
    {
        // Bind info
        VkBindImageMemoryInfo bindInfo{};
        bindInfo.sType        = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bindInfo.image        = g_StorageImage.pImage;
        bindInfo.memory       = g_StorageImage.pMemory;
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

    }
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
    CreateDescriptorHeap();
    CreateStorageImage();
    CreatePipeline();

    std::cout << "Finished app init\n\n";
}

// Dispatch
void Dispatch()
{
    std::cout << "Dispatching...\n";

    // Reset command pool

    // Begin command buffer

    // Bind descriptor heap

    // Bind pipeline

    // Push descriptor

    // Transition storage image

    // Dispatch

    // End command buffer

    // Submit

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
        PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT { VK_NULL_HANDLE };
        pfnDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(g_pInstance, "vkDestroyDebugUtilsMessengerEXT")
		);
		if (!pfnDestroyDebugUtilsMessengerEXT)
			throw std::runtime_error("Unable to load vkDestroyDebugUtilsMessengerEXT");

        // Destroy
        pfnDestroyDebugUtilsMessengerEXT(
            g_pInstance,
            g_pDebugMessenger,
            VK_NULL_HANDLE
        );
    }
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
