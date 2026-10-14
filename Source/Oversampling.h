#pragma once
// Axell Penta EQ - linear-phase polyphase FIR oversampling (framework independent)
//
// Why FIR and not JUCE's IIR halfbands: a linear-phase chain is a pure delay in the
// passband, so (a) the total latency is an exact integer number of base-rate samples that
// can be reported to the host and matched by the dry path, and (b) parallel (Mix) blending
// of dry and wet signals has no phase smear / comb filtering.
//
// Base rates below 64 kHz: Kaiser halfbands (N = 4k + 3, >= 100 dB stopband) with the passband
// to 0.4535 * fs (20 kHz at 44.1 kHz) - the band edge has to sit close to Nyquist here.
// Base rates of 64 kHz and above: there is no reason to keep a near-Nyquist passband, so the
// final decimator is a full (non-halfband) low-pass whose stopband starts AT base Nyquist
// (passband to 0.30 * fs = 26 kHz at 88.2 kHz). Halfband decimators alias whatever lands in
// their transition band around Nyquist; at 88.2 kHz the 3rd harmonic of a 15 kHz tone (45 kHz)
// sits exactly there (measured -65.8 dBFS alias at 43.2 kHz with the halfband). With the
// full low-pass it is rejected by >= 100 dB.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace penta
{
namespace os_detail
{
constexpr double kPi = 3.141592653589793;

inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

inline double kaiserBeta (double attenDb)
{
    return attenDb > 50.0 ? 0.1102 * (attenDb - 8.7)
                          : 0.5842 * std::pow (attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0);
}

// Kaiser-windowed sinc low-pass, odd length n, cutoff fc (fraction of the sample rate)
inline std::vector<double> kaiserLowpass (int n, double fc, double attenDb)
{
    const double beta = kaiserBeta (attenDb), i0b = besselI0 (beta);
    const int c = (n - 1) / 2;
    std::vector<double> h ((size_t) n);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double m = i - c, r = m / (double) c;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        const double x = 2.0 * fc * m;
        const double s = (i == c) ? 1.0 : std::sin (kPi * x) / (kPi * x);
        h[(size_t) i] = 2.0 * fc * s * w;
        sum += h[(size_t) i];
    }
    for (auto& v : h)
        v /= sum; // exactly unity DC gain
    return h;
}

inline int kaiserLength (double transition, double attenDb)
{
    return (int) std::ceil ((attenDb - 7.95) / (2.285 * 2.0 * kPi * transition)) + 1;
}

// measured peak stopband magnitude (dB) of FIR h over [stopEdge, 0.5] (fractions of its rate)
inline double stopbandPeakDb (const std::vector<double>& h, double stopEdge)
{
    double worst = 0.0;
    for (int i = 0; i <= 2000; ++i)
    {
        const double f = stopEdge + (0.5 - stopEdge) * i / 2000.0;
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < h.size(); ++n)
        {
            re += h[n] * std::cos (2.0 * kPi * f * (double) n);
            im -= h[n] * std::sin (2.0 * kPi * f * (double) n);
        }
        worst = std::max (worst, re * re + im * im);
    }
    return 10.0 * std::log10 (std::max (worst, 1.0e-300));
}

// deterministic, fixed-order dot product written so the compiler can vectorise it
// without -ffast-math (8 independent accumulators, summed in a fixed order)
inline float dot (const float* a, const float* b, int n) noexcept
{
    float acc[8] = {};
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int j = 0; j < 8; ++j)
            acc[j] += a[i + j] * b[i + j];
    for (; i < n; ++i)
        acc[i & 7] += a[i] * b[i];
    return ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
}

