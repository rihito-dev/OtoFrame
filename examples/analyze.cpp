#include <otoframe/extractor.hpp>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using otoframe::Extractor;
using otoframe::Features;
constexpr double kPi = otoframe::pi;

std::uint16_t u16(const unsigned char* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
struct Audio { double rate = 48000; std::vector<float> samples; };

// Deliberately limited example reader: RIFF PCM16 mono/stereo, <= 60 seconds.
// A production host should supply decoded mono Float32 PCM directly to Extractor.
Audio read_wav(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open WAV input");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 12 || length > 64 * 1024 * 1024) throw std::runtime_error("WAV must be between 12 bytes and 64 MiB");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Truncated WAV");
    auto id = [&](std::size_t offset, const char* s) { return std::equal(bytes.begin() + offset, bytes.begin() + offset + 4, s); };
    if (!id(0, "RIFF") || !id(8, "WAVE") || std::uint64_t(u32(bytes.data() + 4)) + 8 != bytes.size())
        throw std::runtime_error("Expected a complete RIFF/WAVE file");
    std::size_t data_offset = 0, data_size = 0;
    unsigned channels = 0, block = 0, rate = 0;
    bool have_format = false, have_data = false;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const auto size = static_cast<std::size_t>(u32(bytes.data() + offset + 4));
        const auto payload = offset + 8;
        if (size > bytes.size() - payload) throw std::runtime_error("Invalid WAV chunk length");
        const auto* p = bytes.data() + payload;
        if (id(offset, "fmt ")) {
            if (have_format || size < 16 || u16(p) != 1 || u16(p + 14) != 16) throw std::runtime_error("Only uncompressed PCM16 WAV is supported");
            channels = u16(p + 2); rate = u32(p + 4); block = u16(p + 12);
            if ((channels != 1 && channels != 2) || block != channels * 2 || rate < 8000 || rate > 384000 || u32(p + 8) != rate * block)
                throw std::runtime_error("Invalid PCM16 WAV format");
            have_format = true;
        } else if (id(offset, "data")) {
            if (have_data) throw std::runtime_error("Multiple data chunks unsupported");
            data_offset = payload; data_size = size; have_data = true;
        }
        const auto padded = size + (size & 1);
        if (padded > bytes.size() - payload) throw std::runtime_error("Missing WAV chunk padding");
        offset = payload + padded;
        if (offset != bytes.size() && bytes.size() - offset < 8) throw std::runtime_error("Truncated WAV chunk header");
    }
    if (!have_format || !have_data || data_size == 0 || data_size % block != 0) throw std::runtime_error("WAV needs valid format and nonempty complete sample data");
    const auto frames = data_size / block;
    if (frames > std::size_t(rate) * 60) throw std::runtime_error("Example reader accepts up to 60 seconds");
    Audio audio; audio.rate = rate; audio.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double mono = 0;
        for (unsigned c = 0; c < channels; ++c) {
            const auto raw = u16(bytes.data() + data_offset + i * block + c * 2);
            const int value = raw < 32768 ? raw : int(raw) - 65536;
            mono += value / 32768.0;
        }
        audio.samples[i] = static_cast<float>(mono / channels);
    }
    return audio;
}

Audio demo_audio() {
    Audio audio;
    audio.samples.resize(48000 * 8);
    unsigned state = 42;
    for (std::size_t i = 0; i < audio.samples.size(); ++i) {
        const double t = i / audio.rate;
        state = 1664525u * state + 1013904223u;
        const double noise = (state >> 8) / 16777216.0 * 2 - 1;
        const double fade = std::min({1.0, std::fmod(t, 2.0) / 0.04, (2.0 - std::fmod(t, 2.0)) / 0.04});
        double sample = 0;
        if (t < 2) sample = 0.55 * std::sin(2 * kPi * 110 * t);
        else if (t < 4) sample = 0.19 * (std::sin(2 * kPi * 261.625565 * t) + std::sin(2 * kPi * 329.627557 * t) + std::sin(2 * kPi * 391.995436 * t));
        else if (t < 6) sample = 0.4 * noise;
        else if (std::fmod(t - 6, 0.4) < 0.12) sample = 0.7 * std::exp(-std::fmod(t - 6, 0.4) * 60) * std::sin(2 * kPi * 1000 * t);
        audio.samples[i] = static_cast<float>(fade * sample);
    }
    return audio;
}

void write_report(const Audio& audio, const std::string& path, bool demo) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot open report output");
    Extractor extractor(2048, audio.rate);
    Features f;
    output << std::setprecision(7) << "{\"schema\":1,\"sample_rate\":" << audio.rate
           << ",\"frame_size\":2048,\"hop_size\":1024,\"duration\":" << audio.samples.size() / audio.rate
           << ",\"backend\":\"" << Extractor::backend() << "\",\"synthetic\":" << (demo ? "true" : "false") << ",\"frames\":[";
    bool first = true;
    for (std::size_t offset = 0; offset < audio.samples.size(); offset += 1024) {
        const auto length = std::min<std::size_t>(2048, audio.samples.size() - offset);
        if (!extractor.process(audio.samples.data() + offset, length, f)) throw std::runtime_error("Invalid PCM frame");
        if (!first) output << ',';
        first = false;
        output << "{\"time\":" << offset / audio.rate << ",\"valid_samples\":" << length << ",\"rms\":" << f.rms
               << ",\"peak\":" << f.peak << ",\"centroid_hz\":" << f.centroid_hz << ",\"rolloff_hz\":" << f.rolloff_hz
               << ",\"entropy\":" << f.spectral_entropy << ",\"flux\":" << f.spectral_flux
               << ",\"has_previous_frame\":" << (f.has_previous_frame ? "true" : "false") << ",\"bands\":[";
        for (std::size_t b = 0; b < 3; ++b) { if (b) output << ','; output << f.band_power_ratio[b]; }
        output << "],\"chroma\":[";
        for (std::size_t p = 0; p < 12; ++p) { if (p) output << ','; output << f.chroma[p]; }
        output << "],\"spectrum\":[";
        for (std::size_t k = 0; k < extractor.spectrum().size(); ++k) { if (k) output << ','; output << extractor.spectrum()[k]; }
        output << "]}";
    }
    output << "]}\n";
    if (!output) throw std::runtime_error("Report write failed");
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--demo") {
            write_report(demo_audio(), argv[2], true);
        } else if (argc == 5 && std::string(argv[1]) == "--wav" && std::string(argv[3]) == "--output") {
            write_report(read_wav(argv[2]), argv[4], false);
        } else {
            std::cerr << "Usage: otoframe --demo report.json\n       otoframe --wav input.wav --output report.json\n";
            return 2;
        }
        std::cout << "Report saved (" << Extractor::backend() << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
