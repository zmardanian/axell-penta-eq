#pragma once
// Axell Penta EQ - parameter table (framework independent).
//
// One table drives everything: the JUCE processor builds its APVTS parameters from it, the engine
// decodes a flat value array with it, presets are written as {id, value} overrides on top of the
// defaults, and the tests use it to fuzz every parameter. IDs and ORDER are frozen once released
// (saved projects / automation); new parameters may only be appended with a higher version.

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace penta
{
constexpr int kNumBands = 12;

enum class PType { Float, Bool, Choice };

struct ParamSpec
{
    std::string id, name;
    PType type = PType::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    float centre = 0.0f;      // value at the slider centre (0 = linear)
    float step = 0.0f;        // 0 = continuous
    std::vector<std::string> choices;
    std::string unit;         // "dB", "Hz", "ms", "%", "" ...
    int version = 1;
};

namespace choice
{
inline const std::vector<std::string> bandType { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                                 "Notch", "Band Pass", "Tilt" };
inline const std::vector<std::string> slope { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct",
                                              "48 dB/oct", "72 dB/oct", "96 dB/oct" };
inline const std::vector<std::string> mode { "Clean", "Character" };
inline const std::vector<std::string> place { "Stereo", "Left", "Right", "Mid", "Side" };
inline const std::vector<std::string> phase { "Zero Latency", "Natural Phase", "Linear Phase" };
inline const std::vector<std::string> quality { "Low", "Medium", "High" };
inline const std::vector<std::string> os { "1x", "2x", "4x" };
inline const std::vector<std::string> rackPos { "Post EQ", "Pre EQ" };
inline const std::vector<std::string> ptLow { "20 Hz", "30 Hz", "60 Hz", "100 Hz" };
inline const std::vector<std::string> ptHigh { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" };
inline const std::vector<std::string> ptAtten { "5 kHz", "10 kHz", "20 kHz" };
inline const std::vector<std::string> indHpf { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
inline const std::vector<std::string> indLow { "35 Hz", "60 Hz", "110 Hz", "220 Hz" };
inline const std::vector<std::string> indMid { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" };
inline const std::vector<std::string> conMode { "E", "G" };
inline const std::vector<std::string> airFreq { "2.5 kHz", "5 kHz", "10 kHz", "20 kHz", "40 kHz" };
inline const std::vector<std::string> dynMode { "Broadband", "Spectral" };

inline std::vector<std::string> dynSource()
{
    std::vector<std::string> v { "Self" };
    for (int b = 1; b <= kNumBands; ++b)
        v.push_back ("Band " + std::to_string (b));
    v.push_back ("External");
    return v;
}
} // namespace choice

// numeric values behind stepped choices
inline constexpr int kSlopeDb[] = { 6, 12, 18, 24, 36, 48, 72, 96 };
inline constexpr float kPtLowHz[] = { 20.0f, 30.0f, 60.0f, 100.0f };
inline constexpr float kPtHighHz[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
inline constexpr float kPtAttenHz[] = { 5000.0f, 10000.0f, 20000.0f };
inline constexpr float kIndHpfHz[] = { 0.0f, 50.0f, 80.0f, 160.0f, 300.0f };
inline constexpr float kIndLowHz[] = { 35.0f, 60.0f, 110.0f, 220.0f };
inline constexpr float kIndMidHz[] = { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
inline constexpr float kAirHz[] = { 2500.0f, 5000.0f, 10000.0f, 20000.0f, 40000.0f };

enum BandType { kBell = 0, kLowShelf, kHighShelf, kLowCut, kHighCut, kNotch, kBandPass, kTilt };
enum Placement { kStereo = 0, kLeft, kRight, kMid, kSide };
enum PhaseMode { kZeroLatency = 0, kNatural, kLinear };

// default (unused) band frequencies spread over the spectrum, so a new band appears sensibly
inline constexpr float kBandDefaultHz[kNumBands] = { 40.0f, 80.0f, 160.0f, 300.0f, 500.0f, 800.0f,
                                                    1200.0f, 2000.0f, 3500.0f, 6000.0f, 9000.0f, 14000.0f };

// The per-band parameter suffixes. bpUsed..bpSrc are the band block of the table (1.0); parameters added
// later (bpDynMode, 2.0) sit at the end of the table, so every earlier index and VST3 id stays unchanged.
enum BandParam { bpUsed = 0, bpBypass, bpType, bpFreq, bpGain, bpQ, bpSlope, bpMode, bpPlace,
                 bpDyn, bpThr, bpRange, bpRatio, bpAtk, bpRel, bpSrc, bpDynMode, kBandParamCount };

inline std::string bandId (int band, BandParam p)
{
    static const char* suffix[kBandParamCount] = { "used", "byp", "type", "freq", "gain", "q", "slope", "mode",
                                                   "place", "dyn", "thr", "range", "ratio", "atk", "rel", "src", "dmode" };
    return "b" + std::to_string (band + 1) + "_" + suffix[p];
}

//==============================================================================
namespace detail
{
inline ParamSpec fl (std::string id, std::string name, float mn, float mx, float def, std::string unit,
                     float centre = 0.0f, float step = 0.0f)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.type = PType::Float;
    p.min = mn;
    p.max = mx;
    p.def = def;
    p.unit = std::move (unit);
    p.centre = centre;
    p.step = step;
    return p;
}
inline ParamSpec bo (std::string id, std::string name, bool def)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.type = PType::Bool;
    p.min = 0.0f;
    p.max = 1.0f;
    p.def = def ? 1.0f : 0.0f;
    p.step = 1.0f;
    return p;
}
inline ParamSpec ch (std::string id, std::string name, std::vector<std::string> choices, int def)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.type = PType::Choice;
    p.min = 0.0f;
    p.max = (float) choices.size() - 1.0f;
    p.def = (float) def;
    p.step = 1.0f;
    p.choices = std::move (choices);
    return p;
}

inline std::vector<ParamSpec> buildTable()
{
    std::vector<ParamSpec> t;
    // global
    t.push_back (fl ("inGain", "Input Gain", -24.0f, 24.0f, 0.0f, "dB"));
    t.push_back (fl ("outGain", "Output Gain", -24.0f, 24.0f, 0.0f, "dB"));
    t.push_back (fl ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%"));
    t.push_back (fl ("drive", "Drive", 0.0f, 100.0f, 30.0f, "%"));
    t.push_back (bo ("autoGain", "Auto Gain", false));
    t.push_back (ch ("phase", "Phase Mode", choice::phase, kZeroLatency));
    t.push_back (ch ("lpQuality", "Linear Phase Quality", choice::quality, 1));
    t.push_back (ch ("os", "Oversampling", choice::os, 0));
    t.push_back (ch ("rackPos", "Character Position", choice::rackPos, 0));

    // dedicated cuts
    t.push_back (bo ("hpOn", "HP On", false));
    t.push_back (fl ("hpFreq", "HP Frequency", 10.0f, 2000.0f, 30.0f, "Hz", 100.0f));
    t.push_back (ch ("hpSlope", "HP Slope", choice::slope, 3));
    t.push_back (fl ("hpQ", "HP Resonance", 0.5f, 4.0f, 0.7071f, "", 1.0f));
    t.push_back (bo ("lpOn", "LP On", false));
    t.push_back (fl ("lpFreq", "LP Frequency", 1000.0f, 30000.0f, 18000.0f, "Hz", 8000.0f));
    t.push_back (ch ("lpSlope", "LP Slope", choice::slope, 1));
    t.push_back (fl ("lpQ", "LP Resonance", 0.5f, 4.0f, 0.7071f, "", 1.0f));

    // bands
    const auto src = choice::dynSource();
    for (int b = 0; b < kNumBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        t.push_back (bo (bandId (b, bpUsed), n + "Active", false));
        t.push_back (bo (bandId (b, bpBypass), n + "Bypass", false));
        t.push_back (ch (bandId (b, bpType), n + "Type", choice::bandType, kBell));
        t.push_back (fl (bandId (b, bpFreq), n + "Frequency", 10.0f, 30000.0f, kBandDefaultHz[b], "Hz", 1000.0f));
        t.push_back (fl (bandId (b, bpGain), n + "Gain", -30.0f, 30.0f, 0.0f, "dB"));
        t.push_back (fl (bandId (b, bpQ), n + "Q", 0.1f, 24.0f, 1.0f, "", 1.0f));
        t.push_back (ch (bandId (b, bpSlope), n + "Slope", choice::slope, 3));
        t.push_back (ch (bandId (b, bpMode), n + "Mode", choice::mode, 0));
        t.push_back (ch (bandId (b, bpPlace), n + "Placement", choice::place, kStereo));
        t.push_back (bo (bandId (b, bpDyn), n + "Dynamic", false));
        t.push_back (fl (bandId (b, bpThr), n + "Threshold", -60.0f, 0.0f, -24.0f, "dB"));
        t.push_back (fl (bandId (b, bpRange), n + "Range", -24.0f, 24.0f, -6.0f, "dB"));
        t.push_back (fl (bandId (b, bpRatio), n + "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f));
        t.push_back (fl (bandId (b, bpAtk), n + "Attack", 0.1f, 200.0f, 5.0f, "ms", 10.0f));
        t.push_back (fl (bandId (b, bpRel), n + "Release", 5.0f, 2000.0f, 120.0f, "ms", 150.0f));
        t.push_back (ch (bandId (b, bpSrc), n + "Detector", src, 0));
    }

    // character rack: Passive Tube
    t.push_back (bo ("ptOn", "Passive Tube On", false));
    t.push_back (ch ("ptLowFreq", "Passive Tube Low Frequency", choice::ptLow, 2));
    t.push_back (fl ("ptLowBoost", "Passive Tube Low Boost", 0.0f, 10.0f, 0.0f, "", 0.0f, 0.0f));
    t.push_back (fl ("ptLowAtten", "Passive Tube Low Atten", 0.0f, 10.0f, 0.0f, ""));
    t.push_back (ch ("ptHighFreq", "Passive Tube High Frequency", choice::ptHigh, 4));
    t.push_back (fl ("ptHighBw", "Passive Tube High Bandwidth", 0.0f, 10.0f, 5.0f, ""));
    t.push_back (fl ("ptHighBoost", "Passive Tube High Boost", 0.0f, 10.0f, 0.0f, ""));
    t.push_back (ch ("ptAttenFreq", "Passive Tube Atten Select", choice::ptAtten, 1));
    t.push_back (fl ("ptHighAtten", "Passive Tube High Atten", 0.0f, 10.0f, 0.0f, ""));
    // Inductor
    t.push_back (bo ("indOn", "Inductor On", false));
    t.push_back (ch ("indHpf", "Inductor HPF", choice::indHpf, 0));
    t.push_back (ch ("indLowFreq", "Inductor Low Frequency", choice::indLow, 2));
    t.push_back (fl ("indLowGain", "Inductor Low Gain", -16.0f, 16.0f, 0.0f, "dB"));
    t.push_back (ch ("indMidFreq", "Inductor Mid Frequency", choice::indMid, 3));
    t.push_back (fl ("indMidGain", "Inductor Mid Gain", -18.0f, 18.0f, 0.0f, "dB"));
    t.push_back (fl ("indHighGain", "Inductor High Gain", -16.0f, 16.0f, 0.0f, "dB"));
    // Console E/G
    t.push_back (bo ("conOn", "Console On", false));
    t.push_back (ch ("conMode", "Console Mode", choice::conMode, 0));
    t.push_back (fl ("conHpf", "Console HPF", 15.0f, 350.0f, 15.0f, "Hz", 60.0f));
    t.push_back (fl ("conLpf", "Console LPF", 3000.0f, 22000.0f, 22000.0f, "Hz", 10000.0f));
    t.push_back (fl ("conLfGain", "Console LF Gain", -15.0f, 15.0f, 0.0f, "dB"));
    t.push_back (fl ("conLfFreq", "Console LF Frequency", 30.0f, 450.0f, 100.0f, "Hz", 100.0f));
    t.push_back (bo ("conLfBell", "Console LF Bell", false));
    t.push_back (fl ("conLmfGain", "Console LMF Gain", -15.0f, 15.0f, 0.0f, "dB"));
    t.push_back (fl ("conLmfFreq", "Console LMF Frequency", 200.0f, 2500.0f, 600.0f, "Hz", 700.0f));
    t.push_back (fl ("conLmfQ", "Console LMF Q", 0.5f, 2.5f, 1.0f, "", 1.0f));
    t.push_back (fl ("conHmfGain", "Console HMF Gain", -15.0f, 15.0f, 0.0f, "dB"));
    t.push_back (fl ("conHmfFreq", "Console HMF Frequency", 600.0f, 7000.0f, 2500.0f, "Hz", 2000.0f));
    t.push_back (fl ("conHmfQ", "Console HMF Q", 0.5f, 2.5f, 1.0f, "", 1.0f));
    t.push_back (fl ("conHfGain", "Console HF Gain", -15.0f, 15.0f, 0.0f, "dB"));
    t.push_back (fl ("conHfFreq", "Console HF Frequency", 1500.0f, 16000.0f, 8000.0f, "Hz", 5000.0f));
    t.push_back (bo ("conHfBell", "Console HF Bell", false));
    // Air
    t.push_back (bo ("airOn", "Air On", false));
    t.push_back (ch ("airFreq", "Air Frequency", choice::airFreq, 3));
    t.push_back (fl ("airGain", "Air Gain", 0.0f, 20.0f, 0.0f, "dB"));

    // 2.0 (appended: earlier sessions, presets and automation keep their parameters)
    for (int b = 0; b < kNumBands; ++b)
    {
        t.push_back (ch (bandId (b, bpDynMode), "Band " + std::to_string (b + 1) + " Dynamic Mode", choice::dynMode, 0));
        t.back().version = 2;
    }
    t.push_back (fl ("gainScale", "Gain Scale", 0.0f, 200.0f, 100.0f, "%"));
    t.back().version = 2;
    return t;
}
} // namespace detail

inline const std::vector<ParamSpec>& paramTable()
{
    static const std::vector<ParamSpec> t = detail::buildTable();
    return t;
}

inline int numParams() { return (int) paramTable().size(); }

inline int paramIndex (const std::string& id)
{
    const auto& t = paramTable();
    for (int i = 0; i < (int) t.size(); ++i)
        if (t[(size_t) i].id == id)
            return i;
    return -1;
}

// Indices resolved once (by id, so the table order can't silently break the decoder)
struct ParamIdx
{
    int inGain, outGain, mix, drive, autoGain, phase, lpQuality, os, rackPos;
    int hpOn, hpFreq, hpSlope, hpQ, lpOn, lpFreq, lpSlope, lpQ;
    int band[kNumBands][kBandParamCount];
    int ptOn, ptLowFreq, ptLowBoost, ptLowAtten, ptHighFreq, ptHighBw, ptHighBoost, ptAttenFreq, ptHighAtten;
    int indOn, indHpf, indLowFreq, indLowGain, indMidFreq, indMidGain, indHighGain;
    int conOn, conMode, conHpf, conLpf, conLfGain, conLfFreq, conLfBell, conLmfGain, conLmfFreq, conLmfQ,
        conHmfGain, conHmfFreq, conHmfQ, conHfGain, conHfFreq, conHfBell;
    int airOn, airFreq, airGain;
    int gainScale;
};

inline const ParamIdx& idx()
{
    static const ParamIdx p = []
    {
        ParamIdx r {};
        auto i = [] (const char* id) { return paramIndex (id); };
        r.inGain = i ("inGain"); r.outGain = i ("outGain"); r.mix = i ("mix"); r.drive = i ("drive");
        r.autoGain = i ("autoGain"); r.phase = i ("phase"); r.lpQuality = i ("lpQuality"); r.os = i ("os");
        r.rackPos = i ("rackPos");
        r.hpOn = i ("hpOn"); r.hpFreq = i ("hpFreq"); r.hpSlope = i ("hpSlope"); r.hpQ = i ("hpQ");
        r.lpOn = i ("lpOn"); r.lpFreq = i ("lpFreq"); r.lpSlope = i ("lpSlope"); r.lpQ = i ("lpQ");
        for (int b = 0; b < kNumBands; ++b)
            for (int k = 0; k < kBandParamCount; ++k)
                r.band[b][k] = paramIndex (bandId (b, (BandParam) k));
        r.ptOn = i ("ptOn"); r.ptLowFreq = i ("ptLowFreq"); r.ptLowBoost = i ("ptLowBoost");
        r.ptLowAtten = i ("ptLowAtten"); r.ptHighFreq = i ("ptHighFreq"); r.ptHighBw = i ("ptHighBw");
        r.ptHighBoost = i ("ptHighBoost"); r.ptAttenFreq = i ("ptAttenFreq"); r.ptHighAtten = i ("ptHighAtten");
        r.indOn = i ("indOn"); r.indHpf = i ("indHpf"); r.indLowFreq = i ("indLowFreq"); r.indLowGain = i ("indLowGain");
        r.indMidFreq = i ("indMidFreq"); r.indMidGain = i ("indMidGain"); r.indHighGain = i ("indHighGain");
        r.conOn = i ("conOn"); r.conMode = i ("conMode"); r.conHpf = i ("conHpf"); r.conLpf = i ("conLpf");
        r.conLfGain = i ("conLfGain"); r.conLfFreq = i ("conLfFreq"); r.conLfBell = i ("conLfBell");
        r.conLmfGain = i ("conLmfGain"); r.conLmfFreq = i ("conLmfFreq"); r.conLmfQ = i ("conLmfQ");
        r.conHmfGain = i ("conHmfGain"); r.conHmfFreq = i ("conHmfFreq"); r.conHmfQ = i ("conHmfQ");
        r.conHfGain = i ("conHfGain"); r.conHfFreq = i ("conHfFreq"); r.conHfBell = i ("conHfBell");
        r.airOn = i ("airOn"); r.airFreq = i ("airFreq"); r.airGain = i ("airGain");
        r.gainScale = i ("gainScale");
        return r;
    }();
    return p;
}

// A full parameter state: plain values (not normalised), in table order.
struct ParamValues
{
    std::vector<float> v;
    ParamValues()
    {
        const auto& t = paramTable();
        v.resize (t.size());
        for (size_t i = 0; i < t.size(); ++i)
            v[i] = t[i].def;
    }
    float operator[] (int i) const { return v[(size_t) i]; }
    float& operator[] (int i) { return v[(size_t) i]; }
    bool set (const std::string& id, float value)
    {
        const int i = paramIndex (id);
        if (i < 0)
            return false;
        v[(size_t) i] = value;
        return true;
    }
    float get (const std::string& id) const
    {
        const int i = paramIndex (id);
        return i < 0 ? 0.0f : v[(size_t) i];
    }
};

// value <-> text helpers shared by the processor and the GUI
inline std::string formatValue (const ParamSpec& s, float v)
{
    char b[64];
    if (s.type == PType::Bool)
        return v > 0.5f ? "On" : "Off";
    if (s.type == PType::Choice)
    {
        const int i = (int) std::lround (v);
        return (i >= 0 && i < (int) s.choices.size()) ? s.choices[(size_t) i] : std::string();
    }
    if (s.unit == "Hz")
    {
        if (s.id == "conHpf" && v < 15.5f)
            return "Off";
        if (s.id == "conLpf" && v > 21900.0f)
            return "Off";
        if (v >= 10000.0f)
            std::snprintf (b, sizeof b, "%.1f kHz", v / 1000.0f);
        else if (v >= 1000.0f)
            std::snprintf (b, sizeof b, "%.2f kHz", v / 1000.0f);
        else
            std::snprintf (b, sizeof b, "%.1f Hz", v);
        return b;
    }
    if (s.unit == "dB")
        std::snprintf (b, sizeof b, "%+.1f dB", v);
    else if (s.unit == "%")
        std::snprintf (b, sizeof b, "%.0f %%", v);
    else if (s.unit == "ms")
        std::snprintf (b, sizeof b, v < 10.0f ? "%.1f ms" : "%.0f ms", v);
    else if (s.unit == ":1")
        std::snprintf (b, sizeof b, "%.1f:1", v);
    else
        std::snprintf (b, sizeof b, "%.2f", v);
    return b;
}
} // namespace penta
