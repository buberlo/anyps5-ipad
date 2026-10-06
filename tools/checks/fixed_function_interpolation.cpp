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
    std::cout<<"PASS "<<passed<<" actual fixed-function interpolation validation cases\n";
}