// contiguous history: each sample is written twice so the newest `len` samples are always
// available as one contiguous block (oldest .. newest) starting at data() + pos
struct History
{
    std::vector<float> buf;
    int len = 1, pos = 0;
    void init (int n)
    {
        len = std::max (1, n);
        buf.assign ((size_t) (2 * len), 0.0f);
        pos = 0;
    }
    void clear() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }
    const float* push (float v) noexcept
    {
        buf[(size_t) pos] = buf[(size_t) (pos + len)] = v;
        pos = (pos + 1 == len) ? 0 : pos + 1;
        return buf.data() + pos;
    }
};
} // namespace os_detail

//==============================================================================
// One 2x halfband stage (low rate <-> 2 * low rate) for up to kMaxCh channels.
// Every second tap is zero except the centre (0.5): the odd polyphase branch is a pure delay.
class HalfbandStage
{
public:
    static constexpr int kMaxCh = 2;

    // passEdge: passband edge as a fraction of the HIGH rate (stopband edge = 0.5 - passEdge)
    // The Kaiser length formula is only an estimate (it gave -97.5 dB for a "100 dB" design),
    // so the window attenuation is raised until the MEASURED stopband meets the target.
    void design (double passEdge, double stopAttenDb)
    {
        for (double a = stopAttenDb; a < stopAttenDb + 30.0; a += 0.5)
        {
            designWith (passEdge, a);
            if (os_detail::stopbandPeakDb (full, 0.5 - passEdge) <= -stopAttenDb)
                break;
        }
        for (int ch = 0; ch < kMaxCh; ++ch)
        {
            upHist[ch].init (len);
            dnEven[ch].init (len);
            dnOdd[ch].assign ((size_t) (k + 2), 0.0f);
        }
        reset();
    }

private:
    void designWith (double passEdge, double stopAttenDb)
    {
        const double transition = 0.5 - 2.0 * passEdge;
        const int nMin = os_detail::kaiserLength (transition, stopAttenDb);
        k = std::max (1, (int) std::ceil ((nMin - 3) / 4.0));
        const int n = 4 * k + 3;
        full = os_detail::kaiserLowpass (n, 0.25, stopAttenDb);
        const int c = 2 * k + 1;
        for (int i = 0; i < n; ++i)
            if ((i - c) % 2 == 0 && i != c)
                full[(size_t) i] = 0.0;
        full[(size_t) c] = 0.5;
        len = 2 * k + 2;
        double sum = 0.0;
        for (int j = 0; j < len; ++j)
            sum += full[(size_t) (2 * j)];
        taps.assign ((size_t) len, 0.0f);
        taps2.assign ((size_t) len, 0.0f);
        for (int j = 0; j < len; ++j)
        {
            full[(size_t) (2 * j)] *= 0.5 / sum;
            taps[(size_t) j] = (float) full[(size_t) (2 * j)];
            taps2[(size_t) j] = 2.0f * taps[(size_t) j];
        }
    }

public:
    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxCh; ++ch)
        {
            upHist[ch].clear();
            dnEven[ch].clear();
            std::fill (dnOdd[ch].begin(), dnOdd[ch].end(), 0.0f);
            oddPos[ch] = 0;
        }
    }

    // delay of one pass (up OR down) in samples of the HIGH rate
    int delayHigh() const noexcept { return 2 * k + 1; }
    int numTaps() const noexcept { return 4 * k + 3; }
    const std::vector<double>& coefficients() const noexcept { return full; }

    // n low-rate samples in -> 2n high-rate samples out
    void up (const float* in, float* out, int n, int ch) noexcept
    {
        auto& h = upHist[ch];
        for (int i = 0; i < n; ++i)
        {
            const float* win = h.push (in[i]);              // oldest .. newest
            out[2 * i]     = os_detail::dot (win, taps2.data(), len);
            out[2 * i + 1] = win[len - 1 - k];              // centre tap: 2 * 0.5 * x[n - k]
        }
    }

    // 2n high-rate samples in -> n low-rate samples out
    void down (const float* in, float* out, int n, int ch) noexcept
    {
        auto& e = dnEven[ch];
        float* o = dnOdd[ch].data();
        const int oddLen = k + 2;
        int op = oddPos[ch];
        for (int i = 0; i < n; ++i)
        {
            const float* win = e.push (in[2 * i]);
            int idx = op - 1 - k;
            while (idx < 0)
                idx += oddLen;
            out[i] = os_detail::dot (win, taps.data(), len) + 0.5f * o[idx];
            o[op] = in[2 * i + 1];
            op = (op + 1 == oddLen) ? 0 : op + 1;
        }
        oddPos[ch] = op;
    }

