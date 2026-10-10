#include "spirv_msl.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: translator input.spv output.metal");
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        const auto bytes = input.tellg();
        if (!input || bytes <= 0 || bytes % 4 != 0) throw std::runtime_error("invalid SPIR-V byte count");
        std::vector<uint32_t> words(static_cast<size_t>(bytes) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), bytes);
        if (!input) throw std::runtime_error("SPIR-V read failed");
        MVK_spirv_cross::CompilerMSL compiler(std::move(words));
        auto options = compiler.get_msl_options();
        options.msl_version = MVK_spirv_cross::CompilerMSL::Options::make_msl_version(2, 4);
        compiler.set_msl_options(options);
        // Match the host buffers explicitly instead of depending on reflection
        // declaration order. MoltenVK uses the same production binding API.
        for (uint32_t binding = 0; binding < 2; ++binding) {
            MVK_spirv_cross::MSLResourceBinding resource;
            resource.stage = spv::ExecutionModelGLCompute;
            resource.desc_set = 0;
            resource.binding = binding;
            resource.msl_buffer = binding;
            compiler.add_msl_resource_binding(resource);
        }
        const auto msl = compiler.compile();
        std::ofstream output(argv[2], std::ios::binary);
        output << msl;
        if (!output) throw std::runtime_error("MSL write failed");
        std::cout << "Translated through MoltenVK's production SPIRV-Cross CompilerMSL\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
