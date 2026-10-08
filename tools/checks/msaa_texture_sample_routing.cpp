#include "GroupedMsaa.hpp"
#include "CacheKey.hpp"
#include "ControlFlow/RequestSerializer.hpp"
#include "IntermediateRepresentation/IrBuilder.hpp"
#include "Optimization/DescriptorBindingBuilder.hpp"
#include "Optimization/ResourceMaterializer.hpp"
#include "ShaderDiskCache.hpp"
#include "SpirvBackend/SpirvEmitterInstructions.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace ShaderRecompiler;

// This focused fixture never reaches regular mip-allocation inference.
// Keep that external dependency explicit rather than substituting a layout.
namespace AgcDriver::Graphics {
bool LevelsFitAllocation(const GuestTextureResource&, std::uint32_t) {
    throw std::logic_error("regular mip-allocation adapter was unexpectedly reached");
}
}

static void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> static void rejects(F action, const char* reason) {
    try { action(); } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find(reason) != std::string::npos, error.what()); return;
    }
    throw std::runtime_error(std::string("expected rejection: ") + reason);
}

static std::array<std::uint32_t, 8> descriptor(std::uint32_t log, bool array) {
    return {0x120000u, 56u << 20u | 3u << 30u, 3u << 14u,
            0xfacu | log << 16u | 27u << 20u | (array ? 15u : 14u) << 28u,
            0u, log << 4u, 0u, 0u};
}

static ResourceSpecialization materialize(IrProgram& program, const std::array<std::uint32_t, 8>& words,
                                         std::uint32_t maximum, bool array, ResourceSnapshot& snapshot) {
    auto& plan = program.Resources();
    plan.stage = IrShaderStage::Compute; plan.userDataCount = 0;
    plan.resourceTrackingComplete = plan.srtPlanComplete = true;
    plan.groupedMsaaMaxSamples = maximum;
    IrBuilder ir(program);
    DescriptorSource source; source.dwordCount = 8;
    for (std::size_t i = 0; i < words.size(); ++i) {
        auto& v = ir.Constant(words[i]);
        v.SetImmediateU32(words[i]); source.dwords[i] = &v;
    }
    plan.descriptorSources.push_back(source); plan.materializationSources.push_back(0);
    ImageResource image; image.resourceClass = ImageResourceClass::Sampled;
    image.read = true; image.dimension = array ? RdnaImageDimension::Dim2DMsaaArray : RdnaImageDimension::Dim2DMsaa;
    plan.info.images.push_back(image);
    ResourceSpecialization specialization;
    ResourceMaterializer{}.Materialize(plan, {}, snapshot, specialization);
    ResourceMaterializer{}.Apply(program, specialization);
    return specialization;
}

