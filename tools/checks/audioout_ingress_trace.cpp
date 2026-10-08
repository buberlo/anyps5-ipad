#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif
#include "SDL.h"
#include "AudioOutIngressTrace.hpp"

struct Mock {
    int cvt = 0, convertResult = 0, queueResult = 0, busy = 0;
    bool alwaysBusy = false, mutateOnSleep = false;
    std::uint64_t us = 0;
    unsigned char* source = nullptr;
    std::size_t sourceBytes = 0;
    std::vector<std::string> calls;
    std::vector<unsigned char> queued;
} mock;
static std::uint64_t sceKernelGetProcessTime() { return mock.us; }
static int fixture_nanosleep(const struct timespec*, struct timespec*) {
    mock.calls.push_back("sleep");
    mock.us += mock.alwaysBusy ? 50000 : 1000;
    if (mock.mutateOnSleep) {
        std::memset(mock.source, 0x3c, mock.sourceBytes);
        mock.mutateOnSleep = false;
    }
    return 0;
}
extern "C" {
int SDL_InitSubSystem(Uint32) { return 0; }
Uint32 SDL_WasInit(Uint32 flags) { return flags; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained, int) {
    *obtained = *desired; return 1;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
void SDL_CloseAudioDevice(SDL_AudioDeviceID) { mock.calls.push_back("close"); }
void SDL_ClearQueuedAudio(SDL_AudioDeviceID) { mock.calls.push_back("clear"); mock.busy = 0; mock.alwaysBusy = false; }
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID) {
    mock.calls.push_back("get");
    if (mock.alwaysBusy) return 65536;
    if (mock.busy > 0) { --mock.busy; return 65536; }
    return 0;
}
const char* SDL_GetError() { return "original mock error"; }
int SDL_BuildAudioCVT(SDL_AudioCVT* cvt, SDL_AudioFormat, Uint8, int, SDL_AudioFormat, Uint8, int) {
    mock.calls.push_back("build"); cvt->len_mult = 8; return mock.cvt;
}
int SDL_ConvertAudio(SDL_AudioCVT* cvt) {
    mock.calls.push_back("convert");
    if (mock.convertResult < 0) return mock.convertResult;
    // Original deterministic conversion boundary mock, not SDL's resampler.
    for (int index = 0; index < cvt->len / 2; ++index) cvt->buf[index] = static_cast<Uint8>(index * 29 + 17);
    cvt->len_cvt = cvt->len / 2; return 0;
}
int SDL_QueueAudio(SDL_AudioDeviceID, const void* data, Uint32 bytes) {
    mock.calls.push_back("queue");
    const auto* begin = static_cast<const unsigned char*>(data);
    mock.queued.assign(begin, begin + bytes);
    return mock.queueResult;
}
}

#include "production_audioout_ingress.inc"

static std::size_t checks = 0;
static void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED %s\n", message); std::abort(); }
}
static std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void IoChecks(const std::filesystem::path& path, bool partialFailure) {
    using AudioOutIngressTrace::WritePrivateJson;
    std::filesystem::create_directories(path);
#ifndef _WIN32
    if (partialFailure) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit limit{};
        Require(getrlimit(RLIMIT_FSIZE, &limit) == 0, "get file-size limit");
        limit.rlim_cur = 128;
        Require(setrlimit(RLIMIT_FSIZE, &limit) == 0, "set file-size limit");
        const auto final = path / "partial-failure.json";
        Require(!WritePrivateJson(final, std::string(1024, 'x')), "short write/EFBIG rejected");
        Require(!std::filesystem::exists(final) && !std::filesystem::exists(final.string() + ".partial"), "own incomplete export removed");
        return;
    }
#else
    (void)partialFailure;
#endif
    const auto final = path / "record.json";
    Require(WritePrivateJson(final, "{\"synthetic\":true}\n"), "complete exclusive export");
    Require(Read(final) == "{\"synthetic\":true}\n", "export bytes");
    Require(!WritePrivateJson(final, "replace"), "existing final rejected");
    Require(Read(final) == "{\"synthetic\":true}\n", "existing final preserved");
    Require(!std::filesystem::exists(final.string() + ".partial"), "failed publication temporary removed");
    const auto precreated = path / "precreated.json.partial";
    { std::ofstream file(precreated); file << "keep"; }
    Require(!WritePrivateJson(path / "precreated.json", "replace") && Read(precreated) == "keep", "existing temporary preserved");
