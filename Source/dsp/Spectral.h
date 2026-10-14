#pragma once
// Axell Penta EQ - spectral dynamics of one band (framework independent, allocation only in prepare()).
//
// A dynamic band in Spectral mode does not move as a whole: every frequency inside the band's region is
// compressed (or expanded) on its own, so only what is loud gets reduced - a resonance, a whistle, a
// harsh partial - while the rest of the region stays untouched.
//
//   detector  short-time spectrum (Hann, N = 1024 at 44.1/48 kHz, hop N/4) of the band's detector signal
//             (its input, or the side-chain); per frequency the level is calibrated so that a steady tone
//             of amplitude A reads 20 log10 A dBFS. Noise spreads over many bins and reads lower: tonal
//             peaks and resonances are caught first.
//   law       the band's threshold / ratio / range / attack / release per frequency (6 dB soft knee),
//             weighted by the band's own shape (0 outside its region, 1 at its core), lightly smoothed
//             across frequency.
//   apply     the gain curve becomes a minimum-phase FIR (real cepstrum; N/2 taps per base-rate
//             factor) - zero added latency in every phase mode - convolved right after the band's
//             filter, in the band's own L/R/M/S domain, crossfaded from one update to the next.
//
// The engine calls pushDetector() with base-rate samples, update() at every hop boundary (aligned to the
// absolute sample position: deterministic for any host block size) and process() at the band's rate.

