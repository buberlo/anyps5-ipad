#include "prx/libSceAgcDriver/Graphics/include/InitialDescriptorWrites.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <type_traits>

using namespace AgcDriver::Graphics;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Handle> Handle handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) return reinterpret_cast<Handle>(value);
    else return static_cast<Handle>(value);
}
bool same(VkDescriptorBufferInfo a, VkDescriptorBufferInfo b) {
    return a.buffer == b.buffer && a.offset == b.offset && a.range == b.range;
}
bool same(VkDescriptorImageInfo a, VkDescriptorImageInfo b) {
    return a.sampler == b.sampler && a.imageView == b.imageView && a.imageLayout == b.imageLayout;
}
struct Binding {
    VkDescriptorType type;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
};
struct MockDevice {
    std::map<std::uint32_t, Binding> bindings;
    unsigned calls = 0;
    void Update(VkDescriptorSet expectedSet, std::span<const VkWriteDescriptorSet> writes, unsigned copies = 0) {
        ++calls;
        check(copies == 0, "explicit replay used descriptor copies");
        for (const auto& write : writes) {
            check(write.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && write.pNext == nullptr,
                "write metadata changed");
            check(write.dstSet == expectedSet, "wrong destination set");
            auto& entry = bindings[write.dstBinding];
            entry.type = write.descriptorType;
            if (write.pBufferInfo != nullptr) {
                entry.buffers.resize(std::max(entry.buffers.size(), std::size_t(write.dstArrayElement) + write.descriptorCount));
                for (unsigned i=0; i<write.descriptorCount; ++i) entry.buffers[write.dstArrayElement+i] = write.pBufferInfo[i];
            } else {
                check(write.pImageInfo != nullptr, "image infos missing");
                entry.images.resize(std::max(entry.images.size(), std::size_t(write.dstArrayElement) + write.descriptorCount));
                for (unsigned i=0; i<write.descriptorCount; ++i) entry.images[write.dstArrayElement+i] = write.pImageInfo[i];
            }
        }
    }
};
template<class F> void rejected(F&& operation) {
    bool threw = false;
    try { operation(); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "invalid write plan was accepted");
}
}

