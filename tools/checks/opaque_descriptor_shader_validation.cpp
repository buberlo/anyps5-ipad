// Original synthetic full production inspector fixtures; no GPU, game shaders
// or replacement type-table/validation implementation is used.
#include "prx/libSceAgcDriver/Graphics/include/ColorResolve.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
using namespace ShaderRecompiler;
using namespace AgcDriver::Graphics;
namespace {
unsigned accepted = 0, rejected = 0;
RecompileResult Load(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() % 4) throw std::runtime_error("invalid original fixture");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(file.tellg())/4); file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()),words.size()*4);
    if (!file) throw std::runtime_error("original fixture read failed");
    RecompileResult result{};result.spirv=std::move(words);return result;
}
void Save(const std::filesystem::path& path,const RecompileResult& shader) {
    std::ofstream file(path,std::ios::binary);const auto& w=shader.spirv.Words();
    file.write(reinterpret_cast<const char*>(w.data()),w.size()*4);
    if (!file) throw std::runtime_error("fixture write failed");
}
std::size_t Instruction(const RecompileResult& shader,spv::Op op) {
    const auto& w=shader.spirv.Words();
    for(std::size_t i=5;i<w.size();) {const auto n=w[i]>>16; if(!n||n>w.size()-i) throw std::runtime_error("malformed fixture instruction");
        if((w[i]&65535)==op) return i;i+=n;}
    throw std::runtime_error("fixture does not contain required instruction");
}
void Remove(RecompileResult& shader,spv::Op op) {
    auto words=shader.spirv.Words();const auto i=Instruction(shader,op);const auto n=words[i]>>16;
    words.erase(words.begin()+i,words.begin()+i+n);shader.spirv=std::move(words);
}
void Reject(const char* name,const std::function<void()>& action,const char* reason) {
    try {action();} catch(const std::runtime_error& e) {
        if(std::string(e.what()).find(reason)==std::string::npos) throw std::runtime_error(std::string(name)+": got '"+e.what()+"', wanted '"+reason+"'");
        ++rejected;return;
    }
    throw std::runtime_error(std::string(name)+": missing rejection");
}
void Validate(const RecompileResult& vs,const RecompileResult& ps,const SpirvTarget& target,bool rectangle) {
    State state{};state.stages.path=ShaderPath::Vertex;state.blends.resize(1);
    state.rectList=rectangle;state.topology=rectangle?VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.cullMode=VK_CULL_MODE_NONE;
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    subgroup.subgroupSize=32;subgroup.supportedStages=VK_SHADER_STAGE_ALL;subgroup.supportedOperations=0xff;
    std::vector<CompiledShader> stages{{ShaderStage::Vertex,&vs,0}};
    RectListShaders rect{};
    if(rectangle) {rect=BuildRectListShaders(vs,ps,target);
        stages.push_back({ShaderStage::TessellationControl,&rect.control,0});
        stages.push_back({ShaderStage::TessellationEvaluation,&rect.evaluation,0});}
    stages.push_back({ShaderStage::Fragment,&ps,0});
    const auto outputs=ValidateShaders(stages,state,subgroup,true,true,true,true,true,true);
    if(outputs!=std::set<std::uint32_t>{0}) throw std::runtime_error("production inspector lost color output");
}
DescriptorBinding Binding(unsigned index,DescriptorKind kind,DescriptorImageShape shape,unsigned count=1) {
    DescriptorBinding b{};b.kind=kind;b.role=kind==DescriptorKind::Sampler?DescriptorRole::GuestSamplers:DescriptorRole::GuestImages;
    b.descriptorSet=0;b.binding=index;b.count=count;if(kind!=DescriptorKind::Sampler) b.imageShape=shape;
    return b;
}
}
int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("usage: check fixture-directory generated-directory [legacy]");
        const std::filesystem::path in=argv[1],out=argv[2];std::filesystem::create_directories(out);
        const auto vs=Load(in/"vertex.spv");SpirvTarget target{};
        const std::array<std::uint32_t,3> caps{spv::CapabilityShader,spv::CapabilityTessellation,spv::CapabilityImageMSArray};target.supportedCapabilities=caps;
        target.spirvVersion=0x10300;target.vulkanVersion=0x401000;
        target.tessellation=TessellationTargetLimits{32,128,128,120,4096,128,128};
        target.groupedMsaaMaxSamples=4;
        for(unsigned samples:{2u,4u,8u}) for(unsigned binding:{0u,6u}) {
            ColorTarget source{};source.samples=source.fragments=samples;source.extent={17,19};source.elementBytes=4;
            source.format=VK_FORMAT_R8G8B8A8_UNORM;source.tileMode=ColorTileMode::RenderTarget;source.address=65536;
            RecompileResult previous{};if(binding){auto b=Binding(binding-1,DescriptorKind::Sampler,DescriptorImageShape::Image2D);previous.bindings.push_back(b);}
            const auto ps=BuildColorResolveFragment(source,previous);
            if(argc>3) {
                Reject("legacy exact factory failure",[&]{Validate(vs,ps,target,true);},"SPIR-V refers to an unknown type");
                std::puts("Legacy production ValidateShaders reproduces exact unknown-type rejection on original factory PS + canonical VS/TCS/TES.");return 0;
            }
            Validate(vs,ps,target,true);++accepted;
            const auto stem=std::to_string(samples)+"-"+std::to_string(binding);
            const auto rect=BuildRectListShaders(vs,ps,target);
            Save(out/("resolve-"+stem+".spv"),ps);Save(out/("control-"+stem+".spv"),rect.control);Save(out/("evaluation-"+stem+".spv"),rect.evaluation);
            auto missing=ps;Remove(missing,spv::OpTypeImage);Reject("missing image type",[&]{Validate(vs,missing,target,true);},"unknown type");
            auto mismatch=ps;mismatch.bindings[0].count=2;Reject("nonarray count",[&]{Validate(vs,mismatch,target,true);},"binding count");
            mismatch=ps;mismatch.bindings[0].kind=DescriptorKind::Sampler;Reject("image kind",[&]{Validate(vs,mismatch,target,true);},"descriptor type");
            mismatch=ps;mismatch.bindings[0].imageShape=DescriptorImageShape::Image2D;Reject("image shape",[&]{Validate(vs,mismatch,target,true);},"image shape");
            mismatch=ps;mismatch.bindings[0].imageShape.reset();Reject("absent shape",[&]{Validate(vs,mismatch,target,true);},"image shape");
            mismatch=ps;mismatch.bindings.clear();Reject("absent metadata",[&]{Validate(vs,mismatch,target,true);},"absent from recompiler");
            mismatch=ps;mismatch.spirv[Instruction(mismatch,spv::OpTypeImage)+7]=2;Reject("sampled storage mismatch",[&]{Validate(vs,mismatch,target,true);},"descriptor type");
        }
        for(const unsigned arrayCount:{0u,1u,2u}) {
            auto ps=Load(in/(arrayCount==0?"separate.spv":arrayCount==1?"separate-array-one.spv":"separate-array.spv"));
            const unsigned count=std::max(arrayCount,1u);
            ps.bindings={Binding(0,DescriptorKind::SampledImage,DescriptorImageShape::Image2D,count),Binding(1,DescriptorKind::Sampler,DescriptorImageShape::Image2D,count)};
            Validate(vs,ps,target,false);++accepted;
            auto wrong=ps;wrong.bindings[1].kind=DescriptorKind::StorageImage;
            Reject("sampler metadata kind",[&]{Validate(vs,wrong,target,false);},"descriptor type");
            wrong=ps;wrong.bindings[0].count=count+1;Reject("image array count",[&]{Validate(vs,wrong,target,false);},"binding count");
            wrong=ps;wrong.bindings[1].count=count+1;Reject("sampler array count",[&]{Validate(vs,wrong,target,false);},"binding count");
            wrong=ps;Remove(wrong,spv::OpTypeSampler);Reject("missing sampler type",[&]{Validate(vs,wrong,target,false);},"unknown type");
        }
        auto storage=Load(in/"storage.spv");storage.bindings={Binding(0,DescriptorKind::StorageImage,DescriptorImageShape::Image2D)};
        Validate(vs,storage,target,false);++accepted;
        auto wrong=storage;wrong.bindings[0].kind=DescriptorKind::SampledImage;Reject("storage image metadata kind",[&]{Validate(vs,wrong,target,false);},"descriptor type");
        auto combined=Load(in/"combined.spv");combined.bindings={Binding(0,DescriptorKind::SampledImage,DescriptorImageShape::Image2D)};
        Reject("unsupported combined descriptor",[&]{Validate(vs,combined,target,false);},"combined sampled image");
        std::printf("Full production ValidateShaders opaque descriptors: %u accepted, %u rejected; real resolve factory2/4/8 + canonical rectangles, separate/array samplers/images, storage images. No GPU/runtime claim.\n",accepted,rejected);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
