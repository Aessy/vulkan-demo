#pragma once

#include <iterator>
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <iostream>

#include "Textures.h"

template<typename ObjectType>
struct UniformBinding
{
    using UniformType = ObjectType;

    vk::DescriptorSetLayoutBinding layout_binding;
};

struct TextureSamplerBinding
{
    vk::DescriptorSetLayoutBinding layout_binding;
};

inline vk::DescriptorSetLayoutBinding createLayoutBinding(int binding, vk::DescriptorType type, int count,
                                                   vk::ShaderStageFlags shader_flags)
{
    vk::DescriptorSetLayoutBinding desc_binding{};
    desc_binding.binding = binding;
    desc_binding.descriptorType = type;
    desc_binding.descriptorCount = count;
    desc_binding.stageFlags = shader_flags;
    desc_binding.setPImmutableSamplers(nullptr);

    return desc_binding;
}

inline auto createUniformBinding(int binding, int count, vk::ShaderStageFlags shader_flags)
{
    return createLayoutBinding(binding, vk::DescriptorType::eUniformBufferDynamic, count, shader_flags);

}

inline auto createTextureSamplerBinding(int binding, int count, vk::ShaderStageFlags shader_flags)
{
    return createLayoutBinding(binding, vk::DescriptorType::eCombinedImageSampler, count, shader_flags);
}

inline auto createStorageBufferBinding(int binding, int count, vk::ShaderStageFlags shader_flags)
{
    return createLayoutBinding(binding, vk::DescriptorType::eStorageBuffer, count, shader_flags);
}

template<typename Device>
inline auto createDescriptorPoolInfo(Device const& device, std::vector<vk::DescriptorSetLayoutBinding> const& bindings)
{
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    std::transform(bindings.begin(), bindings.end(), std::back_inserter(pool_sizes),
            [](vk::DescriptorSetLayoutBinding const& binding)
                 {
                    vk::DescriptorPoolSize pool;
                    pool.type = binding.descriptorType;
                    pool.setDescriptorCount(2);

                    return pool;
                 });

    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.sType = vk::StructureType::eDescriptorPoolCreateInfo;
    pool_info.setPoolSizes(pool_sizes);
    pool_info.maxSets = 2;

    auto pool = device.createDescriptorPool(pool_info);
    return pool.value;
}

template<typename Device>
inline auto createDescriptorSetLayout(Device const& device, std::vector<vk::DescriptorSetLayoutBinding> const& bindings)
{
    std::vector<vk::DescriptorBindingFlags> flags;
    std::transform(bindings.begin(), bindings.end(), std::back_inserter(flags),
            [](vk::DescriptorSetLayoutBinding const& binding)
                 {
                    if( binding.descriptorCount <= 1)
                    {
                        return vk::DescriptorBindingFlags{};
                    }
                    else
                    {
                        return   vk::DescriptorBindingFlagBits::eVariableDescriptorCount
                               | vk::DescriptorBindingFlagBits::ePartiallyBound;
                    }
                 });

    vk::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags{};
    binding_flags.sType = vk::StructureType::eDescriptorSetLayoutBindingFlagsCreateInfo;
    binding_flags.setBindingFlags(flags);

    vk::DescriptorSetLayoutCreateInfo  layout_info;
    layout_info.sType = vk::StructureType::eDescriptorSetLayoutCreateInfo;
    layout_info.setBindings(bindings);
    layout_info.pNext = &binding_flags;

    auto layout = device.createDescriptorSetLayout(layout_info);

    std::cout << "Layout size: " << static_cast<int>(layout_info.bindingCount) <<'\n';
    std::cout << "Layout create: " << static_cast<int>(layout.result) <<'\n';
    return layout.value;
}

template<typename Device>
inline auto createSets(Device const& device, vk::DescriptorPool const& pool, vk::DescriptorSetLayout const& layout,
                       std::vector<vk::DescriptorSetLayoutBinding> const& layout_bindings)
{
    std::vector<vk::DescriptorSetLayout> layouts(2, layout);

    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eDescriptorSetAllocateInfo;
    alloc_info.setDescriptorPool(pool);
    alloc_info.setSetLayouts(layouts);
    alloc_info.setDescriptorSetCount(2);

    auto const descriptor_count = layout_bindings.back().descriptorCount;
    if (descriptor_count > 1)
    {
        uint32_t variable_desc_count[] = {descriptor_count,descriptor_count};

        vk::DescriptorSetVariableDescriptorCountAllocateInfo variable_desc_count_alloc_info{};
        variable_desc_count_alloc_info.sType = vk::StructureType::eDescriptorSetVariableDescriptorCountAllocateInfo;
        variable_desc_count_alloc_info.descriptorSetCount = 2;
        variable_desc_count_alloc_info.pDescriptorCounts = variable_desc_count;

        alloc_info.setPNext(&variable_desc_count_alloc_info);
        auto allocate_sets = device.allocateDescriptorSets(alloc_info);
//        checkResult(allocate_sets.result);
        return allocate_sets.value;
    }


    auto allocate_sets = device.allocateDescriptorSets(alloc_info);
    return allocate_sets.value;
}

template<typename UniformObject, typename UniformBuffer, typename Device>
inline void updateUniformBuffer(Device const& device, std::vector<UniformBuffer> const& uniform_buffer,
        std::vector<vk::DescriptorSet> const& sets, vk::DescriptorSetLayoutBinding const& binding,
        uint32_t size)
{
    int i = 0;
    for (auto const& set : sets)
    {
        vk::DescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffer[i].uniform_buffers;
        std::cout << sizeof(UniformObject) << '\n';
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformObject) * size;

        vk::WriteDescriptorSet desc_writes{};
        desc_writes.sType = vk::StructureType::eWriteDescriptorSet;
        desc_writes.setDstSet(set);
        desc_writes.dstBinding = binding.binding;
        desc_writes.dstArrayElement = 0;
        desc_writes.descriptorType = binding.descriptorType;
        desc_writes.descriptorCount = size; // TODO: Add support for array
        desc_writes.setBufferInfo(buffer_info);
        ++i;

        device.updateDescriptorSets(desc_writes, nullptr);
    }
}

template<typename Device>
inline void updateImageSampler(Device const& device,
        std::vector<Texture> const& textures,
        std::vector<vk::DescriptorSet> const& sets, vk::DescriptorSetLayoutBinding const& binding)
{
    for (auto const& set : sets)
    {
        std::vector<vk::DescriptorImageInfo> image_info{};
        std::transform(textures.begin(), textures.end(), std::back_inserter(image_info), [](auto const& texture) {
                vk::DescriptorImageInfo image_info{};
                    image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    image_info.imageView = texture.view;
                    image_info.sampler = texture.sampler;
                    return image_info;
                });

        vk::WriteDescriptorSet desc_writes{};
        desc_writes.sType = vk::StructureType::eWriteDescriptorSet;
        desc_writes.setDstSet(set);
        desc_writes.dstBinding = binding.binding;
        desc_writes.dstArrayElement = 0;
        desc_writes.descriptorType = binding.descriptorType;
        desc_writes.descriptorCount = image_info.size();
        desc_writes.setImageInfo(image_info);

        device.updateDescriptorSets(desc_writes, nullptr);
    }
}
