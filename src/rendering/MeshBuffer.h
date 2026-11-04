#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace rendering
{

struct Vertex
{
    float position[3]{0.0f, 0.0f, 0.0f};
    float normal[3]{0.0f, 1.0f, 0.0f};
    float color[3]{0.8f, 0.8f, 0.8f};
    float pbr[3]{0.35f, 0.35f, 0.0f};  // x: metallic, y: roughness, z: surfaceId

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();
};

class MeshBuffer
{
   public:
    MeshBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
               const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices = {});
    ~MeshBuffer();

    // Non-copyable
    MeshBuffer(const MeshBuffer&) = delete;
    MeshBuffer& operator=(const MeshBuffer&) = delete;

    // Movable
    MeshBuffer(MeshBuffer&& other) noexcept;
    MeshBuffer& operator=(MeshBuffer&& other) noexcept;

    VkBuffer getVertexBuffer() const noexcept { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const noexcept { return m_indexBuffer; }
    uint32_t getVertexCount() const noexcept { return m_vertexCount; }
    uint32_t getIndexCount() const noexcept { return m_indexCount; }
    bool hasIndices() const noexcept { return m_indexBuffer != VK_NULL_HANDLE && m_indexCount > 0; }

    static uint32_t findMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);

   private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkBuffer m_vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_vertexMemory{VK_NULL_HANDLE};
    uint32_t m_vertexCount{0};

    VkBuffer m_indexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_indexMemory{VK_NULL_HANDLE};
    uint32_t m_indexCount{0};

    void cleanup() noexcept;
    void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                      VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory);
};

}  // namespace rendering
