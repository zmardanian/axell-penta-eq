#pragma once
// Axell Penta EQ - correction FIRs (framework independent, allocation only in prepare()).
//
// The matched biquads (Sections.h) follow the analog prototypes closely but not perfectly near
// Nyquist and for resonant shelves. The CorrectionDesigner accumulates, per frequency bin and per
// lane (L, R, M, S), the ratio analog / digital of every static section, then builds:
//   * Zero Latency : a minimum-phase FIR of |analog/digital| (real cepstrum) - no added latency
//   * Natural Phase: a FIR of the complex ratio (analog/digital) * e^{-jwD} - analog phase, latency D
// Measured in tools/proto: 256 taps at 44.1/48 kHz bring every tested case below 0.0001 dB.

#include "Fft.h"
#include "Sections.h"
#include "../Oversampling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace penta::dsp
{
enum LaneBit { laneL = 1, laneR = 2, laneM = 4, laneS = 8 };
constexpr int kNumLanes = 4;

struct FirSet
{
    // taps in natural order (h[0] = first output sample); `active` = lane needs a FIR
    std::array<std::vector<float>, kNumLanes> taps;
    std::array<bool, kNumLanes> active {};
    int length = 0;
};

class CorrectionDesigner
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        n = sampleRate <= 50000.0 ? 2048 : (sampleRate <= 100000.0 ? 4096 : 8192);
        tapsMin = n / 8;
        naturalDelay = n <= 2048 ? 128 : (n <= 4096 ? 256 : 512); // 2.7 ms: steep cuts need it (tools: np.cpp)
        nb = n / 2 + 1;
        fft.prepare (n);
        work.assign ((size_t) n, {});
        for (int os = 0; os < 3; ++os)
        {
            auto& t = tab[os];
            t.p0.resize ((size_t) nb); t.p1.resize ((size_t) nb); t.p2.resize ((size_t) nb);
            t.w.resize ((size_t) nb); t.z1.resize ((size_t) nb);
            for (int k = 0; k < nb; ++k)
            {
                const double w = kPi * k / (nb - 1) / (double) (1 << os); // rad/sample of the OS rate
                t.w[(size_t) k] = w;
                const double s = std::sin (0.5 * w);
                t.p1[(size_t) k] = s * s;
                t.p0[(size_t) k] = 1.0 - s * s;
                t.p2[(size_t) k] = 4.0 * t.p0[(size_t) k] * t.p1[(size_t) k];
                t.z1[(size_t) k] = std::polar (1.0, -w);
            }
        }
        matA.assign ((size_t) nb, {});
        matD.assign ((size_t) nb, {});
        matC.assign ((size_t) nb, {});
        matLevel.assign ((size_t) nb, { 1.0, 1.0 });
        for (auto& l : lanes)
        {
            l.ratio.assign ((size_t) nb, 1.0);
            l.analog.assign ((size_t) nb, 1.0);
            l.cratio.assign ((size_t) nb, { 1.0, 0.0 });
            l.logRes.assign ((size_t) nb, 0.0);
        }
        clear (false);
    }

    void setNaturalDelay (int d) noexcept { naturalDelay = std::clamp (d, 8, n / 4); } // tests / tuning
    int minPhaseLength() const noexcept { return tapsMin; }
    int naturalLength() const noexcept { return 2 * naturalDelay; }
    int naturalLatency() const noexcept { return naturalDelay; }
    int fftSize() const noexcept { return n; }

    void clear (bool complexToo)
    {
        complexMode = complexToo;
        for (auto& l : lanes)
        {
            std::fill (l.ratio.begin(), l.ratio.end(), 1.0);
            std::fill (l.analog.begin(), l.analog.end(), 1.0);
            if (complexToo)
                std::fill (l.cratio.begin(), l.cratio.end(), std::complex<double> (1.0, 0.0));
            l.used = false;
        }
    }

    // accumulate one static section running at rate fs * 2^osLog2, applied to the lanes in laneMask
    void add (int laneMask, const Section& s, const Biquad& q, int osLog2) noexcept
    {
        if (s.kind == Kind::unity || laneMask == 0)
            return;
        const auto& t = tab[std::clamp (osLog2, 0, 2)];
        for (int li = 0; li < kNumLanes; ++li)
        {
            if ((laneMask & (1 << li)) == 0)
                continue;
            auto& l = lanes[(size_t) li];
            l.used = true;
            for (int k = 0; k < nb; ++k)
            {
                const double w = t.w[(size_t) k];
                const double a = s.magSq (w);
                const double d = q.magSq (t.p0[(size_t) k], t.p1[(size_t) k], t.p2[(size_t) k]);
                l.analog[(size_t) k] *= a;
                l.ratio[(size_t) k] *= a / std::max (d, 1.0e-300);
            }
            if (complexMode)
                for (int k = 0; k < nb; ++k)
                {
                    const auto z1 = t.z1[(size_t) k], z2 = z1 * z1;
                    const auto hd = (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
                    l.cratio[(size_t) k] *= s.response (t.w[(size_t) k]) / hd;
                }
        }
    }

    // a section that is part of the analog target but realised exactly (only shapes the weighting)
    void addAnalogOnly (int laneMask, const Section& s, int osLog2) noexcept
    {
        const auto& t = tab[std::clamp (osLog2, 0, 2)];
        for (int li = 0; li < kNumLanes; ++li)
            if (laneMask & (1 << li))
                for (int k = 0; k < nb; ++k)
                    lanes[(size_t) li].analog[(size_t) k] *= s.magSq (t.w[(size_t) k]);
    }

    // returns true if any lane needs correction; fills `out` (taps sized to the mode's length)
    bool finish (FirSet& out, bool natural)
    {
        bool any = false;
        const int len = natural ? naturalLength() : tapsMin;
        out.length = len;
        for (int li = 0; li < kNumLanes; ++li)
        {
            auto& l = lanes[(size_t) li];
            auto& taps = out.taps[(size_t) li];
            taps.assign ((size_t) len, 0.0f);
            out.active[(size_t) li] = false;
            if (! l.used)
                continue;
            // weighted, clamped log residual
            double worst = 0.0;
            for (int k = 0; k < nb; ++k)
            {
                const double adb = 10.0 * std::log10 (std::max (l.analog[(size_t) k], 1.0e-300));
                const double wgt = std::clamp ((adb + 90.0) / 30.0, 0.0, 1.0);
                const double lr = 0.5 * std::log (std::max (l.ratio[(size_t) k], 1.0e-300));
                const double v = wgt * std::clamp (lr, -kClampLn, kClampLn);
                l.logRes[(size_t) k] = v;
                worst = std::max (worst, std::abs (v));
            }
            double worstPhase = 0.0;
            if (natural)
                for (int k = 0; k < nb; ++k)
                    worstPhase = std::max (worstPhase, std::abs (std::arg (l.cratio[(size_t) k])));
            if (worst < kBypassLn && (! natural || worstPhase < 1.0e-5))
                continue;
            any = true;
            out.active[(size_t) li] = true;
            if (natural)
                buildNatural (l, taps);
            else
                buildMinPhase (l, taps);
        }
        return any;
    }

    //== Natural Phase: 2x2 transfer matrices (exact for any mix of Stereo/L/R/M/S placements) ==
    // lane order of the produced FirSet: 0 = L<-L, 1 = L<-R, 2 = R<-L, 3 = R<-R
    void clearMatrix (int numChannels)
    {
        matCh = std::clamp (numChannels, 1, 2);
        for (int k = 0; k < nb; ++k)
        {
            matA[(size_t) k] = { 1, 0, 0, 1 };
            matD[(size_t) k] = { 1, 0, 0, 1 };
            matLevel[(size_t) k] = { 1.0, 1.0 };
        }
        matUsed = false;
    }

    // placement: 0 stereo, 1 L, 2 R, 3 M, 4 S
    void addMatrix (int placement, const Section& s, const Biquad& q, int osLog2) noexcept
    {
        if (s.kind == Kind::unity)
            return;
        if (matCh == 1 && placement == 4)
            return;
        matUsed = true;
        const auto& t = tab[std::clamp (osLog2, 0, 2)];
        for (int k = 0; k < nb; ++k)
        {
            const auto z1 = t.z1[(size_t) k], z2 = z1 * z1;
            const auto hd = (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
            const auto ha = s.response (t.w[(size_t) k]);
            const double lev = s.magSq (t.w[(size_t) k]);
            auto& lv = matLevel[(size_t) k];
            if (matCh == 1 || placement == 0) { lv[0] *= lev; lv[1] *= lev; }
            else if (placement == 1) lv[0] *= lev;
            else if (placement == 2) lv[1] *= lev;
            else { lv[0] *= std::sqrt (lev); lv[1] *= std::sqrt (lev); }
            leftMultiply (matA[(size_t) k], placement, ha);
            leftMultiply (matD[(size_t) k], placement, hd);
        }
    }

    // returns false when the correction is an exact identity (then lanes hold a pure delay)
    bool finishMatrix (FirSet& out)
    {
        const int len = naturalLength();
        out.length = len;
        for (auto& t : out.taps)
            t.assign ((size_t) len, 0.0f);
        out.active = { true, false, false, matCh == 2 };
        if (! matUsed)
            return false;
        // C = A * D^-1, faded towards identity where the analog target is extremely low
        for (int k = 0; k < nb; ++k)
        {
            const auto& A = matA[(size_t) k];
            const auto& D = matD[(size_t) k];
            const std::complex<double> det = D[0] * D[3] - D[1] * D[2];
            std::array<std::complex<double>, 4> inv { D[3] / det, -D[1] / det, -D[2] / det, D[0] / det };
            if (matCh == 1)
                inv = { 1.0 / D[0], 0.0, 0.0, 0.0 };
            std::array<std::complex<double>, 4> C { A[0] * inv[0] + A[1] * inv[2], A[0] * inv[1] + A[1] * inv[3],
                                                    A[2] * inv[0] + A[3] * inv[2], A[2] * inv[1] + A[3] * inv[3] };
            if (matCh == 1)
                C = { A[0] * inv[0], 0.0, 0.0, 0.0 };
            // zeros on the unit circle (e.g. a high-pass at DC) make D singular: no correction there
            bool finite = std::isfinite (std::abs (det)) && std::abs (det) > 1.0e-200;
            for (const auto& e : C)
                finite = finite && std::isfinite (e.real()) && std::isfinite (e.imag());
            if (! finite)
                C = { 1.0, 0.0, 0.0, 1.0 };
            for (int r = 0; r < 2; ++r)
            {
                const double adb = 10.0 * std::log10 (std::max (matLevel[(size_t) k][(size_t) r], 1.0e-300));
                const double wgt = std::clamp ((adb + 90.0) / 30.0, 0.0, 1.0);
                for (int c = 0; c < 2; ++c)
                {
                    auto& e = C[(size_t) (2 * r + c)];
                    const std::complex<double> id = r == c ? 1.0 : 0.0;
                    e = id + wgt * (e - id);
                    const double m = std::abs (e);
                    if (m > 4.0)
                        e *= 4.0 / m;
                }
            }
            matC[(size_t) k] = C;
        }
        const int lanesN = matCh == 1 ? 1 : 4;
        for (int li = 0; li < lanesN; ++li)
        {
            double worst = 0.0;
            const std::complex<double> id = (li == 0 || li == 3) ? 1.0 : 0.0;
            for (int k = 0; k < nb; ++k)
                worst = std::max (worst, std::abs (matC[(size_t) k][(size_t) li] - id));
            out.active[(size_t) li] = (li == 0 || li == 3) || worst > 1.0e-7;
            if (! out.active[(size_t) li])
                continue;
            for (int k = 0; k < nb; ++k)
                lanes[0].cratio[(size_t) k] = matC[(size_t) k][(size_t) li];
            buildComplex (lanes[0].cratio, out.taps[(size_t) li]);
        }
        return true;
    }

private:
    static void leftMultiply (std::array<std::complex<double>, 4>& M, int placement, std::complex<double> h) noexcept
    {
        // band matrix B (2x2) times M
        std::complex<double> b[4];
        switch (placement)
        {
            case 1: b[0] = h; b[1] = 0; b[2] = 0; b[3] = 1; break;
            case 2: b[0] = 1; b[1] = 0; b[2] = 0; b[3] = h; break;
            case 3: b[0] = 0.5 * (h + 1.0); b[1] = 0.5 * (h - 1.0); b[2] = b[1]; b[3] = b[0]; break;
            case 4: b[0] = 0.5 * (1.0 + h); b[1] = 0.5 * (1.0 - h); b[2] = b[1]; b[3] = b[0]; break;
            default: b[0] = h; b[1] = 0; b[2] = 0; b[3] = h; break;
        }
        const std::array<std::complex<double>, 4> r { b[0] * M[0] + b[1] * M[2], b[0] * M[1] + b[1] * M[3],
                                                      b[2] * M[0] + b[3] * M[2], b[2] * M[1] + b[3] * M[3] };
        M = r;
    }

    void buildComplex (const std::vector<std::complex<double>>& spec, std::vector<float>& taps)
    {
        const int D = naturalDelay;
        for (int k = 0; k < nb; ++k)
            work[(size_t) k] = spec[(size_t) k] * std::polar (1.0, -kPi * k / (nb - 1) * D);
        work[0] = { work[0].real(), 0.0 };
        work[(size_t) (nb - 1)] = { work[(size_t) (nb - 1)].real(), 0.0 };
        for (int k = nb; k < n; ++k)
            work[(size_t) k] = std::conj (work[(size_t) (n - k)]);
        fft.inverse (work.data());
        const double sc = 1.0 / n;
        const int len = (int) taps.size(), edge = len / 4;
        for (int i = 0; i < len; ++i)
        {
            double wgt = 1.0;
            if (i < edge)
                wgt = 0.5 * (1.0 - std::cos (kPi * (i + 1) / (edge + 1)));
            else if (i >= len - edge)
                wgt = 0.5 * (1.0 + std::cos (kPi * (i - (len - edge) + 1) / (edge + 1)));
            taps[(size_t) i] = (float) (work[(size_t) i].real() * sc * wgt);
        }
    }

    std::vector<std::array<std::complex<double>, 4>> matA, matD, matC;
    std::vector<std::array<double, 2>> matLevel;
    int matCh = 2;
    bool matUsed = false;

    static constexpr double kClampLn = 1.3815510557964274; // 12 dB
    static constexpr double kBypassLn = 2.3e-4;            // 0.002 dB

    struct Lane
    {
        std::vector<double> ratio, analog;
        std::vector<std::complex<double>> cratio;
        std::vector<double> logRes;
        bool used = false;
    };
    struct Tables
    {
        std::vector<double> p0, p1, p2, w;
        std::vector<std::complex<double>> z1;
    };

    void buildMinPhase (const Lane& l, std::vector<float>& taps)
    {
        // real cepstrum of the log magnitude, folded -> minimum phase
        for (int k = 0; k < nb; ++k)
            work[(size_t) k] = { l.logRes[(size_t) k], 0.0 };
        for (int k = nb; k < n; ++k)
            work[(size_t) k] = { l.logRes[(size_t) (n - k)], 0.0 };
        fft.inverse (work.data());
        const double sc = 1.0 / n;
        for (int i = 0; i < n; ++i)
        {
            double c = work[(size_t) i].real() * sc;
            if (i > 0 && i < n / 2)
                c *= 2.0;
            else if (i > n / 2)
                c = 0.0;
            work[(size_t) i] = { c, 0.0 };
        }
        fft.forward (work.data());
        for (int k = 0; k < n; ++k)
            work[(size_t) k] = std::exp (work[(size_t) k]);
        fft.inverse (work.data());
        const int len = (int) taps.size(), tail = len / 4;
        for (int i = 0; i < len; ++i)
        {
            double h = work[(size_t) i].real() * sc;
            if (i >= len - tail)
                h *= 0.5 * (1.0 + std::cos (kPi * (i - (len - tail) + 1) / (tail + 1)));
            taps[(size_t) i] = (float) h;
        }
    }

    void buildNatural (const Lane& l, std::vector<float>& taps)
    {
        const int D = naturalDelay;
        for (int k = 0; k < nb; ++k)
        {
            auto c = l.cratio[(size_t) k];
            const double mag = std::abs (c);
            const double target = std::exp (l.logRes[(size_t) k]); // weighted/clamped magnitude
            c = mag > 0.0 ? c * (target / mag) : std::complex<double> (target, 0.0);
            const double w = kPi * k / (nb - 1);
            work[(size_t) k] = c * std::polar (1.0, -w * D);
        }
        work[(size_t) (nb - 1)] = { work[(size_t) (nb - 1)].real(), 0.0 };
        work[0] = { work[0].real(), 0.0 };
        for (int k = nb; k < n; ++k)
            work[(size_t) k] = std::conj (work[(size_t) (n - k)]);
        fft.inverse (work.data());
        const double sc = 1.0 / n;
        const int len = (int) taps.size(), edge = len / 4;
        for (int i = 0; i < len; ++i)
        {
            double h = work[(size_t) i].real() * sc;
            double wgt = 1.0;
            if (i < edge)
                wgt = 0.5 * (1.0 - std::cos (kPi * (i + 1) / (edge + 1)));
            else if (i >= len - edge)
                wgt = 0.5 * (1.0 + std::cos (kPi * (i - (len - edge) + 1) / (edge + 1)));
            taps[(size_t) i] = (float) (h * wgt);
        }
    }

    double fs = 48000.0;
    int n = 2048, nb = 1025, tapsMin = 256, naturalDelay = 64;
    bool complexMode = false;
    ComplexFft<double> fft;
    std::vector<std::complex<double>> work;
    Tables tab[3];
    std::array<Lane, kNumLanes> lanes;
};

//==============================================================================
// One FIR lane with click-free coefficient changes (output crossfade between old and new taps).
class FirLane
{
public:
    void prepare (int maxLen, int fadeLen = 256)
    {
        maxTaps = std::max (1, maxLen);
        hist.init (maxTaps);
        cur.assign ((size_t) maxTaps, 0.0f);
        nxt.assign ((size_t) maxTaps, 0.0f);
        fade = std::max (1, fadeLen);
        reset();
    }

    void reset() noexcept
    {
        hist.clear();
        fadePos = -1;
    }

    // taps in natural order; len <= maxTaps
    void setTaps (const float* taps, int len, bool crossfade) noexcept
    {
        auto& dst = beginWrite (crossfade);
        for (int i = 0; i < len && i < maxTaps; ++i)
            dst[(size_t) (maxTaps - 1 - i)] = taps[i]; // reversed: oldest .. newest
        endWrite (crossfade);
    }

    // pure delay (used by flat lanes in Natural Phase so every lane has the same latency)
    void setIdentity (int delay, bool crossfade) noexcept
    {
        auto& dst = beginWrite (crossfade);
        dst[(size_t) (maxTaps - 1 - std::clamp (delay, 0, maxTaps - 1))] = 1.0f;
        endWrite (crossfade);
    }

    void process (float* x, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float* w = hist.push (x[i]);
            float y = os_detail::dot (w, cur.data(), maxTaps);
            if (fadePos >= 0)
            {
                const float yn = os_detail::dot (w, nxt.data(), maxTaps);
                const float t = (float) (fadePos + 1) / (float) fade;
                y += (yn - y) * t;
                if (++fadePos >= fade)
                {
                    cur.swap (nxt);
                    fadePos = -1;
                }
            }
            x[i] = y;
        }
    }

    void setZero() noexcept
    {
        beginWrite (false);
        endWrite (false);
    }

    // keep the history warm without computing an output (inactive lanes / cross-terms)
    void pushOnly (const float* x, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            hist.push (x[i]);
    }

    // y[i] += fir(x)[i] (x is not modified) - for the 2x2 natural-phase matrix
    void processAdd (const float* x, float* y, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float* w = hist.push (x[i]);
            float v = os_detail::dot (w, cur.data(), maxTaps);
            if (fadePos >= 0)
            {
                const float vn = os_detail::dot (w, nxt.data(), maxTaps);
                v += (vn - v) * ((float) (fadePos + 1) / (float) fade);
                if (++fadePos >= fade)
                {
                    cur.swap (nxt);
                    fadePos = -1;
                }
            }
            y[i] += v;
        }
    }

    bool isFading() const noexcept { return fadePos >= 0; }

private:
    std::vector<float>& beginWrite (bool crossfade) noexcept
    {
        if (fadePos >= 0)
        {
            // a new set arrived mid-fade: settle on the fading target first
            cur.swap (nxt);
            fadePos = -1;
        }
        auto& dst = crossfade && configured ? nxt : cur;
        std::fill (dst.begin(), dst.end(), 0.0f);
        return dst;
    }
    void endWrite (bool crossfade) noexcept
    {
        if (crossfade && configured)
            fadePos = 0;
        configured = true;
    }

    int maxTaps = 1, fade = 256, fadePos = -1;
    bool configured = false;
    os_detail::History hist;
    std::vector<float> cur, nxt;
};
} // namespace penta::dsp
