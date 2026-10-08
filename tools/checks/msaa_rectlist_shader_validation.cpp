// Synthetic shader-only prerequisites. This is not GPU or dynamic alias qualification.
#include "BdaAbi.hpp"
#include "prx/libSceAgcDriver/Graphics/include/SplitDrawShaderValidation.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace ShaderRecompiler;
using namespace AgcDriver::Graphics;

namespace {

RecompileResult Load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() % 4 != 0) throw std::runtime_error("invalid fixture file");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(file.tellg()) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    if (!file) throw std::runtime_error("failed to read fixture");
    RecompileResult result{};
    result.spirv = std::move(words);
    return result;
}

void Save(const std::filesystem::path& directory, const std::string& name, const RecompileResult& result) {
    std::ofstream file(directory / (name + ".spv"), std::ios::binary);
    const auto& words = result.spirv.Words();
    file.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    if (!file) throw std::runtime_error("failed to write generated shader");
}

std::vector<std::size_t> Instructions(const RecompileResult& program, spv::Op op) {
    std::vector<std::size_t> positions;
    const auto& words = program.spirv.Words();
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> 16;
        if (count == 0 || count > words.size() - cursor) throw std::runtime_error("malformed test shader");
        if ((words[cursor] & 65535u) == op) positions.push_back(cursor);
        cursor += count;
    }
    return positions;
}

std::size_t First(const RecompileResult& program, spv::Op op) {
    const auto positions = Instructions(program, op);
    if (positions.empty()) throw std::runtime_error("required synthetic operation is missing");
    return positions.front();
}

std::size_t Find(const std::vector<std::size_t>& positions, const std::function<bool(std::size_t)>& predicate) {
    const auto found = std::find_if(positions.begin(), positions.end(), predicate);
    if (found == positions.end()) throw std::runtime_error("required synthetic diagnostic instruction is missing");
    return *found;
}

struct Fixture {
    RecompileResult vertex, fragment;
    RectListShaders auxiliary;
    std::array<CompiledShader, 4> Shaders() const {
        return {{{ShaderStage::Vertex, &vertex, 0},
                 {ShaderStage::TessellationControl, &auxiliary.control, 0},
                 {ShaderStage::TessellationEvaluation, &auxiliary.evaluation, 0},
                 {ShaderStage::Fragment, &fragment, 0}}};
    }
};

std::size_t passes = 0, rejects = 0;

void Expect(const char* name, std::string reason, bool accepted, const char* expected = "") {
    if (reason.empty() != accepted || (!accepted && reason.find(expected) == std::string::npos)) {
        throw std::runtime_error(std::string(name) + ": unexpected guard result '" + reason + "'");
    }
    if (accepted) ++passes; else ++rejects;
}

void Check(const char* name, const Fixture& fixture, const SpirvTarget& target, bool accepted, const char* expected = "") {
    Expect(name, SplitRectListShaderRejection(fixture.Shaders(), target), accepted, expected);
}

