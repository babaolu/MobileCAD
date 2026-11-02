#include "rendering/MeshBuffer.h"

#include <cstring>
#include <stdexcept>

namespace rendering
{

VkVertexInputBindingDescription Vertex::getBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Location 0: position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    // Location 1: normal
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    // Location 2: color
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, color);

    return attributeDescriptions;
}

uint32_t MeshBuffer::findMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter,
                                    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for buffer");
}

void MeshBuffer::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                              VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                              VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, bufferMemory, nullptr);
        buffer = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to bind buffer memory");
    }
}

MeshBuffer::MeshBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                       const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_device(device),
      m_vertexCount(static_cast<uint32_t>(vertices.size())),
      m_indexCount(static_cast<uint32_t>(indices.size()))
{
    if (m_vertexCount == 0)
    {
        return;
    }

    VkMemoryPropertyFlags memProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // 1. Create and upload vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
    createBuffer(device, physicalDevice, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 memProperties, m_vertexBuffer, m_vertexMemory);

    void* data = nullptr;
    if (vkMapMemory(device, m_vertexMemory, 0, vertexBufferSize, 0, &data) != VK_SUCCESS)
    {
        cleanup();
        throw std::runtime_error("Failed to map vertex buffer memory");
    }
    std::memcpy(data, vertices.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(device, m_vertexMemory);

    // 2. Create and upload index buffer (if indices provided)
    if (m_indexCount > 0)
    {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        createBuffer(device, physicalDevice, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     memProperties, m_indexBuffer, m_indexMemory);

        void* indexData = nullptr;
        if (vkMapMemory(device, m_indexMemory, 0, indexBufferSize, 0, &indexData) != VK_SUCCESS)
        {
            cleanup();
            throw std::runtime_error("Failed to map index buffer memory");
        }
        std::memcpy(indexData, indices.data(), static_cast<size_t>(indexBufferSize));
        vkUnmapMemory(device, m_indexMemory);
    }
}

MeshBuffer::~MeshBuffer()
{
    cleanup();
}

MeshBuffer::MeshBuffer(MeshBuffer&& other) noexcept
    : m_device(other.m_device),
      m_vertexBuffer(other.m_vertexBuffer),
      m_vertexMemory(other.m_vertexMemory),
      m_vertexCount(other.m_vertexCount),
      m_indexBuffer(other.m_indexBuffer),
      m_indexMemory(other.m_indexMemory),
      m_indexCount(other.m_indexCount)
{
    other.m_device = VK_NULL_HANDLE;
    other.m_vertexBuffer = VK_NULL_HANDLE;
    other.m_vertexMemory = VK_NULL_HANDLE;
    other.m_vertexCount = 0;
    other.m_indexBuffer = VK_NULL_HANDLE;
    other.m_indexMemory = VK_NULL_HANDLE;
    other.m_indexCount = 0;
}

MeshBuffer& MeshBuffer::operator=(MeshBuffer&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_device = other.m_device;
        m_vertexBuffer = other.m_vertexBuffer;
        m_vertexMemory = other.m_vertexMemory;
        m_vertexCount = other.m_vertexCount;
        m_indexBuffer = other.m_indexBuffer;
        m_indexMemory = other.m_indexMemory;
        m_indexCount = other.m_indexCount;

        other.m_device = VK_NULL_HANDLE;
        other.m_vertexBuffer = VK_NULL_HANDLE;
        other.m_vertexMemory = VK_NULL_HANDLE;
        other.m_vertexCount = 0;
        other.m_indexBuffer = VK_NULL_HANDLE;
        other.m_indexMemory = VK_NULL_HANDLE;
        other.m_indexCount = 0;
    }
    return *this;
}

void MeshBuffer::cleanup() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        if (m_indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
            m_indexBuffer = VK_NULL_HANDLE;
        }
        if (m_indexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, m_indexMemory, nullptr);
            m_indexMemory = VK_NULL_HANDLE;
        }
        if (m_vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
            m_vertexBuffer = VK_NULL_HANDLE;
        }
        if (m_vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, m_vertexMemory, nullptr);
            m_vertexMemory = VK_NULL_HANDLE;
        }
        m_device = VK_NULL_HANDLE;
    }
    m_vertexCount = 0;
    m_indexCount = 0;
}

}  // namespace rendering
