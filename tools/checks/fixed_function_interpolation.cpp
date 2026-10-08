#include "Translation/FixedFunctionInterpolation.hpp"
#include <functional>
#include <iostream>
using namespace ShaderRecompiler;
RdnaOperand reg(unsigned r) { RdnaOperand o; o.kind=RdnaOperandKind::VectorRegister; o.reg=r; return o; }
RdnaInstruction interp(RdnaOpcode op, unsigned dst, unsigned src, unsigned component=0) {
    RdnaInstruction i; i.op=op; i.destination=reg(dst); i.source0=reg(src); i.source1.value=0; i.source2.value=component; return i;
}
struct Case {
    RdnaProgram code;
    ControlFlowGraph cfg;
    ShaderPixelInputInfo pixel;
    Case() {
        cfg.blocks.emplace_back(); pixel.inputNum=1;
        pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveCenter)]=0;
        code.instructions={interp(RdnaOpcode::VInterpP1F32,4,0),interp(RdnaOpcode::VInterpP1F32,5,0,1),
                           interp(RdnaOpcode::VInterpP2F32,4,1),interp(RdnaOpcode::VInterpP2F32,5,1,1)};
    }
    void check() { ValidateFixedFunctionInterpolation(code,cfg,pixel); }
};
int main() {
    unsigned passed=0;
    const auto test=[&](const char* name,bool accept,const std::function<void(Case&)>& change) {
        Case c; change(c); bool ok=true;
        try { c.check(); } catch(const std::runtime_error&) { ok=false; }
        if(ok!=accept) { std::cerr<<"FAIL "<<name<<"\n"; std::exit(1); } ++passed;
    };
    test("perspective pair",true,[](Case&){});
    test("linear pair",true,[](Case& c){ c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveCenter)]=ShaderPixelInputInfo::NoPixelInputVgpr; c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::LinearCenter)]=0; });
    test("no interpolation or barycentric input",true,[](Case& c){c.code.instructions.clear();c.pixel.psInputVgpr.fill(ShaderPixelInputInfo::NoPixelInputVgpr);});
    test("arbitrary I",false,[](Case& c){c.code.instructions[0].source0.reg=2;});
    test("arbitrary J",false,[](Case& c){c.code.instructions[2].source0.reg=2;});
    test("wrong attribute",false,[](Case& c){c.code.instructions[2].source1.value=1;});
    test("wrong component",false,[](Case& c){c.code.instructions[2].source2.value=1;});
    test("missing P1",false,[](Case& c){c.code.instructions.erase(c.code.instructions.begin());});
    test("missing P2",false,[](Case& c){c.code.instructions.pop_back();});
    test("duplicate P1",false,[](Case& c){c.code.instructions[1]=c.code.instructions[0];});
    test("modified source",false,[](Case& c){c.code.instructions[0].source0.negate=true;});
    test("modified destination",false,[](Case& c){c.code.instructions[2].destination.omod=1;});
    test("clamped interpolation",false,[](Case& c){c.code.instructions[2].clampResult=true;});
    test("clobbered I",false,[](Case& c){RdnaInstruction i;i.destination=reg(0);c.code.instructions.insert(c.code.instructions.begin(),i);});
    test("clobbered J",false,[](Case& c){RdnaInstruction i;i.destination=reg(1);c.code.instructions.insert(c.code.instructions.begin(),i);});
    test("P1 writes input",false,[](Case& c){c.code.instructions[0].destination=reg(1);});
    test("partial result or EXEC change",false,[](Case& c){c.code.instructions.insert(c.code.instructions.begin()+2,RdnaInstruction{});});
    test("custom interpolation",false,[](Case& c){c.pixel.customInterpolationMask=1;});
    test("raw per-vertex value",false,[](Case& c){c.code.instructions[0].op=RdnaOpcode::VInterpMovF32;});
    test("centroid not represented",false,[](Case& c){c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveCenter)]=ShaderPixelInputInfo::NoPixelInputVgpr;c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveCentroid)]=0;});
    test("sample not represented",false,[](Case& c){c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveCenter)]=ShaderPixelInputInfo::NoPixelInputVgpr;c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::PerspectiveSample)]=0;});
    test("multiple basic blocks",false,[](Case& c){c.cfg.blocks.emplace_back();});
    test("direct raw barycentric arithmetic",false,[](Case& c){RdnaInstruction i;i.source0=reg(0);c.code.instructions.push_back(i);});
    test("ordinary overwritten input register",true,[](Case& c){RdnaInstruction i;i.destination=reg(0);c.code.instructions.push_back(i);i.destination=reg(1);c.code.instructions.push_back(i);i.destination=reg(4);i.source0=reg(0);c.code.instructions.push_back(i);});
    test("unmodified other half of input",false,[](Case& c){RdnaInstruction i;i.destination=reg(0);c.code.instructions.push_back(i);i.destination=reg(4);i.source0=reg(0);c.code.instructions.push_back(i);});
    test("out of range component",false,[](Case& c){c.code.instructions[0].source2.value=4;});
    test("memory overwrites entire input pair",true,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::MIMG;i.dataDwordCount=4;i.destination=reg(0);c.code.instructions.push_back(i);i.family=RdnaInstructionFamily::VOP3;i.source0=reg(0);i.destination=reg(4);c.code.instructions.push_back(i);});
    test("partial memory write retains J",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::MIMG;i.dataDwordCount=1;i.destination=reg(0);c.code.instructions.push_back(i);i.family=RdnaInstructionFamily::VOP3;i.source0=reg(1);i.destination=reg(4);c.code.instructions.push_back(i);});
    test("EXEC reactivation after writes",false,[](Case& c){RdnaInstruction i;i.destination=reg(0);c.code.instructions.push_back(i);i.destination.kind=RdnaOperandKind::ExecLo;c.code.instructions.push_back(i);});
    test("DPP reads possibly inactive lane",false,[](Case& c){RdnaInstruction i;i.source0=reg(4);i.source0.dpp=true;c.code.instructions.push_back(i);});
    test("P1 consumes I after other components",true,[](Case& c){c.code.instructions[1].destination=reg(0);c.code.instructions[3].destination=reg(0);});
    test("P1 consumes J cannot complete",false,[](Case& c){c.code.instructions[1].destination=reg(1);c.code.instructions[3].destination=reg(1);});
    test("raw I reused after in-place P1",false,[](Case& c){c.code.instructions[0].destination=reg(0);c.code.instructions[2].destination=reg(0);});
    test("independent ALU between pairs",true,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP3;i.destination=reg(12);i.source0=reg(8);i.source1=reg(9);i.source2=reg(10);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("ALU observes partial",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP1;i.destination=reg(12);i.source0=reg(4);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("ALU overwrites partial",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP1;i.destination=reg(5);i.source0=reg(9);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("wide ALU observes partial",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP1;i.is64Bit=true;i.destination=reg(12);i.source0=reg(3);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("wide ALU overwrites partial",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP1;i.is64Bit=true;i.destination=reg(3);i.source0=reg(8);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("secondary destination overwrites partial",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP3;i.destination=reg(12);i.destination2=reg(4);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("secondary destination changes EXEC",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP3;i.destination=reg(12);i.destination2.kind=RdnaOperandKind::ExecLo;c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("memory side effects between pairs",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::MIMG;i.destination=reg(12);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("compare between pairs",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOPC;i.destination=reg(12);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    test("independent ALU still reads raw J",false,[](Case& c){RdnaInstruction i;i.family=RdnaInstructionFamily::VOP1;i.destination=reg(12);i.source0=reg(1);c.code.instructions.insert(c.code.instructions.begin()+2,i);});
    const auto sampled = [](Case& c) {
        RdnaInstruction i;
        i.family=RdnaInstructionFamily::MIMG; i.op=RdnaOpcode::ImageSample;
        i.opcodeId=i.imageOpcodeId=0x20; i.imageDimension=RdnaImageDimension::Dim2D;
        i.imageAddressComponents=2; i.wordCount=2; i.sourceCount=3; i.dataDwordCount=4;
        i.destination=reg(12); i.source0=reg(8);
        i.source1.kind=i.source2.kind=RdnaOperandKind::ScalarRegister;
        c.code.instructions.insert(c.code.instructions.begin()+2,i);
    };
    test("independent read-only 2D sample between pairs",true,sampled);
    test("sample consumes partial coordinate",false,[&](Case& c){sampled(c);c.code.instructions[2].source0=reg(4);});
    test("sample coordinate second word is partial",false,[&](Case& c){sampled(c);c.code.instructions[2].source0=reg(3);});
    test("sample overwrites partial in fourth result",false,[&](Case& c){sampled(c);c.code.instructions[2].destination=reg(2);});
    test("sample reads original raw J",false,[&](Case& c){sampled(c);c.code.instructions[2].source0=reg(1);});
    test("sample writes original raw J",false,[&](Case& c){sampled(c);c.code.instructions[2].destination=reg(1);});
    test("sample with status result rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].rawWords[0]=0x10000;});
    test("sample with packed results rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].imageD16=true;});
    test("sample with packed addresses rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].imageA16=true;});
    test("sample with NSA coordinates rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].imageNsaDwordCount=1;});
    test("sample with explicit LOD rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].imageSampleFlags=RdnaImageSampleFlagLod;});
    test("sample with ambiguous footprint rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].dataDwordCount=0;});
    test("sample with unknown address count rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].imageAddressComponents=0;});
    test("sample with modified coordinates rejected",false,[&](Case& c){sampled(c);c.code.instructions[2].source0.negate=true;});
    test("sample secondary result overlaps partial",false,[&](Case& c){sampled(c);c.code.instructions[2].destination2=reg(4);});
    test("sample secondary result changes EXEC",false,[&](Case& c){sampled(c);c.code.instructions[2].destination2.kind=RdnaOperandKind::ExecLo;});
    test("sample cannot hide missing P2",false,[&](Case& c){sampled(c);c.code.instructions.pop_back();});
    const auto branching = [](Case& c) {
        RdnaInstruction write; write.family=RdnaInstructionFamily::MIMG; write.destination=reg(0); write.dataDwordCount=4; c.code.instructions.push_back(write);
        RdnaInstruction branch; branch.op=RdnaOpcode::SBranch; c.code.instructions.push_back(branch);
        RdnaInstruction use; use.family=RdnaInstructionFamily::VOP1; use.destination=reg(8); use.source0=reg(0); c.code.instructions.push_back(use);
        c.cfg.entryBlock=0; c.cfg.blocks[0].instructionBegin=0; c.cfg.blocks[0].instructionEnd=6;
        c.cfg.blocks.emplace_back(); c.cfg.blocks[1].id=1; c.cfg.blocks[1].instructionBegin=6; c.cfg.blocks[1].instructionEnd=7;
    };
    test("branch after complete consumed entry prefix",true,branching);
    test("branch retains raw J",false,[&](Case& c){branching(c);c.code.instructions[4].dataDwordCount=1;});
    test("pair crosses entry boundary",false,[&](Case& c){branching(c);c.cfg.blocks[0].instructionEnd=2;});
    test("later interpolation block",false,[&](Case& c){branching(c);c.code.instructions.push_back(interp(RdnaOpcode::VInterpP1F32,8,0));});
    test("loop back into interpolation entry",false,[&](Case& c){branching(c);c.cfg.blocks[0].predecessors.push_back(1);});
    test("unknown entry block",false,[&](Case& c){branching(c);c.cfg.entryBlock=99;});
    test("entry skips initial code",false,[&](Case& c){branching(c);c.cfg.blocks[0].instructionBegin=1;});
    test("entry exceeds decoded code",false,[&](Case& c){branching(c);c.cfg.blocks[0].instructionEnd=99;});
    test("irreducible continuation",false,[&](Case& c){branching(c);c.cfg.irreducible=true;});
    test("unsupported continuation",false,[&](Case& c){branching(c);c.cfg.unsupported=true;});
    const auto scalarBetween = [](Case& c, RdnaOpcode op, RdnaInstructionFamily family) {
        RdnaInstruction i; i.op=op; i.family=family;
        i.destination.kind=RdnaOperandKind::ScalarRegister; i.destination.reg=16;
        i.source0.kind=RdnaOperandKind::ScalarRegister; i.source0.reg=12;
        c.code.instructions.insert(c.code.instructions.begin()+2,i);
    };
    test("scalar descriptor load between pairs",true,[&](Case& c){scalarBetween(c,RdnaOpcode::SBufferLoadDwordx4,RdnaInstructionFamily::SMEM);c.code.instructions[2].dataDwordCount=4;});
    test("two-word scalar load into VCC",true,[&](Case& c){scalarBetween(c,RdnaOpcode::SBufferLoadDwordx2,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.kind=RdnaOperandKind::VccLo;c.code.instructions[2].dataDwordCount=2;});
    test("scalar load may not overrun VCC low",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SBufferLoadDwordx4,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.kind=RdnaOperandKind::VccLo;c.code.instructions[2].dataDwordCount=4;});
    test("scalar load may not overrun VCC high",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SBufferLoadDwordx2,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.kind=RdnaOperandKind::VccHi;c.code.instructions[2].dataDwordCount=2;});
    test("wait counter between pairs",true,[&](Case& c){scalarBetween(c,RdnaOpcode::SWaitcnt,RdnaInstructionFamily::SOPP);c.code.instructions[2].destination={};});
    test("NOP between pairs",true,[&](Case& c){scalarBetween(c,RdnaOpcode::SNop,RdnaInstructionFamily::SOPP);c.code.instructions[2].destination={};});
    test("scalar load cannot read vector partial",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SLoadDword,RdnaInstructionFamily::SMEM);c.code.instructions[2].source0=reg(4);});
    test("scalar load cannot change M0",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SLoadDword,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.kind=RdnaOperandKind::M0;});
    test("scalar load cannot reach special registers",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SLoadDwordx16,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.reg=100;c.code.instructions[2].dataDwordCount=16;});
    test("scalar load cannot change EXEC",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SLoadDword,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination.kind=RdnaOperandKind::ExecLo;});
    test("scalar load secondary write cannot change EXEC",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SLoadDword,RdnaInstructionFamily::SMEM);c.code.instructions[2].destination2.kind=RdnaOperandKind::ExecLo;});
    test("scalar unknown memory opcode stays rejected",false,[&](Case& c){scalarBetween(c,RdnaOpcode::Unknown,RdnaInstructionFamily::SMEM);});
    test("barrier between pairs stays rejected",false,[&](Case& c){scalarBetween(c,RdnaOpcode::SBarrier,RdnaInstructionFamily::SOPP);});
    const auto discardDiamond = [](Case& c) {
        RdnaInstruction alu; alu.family=RdnaInstructionFamily::VOP1; alu.destination=reg(12); alu.source0=reg(4);
        RdnaInstruction branch; branch.op=RdnaOpcode::SCbranchScc0;
        RdnaInstruction exec; exec.op=RdnaOpcode::SWqmB64; exec.family=RdnaInstructionFamily::SOP1;
        exec.destination.kind=RdnaOperandKind::ExecLo; exec.source0.kind=RdnaOperandKind::ScalarRegister;
        RdnaInstruction end; end.op=RdnaOpcode::SEndpgm;
        RdnaInstruction zero; zero.op=RdnaOpcode::SMovB64; zero.destination.kind=RdnaOperandKind::ExecLo;
        zero.source0.kind=RdnaOperandKind::IntegerInlineConstant;
        RdnaInstruction output; output.op=RdnaOpcode::Exp; output.exportIsLast=true; output.exportValidMask=true;
        c.code.instructions.insert(c.code.instructions.end(),{alu,branch,exec,
            interp(RdnaOpcode::VInterpP1F32,8,0),interp(RdnaOpcode::VInterpP2F32,8,1),end,zero,output,end});
        c.cfg.entryBlock=0; c.cfg.blocks.resize(3);
        auto& a=c.cfg.blocks[0]; auto& b=c.cfg.blocks[1]; auto& d=c.cfg.blocks[2];
        a.id=0; a.instructionBegin=0; a.instructionEnd=6; a.successors={1,2};
        a.terminator.kind=TerminatorKind::ConditionalBranch; a.terminator.condition=BranchCondition::SccZero;
        a.terminator.trueBlock=2; a.terminator.falseBlock=1;
        b.id=1; b.instructionBegin=6; b.instructionEnd=10; b.predecessors={0}; b.terminator.kind=TerminatorKind::Return;
        d.id=2; d.instructionBegin=10; d.instructionEnd=13; d.predecessors={0}; d.terminator.kind=TerminatorKind::Return;
    };
    test("discard-only branch preserves I/J for later interpolation",true,discardDiamond);
    test("discard diamond with changed I stays rejected",false,[&](Case& c){discardDiamond(c);c.code.instructions[4].destination=reg(0);});
    test("discard diamond with changed J stays rejected",false,[&](Case& c){discardDiamond(c);c.code.instructions[4].destination=reg(1);});
    test("discard cannot keep active EXEC",false,[&](Case& c){discardDiamond(c);c.code.instructions[10].source0.value=1;});
    test("discard cannot export a color component",false,[&](Case& c){discardDiamond(c);c.code.instructions[11].exportEnableMask=1;});
    test("discard cannot consume raw inputs",false,[&](Case& c){discardDiamond(c);c.code.instructions[11].sourceCount=1;});
    test("discard must perform a final export",false,[&](Case& c){discardDiamond(c);c.code.instructions[11].exportIsLast=false;});
    test("discard must terminate",false,[&](Case& c){discardDiamond(c);c.code.instructions[12].op=RdnaOpcode::SBranch;});
    test("surviving path must terminate",false,[&](Case& c){discardDiamond(c);c.code.instructions[9].op=RdnaOpcode::SBranch;});
    test("discard diamond may not merge back",false,[&](Case& c){discardDiamond(c);c.cfg.blocks[2].successors={1};c.cfg.blocks[1].predecessors={0,2};});
    test("discard diamond may not loop",false,[&](Case& c){discardDiamond(c);c.cfg.backEdges.push_back({1,0,false});c.cfg.blocks[0].predecessors={1};});
    test("discard branch may not split P1/P2",false,[&](Case& c){discardDiamond(c);c.code.instructions[4]=interp(RdnaOpcode::VInterpP1F32,12,0);});
    test("discard diamond must cover every instruction",false,[&](Case& c){discardDiamond(c);c.cfg.blocks[1].instructionBegin=7;});
    test("discard diamond cannot alias block ids",false,[&](Case& c){discardDiamond(c);c.cfg.blocks[2].id=1;});
    test("discard branch must match decoded control flow",false,[&](Case& c){discardDiamond(c);c.code.instructions[5].op=RdnaOpcode::SSetpcB64;});
    const auto outputOnly = [](Case& c, unsigned mask, bool compressed) {
        c.code.instructions.clear();
        RdnaInstruction output;
        output.op=RdnaOpcode::Exp; output.family=RdnaInstructionFamily::EXP;
        output.exportEnableMask=mask; output.exportIsCompressed=compressed;
        output.source0=reg(2); output.source1=reg(3); output.source2=reg(6); output.source3=reg(7);
        c.code.instructions.push_back(output);
    };
    test("depth export ignores disabled raw-I operands",true,[&](Case& c){outputOnly(c,1,false);c.code.instructions[0].source1=reg(0);c.code.instructions[0].source2=reg(0);c.code.instructions[0].source3=reg(0);});
    test("scalar export does not read adjacent raw I/J",true,[&](Case& c){outputOnly(c,2,false);c.pixel.psInputVgpr[static_cast<unsigned>(PixelInput::LinearCenter)]=4;});
    for(unsigned channel=0;channel<4;++channel) {
        const auto change=[&](Case& c,unsigned mask){outputOnly(c,mask,false);auto& i=c.code.instructions[0];std::array<RdnaOperand*,4> sources{&i.source0,&i.source1,&i.source2,&i.source3};*sources[channel]=reg(0);};
        test("enabled export channel reads raw I",false,[&](Case& c){change(c,1u<<channel);});
        test("disabled export channel ignores raw I",true,[&](Case& c){change(c,15u & ~(1u<<channel));});
    }
    test("packed XY export ignores disabled ZW raw I",true,[&](Case& c){outputOnly(c,3,true);c.code.instructions[0].source1=reg(0);});
    test("packed ZW export reads raw I",false,[&](Case& c){outputOnly(c,12,true);c.code.instructions[0].source1=reg(0);});
    test("empty export ignores all operands",true,[&](Case& c){outputOnly(c,0,false);auto& i=c.code.instructions[0];i.source0=i.source1=i.source2=i.source3=reg(0);});
    std::cout<<"PASS "<<passed<<" actual fixed-function interpolation validation cases\n";
}
