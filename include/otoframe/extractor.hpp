#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__) && !defined(OTOFRAME_PORTABLE)
#include <Accelerate/Accelerate.h>
#define OTOFRAME_HAS_ACCELERATE 1
#else
#define OTOFRAME_HAS_ACCELERATE 0
#endif

namespace otoframe {

inline constexpr double pi = 3.14159265358979323846;

struct Features {
    float rms = 0;
    float peak = 0;
    float centroid_hz = 0;
    float rolloff_hz = 0;
    float spectral_entropy = 0;
    float spectral_flux = 0;
    // Power fractions in [20,250), [250,2000), [2000,20000] Hz.
    std::array<float, 3> band_power_ratio{};
    // C, C#, D, D#, E, F, F#, G, G#, A, A#, B; max-normalized.
    std::array<float, 12> chroma{};
    bool has_previous_frame = false;
};

// One instance per mono stream. Construct away from the audio callback.
// Input must be finite normalized PCM in [-1,1]. Instances are not thread safe.
class Extractor {
public:
    explicit Extractor(std::size_t frame_size = 2048, double sample_rate = 48000)
        : size_(validate_size(frame_size)), rate_(validate_rate(sample_rate)),
          input_(size_), window_(size_), spectrum_(size_ / 2 + 1),
          previous_(size_ / 2 + 1)
#if OTOFRAME_HAS_ACCELERATE
          , real_(size_ / 2), imag_(size_ / 2)
#else
          , complex_(size_), roots_(size_ / 2), reverse_(size_)
#endif
    {
        unsigned bits = 0;
        for (auto n = size_; n > 1; n >>= 1) ++bits;
        for (std::size_t i = 0; i < size_; ++i) {
            // Periodic Hamming window: fixed across both FFT backends.
            window_[i] = static_cast<float>(0.54 - 0.46 * std::cos(2 * pi * i / size_));
            window_sum_ += window_[i];
        }
#if OTOFRAME_HAS_ACCELERATE
        log2_size_ = bits;
        setup_ = vDSP_create_fftsetup(log2_size_, kFFTRadix2);
        if (!setup_) throw std::runtime_error("Cannot create FFT setup");
#else
        for (std::size_t i = 0; i < size_ / 2; ++i) {
            const double angle = -2 * pi * i / size_;
            roots_[i] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
        }
        for (std::size_t i = 0; i < size_; ++i) {
            auto value = i;
            std::size_t reversed = 0;
            for (unsigned bit = 0; bit < bits; ++bit) {
                reversed = (reversed << 1) | (value & 1);
                value >>= 1;
            }
            reverse_[i] = reversed;
        }
#endif
    }

    ~Extractor() {
#if OTOFRAME_HAS_ACCELERATE
        if (setup_) vDSP_destroy_fftsetup(setup_);
#endif
    }

    // Prevent accidental sharing of FFT state and pointers.
    Extractor(const Extractor&) = delete;
    Extractor& operator=(const Extractor&) = delete;
    Extractor(Extractor&&) = delete;
    Extractor& operator=(Extractor&&) = delete;

    // Accepts 1..frame_size samples; a short final frame is zero-padded for FFT.
    // Invalid input returns false, zeros out, and does not change stream state.
    bool process(const float* samples, std::size_t length, Features& out) noexcept {
        out = {};
        if (!samples || length == 0 || length > size_) return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!std::isfinite(samples[i]) || std::abs(samples[i]) > 1) return false;

        double sum_squares = 0;
        for (std::size_t i = 0; i < length; ++i) {
            const double sample = samples[i];
            sum_squares += sample * sample;
            out.peak = std::max(out.peak, std::abs(samples[i]));
            input_[i] = samples[i] * window_[i];
        }
        std::fill(input_.begin() + length, input_.end(), 0);
        out.rms = static_cast<float>(std::sqrt(sum_squares / length));
        transform();

        double magnitude_sum = 0, weighted_sum = 0, power_sum = 0, flux_sum = 0;
        std::array<double, 3> band_power{};
        std::array<double, 12> pitch_power{};
        for (std::size_t k = 0; k < spectrum_.size(); ++k) {
            const double magnitude = spectrum_[k];
            const double hz = bin_hz(k);
            // Interior one-sided amplitude is doubled: halve its square for power.
            const double power = magnitude * magnitude * ((k == 0 || k == size_ / 2) ? 1 : 0.5);
            magnitude_sum += magnitude;
            weighted_sum += hz * magnitude;
            power_sum += power;
            if (hz >= 20 && hz < 250) band_power[0] += power;
            else if (hz >= 250 && hz < 2000) band_power[1] += power;
            else if (hz >= 2000 && hz <= 20000) band_power[2] += power;
            const double delta = magnitude - previous_[k];
            flux_sum += delta * delta;
            if (hz >= 27.5 && hz <= 4186) {
                const int midi = static_cast<int>(std::round(69 + 12 * std::log2(hz / 440)));
                const auto pitch = static_cast<std::size_t>((midi % 12 + 12) % 12);
                pitch_power[pitch] += magnitude * magnitude;
            }
        }

