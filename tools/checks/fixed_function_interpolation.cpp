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
    std::cout<<"PASS "<<passed<<" actual fixed-function interpolation validation cases\n";
}
