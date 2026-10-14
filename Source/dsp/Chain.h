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
    // A section's realisation changes either smoothly (coefficients ramp over one control block) or,
    // when it jumps - classic matched biquad <-> fitted near-Nyquist pair, a fit that restarted - by a
    // crossfade: the new realisation runs on the same input with fresh states, silently until it has
    // settled, then fades in while the old one keeps playing (kWarmBlocks + kFadeBlocks control
    // blocks, ~12 ms each). Interpolating the coefficients of two very different filters that hold
    // signal would click.
    static constexpr int kWarmBlocks = 32, kFadeBlocks = 32;

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
            const auto u = (size_t) i;
            const bool wasTwo = curTwo (u);
            realiseSection (d, i);
            auto& sl = sec[u];
            const bool jump = real[u].two != wasTwo || (real[u].two && track[u].restarted);
            if (jump)
            {
                // start (or restart) the crossfade towards the new realisation
                sl.pend = true;
                sl.pPos = 0;
                sl.warmLen = kWarmBlocks * steps;
                sl.fadeLen = kFadeBlocks * steps;
                sl.pTwo = real[u].two;
                sl.pA.jump (toSvf (real[u].q[0]));
                sl.pB.jump (sl.pTwo ? toSvf (real[u].q[1]) : SvfCoefs {});
                for (auto& ch : sl.pst)
                    ch = {};
                // the playing realisation keeps its family meanwhile: a classic one follows the target
                // with classic designs, a fitted one holds
                if (! sl.two)
                    sl.A.begin (toSvf (designMatched (d.sect[u])), steps);
                else
                    holdRamps (sl, steps);
            }
            else if (sl.pend)
            {
                sl.pA.begin (toSvf (real[u].q[0]), steps);
                sl.pB.begin (sl.pTwo ? toSvf (real[u].q[1]) : SvfCoefs {}, steps);
                if (! sl.two)
                    sl.A.begin (toSvf (designMatched (d.sect[u])), steps);
                else
                    holdRamps (sl, steps);
            }
            else
            {
                sl.A.begin (toSvf (real[u].q[0]), steps);
                sl.B.begin (sl.two ? toSvf (real[u].q[1]) : SvfCoefs {}, steps);
            }
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
        for (auto& t : track)
            t = FitTrack {}; // a fresh design: cold fits (deterministic)
        for (int i = 0; i < count; ++i)
        {
            const auto u = (size_t) i;
            realiseSection (d, i);
            auto& sl = sec[u];
            sl.A.jump (toSvf (real[u].q[0]));
            sl.two = real[u].two;
            sl.B.jump (sl.two ? toSvf (real[u].q[1]) : SvfCoefs {});
            sl.pend = false;
        }
        cCur = cTgt = d.parallelC;
        cInc = 0.0;
        remaining = 0;
        design = d;
        resetStates();
    }

    void resetStates() noexcept
    {
        for (auto& sl : sec)
        {
            for (auto& ch : sl.st)
                ch = {};
            for (auto& ch : sl.pst)
                ch = {};
        }
    }

    // process `n` samples of channel `ch` in place. All channels of a control block must be
    // processed with the same n; call advance(n) once afterwards.
    void process (float* x, int n, int ch, double satK, const float* fade) const noexcept
    {
        bool simple = true; // no fitted pair, no crossfade: one SVF per section (the common case)
        for (int i = 0; i < count; ++i)
            simple = simple && ! sec[(size_t) i].two && ! sec[(size_t) i].pend;
        if (simple)
        {
            processSimple (x, n, ch, satK, fade);
            return;
        }
        struct Local { SvfRamp A, B, pA, pB; };
        std::array<Local, kMaxSections> loc;
        for (int i = 0; i < count; ++i)
        {
            const auto& sl = sec[(size_t) i];
            loc[(size_t) i] = { sl.A, sl.B, sl.pA, sl.pB };
        }
        double c = cCur;
        const bool ramping = remaining > 0;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            double y = in;
            for (int k = 0; k < count; ++k)
            {
                const auto u = (size_t) k;
                auto& sl = sec[u];
                auto& l = loc[u];
                auto& s = sl.st[(size_t) ch];
                double yc = svfTick (l.A.cur, s[0], y);
                if (sl.two)
                    yc = svfTick (l.B.cur, s[1], yc);
                if (sl.pend)
                {
                    auto& ps = sl.pst[(size_t) ch];
                    double yn = svfTick (l.pA.cur, ps[0], y);
                    if (sl.pTwo)
                        yn = svfTick (l.pB.cur, ps[1], yn);
                    const int t = sl.pPos + i - sl.warmLen;
                    const double w = t <= 0 ? 0.0 : (t >= sl.fadeLen ? 1.0 : 0.5 - 0.5 * std::cos (kPi * t / sl.fadeLen));
                    yc += w * (yn - yc);
                }
                y = yc;
                if (ramping)
                {
                    l.A.step();
                    if (sl.two)
                        l.B.step();
                    if (sl.pend)
                    {
                        l.pA.step();
                        if (sl.pTwo)
                            l.pB.step();
                    }
                }
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
        {
            finishBlock (n);
            return;
        }
        for (int i = 0; i < count; ++i)
        {
            auto& sl = sec[(size_t) i];
            for (int k = 0; k < n; ++k)
            {
                sl.A.step();
                sl.B.step();
                sl.pA.step();
                sl.pB.step();
            }
        }
        for (int k = 0; k < n; ++k)
            cCur += cInc;
        remaining -= n;
        if (remaining <= 0)
        {
            for (int i = 0; i < count; ++i)
            {
                auto& sl = sec[(size_t) i];
                sl.A.jump (sl.A.tgt);
                sl.B.jump (sl.B.tgt);
                sl.pA.jump (sl.pA.tgt);
                sl.pB.jump (sl.pB.tgt);
            }
            cCur = cTgt;
            cInc = 0.0;
            remaining = 0;
        }
        finishBlock (n);
    }

    const ChainDesign& current() const noexcept { return design; }
    // the realisation the chain is heading for (exactly what it runs once a crossfade has ended)
    const Realisation& realisation (int i) const noexcept { return real[(size_t) i]; }
    bool settled() const noexcept
    {
        for (int i = 0; i < count; ++i)
            if (sec[(size_t) i].pend)
                return false;
        return true;
    }
    int size() const noexcept { return count; }

    // digital equivalent of the whole parallel form (for the correction designer)
    Biquad parallelBiquad() const noexcept
    {
        const auto& b = real[0].q[0]; // parallel designs are never split (buildBand)
        Biquad e = b;
        const double c = design.parallelC;
        e.b0 = 1.0 + c * b.b0;
        e.b1 = b.a1 + c * b.b1;
        e.b2 = b.a2 + c * b.b2;
        return e;
    }

private:
    void processSimple (float* x, int n, int ch, double satK, const float* fade) const noexcept
    {
        std::array<SvfRamp, kMaxSections> r;
        std::array<SvfState*, kMaxSections> s {};
        for (int i = 0; i < count; ++i)
        {
            r[(size_t) i] = sec[(size_t) i].A;
            s[(size_t) i] = &sec[(size_t) i].st[(size_t) ch][0];
        }
        double c = cCur;
        const bool ramping = remaining > 0;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            double y = in;
            for (int k = 0; k < count; ++k)
            {
                y = svfTick (r[(size_t) k].cur, *s[(size_t) k], y);
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

    struct Slot
    {
        SvfRamp A, B;              // playing realisation (B: second stage of a fitted pair)
        bool two = false;
        bool pend = false, pTwo = false;
        SvfRamp pA, pB;            // realisation fading in
        int pPos = 0, warmLen = 0, fadeLen = 1; // samples since the crossfade started, its phases
        std::array<std::array<SvfState, 2>, kMaxChannels> st {}, pst {};
    };

    bool curTwo (size_t u) const noexcept { return sec[u].pend ? sec[u].pTwo : sec[u].two; }

    static void holdRamps (Slot& sl, int steps) noexcept
    {
        sl.A.begin (sl.A.tgt, steps);
        sl.B.begin (sl.B.tgt, steps);
    }

    // n samples were processed: advance crossfades, hand over the finished ones
    void finishBlock (int n) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            auto& sl = sec[(size_t) i];
            if (! sl.pend)
                continue;
            sl.pPos += n;
            if (sl.pPos >= sl.warmLen + sl.fadeLen)
            {
                sl.A = sl.pA;
                sl.B = sl.pB;
                sl.two = sl.pTwo;
                sl.st = sl.pst;
                sl.pend = false;
            }
        }
    }

    void realiseSection (const ChainDesign& d, int i) noexcept
    {
        const auto u = (size_t) i;
        // identical sections (band-pass cascades) share one fit
        if (i > 0 && sameSection (d.sect[u], d.sect[u - 1]))
        {
            real[u] = real[u - 1];
            track[u] = track[u - 1];
            return;
        }
        real[u] = realise (d.sect[u], &track[u]);
    }

    static bool sameSection (const Section& a, const Section& b) noexcept
    {
        return same (a.n0, b.n0) && same (a.n1, b.n1) && same (a.n2, b.n2) && same (a.d0, b.d0) && same (a.d1, b.d1)
            && same (a.d2, b.d2) && a.kind == b.kind && a.invert == b.invert && same (a.w0, b.w0) && same (a.wTop, b.wTop);
    }

    void cBegin (double c, int steps) noexcept
    {
        cCur = cTgt;
        cTgt = c;
        cInc = (cTgt - cCur) / std::max (1, steps);
    }

    int count = 0;
    bool parallel = false;
    ChainDesign design;
    std::array<Realisation, kMaxSections> real {};
    std::array<FitTrack, kMaxSections> track {};
    mutable std::array<Slot, kMaxSections> sec {};
    double cCur = 0, cTgt = 0, cInc = 0;
    int remaining = 0;
};
} // namespace penta::dsp