        if (magnitude_sum > 1e-12) {
            out.centroid_hz = static_cast<float>(weighted_sum / magnitude_sum);
            double cumulative = 0, entropy = 0;
            bool found_rolloff = false;
            for (std::size_t k = 0; k < spectrum_.size(); ++k) {
                const double magnitude = spectrum_[k];
                cumulative += magnitude;
                if (!found_rolloff && cumulative >= magnitude_sum * 0.85) {
                    out.rolloff_hz = static_cast<float>(bin_hz(k));
                    found_rolloff = true;
                }
                if (magnitude > 0) {
                    const double p = magnitude / magnitude_sum;
                    entropy -= p * std::log2(p);
                }
            }
            out.spectral_entropy = static_cast<float>(std::clamp(entropy / std::log2(spectrum_.size()), 0.0, 1.0));
        }
        if (power_sum > 1e-24)
            for (std::size_t b = 0; b < 3; ++b) out.band_power_ratio[b] = static_cast<float>(band_power[b] / power_sum);
        const double max_pitch = *std::max_element(pitch_power.begin(), pitch_power.end());
        if (max_pitch > 1e-24)
            for (std::size_t p = 0; p < 12; ++p) out.chroma[p] = static_cast<float>(pitch_power[p] / max_pitch);
        out.has_previous_frame = has_previous_;
        out.spectral_flux = has_previous_ ? static_cast<float>(std::sqrt(flux_sum / spectrum_.size())) : 0;
        std::copy(spectrum_.begin(), spectrum_.end(), previous_.begin());
        has_previous_ = true;
        return true;
    }

    void reset() noexcept {
        std::fill(spectrum_.begin(), spectrum_.end(), 0);
        std::fill(previous_.begin(), previous_.end(), 0);
        has_previous_ = false;
    }

    std::size_t frame_size() const noexcept { return size_; }
    double sample_rate() const noexcept { return rate_; }
    double bin_hz(std::size_t bin) const noexcept { return bin * rate_ / size_; }
    // DC through Nyquist, one-sided amplitudes, corrected for window coherent gain.
    const std::vector<float>& spectrum() const noexcept { return spectrum_; }
    static const char* backend() noexcept {
#if OTOFRAME_HAS_ACCELERATE
        return "Apple Accelerate";
#else
        return "Portable radix-2";
#endif
    }

private:
    static std::size_t validate_size(std::size_t n) {
        if (n < 32 || n > 32768 || (n & (n - 1)) != 0)
            throw std::invalid_argument("Frame size must be a power of two from 32 to 32768");
        return n;
    }
    static double validate_rate(double rate) {
        if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
            throw std::invalid_argument("Sample rate must be finite and between 8000 and 384000 Hz");
        return rate;
    }

    void transform() noexcept {
#if OTOFRAME_HAS_ACCELERATE
        for (std::size_t i = 0; i < size_ / 2; ++i) {
            real_[i] = input_[2 * i];
            imag_[i] = input_[2 * i + 1];
        }
        DSPSplitComplex split{real_.data(), imag_.data()};
        vDSP_fft_zrip(setup_, &split, 1, log2_size_, FFT_FORWARD);
        // Real forward vDSP FFT = 2 * mathematical DFT. imag[0] is Nyquist.
        spectrum_[0] = static_cast<float>(std::abs(real_[0]) / (2 * window_sum_));
        spectrum_[size_ / 2] = static_cast<float>(std::abs(imag_[0]) / (2 * window_sum_));
        for (std::size_t k = 1; k < size_ / 2; ++k)
            spectrum_[k] = static_cast<float>(std::hypot(real_[k], imag_[k]) / window_sum_);
#else
        for (std::size_t i = 0; i < size_; ++i) complex_[reverse_[i]] = {input_[i], 0};
        for (std::size_t width = 2; width <= size_; width *= 2) {
            const auto half = width / 2;
            const auto step = size_ / width;
            for (std::size_t base = 0; base < size_; base += width)
                for (std::size_t j = 0; j < half; ++j) {
                    const auto even = complex_[base + j];
                    const auto odd = complex_[base + j + half] * roots_[j * step];
                    complex_[base + j] = even + odd;
                    complex_[base + j + half] = even - odd;
                }
        }
        for (std::size_t k = 0; k < spectrum_.size(); ++k) {
            const double scale = (k == 0 || k == size_ / 2) ? 1 : 2;
            spectrum_[k] = static_cast<float>(scale * std::abs(complex_[k]) / window_sum_);
        }
#endif
    }

    const std::size_t size_;
    const double rate_;
    double window_sum_ = 0;
    std::vector<float> input_, window_, spectrum_, previous_;
    bool has_previous_ = false;
#if OTOFRAME_HAS_ACCELERATE
    FFTSetup setup_ = nullptr;
    vDSP_Length log2_size_ = 0;
    std::vector<float> real_, imag_;
#else
    std::vector<std::complex<float>> complex_, roots_;
    std::vector<std::size_t> reverse_;
#endif
};

} // namespace otoframe