#ifndef _WIN32
    struct stat info{};
    Require(stat(final.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "private file mode");
    const auto target = path / "link-target";
    { std::ofstream file(target); file << "private target"; }
    const auto temporaryLink = path / "linked.json.partial";
    std::filesystem::create_symlink(target, temporaryLink);
    Require(!WritePrivateJson(path / "linked.json", "replace") && Read(target) == "private target", "temporary symlink rejected without following");
    Require(std::filesystem::is_symlink(temporaryLink), "pre-existing symlink preserved");
    const auto finalLink = path / "final-link.json";
    std::filesystem::create_symlink(target, finalLink);
    Require(!WritePrivateJson(finalLink, "replace") && Read(target) == "private target", "final symlink publication rejected");
    Require(std::filesystem::is_symlink(finalLink), "final symlink preserved");
#endif
    Require(!WritePrivateJson(path / "too-large.json", std::string(AudioOutIngressTrace::MaxExportBytes + 1, 'x')), "export byte cap");
    Require(!std::filesystem::exists(path / "too-large.json.partial"), "oversize export creates no file");
}
static AudioOutIngressTrace::Metadata MakeMetadata(int port, unsigned channels = 2, unsigned bits = 32) {
    AudioOutIngressTrace::Metadata meta;
    meta.port = port; meta.type = 1; meta.gameFormat = 5;
    meta.requestedFrames = 256; meta.device = port; meta.obtainedSamples = 1024;
    meta.raw = {48000, channels, bits, 0x8120, bits == 32};
    meta.queued = meta.raw;
    return meta;
}
static After::Port MakePort() {
    After::Port port;
    port.used = true; port.type = 1; port.samplesNum = 8; port.freq = 48000;
    port.format = After::Format::F32Stereo; port.channels = 2; port.device = 1;
    port.spec.freq = 48000; port.spec.format = AUDIO_F32SYS; port.spec.channels = 2; port.spec.samples = 1024;
    for (int& volume : port.volume) volume = 32768;
    return port;
}
static Before::Port BeforePort(const After::Port& port) {
    Before::Port before;
    before.used = port.used; before.type = port.type; before.samplesNum = port.samplesNum;
    before.freq = port.freq; before.format = static_cast<Before::Format>(port.format);
    before.channels = port.channels; before.mixLevel = port.mixLevel;
    std::copy(std::begin(port.volume), std::end(port.volume), std::begin(before.volume));
    before.device = port.device; before.spec = port.spec;
    return before;
}
static AudioOutIngressTrace::Stream* RegisterPort(After::Port& port, int index) {
    // Exact registration block extracted from actual sceAudioOutOpen.
    After::RegisterProduction(port, index);
    return port.ingressTrace;
}
static void Parity(const std::string& name, After::Port port, Mock scenario, bool nullData = false) {
    std::array<float, 64> initial{};
    for (std::size_t index = 0; index < initial.size(); ++index) initial[index] = static_cast<float>(index + 1) / 128.0f;
    auto oldInput = initial, newInput = initial;
    auto before = BeforePort(port);
    mock = scenario; mock.source = reinterpret_cast<unsigned char*>(oldInput.data()); mock.sourceBytes = sizeof oldInput;
    std::string oldError;
    try { Before::queueAudio(before, nullData ? nullptr : oldInput.data()); }
    catch (const std::exception& error) { oldError = error.what(); }
    const auto oldCalls = mock.calls; const auto oldBytes = mock.queued; const auto oldUs = mock.us;
    mock = scenario; mock.source = reinterpret_cast<unsigned char*>(newInput.data()); mock.sourceBytes = sizeof newInput;
    static int generation = 0;
    auto* traced = RegisterPort(port, generation++);
    std::string newError;
    try { After::queueAudio(port, nullData ? nullptr : newInput.data()); }
    catch (const std::exception& error) { newError = error.what(); }
    Require(oldCalls == mock.calls, (name + " SDL/pacing call order").c_str());
    Require(oldBytes == mock.queued, (name + " queued bytes").c_str());
    Require(oldError == newError, (name + " error/throw semantics").c_str());
    Require(oldUs == mock.us, (name + " guest pacing time").c_str());
    Require(oldInput == newInput, (name + " source mutation parity").c_str());
    AudioOutIngressTrace::Event(traced, "fixture-close");
}
static std::vector<unsigned char> Pattern(std::uint64_t first, std::uint64_t frames, const AudioOutIngressTrace::Format& format) {
    const auto width = static_cast<std::size_t>(format.FrameBytes());
    std::vector<unsigned char> bytes(19 + frames * width + 19, 0xa7);
    for (std::uint64_t f = 0; f < frames; ++f) {
        for (unsigned c = 0; c < format.channels; ++c) for (unsigned b = 0; b < format.bits / 8; ++b)
            bytes[19 + f * width + c * (format.bits / 8) + b] =
                static_cast<unsigned char>((first + f) * 17 + c * 23 + b * 7 + ((first + f) / 1024) * 5);
    }
    return bytes;
}
static void Focused(const std::filesystem::path& path) {
    using namespace AudioOutIngressTrace;
    std::uint64_t parsed = 99;
    Require(ParseSkip(nullptr, parsed) && parsed == 0, "unset skip");
    Require(ParseSkip("28800000", parsed) && parsed == MaxSkipFrames, "maximum skip");
    for (const char* bad : {"", "-1", "1 ", "+1", "1x", "28800001", "999999999999999999999"})
        Require(!ParseSkip(bad, parsed), "strict skip rejection");
    {
        auto pool = std::make_unique<Pool>(257, path / "independent-variable");
        auto meta = MakeMetadata(101, 8); meta.queued = {48000, 2, 16, 0x8010, false};
        auto* stream = pool->Register(meta); Require(stream != nullptr, "independent metadata admitted");
        std::uint64_t first = 0; std::size_t step = 0;
        const std::array<std::uint64_t, 4> chunks{17, 256, 513, 1024};
        while (first < 257 + WindowFrames) {
            const auto frames = std::min(chunks[step++ % chunks.size()], 257 + WindowFrames - first);
            auto raw = Pattern(first, frames, meta.raw), queued = Pattern(first, frames, meta.queued);
            const auto oldRaw = raw, oldQueued = queued;
            auto token = pool->Raw(stream, raw.data() + 19, frames);
            pool->Queued(token, queued.data() + 19, static_cast<std::uint32_t>(frames * meta.queued.FrameBytes()), 1, true, false);
            pool->Result(token, 0);
            Require(raw == oldRaw && queued == oldQueued, "unaligned source/queue guards unchanged");
            first += frames;
        }
        Require(stream->raw.captured == WindowFrames && stream->queued.captured == WindowFrames, "independent complete windows");
        Require(stream->raw.count == 470 && stream->queued.count == 470, "canonical partial/full block boundaries");
    }
    {
        auto pool = std::make_unique<Pool>(0, path / "guards");
        auto meta = MakeMetadata(102);
        auto bad = meta; bad.raw.channels = 9; Require(pool->Register(bad) == nullptr, "unsupported channels");
        bad = meta; bad.queued.bits = 24; Require(pool->Register(bad) == nullptr, "unsupported format");
        bad = meta; bad.raw.rate = 0; Require(pool->Register(bad) == nullptr, "unsupported rate");
        bad = meta; bad.device = 0; Require(pool->Register(bad) == nullptr, "no-device registration");
        auto* stream = pool->Register(meta); auto bytes = Pattern(0, 16, meta.raw);
        auto token = pool->Raw(stream, bytes.data() + 19, 16);
        pool->Queued(token, bytes.data() + 19, 127, 0, false, true);
        auto* limited = pool->Register(MakeMetadata(103));
        pool->Raw(limited, bytes.data() + 19, MaxCallBytes / 8 + 1);
        auto* interrupted = pool->Register(MakeMetadata(104));
        auto pending = pool->Raw(interrupted, bytes.data() + 19, 16);
        pool->Event(interrupted, "cancel-between-raw-and-queue");
        pool->Queued(pending, bytes.data() + 19, 128, 0, false, true); pool->Result(pending, 0);
        Require(interrupted->queued.captured == 0, "closed generation remains immutable");
        auto* exhausted = pool->Register(MakeMetadata(105)); const std::array<unsigned char, 8> frame{};
        for (std::size_t index = 0; index <= MaxCalls; ++index) {
            auto t = pool->Raw(exhausted, frame.data(), 1);
            pool->Queued(t, frame.data(), 8, 0, false, true); pool->Result(t, 0);
        }
        Require(exhausted->count == MaxCalls, "record limit");
        auto* nullSource = pool->Register(MakeMetadata(106)); pool->Raw(nullSource, nullptr, 1);
    }
    {
        auto pool = std::make_unique<Pool>(0, path / "generation-limit");
        auto meta = MakeMetadata(107);
        for (std::size_t index = 0; index < MaxStreams; ++index) {
            auto* stream = pool->Register(meta); Require(stream != nullptr && stream->generation == index + 1, "never-recycled generation");
            pool->Event(stream, "closed-before-reuse");
        }
        Require(pool->Register(meta) == nullptr && pool->Rejected() == 1, "bounded slot rejection");
    }
    {
        auto pool = std::make_unique<Pool>(0, path / "parallel-streams");
        std::array<Stream*, 4> streams{};
        for (auto& stream : streams) stream = pool->Register(MakeMetadata(108));
        std::array<std::thread, 4> threads;
        for (std::size_t index = 0; index < streams.size(); ++index) threads[index] = std::thread([&, index] {
            auto meta = MakeMetadata(108);
            for (std::uint64_t i = 0; i < 20; ++i) {
                auto bytes = Pattern(i * 64, 64, meta.raw);
                auto token = pool->Raw(streams[index], bytes.data() + 19, 64);
                pool->Queued(token, bytes.data() + 19, 512, 0, false, true); pool->Result(token, 0);
            }
        });
        for (auto& thread : threads) thread.join();
        for (auto* stream : streams) { Require(stream->raw.frames == 1280 && stream->queued.frames == 1280, "parallel independent streams"); pool->Event(stream, "fixture-close"); }
    }
}
int main(int argc, char** argv) {
    if (argc == 3) {
        IoChecks(argv[1], std::string(argv[2]) == "--io-partial");
        std::cout << "{\"status\":\"passed\",\"checks\":" << checks << "}\n";
        return 0;
    }
    Require(argc == 2, "output directory argument");
    const std::filesystem::path path(argv[1]);
    const bool enabled = AudioOutIngressTrace::Enabled();
    auto port = MakePort();
    Parity("direct", port, {});
    auto surround = port; surround.channels = 8; surround.spec.channels = 8; surround.format = After::Format::F32_8ChStd;
    surround.volume[6] = 16384; surround.mixLevel = 20000; Parity("STD volume/channel mapping", surround, {});
    Mock converted; converted.cvt = 1; Parity("CVT", port, converted);
    Mock busy; busy.busy = 3; busy.mutateOnSleep = true; Parity("actual input after pacing wait", port, busy);
    Parity("null drain", port, {}, true);
    Mock stalled; stalled.alwaysBusy = true; Parity("null timeout clear", port, stalled, true);
    Parity("pacing timeout clear", port, stalled);
    Mock buildFail; buildFail.cvt = -1; Parity("CVT build error", port, buildFail);
    Mock convertFail; convertFail.cvt = 1; convertFail.convertResult = -1; Parity("CVT convert error", port, convertFail);
    Mock queueFail; queueFail.queueResult = -1; Parity("SDL queue error", port, queueFail);
    port = MakePort(); port.samplesNum = 256;
    auto* stream = RegisterPort(port, 10);
    const auto firstMock = Mock{};
    for (std::uint64_t first = 0; first < AudioOutIngressTrace::WindowFrames; first += 256) {
        mock = firstMock;
        std::array<float, 512> input{};
        for (std::size_t f = 0; f < 256; ++f) for (std::size_t c = 0; c < 2; ++c)
            input[f * 2 + c] = static_cast<float>(((first + f) * 17 + c * 29) % 4096) / 4096.0f + static_cast<float>(c) * 0.125f;
        After::queueAudio(port, input.data());
        Require(mock.queued.size() == sizeof input && std::memcmp(mock.queued.data(), input.data(), sizeof input) == 0, "full-window production bytes preserved");
    }
    if (enabled) {
        Require(stream != nullptr && stream->raw.captured == AudioOutIngressTrace::WindowFrames, "production raw hook complete");
        Require(stream->queued.captured == AudioOutIngressTrace::WindowFrames, "production pre-queue hook complete");
        Focused(path);
    } else {
        Require(stream == nullptr && AudioOutIngressTrace::Instance() == nullptr, "disabled no pool/no worker");
    }
    std::cout << "{\"status\":\"passed\",\"checks\":" << checks << ",\"enabled\":" << (enabled ? "true" : "false") << "}\n";
}