private:
    int k = 1, len = 4;
    std::vector<double> full;
    std::vector<float> taps, taps2;
    os_detail::History upHist[kMaxCh], dnEven[kMaxCh];
    std::vector<float> dnOdd[kMaxCh];
    int oddPos[kMaxCh] {};
};

//==============================================================================
// General linear-phase low-pass decimator by 2 (all taps non-zero), odd length.
class LowpassDecimator
{
public:
    static constexpr int kMaxCh = 2;

    // pass/stop edges as fractions of the HIGH (input) rate
    void design (double passEdge, double stopEdge, double stopAttenDb)
    {
        int n = 1;
        for (double a = stopAttenDb; a < stopAttenDb + 30.0; a += 0.5)
        {
            n = os_detail::kaiserLength (stopEdge - passEdge, a) | 1;
            full = os_detail::kaiserLowpass (n, 0.5 * (passEdge + stopEdge), a);
            if (os_detail::stopbandPeakDb (full, stopEdge) <= -stopAttenDb)
                break;
        }
        nTaps = n;
        // z[n] = sum_j he[j] v[2(n-j)] + sum_j ho[j] v[2(n-j)-1]
        lenE = (n + 1) / 2;
        lenO = n / 2;
        he.assign ((size_t) lenE, 0.0f);
        ho.assign ((size_t) std::max (1, lenO), 0.0f);
        // reversed so the dot product runs over oldest .. newest
        for (int j = 0; j < lenE; ++j)
            he[(size_t) (lenE - 1 - j)] = (float) full[(size_t) (2 * j)];
        for (int j = 0; j < lenO; ++j)
            ho[(size_t) (lenO - 1 - j)] = (float) full[(size_t) (2 * j + 1)];
        for (int ch = 0; ch < kMaxCh; ++ch)
        {
            evenHist[ch].init (lenE);
            oddHist[ch].init (std::max (1, lenO));
        }
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxCh; ++ch)
        {
            evenHist[ch].clear();
            oddHist[ch].clear();
        }
    }

    int delayHigh() const noexcept { return (nTaps - 1) / 2; }
    int numTaps() const noexcept { return nTaps; }
    const std::vector<double>& coefficients() const noexcept { return full; }

    void down (const float* in, float* out, int n, int ch) noexcept
    {
        auto& e = evenHist[ch];
        auto& o = oddHist[ch];
        for (int i = 0; i < n; ++i)
        {
            // odd history must hold v[2n-1], v[2n-3], ... : read it before pushing v[2n+1]
            const float* we = e.push (in[2 * i]);
            const float* wo = o.buf.data() + o.pos;
            out[i] = os_detail::dot (we, he.data(), lenE) + os_detail::dot (wo, ho.data(), lenO);
            o.push (in[2 * i + 1]);
        }
    }

private:
    int nTaps = 1, lenE = 1, lenO = 0;
    std::vector<double> full;
    std::vector<float> he, ho;
    os_detail::History evenHist[kMaxCh], oddHist[kMaxCh];
};

//==============================================================================
// 1x / 2x / 4x oversampler with an exact integer base-rate latency.
class Oversampler
{
public:
    static constexpr int kMaxCh = HalfbandStage::kMaxCh;
    static constexpr int kMaxFactorLog2 = 2;

