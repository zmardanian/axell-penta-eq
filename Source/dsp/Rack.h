#pragma once
// Axell Penta EQ - character modules (framework independent).
//
// Each module = analog pre-sections (its EQ curve) -> nonlinear stage (scaled by the global Drive;
// Drive 0 = exactly linear) -> analog post-sections. Behavioural models built from published
// specifications and measurements only (docs/research/reference-eqs.md). Order in the rack:
// Inductor -> Console E/G -> Passive Tube -> Air.

#include "../Params.h"
#include "Bands.h"
#include "Chain.h"

#include <algorithm>
#include <cmath>

namespace penta::dsp
{
enum ModuleId { modInductor = 0, modConsole, modPassiveTube, modAir, kNumModules };

// continuous (smoothable) module settings: frequencies in Hz, gains in dB, knobs 0..10
struct ModuleSettings
{
    bool on = false;
    float v[16] {};
};

namespace rack
{
inline double W (double hz, double fs) { return 2.0 * kPi * hz / fs; }

// Passive Tube: v = { lowFreq, lowBoost, lowAtten, highFreq, highBw, highBoost, attenFreq, highAtten }
inline void buildPassiveTube (const ModuleSettings& m, double fs, ChainDesign& pre, ChainDesign& post)
{
    pre.clear();
    post.clear();
    const double fl = m.v[0], fh = m.v[3], fa = m.v[6];
    // LF boost: resonant shelf, midpoint 2x the selected frequency, Q 0.8, 0..+13.5 dB
    // LF atten: gentle 1st-order shelf, midpoint 3.5x higher, 0..-17.5 dB.
    // Together: LF bump + dip at ~3.3-3.8x the selection (calibration in docs/DECISIONS.md D3)
    pre.add (proto::lowShelf2 (W (2.0 * fl, fs), 0.8, 13.5 * m.v[1] / 10.0));
    pre.add (proto::lowShelf1 (W (3.5 * fl, fs), -17.5 * m.v[2] / 10.0));
    // HF boost: constant-pole (passive LC) bell, bandwidth 0 (sharp, Q 2.2) .. 10 (broad, Q 0.45)
    const double bw = std::clamp ((double) m.v[4], 0.0, 10.0) / 10.0;
    const double qb = 2.2 * std::pow (0.45 / 2.2, bw);
    pre.add (proto::parallelBell (W (fh, fs), qb, 18.0 * m.v[5] / 10.0));
    // HF atten: 1st-order shelf at the selected frequency, 0..-16 dB
    pre.add (proto::highShelf1 (W (fa, fs), -16.0 * m.v[7] / 10.0));
    // transformers: 4 Hz high-pass (also removes the DC of the even harmonics), 80 kHz low-pass
    post.add (proto::highpass1 (W (4.0, fs)));
    post.add (proto::lowpass1 (W (80000.0, fs)));
}

// Inductor: v = { hpfHz (2 = off), lowFreq, lowGain, midFreq, midGain, highGain }
inline void buildInductor (const ModuleSettings& m, double fs, ChainDesign& pre, ChainDesign& post)
{
    pre.clear();
    post.clear();
    addButterworth (pre, true, W (m.v[0], fs), 3, 0.70710678118654752);
    // low shelf resonance depends on the switched position: lower positions resonate more
    const double fl = m.v[1];
    const double ql = std::clamp (0.9 - 0.3 * std::log2 (fl / 35.0) / std::log2 (220.0 / 35.0), 0.6, 0.9);
    pre.add (proto::lowShelf2 (W (fl, fs), ql, m.v[2]));
    // mid: Q rises with frequency (0.7 @ 360 Hz .. 1.45 @ 7.2 kHz) and mildly with gain
    const double fm = m.v[3];
    const double qm = (0.7 + 0.75 * std::clamp (std::log2 (fm / 360.0) / std::log2 (20.0), 0.0, 1.0))
                    * (0.8 + 0.02 * std::abs ((double) m.v[4]));
    pre.add (proto::bell (W (fm, fs), qm, m.v[4]));
    // high: very gradual 1st-order shelf at 12 kHz
    pre.add (proto::highShelf1 (W (12000.0, fs), m.v[5]));
    post.add (proto::highpass1 (W (4.0, fs)));
}

// Console: v = { mode(0 E / 1 G), hpf (<=15.5 off), lpf (>=21.9k off), lfGain, lfFreq, lfBell,
//                lmfGain, lmfFreq, lmfQ, hmfGain, hmfFreq, hmfQ, hfGain, hfFreq, hfBell }
inline void buildConsole (const ModuleSettings& m, double fs, ChainDesign& pre, ChainDesign& post)
{
    pre.clear();
    post.clear();
    const bool g = m.v[0] > 0.5f;
    // "off" positions are filters far outside the audio band, so switching them glides click-free
    const double hpf = m.v[1] <= 15.5f ? 2.0 : (double) m.v[1];
    const double lpf = m.v[2] >= 21900.0f ? 120000.0 : (double) m.v[2];
    addButterworth (pre, true, W (hpf, fs), 3, 0.70710678118654752);
    pre.add (proto::lowpass2 (W (lpf, fs), 0.70710678118654752));
    const double shelfQ = g ? 1.1 : 0.70710678118654752; // G shelves overshoot (pre-boost dip)
    auto bellQ = [g] (double q, double gain) { return g ? proportionalQ (q, gain) : q; };
    if (m.v[5] > 0.5f)
        pre.add (proto::bell (W (m.v[4], fs), bellQ (0.8, m.v[3]), m.v[3]));
    else
        pre.add (proto::lowShelf2 (W (m.v[4], fs), shelfQ, m.v[3]));
    pre.add (proto::bell (W (m.v[7], fs), bellQ (m.v[8], m.v[6]), m.v[6]));
    pre.add (proto::bell (W (m.v[10], fs), bellQ (m.v[11], m.v[9]), m.v[9]));
    if (m.v[14] > 0.5f)
        pre.add (proto::bell (W (m.v[13], fs), bellQ (0.8, m.v[12]), m.v[12]));
    else
        pre.add (proto::highShelf2 (W (m.v[13], fs), shelfQ, m.v[12]));
}

// Air: v = { freq (where the shelf approaches full gain), gain }
inline void buildAir (const ModuleSettings& m, double fs, ChainDesign& pre, ChainDesign& post)
{
    pre.clear();
    post.clear();
    pre.add (proto::highShelf1 (W (0.5 * m.v[0], fs), m.v[1]));
}

inline void build (int id, const ModuleSettings& m, double fs, ChainDesign& pre, ChainDesign& post)
{
    switch (id)
    {
        case modInductor:    buildInductor (m, fs, pre, post); break;
        case modConsole:     buildConsole (m, fs, pre, post); break;
        case modPassiveTube: buildPassiveTube (m, fs, pre, post); break;
        default:             buildAir (m, fs, pre, post); break;
    }
}

// read a module's settings from the plain parameter values
inline ModuleSettings read (int id, const std::vector<float>& p)
{
    const auto& x = idx();
    ModuleSettings m;
    auto ch = [&p] (int i) { return (int) std::lround (p[(size_t) i]); };
    switch (id)
    {
        case modInductor:
            m.on = p[(size_t) x.indOn] > 0.5f;
            m.v[0] = ch (x.indHpf) == 0 ? 2.0f : kIndHpfHz[std::clamp (ch (x.indHpf), 0, 4)];
            m.v[1] = kIndLowHz[std::clamp (ch (x.indLowFreq), 0, 3)];
            m.v[2] = p[(size_t) x.indLowGain];
            m.v[3] = kIndMidHz[std::clamp (ch (x.indMidFreq), 0, 5)];
            m.v[4] = p[(size_t) x.indMidGain];
            m.v[5] = p[(size_t) x.indHighGain];
            break;
        case modConsole:
            m.on = p[(size_t) x.conOn] > 0.5f;
            m.v[0] = (float) ch (x.conMode);
            m.v[1] = p[(size_t) x.conHpf];
            m.v[2] = p[(size_t) x.conLpf];
            m.v[3] = p[(size_t) x.conLfGain];
            m.v[4] = p[(size_t) x.conLfFreq];
            m.v[5] = p[(size_t) x.conLfBell];
            m.v[6] = p[(size_t) x.conLmfGain];
            m.v[7] = p[(size_t) x.conLmfFreq];
            m.v[8] = p[(size_t) x.conLmfQ];
            m.v[9] = p[(size_t) x.conHmfGain];
            m.v[10] = p[(size_t) x.conHmfFreq];
            m.v[11] = p[(size_t) x.conHmfQ];
            m.v[12] = p[(size_t) x.conHfGain];
            m.v[13] = p[(size_t) x.conHfFreq];
            m.v[14] = p[(size_t) x.conHfBell];
            break;
        case modPassiveTube:
            m.on = p[(size_t) x.ptOn] > 0.5f;
            m.v[0] = kPtLowHz[std::clamp (ch (x.ptLowFreq), 0, 3)];
            m.v[1] = p[(size_t) x.ptLowBoost];
            m.v[2] = p[(size_t) x.ptLowAtten];
            m.v[3] = kPtHighHz[std::clamp (ch (x.ptHighFreq), 0, 6)];
            m.v[4] = p[(size_t) x.ptHighBw];
            m.v[5] = p[(size_t) x.ptHighBoost];
            m.v[6] = kPtAttenHz[std::clamp (ch (x.ptAttenFreq), 0, 2)];
            m.v[7] = p[(size_t) x.ptHighAtten];
            break;
        default:
            m.on = p[(size_t) x.airOn] > 0.5f;
            m.v[0] = kAirHz[std::clamp (ch (x.airFreq), 0, 4)];
            m.v[1] = p[(size_t) x.airGain];
            break;
    }
    return m;
}

// which settings are frequencies (smoothed in the log domain) and which are discrete switches
inline bool isLogValue (int id, int i)
{
    switch (id)
    {
        case modInductor:    return i == 0 || i == 1 || i == 3;
        case modConsole:     return i == 1 || i == 2 || i == 4 || i == 7 || i == 8 || i == 10 || i == 11 || i == 13;
        case modPassiveTube: return i == 0 || i == 3 || i == 6;
        default:             return i == 0;
    }
}
inline bool isSwitch (int id, int i)
{
    return id == modConsole && (i == 0 || i == 5 || i == 14);
}

// nonlinear stages; k = drive amount 0..1
struct NonlinearState
{
    double lf[kMaxChannels] {}; // inductor LF split one-pole state
};

inline double processNonlinear (int id, double x, double drive, NonlinearState& st, int ch, double lfCoef) noexcept
{
    if (id == modInductor)
    {
        // transformer: core flux follows the low frequencies, so they saturate first.
        // The LF split runs even at Drive 0 so turning Drive up never starts from a stale state.
        double& z = st.lf[ch];
        const double v = (x - z) * lfCoef;
        const double lo = v + z;
        z = lo + v;
        if (drive <= 0.0)
            return x;
        const double k = 2.2 * drive;
        const double s = std::tanh (k * lo) / k;
        const double y = x - lo + s + 0.1 * k * s * s;
        return softSat (y, 0.4 * drive);
    }
    if (drive <= 0.0)
        return x;
    switch (id)
    {
        case modPassiveTube:
        {
            // push-pull tube makeup: odd (3rd) dominant, a little 2nd from imperfect balance
            const double k = 1.6 * drive;
            const double s = std::tanh (k * x) / k;
            return s + 0.08 * k * s * s;
        }
        case modConsole:
            return softSat (x, 0.9 * drive);
        default:
            return x;
    }
}
} // namespace rack
} // namespace penta::dsp