#include "Correction.h"
#include "Fft.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace penta::dsp
{
struct SpectralSettings
{
    double thr = -24.0, range = -6.0, ratio = 4.0, atkMs = 5.0, relMs = 120.0;
};

class SpectralDynamics
{
public:
    static constexpr int kDisplayPoints = 64; // gain curve for the GUI, 20 Hz .. 20 kHz log-spaced

    // fsDet: base (detector) rate; the band may run at fsDet * F, F = 1, 2, 4
    void prepare (double fsDet)
    {
        fs = fsDet;
        n = fsDet <= 50000.0 ? 1024 : (fsDet <= 100000.0 ? 2048 : 4096);
        hop = n / 4;
        nb = n / 2 + 1;
        anaFft.prepare (n);
        for (auto& r : ring)
            r.assign ((size_t) n, 0.0f);
        frame.assign ((size_t) n, 0.0f);
        spec.assign ((size_t) nb, {});
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n)); // periodic Hann
        power.assign ((size_t) nb, 0.0);
        env.assign ((size_t) nb, -200.0);
        weight.assign ((size_t) nb, 0.0);
        gainDb.assign ((size_t) nb, 0.0);
        appliedDb.assign ((size_t) nb, 0.0);
        tmpDb.assign ((size_t) nb, 0.0);
        for (int f = 0; f < 3; ++f)
        {
            const int m = 2 * n * (1 << f);
            designFft[(size_t) f].prepare (m);
        }
        const int mMax = 2 * n * 4;
        logMag.assign ((size_t) (mMax / 2 + 1), 0.0f);
        cep.assign ((size_t) mMax, 0.0f);
        cspec.assign ((size_t) (mMax / 2 + 1), {});
        taps.assign ((size_t) (n / 2 * 4), 0.0f);
        for (auto& l : lane)
            l.prepare (n / 2 * 4, hop);
        for (auto& d : display)
            d.store (0.0f, std::memory_order_relaxed);
        dispK.resize (kDisplayPoints);
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / (double) (kDisplayPoints - 1));
            dispK[(size_t) i] = hz / fs * n;
        }
        setRate (1);
        reset();
    }

    // band rate = fsDet * factor (oversampling); called with the configuration (never mid-block)
    void setRate (int factor) noexcept
    {
        osF = std::clamp (factor, 1, 4);
        osLog2 = osF == 4 ? 2 : (osF == 2 ? 1 : 0);
        len = n / 2 * osF;
        for (auto& l : lane)
        {
            l.setFadeLength (hop * osF);
            l.setActiveLength (len);
        }
    }

    void reset() noexcept
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        ringPos = 0;
        std::fill (env.begin(), env.end(), -200.0);
        std::fill (gainDb.begin(), gainDb.end(), 0.0);
        std::fill (appliedDb.begin(), appliedDb.end(), 0.0);
        for (auto& l : lane)
        {
            l.reset();
            l.setIdentity (0, false);
        }
        idle = true;
        maxCut = 0.0f;
        for (auto& d : display)
            d.store (0.0f, std::memory_order_relaxed);
    }

    // the detector starts over (its ring holds stale signal): levels rise from silence
    void restartDetector() noexcept
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        std::fill (env.begin(), env.end(), -200.0);
    }

    int hopSize() const noexcept { return hop; }
    int analysisBins() const noexcept { return nb; }
    double binHz (int k) const noexcept { return k * fs / n; }

    // the band's region weight per analysis bin (0..1); set when the band's shape changes
    void setWeight (int k, double w) noexcept { weight[(size_t) k] = std::clamp (w, 0.0, 1.0); }

    // detector samples (base rate): two lanes are linked (the louder decides per frequency)
    void pushDetector (const float* a, const float* b, int count) noexcept
    {
        lanes = b != nullptr ? 2 : 1;
        for (int i = 0; i < count; ++i)
        {
            ring[0][(size_t) ringPos] = a[i];
            if (b != nullptr)
                ring[1][(size_t) ringPos] = b[i];
            ringPos = ringPos + 1 == n ? 0 : ringPos + 1;
        }
    }

    // one hop: analyse, apply the law, redesign the FIR if the curve moved
    void update (const SpectralSettings& st) noexcept
    {
        std::fill (power.begin(), power.end(), 0.0);
        const double norm = 4.0 / n; // Hann, periodic: a sine of amplitude A peaks at A
        for (int c = 0; c < lanes; ++c)
        {
            for (int i = 0; i < n; ++i)
                frame[(size_t) i] = ring[(size_t) c][(size_t) ((ringPos + i) % n)] * window[(size_t) i];
            anaFft.forward (frame.data(), spec.data());
            for (int k = 0; k < nb; ++k)
                power[(size_t) k] = std::max (power[(size_t) k], (double) std::norm (spec[(size_t) k]) * norm * norm);
        }
        const double hopSec = hop / fs;
        const double cA = 1.0 - std::exp (-hopSec / std::max (1.0e-4, st.atkMs * 0.001));
        const double cR = 1.0 - std::exp (-hopSec / std::max (1.0e-4, st.relMs * 0.001));
        const double w = 6.0, slope = 1.0 - 1.0 / std::max (1.0, st.ratio);
        const double lim = std::abs (st.range), sign = st.range < 0.0 ? -1.0 : 1.0;
        for (int k = 0; k < nb; ++k)
        {
            const double lvl = 10.0 * std::log10 (power[(size_t) k] + 1.0e-20);
            double& e = env[(size_t) k];
            e += (lvl > e ? cA : cR) * (lvl - e);
        }
        // the level a frequency sees: the loudest within +-2 bins (a steady tone spreads over the window's
        // main lobe - its own frequency must get the full range), then the law, then the band's region
        // weight of that frequency (so the action stays inside the band), then light smoothing across
        // frequency (short FIR, no "musical noise")
        for (int k = 0; k < nb; ++k)
        {
            double e = -200.0;
            for (int j = std::max (0, k - 2); j <= std::min (nb - 1, k + 2); ++j)
                e = std::max (e, env[(size_t) j]);
            const double over = e - st.thr;
            double o = 0.0;
            if (over >= 0.5 * w)
                o = over;
            else if (over > -0.5 * w)
                o = (over + 0.5 * w) * (over + 0.5 * w) / (2.0 * w);
            tmpDb[(size_t) k] = sign * std::min (lim, o * slope) * weight[(size_t) k];
        }
        double worstMove = 0.0, peak = 0.0;
        for (int k = 0; k < nb; ++k)
        {
            const double l = tmpDb[(size_t) std::max (0, k - 1)], r = tmpDb[(size_t) std::min (nb - 1, k + 1)];
            const double g = 0.25 * l + 0.5 * tmpDb[(size_t) k] + 0.25 * r;
            gainDb[(size_t) k] = g;
            worstMove = std::max (worstMove, std::abs (g - appliedDb[(size_t) k]));
            peak = std::max (peak, std::abs (g));
        }
        maxCut = (float) (sign * peak);
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double kk = dispK[(size_t) i];
            const int k0 = std::clamp ((int) kk, 0, nb - 2);
            const double t = std::clamp (kk - k0, 0.0, 1.0);
            display[(size_t) i].store ((float) (gainDb[(size_t) k0] * (1.0 - t) + gainDb[(size_t) k0 + 1] * t), std::memory_order_relaxed);
        }
        if (peak < 0.01)
        {
            if (! idle)
            {
                for (auto& l : lane)
                    l.setIdentity (0, true);
                std::fill (appliedDb.begin(), appliedDb.end(), 0.0);
                idle = true;
            }
            return;
        }
        if (! idle && worstMove < 0.01)
            return; // the curve did not move: keep the FIR
        design();
        for (auto& l : lane)
            l.setTaps (taps.data(), len, true);
        std::copy (gainDb.begin(), gainDb.end(), appliedDb.begin());
        idle = false;
    }

    // band rate, in place: y = x + fade (fir(x) - x)
    void process (float* x, int count, int ch, const float* fade, float* scratch) noexcept
    {
        auto& l = lane[(size_t) std::clamp (ch, 0, 1)];
        if (idle && ! l.isFading())
        {
            l.pushOnly (x, count); // keeps the history warm: no transient when it starts acting
            return;
        }
        if (fade == nullptr)
        {
            l.process (x, count);
            return;
        }
        std::copy (x, x + count, scratch);
        l.process (scratch, count);
        for (int i = 0; i < count; ++i)
            x[i] += fade[i] * (scratch[i] - x[i]);
    }

    bool isIdle() const noexcept { return idle; }
    float maxGainChangeDb() const noexcept { return maxCut; }
    float displayDb (int i) const noexcept { return display[(size_t) i].load (std::memory_order_relaxed); }

