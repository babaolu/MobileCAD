#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct AAssetManager;

namespace rendering {

struct GraphicsPipeline {
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

class Pipeline {
public:
    Pipeline() = default;

    static void setAssetManager(AAssetManager* mgr);
    static std::vector<char> loadShader(const std::string& filename);
    static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
};

} // namespace rendering
