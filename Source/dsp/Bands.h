#pragma once
// Axell Penta EQ - parametric band shapes -> analog section cascades (framework independent).

#include "../Params.h"
#include "Chain.h"

#include <algorithm>
#include <cmath>

namespace penta::dsp
{
struct BandShape
{
    int type = kBell, slopeIdx = 3, mode = 0;
    double freqHz = 1000.0, gainDb = 0.0, q = 1.0;
};

inline bool isGainType (int type) noexcept
{
    return type == kBell || type == kLowShelf || type == kHighShelf || type == kTilt;
}

// proportional Q (Character mode, Console G): wide at low gain, narrow at high gain
inline double proportionalQ (double q, double gainDb) noexcept
{
    return q * std::clamp (0.55 + 0.03 * std::abs (gainDb), 0.55, 1.45);
}

inline int slopeOrder (int slopeIdx) noexcept
{
    return kSlopeDb[std::clamp (slopeIdx, 0, 7)] / 6;
}

// Butterworth low/high cut of the given order; resonance q scales the lowest-Q pole pair (0.7071 = plain
// Butterworth; this keeps the corner peak about as high as a 2nd-order filter's at every slope)
inline void addButterworth (ChainDesign& d, bool highpass, double w0, int order, double resonance)
{
    order = std::clamp (order, 1, 16);
    const int pairs = order / 2;
    for (int k = 0; k < pairs; ++k)
    {
        double q = proto::butterQ (order, k);
        if (k == 0)
            q *= resonance / 0.70710678118654752;
        d.add (highpass ? proto::highpass2 (w0, q) : proto::lowpass2 (w0, q));
    }
    if (order & 1)
        d.add (highpass ? proto::highpass1 (w0) : proto::lowpass1 (w0));
}

// Build the analog cascade of one band at processing rate fsP. `characterAllowed` = false gives the
// Clean shape even in Character mode (Linear Phase delta bands, display of the static curve).
inline void buildBand (const BandShape& b, double fsP, ChainDesign& d, bool characterAllowed = true)
{
    d.clear();
    const double f = std::clamp (b.freqHz, 5.0, 60000.0);
    const double w = 2.0 * kPi * f / fsP;
    const double q = std::clamp (b.q, 0.05, 40.0);
    const double g = b.gainDb;
    const bool character = characterAllowed && b.mode == 1;
    const int order = slopeOrder (b.slopeIdx);
    switch (b.type)
    {
        case kBell:
            if (character)
            {
                const double qe = proportionalQ (q, g);
                d.add (proto::bandpass2 (w, qe));
                d.parallel = true;
                d.parallelC = std::pow (10.0, g / 20.0) - 1.0;
                d.parallelEquiv = proto::parallelBell (w, qe, g);
            }
            else
                d.add (proto::bell (w, q, g));
            break;
        case kLowShelf:
        case kHighShelf:
        {
            const bool low = b.type == kLowShelf;
            if (order == 1)
                d.add (low ? proto::lowShelf1 (w, g) : proto::highShelf1 (w, g));
            else if (character)
            {
                d.add (low ? proto::lowpass2 (w, q) : proto::highpass2 (w, q));
                d.parallel = true;
                const double gl = std::pow (10.0, g / 20.0);
                d.parallelC = gl - 1.0;
                Section e;
                const double i2 = 1.0 / (w * w), i1 = 1.0 / (q * w);
                e.d0 = i2; e.d1 = i1; e.d2 = 1.0;
                if (low) { e.n0 = i2; e.n1 = i1; e.n2 = gl; }
                else     { e.n0 = gl * i2; e.n1 = i1; e.n2 = 1.0; }
                e.kind = Kind::generic;
                e.w0 = w;
                d.parallelEquiv = e;
            }
            else
                d.add (low ? proto::lowShelf2 (w, q, g) : proto::highShelf2 (w, q, g));
            break;
        }
        case kLowCut:
        case kHighCut:
            addButterworth (d, b.type == kLowCut, w, order, std::clamp (q, 0.3, 12.0));
            break;
        case kNotch:
        {
            d.add (proto::notch (w, q)); // above Nyquist too: the in-band part of the analog curve
            break;
        }
        case kBandPass:
        {
            const double wb = w;
            // 2nd-order sections: slope / 12, rounded (6 -> 1, 18 -> 2, 72 -> 6, 96 -> 8)
            const int n = std::clamp ((kSlopeDb[std::clamp (b.slopeIdx, 0, 7)] + 6) / 12, 1, kMaxSections);
            for (int i = 0; i < n; ++i)
                d.add (proto::bandpass2 (wb, q));
            break;
        }
        case kTilt:
            d.add (order == 1 ? proto::tilt1 (w, g) : proto::tilt2 (w, q, g));
            break;
        default:
            break;
    }
    // near-Nyquist refinement up to 20 kHz (Sections.h); below ~0.93 pi at any rate
    // (not for the parallel Character forms: their digital equivalent is one biquad, Chain::parallelBiquad)
    const double wTop = d.parallel ? 0.0 : std::min (2.0 * kPi * 20000.0 / fsP, 0.93 * kPi);
    for (int i = 0; i < d.count; ++i)
        d.sect[(size_t) i].wTop = wTop;
}

// analog magnitude (dB) of a design at a frequency in rad/sample of its processing rate
inline double designMagSq (const ChainDesign& d, double w) noexcept
{
    if (d.parallel)
        return d.parallelEquiv.magSq (w);
    double m = 1.0;
    for (int i = 0; i < d.count; ++i)
        m *= d.sect[(size_t) i].magSq (w);
    return m;
}

//==============================================================================
// Detector / solo region filter: a plain TPT SVF (bilinear), cheap and modulation-safe
struct RegionFilter
{
    enum Mode { bp, lp, hp };
    double a1 = 1, a2 = 0, a3 = 0, k = 1;
    int mode = bp;
    SvfState st[kMaxChannels];

    void set (int bandType, double freqHz, double q, double fs) noexcept
    {
        const double f = std::clamp (freqHz, 10.0, 0.45 * fs);
        const double g = std::tan (kPi * f / fs);
        switch (bandType)
        {
            case kLowShelf: case kLowCut: mode = lp; k = 1.41421356; break;
            case kHighShelf: case kHighCut: case kTilt: mode = hp; k = 1.41421356; break;
            default: mode = bp; k = 1.0 / std::clamp (q, 0.5, 12.0); break;
        }
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept
    {
        for (auto& s : st)
            s.reset();
    }
    double tick (double x, int ch) noexcept
    {
        auto& s = st[ch];
        const double v3 = x - s.ic2;
        const double v1 = a1 * s.ic1 + a2 * v3;
        const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;
        if (mode == lp)
            return v2;
        if (mode == hp)
            return x - k * v1 - v2;
        return k * v1; // unity-peak band-pass
    }
};
} // namespace penta::dsp
