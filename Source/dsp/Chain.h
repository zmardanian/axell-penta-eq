#pragma once
// Axell Penta EQ - a cascade of matched SVF sections with per-sample coefficient interpolation,
// optional parallel ("character") form and a band-level fade. Shared by the parametric bands,
// the dedicated cuts, the delta bands of Linear Phase mode and the character modules.

#include "Sections.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace penta::dsp
{
constexpr int kMaxSections = 8;
constexpr int kMaxChannels = 2;

// soft saturation: identity when k == 0, tanh(k x)/k otherwise (unity small-signal gain)
inline double softSat (double x, double k) noexcept
{
    return k <= 0.0 ? x : std::tanh (k * x) / k;
}

struct ChainDesign
{
    std::array<Section, kMaxSections> sect {};
    int count = 0;
    // parallel form: y = x + sat(c * component), component = sect[0] realised alone
    bool parallel = false;
    double parallelC = 0.0;   // c = g - 1
    Section parallelEquiv;    // analog equivalent 1 + c*component (for correction / display)

    void clear() noexcept { count = 0; parallel = false; parallelC = 0.0; }
    void add (const Section& s) noexcept
    {
        if (count < kMaxSections)
            sect[(size_t) count++] = s;
    }
};

class Chain
{
public:
    // set a new design to be reached at the end of the next `steps` samples
    void target (const ChainDesign& d, int steps) noexcept
    {
        if (d.count != count || d.parallel != parallel)
        {
            jumpTo (d);
            return;
        }
        for (int i = 0; i < count; ++i)
        {
            bq[(size_t) i] = designMatched (d.sect[(size_t) i]);
            ramp[(size_t) i].begin (toSvf (bq[(size_t) i]), steps);
        }
        cBegin (d.parallelC, steps);
        remaining = std::max (1, steps);
        design = d;
    }

    // switch immediately (structural change while faded out / first use); resets the states
    void jumpTo (const ChainDesign& d) noexcept
    {
        count = d.count;
        parallel = d.parallel;
        for (int i = 0; i < count; ++i)
        {
            bq[(size_t) i] = designMatched (d.sect[(size_t) i]);
            ramp[(size_t) i].jump (toSvf (bq[(size_t) i]));
        }
        cCur = cTgt = d.parallelC;
        cInc = 0.0;
        remaining = 0;
        design = d;
        resetStates();
    }

    void resetStates() noexcept
    {
        for (auto& ch : st)
            for (auto& s : ch)
                s.reset();
    }

    // process `n` samples of channel `ch` in place. All channels of a control block must be
    // processed with the same n; call advance(n) once afterwards.
    void process (float* x, int n, int ch, double satK, const float* fade) const noexcept
    {
        std::array<SvfRamp, kMaxSections> r;
        for (int i = 0; i < count; ++i)
            r[(size_t) i] = ramp[(size_t) i];
        double c = cCur;
        auto& s = st[(size_t) ch];
        const bool ramping = remaining > 0;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            double y = in;
            for (int k = 0; k < count; ++k)
            {
                y = svfTick (r[(size_t) k].cur, s[(size_t) k], y);
                if (ramping)
                    r[(size_t) k].step();
            }
            if (parallel)
            {
                const double comp = c * y;
                y = in + (c > 0.0 ? softSat (comp, satK) : comp);
                if (ramping)
                    c += cInc;
            }
            x[i] = fade == nullptr ? (float) y : (float) (in + (double) fade[i] * (y - in));
        }
    }

    // A ramp spans exactly one control block; segments never cross a control-block boundary,
    // so n <= remaining. When the block ends the ramp lands exactly on its target and stops
    // (until the next target() call) - without this the increments would keep running.
    void advance (int n) noexcept
    {
        if (remaining <= 0)
            return;
        for (int i = 0; i < count; ++i)
            for (int k = 0; k < n; ++k)
                ramp[(size_t) i].step();
        for (int k = 0; k < n; ++k)
            cCur += cInc;
        remaining -= n;
        if (remaining <= 0)
        {
            for (int i = 0; i < count; ++i)
                ramp[(size_t) i].jump (ramp[(size_t) i].tgt);
            cCur = cTgt;
            cInc = 0.0;
            remaining = 0;
        }
    }

    const ChainDesign& current() const noexcept { return design; }
    const Biquad& biquad (int i) const noexcept { return bq[(size_t) i]; }
    int size() const noexcept { return count; }

    // digital equivalent of the whole parallel form (for the correction designer)
    Biquad parallelBiquad() const noexcept
    {
        Biquad e = bq[0];
        const double c = design.parallelC;
        e.b0 = 1.0 + c * bq[0].b0;
        e.b1 = bq[0].a1 + c * bq[0].b1;
        e.b2 = bq[0].a2 + c * bq[0].b2;
        return e;
    }

private:
    void cBegin (double c, int steps) noexcept
    {
        cCur = cTgt;
        cTgt = c;
        cInc = (cTgt - cCur) / std::max (1, steps);
    }

    int count = 0;
    bool parallel = false;
    ChainDesign design;
    std::array<Biquad, kMaxSections> bq {};
    std::array<SvfRamp, kMaxSections> ramp {};
    double cCur = 0, cTgt = 0, cInc = 0;
    int remaining = 0;
    mutable std::array<std::array<SvfState, kMaxSections>, kMaxChannels> st {};
};
} // namespace penta::dsp
