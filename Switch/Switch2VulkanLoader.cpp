// Copyright (c) 2012- PPSSPP Project.
// This program is free software under the GNU GPL, version 2 or later.
// See LICENSE.TXT for details.
#if defined(__SWITCH2__)
#include <vulkan/vulkan.h>
#include <cstring>

PFN_vkVoidFunction Switch2VulkanSymbol(const char *name) {
	// dlsym also resolves instance commands before there is an instance.
	// vkGetInstanceProcAddr(nullptr, ...) only promises global commands.
#define STATIC_VK_SYMBOL(symbol) if (std::strcmp(name, #symbol) == 0) return reinterpret_cast<PFN_vkVoidFunction>(symbol)
	STATIC_VK_SYMBOL(vkGetInstanceProcAddr);
	STATIC_VK_SYMBOL(vkGetDeviceProcAddr);
	STATIC_VK_SYMBOL(vkCreateInstance);
	STATIC_VK_SYMBOL(vkDestroyInstance);
	STATIC_VK_SYMBOL(vkEnumeratePhysicalDevices);
	STATIC_VK_SYMBOL(vkGetPhysicalDeviceProperties);
	STATIC_VK_SYMBOL(vkEnumerateInstanceVersion);
	STATIC_VK_SYMBOL(vkEnumerateInstanceExtensionProperties);
	STATIC_VK_SYMBOL(vkEnumerateInstanceLayerProperties);
#undef STATIC_VK_SYMBOL
	return vkGetInstanceProcAddr(VK_NULL_HANDLE, name);
}
#endif
