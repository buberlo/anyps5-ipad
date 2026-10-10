#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Executes the source emitted by the real CompilerMSL. No copied MSL helpers.
int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc != 2) { std::fprintf(stderr, "usage: metal-probe generated.metal\n"); return 2; }
        NSError* error = nil;
        NSString* source = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]
                                                    encoding:NSUTF8StringEncoding error:&error];
        if (!source) { std::fprintf(stderr, "read source: %s\n", error.description.UTF8String); return 2; }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { std::fprintf(stderr, "No Metal device\n"); return 2; }
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.fastMathEnabled = NO;
        options.languageVersion = MTLLanguageVersion2_4;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
        if (!library) { std::fprintf(stderr, "Metal compile: %s\n", error.description.UTF8String); return 2; }
        id<MTLFunction> function = [library newFunctionWithName:@"main0"];
        id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
        if (!pipeline) { std::fprintf(stderr, "Metal pipeline: %s\n", error.description.UTF8String); return 2; }
        const uint32_t inputBits[] = {0, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x40000000u};
        id<MTLBuffer> inputs = [device newBufferWithBytes:inputBits length:sizeof(inputBits) options:MTLResourceStorageModeShared];
        id<MTLBuffer> results = [device newBufferWithLength:40 * sizeof(uint32_t) options:MTLResourceStorageModeShared];
        if (!inputs || !results) { std::fprintf(stderr, "Metal buffers unavailable\n"); return 2; }
        std::memset(results.contents, 0xcd, results.length);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!encoder) { std::fprintf(stderr, "Metal command encoder unavailable\n"); return 2; }
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:results offset:0 atIndex:0];
        [encoder setBuffer:inputs offset:0 atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
            std::fprintf(stderr, "Metal command: %s\n", command.error.description.UTF8String); return 2;
        }
        const uint32_t expected[20] = {
            0x80000000u, 0, 0x80000000u, 0xc0000000u,
            0x80000000u, 0, 0x80000000u, 0,
            0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u,
            0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u,
            0xc0800000u, 0xc0800000u, 0xc0800000u, 0xc0800000u
        };
        const auto* actual = static_cast<const uint32_t*>(results.contents);
        unsigned failures = 0;
        std::printf("device=%s\n", device.name.UTF8String);
        for (unsigned i = 0; i < 40; ++i) {
            const bool pass = actual[i] == expected[i % 20];
            failures += !pass;
            std::printf("row=%u type=%s actual=%08x expected=%08x pass=%u\n", i,
                        i < 20 ? "float" : "half", actual[i], expected[i % 20], pass);
        }
        std::printf("checked=40 failures=%u fastMath=disabled\n", failures);
        return failures ? 1 : 0;
    }
}
