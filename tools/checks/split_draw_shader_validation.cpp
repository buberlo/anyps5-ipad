// Uses synthetic, independently validated SPIR-V; no game code or GPU is needed.
#include "prx/libSceAgcDriver/Graphics/include/SplitDrawShaderValidation.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace AgcDriver::Graphics;

ShaderRecompiler::RecompileResult Load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() % 4 != 0) throw std::runtime_error("invalid fixture file");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(file.tellg()) / 4);
    file.seekg(0); file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    if (!file) throw std::runtime_error("failed to read fixture");
    ShaderRecompiler::RecompileResult result{};
    result.spirv = std::move(words);
    return result;
}

int main(int argc, char** argv) {
    try {
        if (argc < 4 || (argc - 1) % 3 != 0) throw std::runtime_error("expected triples: vertex.spv fragment.spv expected-reason-or-pass");
        std::size_t cases = 0;
        for (int i = 1; i < argc; i += 3) {
            auto vertex = Load(argv[i]), fragment = Load(argv[i + 1]);
            const std::array<CompiledShader, 2> shaders{{{ShaderRecompiler::ShaderStage::Vertex, &vertex, 0}, {ShaderRecompiler::ShaderStage::Fragment, &fragment, 0}}};
            const auto reason = SplitDrawShaderRejection(shaders);
            const bool pass = std::strcmp(argv[i + 2], "pass") == 0;
            if ((pass && !reason.empty()) || (!pass && reason.find(argv[i + 2]) == std::string::npos)) {
                std::fprintf(stderr, "fixture %s expected %s, got '%s'\n", argv[i + 1], argv[i + 2], reason.c_str());
                return 1;
            }
            ++cases;
        }
        std::printf("production split-draw shader eligibility: %zu cases passed\n", cases);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