static std::vector<std::uint32_t> emit(IrProgram& program, std::uint32_t sample, std::uint32_t layer,
                                     bool allowArray = true) {
    auto& image = program.Info().images[0];
    const auto kind = DescriptorBindingForImage(image);
    program.Metadata().bindings.descriptors.push_back({kind, {0}});
    const auto dimension = image.dimension;
    const bool array = dimension == RdnaImageDimension::Dim2DMsaaArray;
    MemoryInfo read; read.kind = ResourceKind::Image; read.resource = 0; read.dmask = 15;
    read.imageDimension = dimension; read.imageAddressComponents = array ? 4u : 3u;
    program.Resources().memoryInfo.push_back(read);
    program.Resources().memoryInfo.push_back(read);
    IrBuilder ir(program);
    auto& handle = program.CreateValue(IrOpcode::GetImageResource, IrType::ImageResource, 0);
    auto& address = program.CreateValue(IrOpcode::MakeImageAddress, IrType::ImageAddress);
    auto& x = ir.Constant(1u); auto& y = ir.Constant(2u);
    auto& z = ir.Constant(layer); auto& s = ir.Constant(sample);
    address.AddArgument(&x); address.AddArgument(&y);
    if (array) address.AddArgument(&z);
    address.AddArgument(&s);
    auto& active = ir.ConstantBool(true);
    auto& fetch = program.CreateValue(IrOpcode::ImageRead, IrType::U32x4, 0);
    fetch.AddArgument(&handle); fetch.AddArgument(&address); fetch.AddArgument(&active);
    auto& query = program.CreateValue(IrOpcode::ImageQueryDimensions, IrType::U32x4, 1);
    query.AddArgument(&handle); query.AddArgument(&address);
    SpirvEmitterState state(program, {});
    const std::array<std::uint32_t, 1> capabilities{spv::CapabilityImageMSArray};
    state.supportedCapabilities = allowArray ? std::span(capabilities) : std::span<const std::uint32_t>{};
    state.module.EmitCapability(spv::CapabilityShader);
    state.module.AddMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    DefineDescriptors(state);
    const auto function = state.module.AllocateId();
    const auto voidType = state.module.Type(spv::OpTypeVoid);
    const auto functionType = state.module.Type(spv::OpTypeFunction, voidType);
    state.module.AddFunction(spv::OpFunction, voidType, function, spv::FunctionControlMaskNone, functionType);
    EmitLabel(state, state.module.AllocateId());
    SpirvValueEmitContext ctx(state);
    EmitImageRead(ctx, fetch); EmitImageQueryDimensions(ctx, query);
    state.module.AddFunction(spv::OpReturn); state.module.AddFunction(spv::OpFunctionEnd);
    state.module.AddExecutionMode(function, spv::ExecutionModeLocalSize, 1u, 1u, 1u);
    state.module.EmitEntryPoint(spv::ExecutionModelGLCompute, function, "main", {});
    return state.module.Finalize();
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected shader output directory");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        std::size_t modules = 0, addresses = 0, rejected = 0;
        using namespace AgcDriver::Graphics;
        for (const bool array : {false, true}) {
            for (std::uint32_t log = 0; log < 4; ++log) {
                const auto words = descriptor(log, array);
                const auto guest = DecodeTextureResource(words);
                check(guest.samples == (1u << log) && guest.fragments == guest.samples, "incorrect fragment count");
                check(guest.mipCount == 1 && guest.lastLevel == 0 && guest.allocatedMipCount == 1, "sample count interpreted as mips");
                auto invalid = words; invalid[5] ^= 1u << 4u;
                rejects([&] { DecodeTextureResource(invalid); }, "fragment-count"); ++rejected;
                invalid = words; invalid[6] = 1u << 21u;
                rejects([&] { DecodeTextureResource(invalid); }, "compressed"); ++rejected;
                if (array) {
                    invalid = words; invalid[4] = 1u;
                    rejects([&] { DecodeTextureResource(invalid); }, "one logical layer"); ++rejected;
                }
                if (log == 0) {
                    rejects([&] { GroupedNativeSamples(1u, 4u); }, "one-fragment"); ++rejected;
                    continue;
                }
                const auto fragments = 1u << log;
                const auto native = fragments > 4 ? 4 : fragments;
                const auto groups = fragments / native;
                std::set<std::pair<std::uint32_t, std::uint32_t>> unique;
                for (std::uint32_t layer = 0; layer < 3; ++layer) {
                    for (std::uint32_t sample = 0; sample < fragments; ++sample) {
                        const auto mapped = GroupedSample(layer, sample, fragments, native);
                        check(mapped.layer == layer * groups + sample / native && mapped.sample == sample % native, "sample routing mismatch");
                        check(unique.emplace(mapped.layer, mapped.sample).second, "logical samples alias"); ++addresses;
                    }
                }
                ResourceSnapshot snapshot;
                IrProgram program;
                const auto specialization = materialize(program, words, 4u, array, snapshot);
                check(specialization.images[0].logicalSamples == fragments && specialization.images[0].nativeSamples == native, "specialization lost sample counts");
                const auto kind = DescriptorBindingForImage(program.Info().images[0]);
                BindingAllocationResult allocation; allocation.layout.descriptors.push_back({kind, {0}});
                DescriptorBindingBuilder{}.Populate(allocation, program.Info(), IrShaderStage::Compute, 0u, snapshot, {});
                check(allocation.bindings.size() == 1, "wrong descriptor count");
                const auto& binding = allocation.bindings[0];
                check(binding.imageShape == DescriptorImageShape::Image2DMsaaArray, "grouped descriptor must be multisampled array");
                check(binding.imageLogicalSamples == std::vector{fragments} && binding.imageNativeSamples == std::vector{native}, "binding sample metadata lost");
                RecompileResult result{}; result.bindings = allocation.bindings;
                std::vector<std::byte> encoded; ShaderDiskCache::EncodeResult(result, encoded);
                RecompileResult decoded{}; check(ShaderDiskCache::DecodeResult(encoded, decoded), "disk round trip failed");
                check(decoded.bindings[0].imageNativeSamples == binding.imageNativeSamples && decoded.bindings[0].imageLogicalSamples == binding.imageLogicalSamples, "disk round trip lost sample metadata");
                for (std::uint32_t sample = 0; sample < fragments; ++sample) {
                    // Use a new program because each emitted fixture owns its descriptor layout.
                    IrProgram fresh; ResourceSnapshot unused;
                    auto shaderWords = words; if (array) shaderWords[4] = 2u;
                    materialize(fresh, shaderWords, 4u, array, unused);
                    const auto spirv = emit(fresh, sample, array ? 2u : 0u);
                    const auto name = std::string(array ? "array-" : "2d-") + std::to_string(fragments) + "-" + std::to_string(sample) + ".spv";
                    std::ofstream stream(directory / name, std::ios::binary);
                    stream.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(std::uint32_t)); ++modules;
                }
                IrProgram missing; ResourceSnapshot unused; materialize(missing, words, 4u, array, unused);
                rejects([&] { emit(missing, 0u, 0u, false); }, "capability"); ++rejected;
                IrProgram natural; ResourceSnapshot naturalSnapshot;
                const auto nativeSpecialization = materialize(natural, words, 0u, array, naturalSnapshot);
                check(nativeSpecialization.images[0].nativeSamples == 0u && !(nativeSpecialization == specialization), "representation cache identity aliases");
                BindingAllocationResult naturalBinding; naturalBinding.layout.descriptors.push_back({DescriptorBindingForImage(natural.Info().images[0]), {0}});
                DescriptorBindingBuilder{}.Populate(naturalBinding, natural.Info(), IrShaderStage::Compute, 0u, naturalSnapshot, {});
                check(naturalBinding.bindings[0].imageShape == (array ? DescriptorImageShape::Image2DMsaaArray : DescriptorImageShape::Image2DMsaa), "native MSAA descriptor shape changed");
                RecompileRequest nativeRequest{}, groupedRequest{}; groupedRequest.target.groupedMsaaMaxSamples = 4;
                std::vector<std::uint64_t> key0, key1;
                RecompileCacheKey::Build(nativeRequest, key0); RecompileCacheKey::Build(groupedRequest, key1);
                check(key0 != key1 && RecompileCacheKey::ContextHash(nativeRequest) != RecompileCacheKey::ContextHash(groupedRequest), "source cache identity aliases grouped representation");
                std::vector<std::byte> disk0, disk1;
                ShaderDiskCache::BuildKey(nativeRequest, 32u, nativeSpecialization, disk0);
                ShaderDiskCache::BuildKey(groupedRequest, 32u, specialization, disk1);
                check(disk0 != disk1, "disk shader cache representation aliases");
                const auto restored = RequestSerializer{}.Deserialize(RequestSerializer{}.Serialize(groupedRequest));
                check(restored.request.target.groupedMsaaMaxSamples == 4u, "request serialization lost representation");
            }
        }
        std::cout << "MSAA texture routing: " << modules << " production SPIR-V fixtures, " << addresses << " distinct sample addresses, " << rejected << " rejection cases\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
