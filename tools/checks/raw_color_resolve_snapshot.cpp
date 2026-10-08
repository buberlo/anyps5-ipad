// Original fixture links production snapshot/transfer/layout code. Native
// mapping checks are real; pending GPU flush and write stamps are explicit mocks.
#include "prx/libSceAgcDriver/Graphics/include/ColorResolve.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>

using namespace AgcDriver;
using namespace AgcDriver::Graphics;
namespace {
std::function<void(std::uint64_t)> beforeRead;
std::uint64_t lastStamp = 0;
unsigned reads = 0, writes = 0, stamps = 0, assertions = 0;
void check(bool value, const char* reason) { ++assertions; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F f) {
    const auto previous = reads;
    try { f(); } catch (const std::runtime_error&) { check(reads == previous, "invalid source read before rejection"); return; }
    throw std::runtime_error("missing source rejection");
}
struct Allocation {
    std::byte* data = nullptr;
    explicit Allocation(std::size_t bytes) {
        check(posix_memalign(reinterpret_cast<void**>(&data), 65536, bytes) == 0, "allocation");
        std::memset(data, 0xa5, bytes);
    }
    ~Allocation() { std::free(data); }
};
ColorTarget color(unsigned w, unsigned h, unsigned samples, std::byte* address) {
    ColorTarget c{};
    c.address = c.surfaceAddress = reinterpret_cast<std::uintptr_t>(address);
    c.extent = c.surfaceExtent = {w, h}; c.samples = c.fragments = samples;
    c.elementBytes = 4; c.format = VK_FORMAT_R8G8B8A8_UNORM;
    c.componentMapping = 0xe4; c.tileMode = ColorTileMode::RenderTarget;
    c.depth = c.mipCount = 1;
    c.bytes = ColorTargetLayout(w, h, c.tileMode, 4, samples).Bytes();
    return c;
}
}
namespace AgcDriver::GuestMemory {
void AdapterRead(std::uint64_t, std::span<std::byte>, std::size_t);
void AdapterWrite(std::uint64_t, std::span<const std::byte>, std::size_t);
void Read(std::uint64_t address, std::span<std::byte> out, std::size_t alignment) {
    ++reads;
    if (beforeRead) beforeRead(address); // Explicit ordering mock, no GPU work.
    AdapterRead(address, out, alignment);
}
void Write(std::uint64_t address, std::span<const std::byte> in, std::size_t alignment) {
    ++writes; AdapterWrite(address, in, alignment); MarkWritten(address, in.size());
}
std::uint64_t MarkWritten(std::uint64_t address, std::size_t bytes) {
    CheckRange(reinterpret_cast<void*>(address), bytes, 1, true);
    ++stamps; lastStamp = address; return stamps;
}
}
int main() {
    const char* option = std::getenv("APS5_RAW_RESOLVE_SNAPSHOT");
    const bool raw = option && std::strcmp(option, "1") == 0;
    unsigned cases = 0;
    for (const unsigned samples : {2u, 4u, 8u}) {
        for (const auto extent : {VkExtent2D{19,11}, VkExtent2D{129,67}}) {
            const ColorTargetLayout layout(extent.width, extent.height, ColorTileMode::RenderTarget, 4, samples);
            Allocation original(layout.Bytes());
            auto source = color(extent.width, extent.height, samples, original.data);
            std::vector<std::byte> expected(layout.LinearBytes());
            for (std::size_t i=0; i<expected.size(); ++i) expected[i]=std::byte((i*37u + i/13u) & 255u);
            layout.Tile(expected, {original.data, static_cast<std::size_t>(layout.Bytes())});
            for (const bool diagnostics : {false, true}) {
                // Simulate a queued producer which completes at the production
                // source Read boundary; its pixels must be in the snapshot.
                expected[0] ^= std::byte{0x71};
                unsigned flushed = 0;
                beforeRead = [&](std::uint64_t address) {
                    if (address != source.address) return;
                    ++flushed;
                    layout.Tile(expected, {original.data, static_cast<std::size_t>(layout.Bytes())});
                };
                reads=writes=stamps=0; lastStamp=0;
                auto captured = CaptureColorResolveSource(source, diagnostics);
                beforeRead = {};
                ++cases;
                check(flushed==1, "source flush ordering differs");
                check(captured.rawCopy==raw, "exact flag selection differs");
                check(captured.source.address!=source.address && captured.source.address%65536==0, "private alignment");
                check(captured.source.surfaceAddress==captured.source.address && captured.source.bytes==source.bytes, "immutable metadata");
                check(stamps==1 && lastStamp==captured.source.address, "copied bytes lack driver invalidation");
                check(raw ? (reads==1 && writes==0) : (reads==2 && writes==1), "transfer count differs");
                std::vector<std::byte> actual(layout.LinearBytes());
                layout.Detile({captured.backing.get(), static_cast<std::size_t>(source.bytes)}, actual);
                check(actual==expected, "captured logical samples differ");
                check(diagnostics ? captured.diagnosticPixels==expected : captured.diagnosticPixels.empty(), "diagnostic source differs");
                if (raw) check(std::memcmp(captured.backing.get(), original.data, source.bytes)==0, "raw padding differs");
                // Original bytes may become the aliased destination or be reused
                // after capture. Held backing must remain immutable until release.
                auto retained = captured.backing;
                auto lifetime = std::weak_ptr<std::byte>(retained);
                std::memset(original.data, 0x3c, source.bytes);
                layout.Detile({retained.get(), static_cast<std::size_t>(source.bytes)}, actual);
                check(actual==expected, "source reuse changes snapshot");
                captured.backing.reset(); check(!lifetime.expired(), "retained backing lost");
                retained.reset(); check(lifetime.expired(), "backing leak");
                auto bad=source; bad.bytes-=1; rejects([&]{CaptureColorResolveSource(bad,false);});
                bad=source; bad.address+=4; rejects([&]{CaptureColorResolveSource(bad,false);});
                bad=source; bad.dccAddress=0x10000; rejects([&]{CaptureColorResolveSource(bad,false);});
                bad=source; bad.fragments=1; rejects([&]{CaptureColorResolveSource(bad,false);});
            }
        }
    }
    std::printf("PASS raw=%d cases=%u assertions=%u; native mapping adapter, mocked flush/stamps; no GPU/Wine/performance proof\n",raw,cases,assertions);
}