    void prepare (int maxBlockLowRate, double baseRate = 44100.0)
    {
        maxBlock = std::max (1, maxBlockLowRate);
        highRate = baseRate >= 64000.0;
        if (highRate)
        {
            stageA.design (0.15, 100.0);            // up: passband 0.30 fs, images rejected from 0.70 fs
            decA.design (0.15, 0.25, 100.0);        // down: passband 0.30 fs, stopband from base Nyquist
        }
        else
        {
            stageA.design (0.4535 / 2.0, 100.0);    // passband 0.4535 fs (20 kHz at 44.1 kHz)
        }
        // stage B (2fs <-> 4fs) only has to reject what would alias into stage A's
        // passband/transition, so its band edge can be much wider (short filter).
        stageB.design (0.1366, 100.0);

        const int downA = highRate ? decA.delayHigh() : stageA.delayHigh();
        const int t2 = stageA.delayHigh() + downA;                       // 2x, in 2fs samples
        const int t4 = t2 + stageB.delayHigh();                          // 4x: + B up/down (in 2fs samples)
        bridge[1] = t2 & 1;
        bridge[2] = t4 & 1;
        latency[0] = 0;
        latency[1] = (t2 + bridge[1]) / 2;
        latency[2] = (t4 + bridge[2]) / 2;

        for (int ch = 0; ch < kMaxCh; ++ch)
        {
            mid[ch].assign ((size_t) (2 * maxBlock), 0.0f);
            midDown[ch].assign ((size_t) (2 * maxBlock), 0.0f);
            high[ch].assign ((size_t) (4 * maxBlock), 0.0f);
        }
        reset();
    }

    void reset() noexcept
    {
        stageA.reset();
        stageB.reset();
        decA.reset();
        for (auto& d : bridgeState)
            d = 0.0f;
    }

    static int factorFor (int log2Factor) noexcept { return 1 << std::clamp (log2Factor, 0, kMaxFactorLog2); }

    // total base-rate latency of up + down for a given factor
    int latencyFor (int log2Factor) const noexcept { return latency[std::clamp (log2Factor, 0, kMaxFactorLog2)]; }
    bool isHighRateDesign() const noexcept { return highRate; }

    // returns pointer to the oversampled data for channel ch (n * factor samples)
    float* up (const float* in, int n, int ch, int log2Factor) noexcept
    {
        assert (n <= maxBlock);
        switch (log2Factor)
        {
            case 0:
                std::copy (in, in + n, high[ch].data());
                break;
            case 1:
                stageA.up (in, high[ch].data(), n, ch);
                break;
            default:
                stageA.up (in, mid[ch].data(), n, ch);
                stageB.up (mid[ch].data(), high[ch].data(), 2 * n, ch);
                break;
        }
        return high[ch].data();
    }

    void down (float* out, int n, int ch, int log2Factor) noexcept
    {
        if (log2Factor == 0)
        {
            std::copy (high[ch].data(), high[ch].data() + n, out);
            return;
        }
        float* m = high[ch].data();
        if (log2Factor >= 2)
        {
            m = midDown[ch].data();
            stageB.down (high[ch].data(), m, 2 * n, ch);
        }
        if (bridge[log2Factor] != 0)
        {
            // one sample of delay at 2fs keeps the total latency an integer number of base samples
            for (int i = 0; i < 2 * n; ++i)
                std::swap (m[i], bridgeState[ch]);
        }
        if (highRate)
            decA.down (m, out, n, ch);
        else
            stageA.down (m, out, n, ch);
    }

    const HalfbandStage& firstStage() const noexcept { return stageA; }
    const HalfbandStage& secondStage() const noexcept { return stageB; }
    const LowpassDecimator& highRateDecimator() const noexcept { return decA; }

private:
    int maxBlock = 0;
    bool highRate = false;
    HalfbandStage stageA, stageB;
    LowpassDecimator decA;
    int bridge[3] {}, latency[3] {};
    std::vector<float> mid[kMaxCh], midDown[kMaxCh], high[kMaxCh];
    float bridgeState[kMaxCh] {};
};
} // namespace penta
