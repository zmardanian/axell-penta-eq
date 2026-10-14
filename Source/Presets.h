#pragma once
// Axell Penta EQ - factory presets (framework independent, verified by the test suites).
//
// Every preset is a set of overrides on top of the parameter defaults. Sources for each move are
// traced in docs/research/preset-sources.md (IDs refer to docs/research/engineering-practice.md).
// Names and descriptions use descriptive terms only (no trademarks, see docs/research/clean-room.md).
// Output trims keep the loudness on pink noise within +-0.05 LU of bypass (measured; DspTests section P gates +-0.1 LU),
// so switching presets compares tone, not level.

#include "Params.h"

#include <string>
#include <utility>
#include <vector>

namespace penta
{
struct FactoryPreset
{
    std::string name, category, description;
    std::vector<std::pair<std::string, float>> values;

    std::vector<float> resolve() const
    {
        ParamValues p;
        for (const auto& kv : values)
            p.set (kv.first, kv.second);
        return p.v;
    }
};

namespace preset_detail
{
// slope indices
enum { s6 = 0, s12, s18, s24, s36, s48, s72, s96 };

struct B
{
    FactoryPreset p;
    B (const char* name, const char* cat, const char* desc) { p.name = name; p.category = cat; p.description = desc; }
    B& set (const std::string& id, float v) { p.values.push_back ({ id, v }); return *this; }
    // band b (1..12)
    B& band (int b, int type, float f, float g, float q, int slope = s12, int mode = 0, int place = kStereo)
    {
        const int i = b - 1;
        set (bandId (i, bpUsed), 1);
        set (bandId (i, bpType), (float) type);
        set (bandId (i, bpFreq), f);
        set (bandId (i, bpGain), g);
        set (bandId (i, bpQ), q);
        set (bandId (i, bpSlope), (float) slope);
        set (bandId (i, bpMode), (float) mode);
        set (bandId (i, bpPlace), (float) place);
        return *this;
    }
    B& bell (int b, float f, float g, float q, int mode = 0, int place = kStereo) { return band (b, kBell, f, g, q, s12, mode, place); }
    B& dyn (int b, float thr, float range, float ratio = 4.0f, float atk = 5.0f, float rel = 120.0f, int src = 0)
    {
        const int i = b - 1;
        set (bandId (i, bpDyn), 1);
        set (bandId (i, bpThr), thr);
        set (bandId (i, bpRange), range);
        set (bandId (i, bpRatio), ratio);
        set (bandId (i, bpAtk), atk);
        set (bandId (i, bpRel), rel);
        set (bandId (i, bpSrc), (float) src);
        return *this;
    }
    // Dynamic Mode = Spectral (2.0): every frequency inside the band reacts on its own
    B& spectral (int b) { return set (bandId (b - 1, bpDynMode), 1); }
    B& hp (float f, int slope = s24, float q = 0.7071f) { return set ("hpOn", 1).set ("hpFreq", f).set ("hpSlope", (float) slope).set ("hpQ", q); }
    B& lp (float f, int slope = s12, float q = 0.7071f) { return set ("lpOn", 1).set ("lpFreq", f).set ("lpSlope", (float) slope).set ("lpQ", q); }
    B& out (float db) { return set ("outGain", db); }
};

constexpr int kExternal = kNumBands + 1;

inline std::vector<FactoryPreset> build()
{
    const char* V = "Vocals";
    const char* I = "Beats & Instruments";
    const char* M = "Mix Bus & Master";
    const char* A = "Axell Signature";
    std::vector<FactoryPreset> v;
    auto add = [&v] (B& b) { v.push_back (b.p); };

    //== Utility =================================================================
    { B b ("Init (Flat)", "Utility", "Everything flat, all character modules off. A clean starting point."); add (b); }

    //== Vocals ==================================================================
    { B b ("Rap Lead - Clean & Forward", V,
           "Modern rap lead: low cut at 80 Hz, a little body at 250 Hz, box cut at 400 Hz, broad presence at 3.5 kHz, "
           "a dynamic de-esser at 7.5 kHz and a soft air shelf.");
      b.hp (80, s24).bell (1, 250, 1.5f, 1.0f).bell (2, 400, -2.5f, 1.4f).bell (3, 3500, 2.0f, 0.9f)
       .bell (4, 7500, 0.0f, 2.5f).dyn (4, -30, -5, 4, 1, 80).band (5, kHighShelf, 12000, 2.0f, 0.7071f).out (-0.3f);
      add (b); }
    { B b ("Boom Bap Lead - Warm Grit", V,
           "Classic boom bap lead with the Inductor module pushing 3.2 kHz bite and 110 Hz weight; the parametric side "
           "cleans 500 Hz and de-esses dynamically. Drive adds transformer density.");
      b.hp (90, s18).bell (1, 500, -2.0f, 1.2f).bell (2, 6500, 0.0f, 2.5f).dyn (2, -30, -5, 4, 1, 80)
       .set ("indOn", 1).set ("indLowFreq", 2).set ("indLowGain", 2.0f).set ("indMidFreq", 3).set ("indMidGain", 3.0f)
       .set ("indHighGain", 1.5f).set ("drive", 45).out (-0.3f);
      add (b); }
    { B b ("Dark & Gritty Lead", V,
           "Darker, saturated lead for dusty beats: Character bell at 1.5 kHz, gentle top roll-off at 9 kHz, Console G "
           "mid push and heavy drive.");
      b.hp (100, s24).bell (1, 1500, 2.5f, 0.8f, 1).band (2, kLowShelf, 200, 1.5f, 0.7071f).lp (9000, s12)
       .set ("conOn", 1).set ("conMode", 1).set ("conHmfGain", 3.0f).set ("conHmfFreq", 2000).set ("conHmfQ", 1.0f)
       .set ("drive", 60).out (-1.3f);
      add (b); }
    { B b ("Doubles - Tuck Behind", V,
           "For double tracks: higher low cut, less low-mid and presence than the lead, deeper de-essing and a softer top "
           "so the double supports instead of competing.");
      b.hp (150, s24).band (1, kLowShelf, 250, -2.0f, 0.7071f).bell (2, 3000, -1.5f, 0.8f).bell (3, 7000, 0.0f, 2.0f)
       .dyn (3, -32, -7, 4, 1, 80).band (4, kHighShelf, 10000, -1.5f, 0.7071f).out (2.3f);
      add (b); }
    { B b ("Ad-libs - Hype Cut", V,
           "Ad-libs that cut through the beat: strong low cut, forward 1.2 kHz and 5 kHz, a little air and a 14 kHz "
           "low pass to keep them out of the cymbals.");
      b.hp (200, s24).bell (1, 1200, 3.0f, 1.0f).bell (2, 5000, 2.0f, 1.0f).band (3, kHighShelf, 10000, 1.0f, 0.7071f)
       .lp (14000, s12).out (-0.3f);
      add (b); }
    { B b ("Backing Stack - Wide & Soft", V,
           "Stereo backing-vocal bus: thinned lows, Mid dip at 3 kHz to make room for the lead, Side-only air shelf for "
           "width.");
      b.hp (180, s24).band (1, kLowShelf, 300, -3.0f, 0.7071f).bell (2, 3000, -2.0f, 0.7f, 0, kMid)
       .band (3, kHighShelf, 9000, 2.5f, 0.7071f, s12, 0, kSide).out (0.6f);
      add (b); }
    { B b ("Telephone / Lo-fi", V,
           "Band-limited phone/radio voice: 400 Hz - 3.4 kHz with two narrow resonances and console drive. Deliberately "
           "thinner and quieter in the lows (loudness not matched).");
      b.hp (400, s24).lp (3400, s24).bell (1, 1500, 6.0f, 2.0f).bell (2, 800, 3.0f, 4.0f)
       .set ("conOn", 1).set ("drive", 80).out (2.4f);
      add (b); }
    { B b ("De-ess + Air", V,
           "Two dynamic de-essing bands (6.5 kHz and 9 kHz) that only act on S and T sounds, plus the Air module at "
           "20 kHz for open highs without extra sibilance.");
      b.hp (70, s12).bell (1, 6500, 0.0f, 3.0f).dyn (1, -32, -6, 6, 0.5f, 60).bell (2, 9000, 0.0f, 2.0f).dyn (2, -34, -4, 4, 0.5f, 60)
       .set ("airOn", 1).set ("airFreq", 3).set ("airGain", 4.0f).out (0.2f);
      add (b); }
    { B b ("Podcast / Spoken Word", V,
           "Clear speech: low cut at 90 Hz, warmth at 120 Hz, less 300 Hz mud, intelligibility at 4 kHz and a gentle "
           "dynamic de-esser.");
      b.hp (90, s18).band (1, kLowShelf, 120, 1.0f, 0.7071f).bell (2, 300, -2.5f, 1.0f).bell (3, 4000, 2.0f, 1.0f)
       .bell (4, 7000, 0.0f, 2.0f).dyn (4, -30, -4, 4, 1, 100).out (0.5f);
      add (b); }
    { B b ("Chorus / Hook Lift", V,
           "Makes a hook bigger and brighter than the verses: Character presence bell, high shelf and the Passive Tube "
           "high boost (10 kHz, broad).");
      b.hp (100, s24).bell (1, 2000, 2.0f, 0.7f, 1).band (2, kHighShelf, 9000, 2.0f, 0.7071f).band (3, kLowShelf, 200, -1.0f, 0.7071f)
       .set ("ptOn", 1).set ("ptHighFreq", 4).set ("ptHighBoost", 3.0f).set ("ptHighBw", 7.0f).out (-4.0f);
      add (b); }
    { B b ("Harsh Mic Tamer", V,
           "For bright or cheap condensers: dynamic bands at 3 kHz and 5.5 kHz only pull back when the voice gets harsh, "
           "a small static dip at 8 kHz.");
      b.hp (80, s18).bell (1, 3000, 0.0f, 1.5f).dyn (1, -30, -5, 4, 3, 120).bell (2, 5500, 0.0f, 2.0f).dyn (2, -32, -4, 4, 2, 100)
       .bell (3, 8000, -1.5f, 2.0f).out (1.7f);
      add (b); }
    { B b ("Room Box Removal", V,
           "Small-room / closet booth fix: three narrow cuts at 280, 450 and 650 Hz where booth modes pile up, plus a "
           "little top to restore openness. EQ only reduces the steady colour, not the reverb tail.");
      b.hp (90, s18).bell (1, 280, -3.0f, 2.0f).bell (2, 450, -4.0f, 3.0f).bell (3, 650, -2.0f, 3.0f)
       .band (4, kHighShelf, 10000, 1.0f, 0.7071f).out (0.4f);
      add (b); }
    { B b ("Rap Lead - Passive Tube Polish", V,
           "Smooth, expensive-sounding lead: Passive Tube low end (100 Hz boost + a touch of atten), broad 10 kHz boost, "
           "20 kHz soft roll; parametric box cut and de-esser.");
      b.hp (80, s24).bell (1, 450, -2.0f, 1.4f).bell (2, 7000, 0.0f, 2.5f).dyn (2, -30, -5, 4, 1, 80)
       .set ("ptOn", 1).set ("ptLowFreq", 3).set ("ptLowBoost", 3.0f).set ("ptLowAtten", 2.0f).set ("ptHighFreq", 4)
       .set ("ptHighBoost", 4.0f).set ("ptHighBw", 8.0f).set ("ptAttenFreq", 2).set ("ptHighAtten", 2.0f).set ("drive", 35)
       .out (-2.5f);
      add (b); }
    { B b ("Vocal - Spectral Harshness Tamer", V,
           "Spectral dynamics on a bright or harsh lead: only the frequencies that jump out between 2 and 5 kHz (and "
           "the sharpest sibilants above 6 kHz) are pulled back, each on its own - the rest of the presence stays. "
           "Zero latency, for tracking too.");
      b.hp (80, s24).bell (1, 250, 1.0f, 1.0f).bell (2, 3200, 0.0f, 0.7f).dyn (2, -30, -6, 4, 2, 90).spectral (2)
       .band (3, kHighShelf, 6500, 0.0f, 0.7071f).dyn (3, -32, -7, 4, 1, 70).spectral (3).out (0.1f);
      add (b); }

    //== Beats & instruments =====================================================
    { B b ("Kick - Punch & Knock", I,
           "Kick weight at 60 Hz, box cut at 400 Hz, beater click at 3.5 kHz; sub-sonic and top clean-up.");
      b.hp (30, s24).bell (1, 60, 3.0f, 1.2f).bell (2, 400, -4.0f, 2.0f).bell (3, 3500, 3.0f, 1.5f).lp (12000, s12).out (0.1f);
      add (b); }
    { B b ("Snare - Crack & Body", I,
           "Snare body at 200 Hz, less honk at 900 Hz, crack at 5 kHz and snap on top.");
      b.hp (80, s18).bell (1, 200, 2.0f, 1.2f).bell (2, 900, -2.0f, 2.0f).bell (3, 5000, 3.0f, 1.0f)
       .band (4, kHighShelf, 10000, 2.0f, 0.7071f).out (-1.3f);
      add (b); }
    { B b ("Hats - Smooth Sizzle", I,
           "Hi-hats and cymbals: steep low cut at 300 Hz, dynamic 3 kHz harshness control, smooth sizzle shelf.");
      b.hp (300, s24).bell (1, 3000, 0.0f, 1.5f).dyn (1, -30, -4, 4, 2, 80).bell (2, 7000, -1.0f, 2.0f)
       .band (3, kHighShelf, 10000, 2.0f, 0.7071f).out (1.1f);
      add (b); }
    { B b ("808 - Clean Sub + Bite", I,
           "808 that translates to small speakers: sub-sonic cut, 50 Hz weight, saturated Character band at 120 Hz to "
           "bring out the harmonics, 300 Hz clean-up.");
      b.hp (25, s24).bell (1, 50, 2.0f, 1.4f).bell (2, 120, 2.5f, 1.0f, 1).bell (3, 300, -3.0f, 1.0f).lp (8000, s12)
       .set ("drive", 60).out (1.2f);
      add (b); }
    { B b ("Bass - Defined Low End", I,
           "Bass guitar or synth bass: 30 Hz cut, 80 Hz shelf, 250 Hz mud cut, 800 Hz growl for definition.");
      b.hp (30, s24).band (1, kLowShelf, 80, 2.0f, 0.7071f).bell (2, 250, -3.0f, 1.2f).bell (3, 800, 2.0f, 1.5f).lp (5000, s12)
       .out (2.1f);
      add (b); }
    { B b ("Dusty Sampler (12-bit era)", I,
           "The rolled-off, gritty top of late-80s 12-bit samplers: 11 kHz low pass, saturated mids through the Inductor "
           "module and a Character bell. Use on drum loops and chops.");
      b.hp (60, s12).lp (11000, s24).bell (1, 1800, 2.0f, 0.8f, 1)
       .set ("indOn", 1).set ("indLowFreq", 2).set ("indLowGain", 3.0f).set ("indMidFreq", 2).set ("indMidGain", 2.0f)
       .set ("drive", 70).out (-0.7f);
      add (b); }
    { B b ("Vinyl Loop Cleanup", I,
           "Sampled vinyl: steep rumble filter, mono low end (Side low cut at 150 Hz), dynamic control of the 9 kHz "
           "crackle zone, a little low-mid clean-up.");
      b.hp (35, s48).band (1, kLowCut, 150, 0.0f, 0.7071f, s12, 0, kSide).bell (2, 9000, 0.0f, 1.5f).dyn (2, -36, -4, 4, 0.5f, 60)
       .bell (3, 250, -1.5f, 1.0f).out (1.2f);
      add (b); }
    { B b ("Keys / Rhodes - Warm Bell", I,
           "Electric piano and keys: clean low end, less 250 Hz mud, tine/bell clarity at 2 kHz, Passive Tube low warmth.");
      b.hp (80, s12).bell (1, 250, -2.0f, 1.0f).bell (2, 2000, 2.0f, 1.0f).band (3, kHighShelf, 8000, 1.0f, 0.7071f)
       .set ("ptOn", 1).set ("ptLowFreq", 2).set ("ptLowBoost", 2.0f).set ("ptLowAtten", 1.0f).out (-0.3f);
      add (b); }
    { B b ("Guitar - Clean Up & Presence", I,
           "Guitar or sampled guitar: 90 Hz cut, 250 Hz mud cut, presence at 3 kHz, smooth top roll-off.");
      b.hp (90, s18).bell (1, 250, -2.0f, 1.0f).bell (2, 3000, 2.5f, 1.2f).lp (14000, s12).out (0.4f);
      add (b); }
    { B b ("Drum Bus - Boom Bap Thump", I,
           "Drum bus weight and knock: Passive Tube low-end trick at 60 Hz (bump + dip that clears mud), Console E mid "
           "cut and 3 kHz push with drive for punch.");
      b.hp (25, s24).set ("ptOn", 1).set ("ptLowFreq", 2).set ("ptLowBoost", 4.0f).set ("ptLowAtten", 2.0f)
       .set ("conOn", 1).set ("conMode", 0).set ("conLmfGain", -2.0f).set ("conLmfFreq", 400).set ("conHmfGain", 2.0f)
       .set ("conHmfFreq", 3000).set ("drive", 50).out (-0.2f);
      add (b); }
    { B b ("Beat Bus - Glue Polish", I,
           "Whole-beat polish: subtle low shelf, low-mid clean-up, open presence and air, gentle Console G drive.");
      b.hp (22, s24).band (1, kLowShelf, 60, 1.0f, 0.7071f).bell (2, 300, -1.0f, 0.8f).bell (3, 3000, 1.0f, 0.7f)
       .band (4, kHighShelf, 10000, 1.5f, 0.7071f).set ("conOn", 1).set ("conMode", 1).set ("drive", 40).out (-0.6f);
      add (b); }
    { B b ("Bass/Kick Carve (side-chain)", I,
           "Put on the 808 or bass and feed the KICK into the side-chain input (REAPER: channels 3/4). The 60 Hz band "
           "ducks by 6 dB and the 3.5 kHz band by 3 dB only while the kick hits.");
      b.hp (25, s24).bell (1, 60, 0.0f, 1.5f).dyn (1, -30, -6, 8, 1, 90, kExternal).bell (2, 3500, 0.0f, 1.5f)
       .dyn (2, -30, -3, 8, 1, 90, kExternal).out (0.0f);
      add (b); }
    { B b ("Sample Chop - Room for Vocals", I,
           "Put on the beat or sample and feed the VOCAL into the side-chain: the Mid 2.5 kHz region ducks while the "
           "rapper talks, plus a static 40 Hz clean-up.");
      b.hp (40, s24).bell (1, 2500, -1.0f, 0.8f, 0, kMid).dyn (1, -34, -4, 4, 10, 200, kExternal).out (0.2f);
      add (b); }

    //== Mix bus & master ========================================================
    { B b ("Master - Gentle Polish", M,
           "Natural Phase mastering polish: sub-sonic cut, +0.8 dB low shelf, -0.7 dB low-mids, a touch of presence and "
           "a 1 dB air shelf. Small, wide moves.");
      b.set ("phase", 1).hp (20, s24).band (1, kLowShelf, 60, 0.8f, 0.7071f).bell (2, 300, -0.7f, 0.8f).bell (3, 3000, 0.5f, 0.7f)
       .band (4, kHighShelf, 12000, 1.0f, 0.7071f).out (-0.3f);
      add (b); }
    { B b ("Master - Boom Bap Warmth", M,
           "Analog-style warmth for boom bap: Passive Tube low trick at 30 Hz and broad 12 kHz lift, light Console E drive.");
      b.set ("phase", 1).hp (20, s24).set ("ptOn", 1).set ("ptLowFreq", 1).set ("ptLowBoost", 3.0f).set ("ptLowAtten", 2.0f)
       .set ("ptHighFreq", 5).set ("ptHighBoost", 2.0f).set ("ptHighBw", 8.0f).set ("conOn", 1).set ("drive", 30).out (-1.4f);
      add (b); }
    { B b ("Master - Air & Clarity", M,
           "Linear Phase: Air module at 40 kHz (+3 dB, lifts only the top octave), slight presence, low-mid clean-up.");
      b.set ("phase", 2).set ("lpQuality", 1).bell (1, 250, -0.8f, 0.9f).bell (2, 4000, 0.7f, 0.6f)
       .set ("airOn", 1).set ("airFreq", 4).set ("airGain", 3.0f).out (-0.5f);
      add (b); }
    { B b ("Master - Low-End Control (dynamic)", M,
           "Dynamic low end: 60 Hz and 150 Hz bands only reduce boomy notes (max -3 / -2 dB), the rest stays untouched.");
      b.set ("phase", 1).hp (22, s24).bell (1, 60, 0.0f, 1.5f).dyn (1, -20, -3, 3, 15, 250).bell (2, 150, 0.0f, 1.0f)
       .dyn (2, -22, -2, 3, 15, 250).out (0.1f);
      add (b); }
    { B b ("Master - M/S Widen Highs", M,
           "Linear Phase M/S: Side air shelf at 8 kHz for width, Side low cut at 120 Hz (keeps the low end centred), "
           "tiny Mid low-mid dip.");
      b.set ("phase", 2).band (1, kHighShelf, 8000, 2.0f, 0.7071f, s12, 0, kSide).band (2, kLowCut, 120, 0.0f, 0.7071f, s12, 0, kSide)
       .bell (3, 300, -0.5f, 0.8f, 0, kMid).out (-0.1f);
      add (b); }
    { B b ("Master - Mono Bass", M,
           "Linear Phase: everything below ~120 Hz in mono (steep Side low cut), +0.5 dB Mid low shelf.");
      b.set ("phase", 2).band (1, kLowCut, 120, 0.0f, 0.7071f, s24, 0, kSide).band (2, kLowShelf, 80, 0.5f, 0.7071f, s12, 0, kMid).out (0.2f);
      add (b); }
    { B b ("Master - De-Harsh", M,
           "Dynamic 3 kHz and 6 kHz bands that soften harsh moments only, plus a very slight tilt down.");
      b.set ("phase", 1).bell (1, 3000, 0.0f, 1.0f).dyn (1, -24, -3, 3, 5, 150).bell (2, 6000, 0.0f, 1.5f).dyn (2, -28, -2, 3, 3, 120)
       .band (3, kTilt, 1500, -0.5f, 0.7071f, s6).out (0.8f);
      add (b); }
    { B b ("Master - Loudness Prep", M,
           "Before a limiter: steep 28 Hz cut (sub energy wastes headroom), mono lows, dynamic 3.5 kHz so limiting "
           "doesn't make it harsh, slight 150 Hz clean-up. Linear Phase, High resolution (steep low cut).");
      b.set ("phase", 2).set ("lpQuality", 2).hp (28, s48).band (1, kLowCut, 100, 0.0f, 0.7071f, s12, 0, kSide).bell (2, 150, -0.5f, 1.0f)
       .bell (3, 3500, 0.0f, 1.2f).dyn (3, -26, -2, 3, 5, 150).out (0.6f);
      add (b); }
    { B b ("Mix Bus - Console Glue", M,
           "Console G on the mix bus: gentle LF and HMF lift with proportional Q and drive for that glued, forward feel.");
      b.set ("conOn", 1).set ("conMode", 1).set ("conLfGain", 1.5f).set ("conLfFreq", 60).set ("conHmfGain", 1.0f)
       .set ("conHmfFreq", 3000).set ("conHfGain", 1.0f).set ("conHfFreq", 12000).set ("drive", 50).out (-0.6f);
      add (b); }
    { B b ("Mix Bus - Spectral Resonance Control", M,
           "Spectral dynamics on the mix bus: ringing low-mids (200-500 Hz) and piercing upper mids (1.5-5 kHz) are "
           "tamed only where and when they stick out; a touch of air on top. Transparent when nothing rings.");
      b.bell (1, 320, 0.0f, 0.8f).dyn (1, -28, -3, 3, 10, 200).spectral (1)
       .bell (2, 2800, 0.0f, 0.6f).dyn (2, -30, -4, 3, 5, 150).spectral (2)
       .band (3, kHighShelf, 12000, 1.0f, 0.7071f).out (0.0f);
      add (b); }

    //== Axell Signature (measured voice: docs/research/voice-profile.md) ========
    { B b ("Axell Signature - LCT 440 Pure", A,
           "Tuned to Axell's measured voice on the condenser: chest shelf (+3 dB 140 Hz), booth box cut (-4 dB 450 Hz), "
           "split presence (2.8 + 4.6 kHz), dynamic sibilance at 8.7 kHz, gentle 40 kHz air.");
      b.hp (65, s24).band (1, kLowShelf, 140, 3.0f, 0.7071f).bell (2, 450, -4.0f, 1.4f).bell (3, 2800, 1.5f, 0.9f)
       .bell (4, 4600, 1.5f, 1.0f).bell (5, 8700, 0.0f, 2.5f).dyn (5, -30, -7, 6, 0.5f, 70)
       .set ("airOn", 1).set ("airFreq", 4).set ("airGain", 1.5f).out (-0.4f);
      add (b); }
    { B b ("Axell Signature - SM7B", A,
           "Tuned to Axell's voice on the dynamic mic: less chest (proximity already adds it), more presence (+2.5 dB at "
           "2.8 and 4.6 kHz), softer de-essing, Air at 20 kHz to open the darker top.");
      b.hp (70, s24).band (1, kLowShelf, 140, 2.0f, 0.7071f).bell (2, 450, -3.0f, 1.4f).bell (3, 2800, 2.5f, 0.9f)
       .bell (4, 4600, 2.5f, 1.0f).bell (5, 8700, 0.0f, 2.5f).dyn (5, -32, -4.5f, 4, 0.5f, 70)
       .set ("airOn", 1).set ("airFreq", 3).set ("airGain", 3.5f).out (-1.7f);
      add (b); }
    { B b ("Axell Signature - Booth Fix", A,
           "For the foam-lined wardrobe booth with either mic: the 450 Hz box cut is DYNAMIC (only when the room builds "
           "up), chest shelf, split presence, dynamic sibilance and air.");
      b.hp (65, s24).band (1, kLowShelf, 140, 2.5f, 0.7071f).bell (2, 450, -1.5f, 1.4f).dyn (2, -34, -4, 4, 10, 150)
       .bell (3, 2800, 2.0f, 0.9f).bell (4, 4600, 2.0f, 1.0f).bell (5, 8700, 0.0f, 2.5f).dyn (5, -30, -6, 6, 0.5f, 70)
       .set ("airOn", 1).set ("airFreq", 3).set ("airGain", 2.5f).out (-1.1f);
      add (b); }
    { B b ("Axell Signature - Tracking (Light)", A,
           "Light, zero-latency version of the booth fix for monitoring while recording: chest, static box cut, one "
           "presence band, fast de-esser. No character, no latency.");
      b.hp (70, s18).band (1, kLowShelf, 140, 2.0f, 0.7071f).bell (2, 450, -3.0f, 1.4f).bell (3, 3600, 2.0f, 0.8f)
       .bell (4, 8700, 0.0f, 2.5f).dyn (4, -30, -5, 4, 0.5f, 60).out (0.0f);
      add (b); }
    return v;
}
} // namespace preset_detail

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> v = preset_detail::build();
    return v;
}

inline std::vector<std::string> presetCategories()
{
    return { "Vocals", "Beats & Instruments", "Mix Bus & Master", "Axell Signature", "Utility" };
}
} // namespace penta
