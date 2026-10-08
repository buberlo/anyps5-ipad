#include "prx/libSceAgcDriver/Graphics/include/FramePixelDiagnostics.hpp"
#include <iostream>
#include <string_view>
#include <vector>

using namespace AgcDriver::Graphics;

namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::byte> Image(unsigned width, unsigned height, unsigned samples,
    std::array<unsigned char, 4> color) {
    std::vector<std::byte> result(static_cast<std::size_t>(width) * height * samples * 4u);
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<std::byte>(color[i % 4u]);
    return result;
}
void Reject(std::span<const std::byte> bytes, unsigned width, unsigned height, unsigned samples) {
    bool rejected = false;
    try { static_cast<void>(SampleFramePixels(bytes, width, height, samples)); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "invalid image dimensions or span were accepted");
}
std::uint64_t OracleFingerprint(std::span<const std::byte> bytes) {
    auto value = FramePixelFnvOffset;
    for (const auto byte : bytes) { value ^= std::to_integer<unsigned char>(byte); value *= 1099511628211ULL; }
    return value;
}
}

int main(int argc, char** argv) {
    try {
        const bool enabled = argc == 2 && std::string_view(argv[1]) == "--enabled";
        Check(FramePixelDiagnosticsEnabled() == enabled, "diagnostics enabled state differs from fixture mode");
        constexpr std::array<std::uint64_t, 8> events{1, 2, 3, 8, 16, 32, 64, 128};
        for (const auto stage : {FramePixelDiagnosticStage::Producer, FramePixelDiagnosticStage::Resolve, FramePixelDiagnosticStage::Present}) {
            unsigned selected = 0;
            for (std::uint64_t ordinal = 1; ordinal <= 140; ++ordinal) {
                const auto event = FramePixelDiagnosticEvent(stage);
                if (!enabled) { Check(!event, "disabled diagnostics selected an event"); continue; }
                const bool expected = std::find(events.begin(), events.end(), ordinal) != events.end();
                Check(event.has_value() == expected, "bounded per-stage schedule differs");
                if (event) { Check(*event == ordinal, "event number differs"); ++selected; }
            }
            Check(selected == (enabled ? events.size() : 0u), "stages share or exceed their bounded event budget");
        }
        Check(!FramePixelDiagnosticEvent(FramePixelDiagnosticStage::Count), "invalid event stage was accepted");
        for (const unsigned samples : {1u, 2u, 4u, 8u}) {
            const auto black = Image(13, 7, samples, {0, 0, 0, 255});
            const auto stats = SampleFramePixels(black, 13, 7, samples);
            Check(stats.sampledPixels == 91u && stats.all.texels == 91u * samples, "small image/sample coverage differs");
            Check(stats.all.blackRgb == stats.all.texels && stats.all.whiteRgb == 0u, "opaque black was misclassified by alpha");
            Check(stats.all.nonzeroAlpha == stats.all.texels && stats.all.opaqueAlpha == stats.all.texels, "opaque alpha counts differ");
            Check(stats.distinctRgba == 1u && stats.distinctRgb == 1u, "uniform image distinct counts differ");
            Check(stats.all.fingerprint == OracleFingerprint(black), "whole small-image FNV oracle differs");
            for (unsigned sample = 0; sample < samples; ++sample)
                Check(stats.samples[sample].texels == 91u && stats.samples[sample].blackRgb == 91u, "logical sample was omitted");
            const auto transparentWhite = Image(13, 7, samples, {255, 255, 255, 0});
            const auto white = SampleFramePixels(transparentWhite, 13, 7, samples);
            Check(white.all.whiteRgb == white.all.texels && white.all.blackRgb == 0u && white.all.nonzeroAlpha == 0u,
                "white RGB and transparent alpha were not separated");
        }
        auto alphaOnly = Image(2, 1, 1, {0, 0, 0, 0});
        alphaOnly[7] = std::byte{255};
        const auto alpha = SampleFramePixels(alphaOnly, 2, 1, 1);
        Check(alpha.distinctRgba == 2u && alpha.distinctRgb == 1u && alpha.all.blackRgb == 2u && alpha.all.nonzeroAlpha == 1u,
            "alpha-only differences lost or counted as RGB differences");
        auto mixed = Image(13, 7, 8, {0, 0, 0, 255});
        for (std::size_t pixel = 0; pixel < 91u; ++pixel)
            for (unsigned sample = 0; sample < 8u; ++sample) {
                const auto offset = (pixel * 8u + sample) * 4u;
                mixed[offset] = static_cast<std::byte>(sample * 31u);
                mixed[offset + 1u] = static_cast<std::byte>(sample * 17u);
            }
        const auto perSample = SampleFramePixels(mixed, 13, 7, 8);
        Check(perSample.distinctRgba == 8u && perSample.distinctRgb == 8u, "eight logical colors were not distinguished");
        Check(perSample.all.fingerprint == OracleFingerprint(mixed), "interleaved aggregate order differs");
        for (unsigned sample = 0; sample < 8u; ++sample) {
            auto plane = Image(13, 7, 1, {static_cast<unsigned char>(sample * 31u), static_cast<unsigned char>(sample * 17u), 0, 255});
            Check(perSample.samples[sample].texels == 91u && perSample.samples[sample].fingerprint == OracleFingerprint(plane),
                "per-sample layout or fingerprint differs");
        }
        mixed[7u * 4u + 2u] = std::byte{1};
        const auto lastChanged = SampleFramePixels(mixed, 13, 7, 8);
        Check(lastChanged.samples[7].fingerprint != perSample.samples[7].fingerprint &&
            lastChanged.samples[0].fingerprint == perSample.samples[0].fingerprint, "last logical sample mutation went undetected");
        auto large = Image(65, 73, 8, {0, 0, 0, 255});
        const auto whiteAt = [&](unsigned x, unsigned y) {
            for (unsigned sample = 0; sample < 8u; ++sample)
                for (unsigned channel = 0; channel < 3u; ++channel)
                    large[(static_cast<std::size_t>(y) * 65u + x) * 32u + sample * 4u + channel] = std::byte{255};
        };
        whiteAt(0, 0); whiteAt(64, 0); whiteAt(0, 72); whiteAt(64, 72);
        const auto bounded = SampleFramePixels(large, 65, 73, 8);
        Check(bounded.sampledPixels == 1024u && bounded.all.texels == 8192u && bounded.all.whiteRgb == 32u,
            "bounded XY grid omitted corners or exceeded its limit");
        whiteAt(1, 1); // Neither coordinate belongs to this fixed grid.
        const auto notWholeImage = SampleFramePixels(large, 65, 73, 8);
        Check(notWholeImage.all.fingerprint == bounded.all.fingerprint, "grid is not the documented sampled subset");
        const auto line = Image(1, 73, 2, {255, 255, 255, 255});
        Check(SampleFramePixels(line, 1, 73, 2).sampledPixels == 32u, "one-dimensional grid limit differs");
        const auto one = Image(1, 1, 1, {0, 0, 0, 0});
        Reject(one, 0, 1, 1); Reject(one, 1, 0, 1); Reject(one, 1, 1, 0);
        Reject(one, 1, 1, 3); Reject(one, 1, 1, 16);
        Reject(std::span<const std::byte>(one).first(3), 1, 1, 1);
        Reject(one, 1, 1, 2); Reject(one, 0xffffffffu, 0xffffffffu, 8);
        if (enabled) PrintFramePixelStatistics("synthetic-fixture", 1, 0, 0, 13, 7, 8, perSample);
        std::cout << "PASS bounded frame pixel diagnostics mode=" << (enabled ? "enabled" : "disabled")
                  << " checks=" << checks << " max_pixels=1024 all_logical_samples=8\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
}