void BindingMutations(const Fixture& reference, const SpirvTarget& target) {
    // Every current DescriptorBinding field belongs to the generated stage's contract.
    const auto mutate = [&](const char* name, const std::function<void(DescriptorBinding&)>& edit) {
        auto fixture = reference;
        edit(fixture.auxiliary.control.bindings.at(0));
        Check(name, fixture, target, false);
    };
#define BINDING(field, value) mutate("binding." #field, [](auto& binding) { binding.field = value; })
    BINDING(kind, DescriptorKind::UniformBuffer);
    BINDING(role, DescriptorRole::GuestBuffers);
    BINDING(descriptorSet, 1);
    BINDING(binding, 77);
    BINDING(count, 2);
    BINDING(guestDescriptor, std::vector<std::uint32_t>{1});
    BINDING(readOnly, true);
    BINDING(imageShape, DescriptorImageShape::Image2DMsaaArray);
    BINDING(samplerDepthCompare, std::vector<bool>{false});
    BINDING(imageWritten, std::vector<bool>{false});
    BINDING(imageDepthCompare, std::vector<bool>{false});
    BINDING(imageAtomic, std::vector<bool>{false});
    BINDING(imageAtomic64, std::vector<bool>{false});
    BINDING(bufferAtomic, std::vector<bool>{false});
    BINDING(bufferWritten, std::vector<bool>{false});
    BINDING(samplerUnnormalized, std::vector<bool>{false});
    BINDING(imageUnnormalized, std::vector<bool>{false});
    BINDING(imageSamplers, std::vector<std::uint32_t>{0});
    BINDING(imageLogicalSamples, std::vector<std::uint32_t>{8});
    BINDING(imageNativeSamples, std::vector<std::uint32_t>{4});
#undef BINDING
    auto extra = reference;
    extra.auxiliary.control.bindings.push_back(extra.auxiliary.control.bindings.at(0));
    Check("extra control descriptor", extra, target, false);
    auto missing = reference;
    missing.auxiliary.control.bindings.clear();
    Check("missing fault descriptor", missing, target, false);
    auto evaluation = reference;
    evaluation.auxiliary.evaluation.bindings = reference.auxiliary.control.bindings;
    Check("evaluation descriptor injection", evaluation, target, false);
}

void MetadataMutations(const Fixture& reference, const SpirvTarget& target) {
    // Test both generated stages, including fields unrelated to present SPIR-V.
    for (const bool control : {true, false}) {
        const auto mutate = [&](const char* name, const std::function<void(RecompileResult&)>& edit) {
            auto fixture = reference;
            edit(control ? fixture.auxiliary.control : fixture.auxiliary.evaluation);
            Check(name, fixture, target, false);
        };
#define META(field, value) mutate("metadata." #field, [](auto& program) { program.field = value; })
        META(pushConstants, std::vector<std::byte>{std::byte{1}});
        META(memoryOffsetDword, 1);
        mutate("metadata.bdaAbiVersion", [](auto& program) { ++program.bdaAbiVersion; });
        mutate("metadata.vertexAttributes", [](auto& program) { program.vertexAttributes.push_back({0, 4, {}, 0}); });
        META(vertexOffsetSgpr, 0);
        META(instanceOffsetSgpr, 0);
        META(vertexOffsetShared, true);
        META(instanceOffsetShared, true);
        META(vertexOffsetConflict, true);
        META(instanceOffsetConflict, true);
        META(hostSubgroupSize, 32);
        META(parameterExports, std::vector<std::uint32_t>{0});
        mutate("metadata.fragmentParameters", [](auto& program) { program.fragmentParameters.push_back({0, 0, false, false}); });
        META(cacheHit, true);
        META(variantId, 1);
#undef META
    }
}