int main(int argc, char**) {
    try {
        const bool enabled = argc > 1;
        check(ExplicitDrawDescriptorsEnabled() == enabled, "environment gate disagrees");
        const auto oldSet = handle<VkDescriptorSet>(0x100);
        const auto newSet = handle<VkDescriptorSet>(0x200);
        std::array<VkDescriptorBufferInfo, 3> buffers{{
            {handle<VkBuffer>(0x1001), 256, 176}, {handle<VkBuffer>(0x1002), 64, VK_WHOLE_SIZE},
            {handle<VkBuffer>(0x1003), 0, 368}}};
        std::array<VkDescriptorImageInfo, 2> sampled{{
            {VK_NULL_HANDLE, handle<VkImageView>(0x2001), VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, handle<VkImageView>(0x2002), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        std::array<VkDescriptorImageInfo, 2> storage{{
            {VK_NULL_HANDLE, handle<VkImageView>(0x3001), VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, handle<VkImageView>(0x3002), VK_IMAGE_LAYOUT_GENERAL}}};
        std::array<VkDescriptorImageInfo, 2> samplers{{
            {handle<VkSampler>(0x4001), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
            {handle<VkSampler>(0x4002), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}}};
        const auto expectedBuffers = buffers;
        const auto expectedSampled = sampled;
        const auto expectedStorage = storage;
        const auto expectedSamplers = samplers;
        std::array<VkWriteDescriptorSet, 4> initial{};
        const std::array<unsigned, 4> indices{3, 17, 41, 53};
        const std::array<VkDescriptorType, 4> types{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER};
        for (unsigned i=0; i<initial.size(); ++i) {
            initial[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            initial[i].dstSet = oldSet;
            initial[i].dstBinding = indices[i];
            initial[i].descriptorCount = i == 0 ? 3 : 2;
            initial[i].descriptorType = types[i];
        }
        initial[0].pBufferInfo = buffers.data();
        initial[1].pImageInfo = sampled.data();
        initial[2].pImageInfo = storage.data();
        initial[3].pImageInfo = samplers.data();
        auto optional = CaptureInitialDrawDescriptors(initial);
        check(bool(optional) == enabled, "default-off captured infos or enabled capture missing");
        if (!enabled) {
            VkWriteDescriptorSet invalid{};
            check(CaptureInitialDrawDescriptors(std::span(&invalid,1)) == nullptr,
                "default-off inspected or copied invalid infos");
        }
        InitialDescriptorWrites plan(initial);
        check(plan.Size() == initial.size(), "binding count changed");
        buffers = {}; sampled = {}; storage = {}; samplers = {}; initial = {};
        auto writes = plan.ForSet(newSet);
        MockDevice mock;
        mock.Update(newSet, writes);
        check(mock.bindings.size() == 4, "a binding was omitted");
        for (unsigned i=0; i<indices.size(); ++i) {
            check(writes[i].dstBinding == indices[i] && writes[i].descriptorType == types[i], "binding/type changed");
            check(writes[i].descriptorCount == (i == 0 ? 3 : 2), "array count changed");
            check(writes[i].dstArrayElement == 0 && writes[i].pTexelBufferView == nullptr, "unexpected write fields");
        }
        for (unsigned i=0; i<expectedBuffers.size(); ++i) check(same(mock.bindings.at(3).buffers[i], expectedBuffers[i]), "buffer handle/offset/range changed");
        for (unsigned i=0; i<2; ++i) {
            check(same(mock.bindings.at(17).images[i], expectedSampled[i]), "sampled image handle/layout changed");
            check(same(mock.bindings.at(41).images[i], expectedStorage[i]), "storage image handle/layout changed");
            check(same(mock.bindings.at(53).images[i], expectedSamplers[i]), "sampler handle changed");
        }
        const auto* stableInfos = writes[0].pBufferInfo;
        writes.clear();
        auto another = plan.ForSet(newSet);
        check(another[0].pBufferInfo == stableInfos && same(stableInfos[1], expectedBuffers[1]), "replay infos did not retain stable ownership");
        VkDescriptorBufferInfo replacement{handle<VkBuffer>(0x5001), 0, 384};
        VkWriteDescriptorSet overwrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        overwrite.dstSet = newSet; overwrite.dstBinding = 3; overwrite.dstArrayElement = 1;
        overwrite.descriptorCount = 1; overwrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        overwrite.pBufferInfo = &replacement;
        mock.Update(newSet, std::span(&overwrite,1));
        check(mock.calls == 2, "snapshot replacement call missing");
        check(same(mock.bindings.at(3).buffers[0], expectedBuffers[0]) && same(mock.bindings.at(3).buffers[2], expectedBuffers[2]), "replacement touched other array elements");
        check(same(mock.bindings.at(3).buffers[1], replacement), "selected snapshot was not bound at offset zero");
        for (unsigned i=0; i<2; ++i) {
            check(same(mock.bindings.at(17).images[i], expectedSampled[i]), "replacement touched sampled image");
            check(same(mock.bindings.at(41).images[i], expectedStorage[i]), "replacement touched storage image");
            check(same(mock.bindings.at(53).images[i], expectedSamplers[i]), "replacement touched sampler");
        }
        rejected([&]{ plan.ForSet(VK_NULL_HANDLE); });
        auto valid = another[0]; valid.dstArrayElement = 2;
        InitialDescriptorWrites subrange(std::span(&valid,1));
        check(subrange.ForSet(newSet)[0].dstArrayElement == 2, "array start changed");
        auto single = another[1]; single.descriptorCount = 1;
        InitialDescriptorWrites scalar(std::span(&single,1));
        const auto scalarWrites = scalar.ForSet(newSet);
        check(scalarWrites[0].descriptorCount == 1 && same(scalarWrites[0].pImageInfo[0], expectedSampled[0]),
            "single-image descriptor changed");
        auto bad = valid; bad.descriptorCount = 0; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.pBufferInfo = nullptr; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.pImageInfo = expectedSampled.data(); rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = another[1]; bad.pImageInfo = nullptr; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = another[1]; bad.pBufferInfo = expectedBuffers.data(); rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        bad = valid; bad.pNext = &plan; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        VkBufferView texel{}; bad = valid; bad.pTexelBufferView = &texel; rejected([&]{ InitialDescriptorWrites item(std::span(&bad,1)); });
        if (enabled) check(optional->ForSet(newSet).size() == 4, "owned opt-in capture lost its initial arrays");
        std::cout << "PASS explicit draw descriptor capture/replay " << checks << " checks gate=" << enabled << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