private:
    // minimum-phase FIR of the gain curve at the band rate (real cepstrum, folded)
    void design() noexcept
    {
        auto& fft = designFft[(size_t) osLog2];
        const int m = fft.size(), mb = m / 2 + 1;
        // design bin j sits at j * fsBand / m = j * fs / (2 n): analysis bin j / 2; above the base Nyquist
        // (oversampled band) the curve is flat
        for (int j = 0; j < mb; ++j)
        {
            double db = 0.0;
            if (j <= n)
            {
                const int k = j / 2;
                db = (j & 1) == 0 ? gainDb[(size_t) k] : 0.5 * (gainDb[(size_t) k] + gainDb[(size_t) std::min (nb - 1, k + 1)]);
            }
            logMag[(size_t) j] = (float) (db * 0.11512925464970228); // ln(10) / 20
        }
        for (int j = 0; j < mb; ++j)
            cspec[(size_t) j] = { logMag[(size_t) j], 0.0f };
        fft.inverse (cspec.data(), cep.data());
        for (int i = 1; i < m / 2; ++i)
        {
            cep[(size_t) i] *= 2.0f;
            cep[(size_t) (m - i)] = 0.0f;
        }
        fft.forward (cep.data(), cspec.data());
        for (int j = 0; j < mb; ++j)
            cspec[(size_t) j] = std::exp (cspec[(size_t) j]);
        fft.inverse (cspec.data(), cep.data());
        const int tail = len / 4;
        for (int i = 0; i < len; ++i)
        {
            float h = cep[(size_t) i];
            if (i >= len - tail)
                h *= (float) (0.5 * (1.0 + std::cos (kPi * (i - (len - tail) + 1) / (tail + 1))));
            taps[(size_t) i] = h;
        }
    }

    double fs = 48000.0;
    int n = 1024, hop = 256, nb = 513, osF = 1, osLog2 = 0, len = 512, lanes = 1, ringPos = 0;
    bool idle = true;
    float maxCut = 0.0f;
    RealFft<float> anaFft;
    std::array<RealFft<float>, 3> designFft;
    std::array<std::vector<float>, 2> ring;
    std::vector<float> frame, window, logMag, cep, taps;
    std::vector<std::complex<float>> spec, cspec;
    std::vector<double> power, env, weight, gainDb, appliedDb, tmpDb, dispK;
    std::array<FirLane, 2> lane;
    std::array<std::atomic<float>, kDisplayPoints> display {};
};
} // namespace penta::dsp
