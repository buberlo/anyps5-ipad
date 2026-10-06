#include "Optimization/ConstantFolder.hpp"
#include "Optimization/DeadCodeEliminator.hpp"
#include "SpirvBackend/SpirvAnalysis.hpp"
#include "IntermediateRepresentation/IrBuilder.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ShaderRecompiler;
void check(std::uint32_t source, std::uint32_t bound, std::uint32_t mask, bool reverse, bool bounded, bool expect) {
    IrProgram p; p.Resources().stage = IrShaderStage::Vertex; p.Metadata().exportInfo.emplace_back();
    auto& block = p.CreateBlock(); p.SetEntryBlock(block); p.BlockOrder().push_back(&block);
    IrBuilder b(p); b.SetInsertionPoint(block);
    auto& lane = b.Emit(IrOpcode::LaneId, IrType::U32, {});
    auto& limit = b.Constant(bound);
    auto& selector = bounded ? b.Emit(IrOpcode::BitwiseAnd32, IrType::U32, reverse ? std::initializer_list<IrValue*>{&limit,&lane} : std::initializer_list<IrValue*>{&lane,&limit}) : lane;
    auto& constant = b.Constant(source);
    auto& shifted = b.Emit(IrOpcode::ShiftRightLogical32, IrType::U32, {&constant,&selector});
    auto& bitmask = b.Constant(mask);
    auto& output = b.Emit(IrOpcode::BitwiseAnd32, IrType::U32, reverse ? std::initializer_list<IrValue*>{&bitmask,&shifted} : std::initializer_list<IrValue*>{&shifted,&bitmask});
    // Keep the result alive as an export predicate, as in real graphics shaders.
    auto& zero = b.Constant(0);
    auto& predicate = b.Emit(IrOpcode::INotEqual32, IrType::U1, {&output,&zero});
    auto& vector = b.Emit(IrOpcode::CompositeConstructU32x4, IrType::Vec4U32, {&zero,&zero,&zero,&zero});
    static_cast<void>(b.Emit(IrOpcode::SetAttribute, IrType::Void, {&vector,&predicate}, 0));
    ConstantFolder{}.Fold(p);
    if (output.Resolve()->HasImmediate() != expect) throw std::runtime_error("unexpected fold decision");
    if (expect && output.Resolve()->ImmediateU32() != (source & mask)) throw std::runtime_error("wrong fold value");
    DeadCodeEliminator{}.RemoveIdentities(p); DeadCodeEliminator{}.Eliminate(p);
    bool laneLeft = false;
    for (const auto* v : block.Instructions()) laneLeft |= v->Opcode() == IrOpcode::LaneId;
    if (laneLeft == expect) throw std::runtime_error("dead lane query not eliminated or real lane query removed");
    if (AnalyzeProgramRequirements(p).subgroupLocalInvocationId == expect) throw std::runtime_error("incorrect subgroup requirement");
}
int main() {
    unsigned cases=0;
    for (bool reverse : {false,true}) {
        for (std::uint32_t bound : {1u,7u,15u,31u}) { check(0xffffffffu,bound,1,reverse,true,true); ++cases; }
        check(0xffffffffu,15,0xffff,reverse,true,true); ++cases;
        check(0xffffffffu,31,3,reverse,true,false); ++cases;
        check(0xfffffffeu,31,1,reverse,true,false); ++cases;
        check(0xffffffffu,32,1,reverse,true,false); ++cases;
        check(0xffffffffu,31,1,reverse,false,false); ++cases;
        check(0,31,1,reverse,true,true); ++cases;
    }
    std::printf("PASS %u actual IR constant-fold, dead-lane and subgroup-analysis cases\n",cases);
}
