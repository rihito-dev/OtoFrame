#include <otoframe/extractor.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>

static bool watch_allocations = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (watch_allocations) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using otoframe::Extractor;
using otoframe::Features;
constexpr double kPi = otoframe::pi;
static int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
std::vector<float> tone(std::size_t size, int bin, float amplitude = 0.5f) {
    std::vector<float> x(size);
    for (std::size_t i = 0; i < size; ++i) x[i] = amplitude * static_cast<float>(std::sin(2 * kPi * bin * i / size));
    return x;
}

// Independent O(N^2), double precision DFT. No FFT implementation is reused.
std::vector<double> reference_spectrum(const std::vector<float>& x, std::size_t size) {
    std::vector<double> magnitudes(size / 2 + 1);
    double window_sum = 0;
    for (std::size_t n = 0; n < size; ++n) window_sum += 0.54 - 0.46 * std::cos(2 * kPi * n / size);
    for (std::size_t k = 0; k < magnitudes.size(); ++k) {
        std::complex<double> sum{};
        for (std::size_t n = 0; n < x.size(); ++n) {
            const double window = 0.54 - 0.46 * std::cos(2 * kPi * n / size);
            const double angle = -2 * kPi * k * n / size;
            sum += static_cast<double>(x[n]) * window * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        magnitudes[k] = std::abs(sum) * ((k == 0 || k == size / 2) ? 1 : 2) / window_sum;
    }
    return magnitudes;
}

void check_dft() {
    for (auto size : {32u, 64u, 256u}) {
        Extractor extractor(size, 48000);
        std::vector<float> x(size);
        unsigned state = 12345;
        for (auto& sample : x) {
            state = 1664525u * state + 1013904223u;
            sample = static_cast<float>((state >> 8) / 16777216.0 - 0.5);
        }
        for (auto length : {size, size / 3}) {
            x.resize(length);
            Features f;
            require(extractor.process(x.data(), x.size(), f), "DFT input accepted");
            const auto reference = reference_spectrum(x, size);
            for (std::size_t k = 0; k < reference.size(); ++k)
                near(extractor.spectrum()[k], reference[k], 2e-6, "Spectrum must match direct DFT");
            double total = 0, weighted = 0, entropy = 0, power = 0;
            std::array<double, 3> bands{};
            for (std::size_t k = 0; k < reference.size(); ++k) {
                total += reference[k];
                weighted += reference[k] * k * 48000 / size;
                double e = reference[k] * reference[k] * ((k == 0 || k == size / 2) ? 1 : 0.5);
                power += e;
                double hz = static_cast<double>(k) * 48000 / size;
                if (hz >= 20 && hz < 250) bands[0] += e;
                else if (hz >= 250 && hz < 2000) bands[1] += e;
                else if (hz >= 2000 && hz <= 20000) bands[2] += e;
            }
            double cumulative = 0, rolloff = 0;
            bool found = false;
            for (std::size_t k = 0; k < reference.size(); ++k) {
                const double p = reference[k] / total;
                if (p > 0) entropy -= p * std::log2(p);
                cumulative += reference[k];
                if (!found && cumulative >= 0.85 * total) { rolloff = static_cast<double>(k) * 48000 / size; found = true; }
            }
            near(f.centroid_hz, weighted / total, 0.01, "Centroid matches DFT reference");
            near(f.rolloff_hz, rolloff, 0.01, "Rolloff matches DFT reference");
            near(f.spectral_entropy, entropy / std::log2(reference.size()), 2e-6, "Entropy matches DFT reference");
            for (std::size_t b = 0; b < 3; ++b) near(f.band_power_ratio[b], bands[b] / power, 2e-6, "Power bands match DFT reference");
        }
    }
}

void check_known_signals() {
    Extractor extractor(2048, 48000);
    Features f;
    std::vector<float> x(2048, 0);
    require(extractor.process(x.data(), x.size(), f), "Silence accepted");
    near(f.rms, 0, 0, "Silent RMS");
    near(f.centroid_hz, 0, 0, "Silent centroid");
    near(f.spectral_entropy, 0, 0, "Silent entropy");
    near(f.spectral_flux, 0, 0, "First flux is undefined and exposed as zero");
    require(!f.has_previous_frame, "First frame is marked");
    x = tone(2048, 64);
    require(extractor.process(x.data(), x.size(), f), "Tone accepted");
    near(f.rms, 0.5 / std::sqrt(2.0), 1e-6, "Tone RMS is A/sqrt(2)");
    near(f.peak, 0.5, 1e-6, "Tone peak is A");
    near(extractor.spectrum()[64], 0.5, 2e-6, "Window gain corrected tone amplitude");
    near(f.centroid_hz, 1500, 0.1, "Bin-centered tone centroid");
    near(f.band_power_ratio[1], 1, 1e-6, "1500 Hz belongs to mid band");
    require(f.has_previous_frame && f.spectral_flux > 0, "Tone after silence has flux");
    extractor.process(x.data(), x.size(), f);
    near(f.spectral_flux, 0, 0, "Repeated frame has zero flux");
    extractor.reset();
    extractor.process(x.data(), x.size(), f);
    require(!f.has_previous_frame && f.spectral_flux == 0, "Reset clears temporal state");

    std::fill(x.begin(), x.end(), 0.25f);
    extractor.process(x.data(), x.size(), f);
    near(extractor.spectrum()[0], 0.25, 2e-6, "DC amplitude is not doubled");
    near(extractor.spectrum().back(), 0, 2e-6, "DC does not leak into Nyquist");
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = (i % 2 == 0 ? 0.25f : -0.25f);
    extractor.process(x.data(), x.size(), f);
    near(extractor.spectrum().back(), 0.25, 2e-6, "Nyquist amplitude separate from DC");
    near(extractor.spectrum()[0], 0, 2e-6, "Nyquist does not leak into DC");

    Extractor pitch(2048, 56320);
    x = tone(2048, 16); // exactly A4, 440 Hz
    pitch.process(x.data(), x.size(), f);
    require(std::distance(f.chroma.begin(), std::max_element(f.chroma.begin(), f.chroma.end())) == 9, "A4 maps to A pitch class");
    near(f.chroma[9], 1, 1e-6, "Max-normalized chroma");
    float quarter = 0.25f;
    extractor.process(&quarter, 1, f);
    near(f.rms, 0.25, 0, "Partial frame RMS uses valid samples only");
}

void check_invalid_and_allocations() {
    for (auto size : {0u, 31u, 48u, 65536u}) {
        bool rejected = false;
        try { Extractor bad(size); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid frame size rejected before setup");
    }
    for (double rate : {0.0, 7999.0, 384001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { Extractor bad(2048, rate); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid sample rate rejected");
    }
    Extractor extractor(512);
    auto x = tone(512, 12);
    Features f;
    extractor.process(x.data(), x.size(), f);
    const auto spectrum = extractor.spectrum();
    require(!extractor.process(nullptr, x.size(), f) && f.rms == 0, "Null rejected and output zeroed");
    require(!extractor.process(x.data(), 0, f), "Empty input rejected");
    require(!extractor.process(x.data(), 513, f), "Long input rejected before reading");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1.01f, -1.01f}) {
        x[0] = invalid;
        require(!extractor.process(x.data(), x.size(), f), "Invalid PCM rejected");
        require(spectrum == extractor.spectrum(), "Invalid frame does not mutate state");
    }
    x = tone(512, 12);
    extractor.process(x.data(), x.size(), f);
    near(f.spectral_flux, 0, 0, "Invalid frames preserve previous valid frame");
    allocations = 0;
    watch_allocations = true;
    for (int i = 0; i < 100; ++i) extractor.process(x.data(), x.size(), f);
    extractor.reset();
    watch_allocations = false;
    require(allocations == 0, "Process and reset perform no C++ operator-new allocations");
}

int main() {
    try {
        check_dft();
        check_known_signals();
        check_invalid_and_allocations();
        std::cout << "PASS " << checks << " assertions; " << Extractor::backend() << '\n';
    } catch (const std::exception& e) {
        watch_allocations = false;
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
