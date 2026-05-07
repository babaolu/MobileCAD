#include "rendering/Pipeline.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/native_activity.h>
#include <stdexcept>
#include <fstream>

namespace rendering {

static AAssetManager* g_assetManager = nullptr;

void Pipeline::setAssetManager(AAssetManager* mgr) {
    g_assetManager = mgr;
}

std::vector<char> Pipeline::loadShader(const std::string& filename) {
    if (g_assetManager) {
        // Load from APK assets
        AAsset* asset = AAssetManager_open(g_assetManager, filename.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            throw std::runtime_error("Failed to open shader asset: " + filename);
        }
        off_t length = AAsset_getLength(asset);
        std::vector<char> buffer(length);
        AAsset_read(asset, buffer.data(), length);
        AAsset_close(asset);
        return buffer;
    } else {
        // Fallback to filesystem (host/standalone)
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }
}

VkShaderModule Pipeline::createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    return shaderModule;
}

} // namespace rendering