void DiagnosticMutations(const Fixture& reference, const SpirvTarget& target, const std::filesystem::path& output) {
    const auto& original = reference.auxiliary.control;
    const auto atomic = First(original, spv::OpAtomicCompareExchange);
    const auto state = original.spirv[atomic + 3];
    const auto writing = original.spirv[atomic + 7];
    // Find a diagnostic store via its storage-buffer access chain, rather than
    // guessing an instruction position or touching the tessellation outputs.
    const auto fault = First(original, spv::OpAtomicStore);
    const auto access = Instructions(original, spv::OpAccessChain);
    const auto stateAccess = Find(access, [&](auto at) { return original.spirv[at + 2] == state; });
    const auto faultVariable = original.spirv[stateAccess + 3];
    const auto stores = Instructions(original, spv::OpStore);
    const auto store = Find(stores, [&](auto at) {
        return std::any_of(access.begin(), access.end(), [&](auto ptr) {
            return original.spirv[ptr + 2] == original.spirv[at + 1] && original.spirv[ptr + 3] == faultVariable;
        });
    });
    const auto mutate = [&](const char* name, const std::function<void(RecompileResult&)>& edit) {
        auto fixture = reference;
        edit(fixture.auxiliary.control);
        Save(output, name, fixture.auxiliary.control);
        Check(name, fixture, target, false);
    };
    mutate("diagnostic_reason", [&](auto& program) { program.spirv[store + 2] = writing; });
    mutate("diagnostic_address", [&](auto& program) { program.spirv[store + 1] = state; });
    mutate("diagnostic_ready_state", [&](auto& program) { program.spirv[fault + 4] = writing; });
    mutate("diagnostic_atomic_add", [&](auto& program) {
        auto words = program.spirv.Words();
        if ((words[atomic] >> 16) != 9) throw std::runtime_error("unexpected diagnostic CAS shape");
        const std::array<std::uint32_t, 7> replacement{{(7u << 16) | spv::OpAtomicIAdd,
            words[atomic + 1], words[atomic + 2], words[atomic + 3], words[atomic + 4], words[atomic + 5], words[atomic + 7]}};
        words.erase(words.begin() + atomic, words.begin() + atomic + 9);
        words.insert(words.begin() + atomic, replacement.begin(), replacement.end());
        program.spirv = std::move(words);
    });
    mutate("diagnostic_descriptor_binding", [&](auto& program) {
        const auto decorations = Instructions(program, spv::OpDecorate);
        const auto at = Find(decorations, [&](auto at) {
            return program.spirv[at + 1] == faultVariable && program.spirv[at + 2] == spv::DecorationBinding;
        });
        program.spirv[at + 3] += 1;
    });
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 9) throw std::runtime_error("expected vertex fragment write-vertex write-fragment sample-fragment arbitrary-control arbitrary-evaluation output-directory");
        const auto output = std::filesystem::path(argv[8]);
        std::filesystem::create_directories(output);
        Fixture fixture{Load(argv[1]), Load(argv[2]), {}};
        fixture.vertex.parameterExports = {0};
        fixture.fragment.fragmentParameters = {{0, 0, false, false}};
        // Include a real read-only guest buffer and a nonzero binding in metadata:
        // the factory must reserve its diagnostic binding after both originals.
        fixture.vertex.bindings.push_back({DescriptorKind::StorageBuffer, DescriptorRole::GuestBuffers, 0, 2, 1, {}, true});
        fixture.fragment.bindings.push_back({DescriptorKind::StorageBuffer, DescriptorRole::GuestBuffers, 0, 5, 1, {}, true});
        const std::array<std::uint32_t, 2> capabilities{{spv::CapabilityShader, spv::CapabilityTessellation}};
        SpirvTarget target{};
        target.vulkanVersion = VK_API_VERSION_1_2;
        target.supportedCapabilities = capabilities;
        target.tessellation = TessellationTargetLimits{32, 128, 128, 120, 4096, 128, 128};
        target.groupedMsaaMaxSamples = 4;
        for (const auto version : {0x00010300u, 0x00010400u, 0x00010500u}) {
            target.spirvVersion = version;
            fixture.auxiliary = BuildRectListShaders(fixture.vertex, fixture.fragment, target);
            const auto again = BuildRectListShaders(fixture.vertex, fixture.fragment, target);
            if (fixture.auxiliary.control.spirv != again.control.spirv || fixture.auxiliary.evaluation.spirv != again.evaluation.spirv)
                throw std::runtime_error("factory is not deterministic");
            if (fixture.auxiliary.control.bindings.at(0).binding != 6 || fixture.auxiliary.control.bdaAbiVersion != BdaAbi::Version)
                throw std::runtime_error("factory fault binding/ABI did not follow original shaders");
            const auto name = std::to_string(version);
            Save(output, "canonical_control_" + name, fixture.auxiliary.control);
            Save(output, "canonical_evaluation_" + name, fixture.auxiliary.evaluation);
            Check("canonical factory", fixture, target, true);
            auto byte = fixture;
            byte.auxiliary.control.spirv[2] ^= 1; // Generator ID is valid SPIR-V, but not factory-identical.
            Save(output, "changed_control_generator_" + name, byte.auxiliary.control);
            Check("control byte mutation", byte, target, false);
            byte = fixture;
            byte.auxiliary.evaluation.spirv[2] ^= 1;
            Save(output, "changed_evaluation_generator_" + name, byte.auxiliary.evaluation);
            Check("evaluation byte mutation", byte, target, false);
        }
        BindingMutations(fixture, target);
        MetadataMutations(fixture, target);
        DiagnosticMutations(fixture, target, output);

        auto stages = fixture.Shaders();
        Expect("old guard still rejects tessellation stages", SplitDrawShaderRejection(stages), false);
        for (const auto count : {0u, 2u, 3u})
            Expect("wrong stage count", SplitRectListShaderRejection(std::span(stages).first(count), target), false);
        for (std::size_t i = 0; i < stages.size(); ++i) {
            auto changed = stages;
            changed[i].stage = ShaderStage::Compute;
            Expect("replacement stage", SplitRectListShaderRejection(changed, target), false);
            changed = stages;
            changed[i].program = nullptr;
            Expect("missing program", SplitRectListShaderRejection(changed, target), false);
        }
        for (const auto i : {1u, 2u}) {
            auto changed = stages;
            changed[i].pushConstantOffset = 4;
            Expect("auxiliary push constant offset", SplitRectListShaderRejection(changed, target), false);
        }
        auto replaced = fixture;
        replaced.auxiliary.control.spirv = Load(argv[6]).spirv;
        Check("arbitrary guest control", replaced, target, false);
        replaced = fixture;
        replaced.auxiliary.evaluation.spirv = Load(argv[7]).spirv;
        Check("arbitrary guest evaluation", replaced, target, false);
        auto stale = fixture;
        ++stale.fragment.bindings[0].binding;
        Check("original descriptors changed after generation", stale, target, false);
        stale = fixture;
        stale.vertex.parameterExports.clear();
        Check("original exports changed after generation", stale, target, false);
        stale = fixture;
        stale.fragment.fragmentParameters[0].flat = true;
        Check("original interpolation changed after generation", stale, target, false);
        auto wrongTarget = target;
        wrongTarget.spirvVersion = 0x00010400u;
        Check("wrong actual target version", fixture, wrongTarget, false);
        wrongTarget = target;
        wrongTarget.tessellation.reset();
        Check("missing device tessellation", fixture, wrongTarget, false);
        wrongTarget = target;
        wrongTarget.tessellation->maxPatchSize = 3;
        Check("insufficient actual target limits", fixture, wrongTarget, false);

        for (const auto [path, vertex, expected] : {
                std::tuple{argv[3], true, "nonlocal storage"},
                std::tuple{argv[4], false, "nonlocal storage"},
                std::tuple{argv[5], false, "builtin"}}) {
            auto unsafe = fixture;
            (vertex ? unsafe.vertex : unsafe.fragment).spirv = Load(path).spirv;
            // Fresh canonical auxiliaries must not authorize the unsafe originals.
            unsafe.auxiliary = BuildRectListShaders(unsafe.vertex, unsafe.fragment, target);
            Check("original guest shader rejection", unsafe, target, false, expected);
        }
        auto metadataWrite = fixture;
        metadataWrite.fragment.bindings[0].bufferWritten = {true};
        metadataWrite.auxiliary = BuildRectListShaders(metadataWrite.vertex, metadataWrite.fragment, target);
        Check("original guest write metadata", metadataWrite, target, false, "guest writes");

        std::printf("production canonical rect-list shader prerequisite: %zu accepts, %zu rejections passed\n", passes, rejects);
        std::printf("Factory FaultBuffer CAS/Store is accepted only with exact provenance; guest writes and sample-dependent originals remain rejected.\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
