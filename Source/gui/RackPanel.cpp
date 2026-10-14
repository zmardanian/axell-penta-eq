#include "RackPanel.h"

using namespace ui;

namespace tip
{
const char* ind = "INDUCTOR: a class-A, inductor-based channel EQ model - fixed musical frequencies, resonant low shelf, "
                  "gentle 12 kHz shelf, 18 dB/oct high-pass and transformer saturation (lows saturate first; DRIVE sets it).";
const char* con = "CONSOLE E/G: four-band channel EQ of a large-format console. E = constant bandwidth, punchy. "
                  "G = proportional Q (wider at small gains, narrower at big ones) and shelves with a little overshoot. "
                  "DRIVE adds console clipping.";
const char* pt = "PASSIVE TUBE: passive LC program EQ with a tube makeup stage. Boost AND atten the same low frequency for "
                 "the classic trick: more bottom with a dip just above that clears the mud. Broad high boost with BANDWIDTH, "
                 "separate high atten shelf. DRIVE sets the tube colour.";
const char* air = "AIR: an ultra-wide, boost-only high shelf (2.5-40 kHz). At 20 or 40 kHz it lifts only the very top "
                  "octave - openness without extra sibilance.";
const char* drive = "Analog colour of the character stages (and of Character-mode bands). 0 % = completely clean.";
const char* mix = "Dry/wet blend of the whole EQ (latency-aligned, no phasing).";
const char* out = "Final output level.";
const char* in = "Input level into the EQ and the character stages (drive them harder or softer).";
const char* ag = "Automatic level match: keeps the loudness the same while you EQ, so you judge tone, not volume. "
                 "Static (based on the curve), so it never pumps.";
const char* pos = "Run the character rack after (POST) or before (PRE) the parametric bands.";
} // namespace tip

RackPanel::RackPanel (PentaProcessor& p) : proc (p)
{
    auto& a = proc.apvts;
    auto g = [this] { proc.pushUndo(); };
    // Inductor
    knob (ind, "HPF", "indHpf", col::inductor, "Inductor high-pass: off / 50 / 80 / 160 / 300 Hz, 18 dB/oct.");
    knob (ind, "LOW FREQ", "indLowFreq", col::inductor, "Low shelf frequency: 35 / 60 / 110 / 220 Hz (lower positions resonate more).");
    knob (ind, "LOW", "indLowGain", col::inductor, "Low shelf gain (+-16 dB).");
    knob (ind, "MID FREQ", "indMidFreq", col::inductor, "Mid frequency: 360 Hz - 7.2 kHz. The bell gets narrower at higher frequencies.");
    knob (ind, "MID", "indMidGain", col::inductor, "Mid gain (+-18 dB).");
    knob (ind, "HIGH 12k", "indHighGain", col::inductor, "Very gentle 12 kHz shelf (+-16 dB).");
    // Console
    knob (con, "HPF", "conHpf", col::console, "Console high-pass, 18 dB/oct (fully left = off).");
    knob (con, "LPF", "conLpf", col::console, "Console low-pass, 12 dB/oct (fully right = off).");
    knob (con, "LF", "conLfGain", col::console, "Low band gain (+-15 dB).");
    knob (con, "LF FREQ", "conLfFreq", col::console, "Low band frequency (30-450 Hz).");
    knob (con, "HF", "conHfGain", col::console, "High band gain (+-15 dB).");
    knob (con, "HF FREQ", "conHfFreq", col::console, "High band frequency (1.5-16 kHz).");
    // (row 1: HPF LPF LF LF-FREQ HF HF-FREQ, row 2: LMF x3, HMF x3)
    knob (con, "LMF", "conLmfGain", col::console, "Low-mid bell gain (+-15 dB).");
    knob (con, "LMF FREQ", "conLmfFreq", col::console, "Low-mid frequency (200 Hz - 2.5 kHz).");
    knob (con, "LMF Q", "conLmfQ", col::console, "Low-mid bandwidth.");
    knob (con, "HMF", "conHmfGain", col::console, "High-mid bell gain (+-15 dB).");
    knob (con, "HMF FREQ", "conHmfFreq", col::console, "High-mid frequency (600 Hz - 7 kHz).");
    knob (con, "HMF Q", "conHmfQ", col::console, "High-mid bandwidth.");
    // Passive Tube
    knob (pt, "LOW FREQ", "ptLowFreq", col::tube, "Low frequency: 20 / 30 / 60 / 100 Hz (both BOOST and ATTEN).");
    knob (pt, "BOOST", "ptLowBoost", col::tube, "Low boost 0-10 (up to +13.5 dB). Use together with ATTEN for the low-end trick.");
    knob (pt, "ATTEN", "ptLowAtten", col::tube, "Low attenuation 0-10 (up to -17.5 dB). Its curve reaches higher than the boost: "
                                                 "with BOOST it carves a dip above the low-end bump.");
    knob (pt, "HIGH FREQ", "ptHighFreq", col::tube, "High boost frequency: 3-16 kHz.");
    knob (pt, "WIDTH", "ptHighBw", col::tube, "High boost width: 0 = sharp, 10 = broad and silky.");
    knob (pt, "BOOST", "ptHighBoost", col::tube, "High boost 0-10 (up to +18 dB).");
    knob (pt, "ATT SEL", "ptAttenFreq", col::tube, "High attenuation shelf: 5 / 10 / 20 kHz.");
    knob (pt, "ATTEN", "ptHighAtten", col::tube, "High attenuation 0-10 (up to -16 dB).");
    // Air
    knob (air, "FREQ", "airFreq", col::air, "Where the Air shelf reaches full lift: 2.5 / 5 / 10 / 20 / 40 kHz.", 44);
    knob (air, "AIR", "airGain", col::air, "Air gain (0 to +20 dB).", 44);
    // global
    knob (glob, "DRIVE", "drive", col::accent, tip::drive, 44);
    knob (glob, "MIX", "mix", col::global, tip::mix, 44);
    knob (glob, "INPUT", "inGain", col::global, tip::in, 44);
    knob (glob, "OUTPUT", "outGain", col::global, tip::out, 44);

    indOn.bind (a, "indOn", tip::ind, g);
    conOn.bind (a, "conOn", tip::con, g);
    ptOn.bind (a, "ptOn", tip::pt, g);
    airOn.bind (a, "airOn", tip::air, g);
    conMode.bind (*a.getParameter ("conMode"), "E: constant bandwidth, punchy. G: proportional Q, shelves with overshoot.", g);
    conLfBell.bind (a, "conLfBell", "Switch the low band from shelf to bell.", g);
    conHfBell.bind (a, "conHfBell", "Switch the high band from shelf to bell.", g);
    autoGain.bind (a, "autoGain", tip::ag, g);
    rackPos.bind (*a.getParameter ("rackPos"), tip::pos, g);
    for (auto* c : std::initializer_list<juce::Component*> { &indOn, &conOn, &ptOn, &airOn, &conMode, &conLfBell, &conHfBell,
                                                            &autoGain, &rackPos })
        addAndMakeVisible (c);
    startTimerHz (10);
}

ui::Knob& RackPanel::knob (std::vector<K>& list, const juce::String& caption, const juce::String& id, juce::Colour c,
                           const juce::String& tooltip, int d)
{
    K k;
    k.knob = std::make_unique<ui::Knob> (caption, c, d);
    k.id = id;
    k.knob->bind (proc.apvts, id, tooltip, [this] { proc.pushUndo(); });
    addAndMakeVisible (*k.knob);
    list.push_back (std::move (k));
    return *list.back().knob;
}

void RackPanel::layoutRow (std::vector<K>& list, int from, int to, juce::Rectangle<int> area)
{
    const int n = to - from;
    if (n <= 0)
        return;
    const int w = area.getWidth() / n;
    for (int i = from; i < to; ++i)
        list[(size_t) i].knob->setBounds (area.removeFromLeft (w));
}

void RackPanel::resized()
{
    auto r = getLocalBounds();
    const int gap = 8;
    globArea = r.removeFromRight (196);
    r.removeFromRight (gap);
    const int total = r.getWidth() - 3 * gap;
    indArea = r.removeFromLeft (total * 22 / 100);
    r.removeFromLeft (gap);
    conArea = r.removeFromLeft (total * 39 / 100);
    r.removeFromLeft (gap);
    airArea = r.removeFromRight (total * 11 / 100);
    r.removeFromRight (gap);
    ptArea = r;

    auto body = [] (juce::Rectangle<int> a) { return a.reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (8); };
    auto power = [] (juce::Component& c, juce::Rectangle<int> a) { c.setBounds (a.getX() + 8, a.getY() + 4, 140, 18); };

    power (indOn, indArea);
    {
        auto b = body (indArea);
        auto top = b.removeFromTop (b.getHeight() / 2);
        layoutRow (ind, 0, 3, top);
        layoutRow (ind, 3, 6, b);
    }
    power (conOn, conArea);
    conMode.setBounds (conArea.getRight() - 64, conArea.getY() + 4, 56, 18);
    conHfBell.setBounds (conArea.getRight() - 64 - 8 - 76, conArea.getY() + 4, 76, 18);
    conLfBell.setBounds (conArea.getRight() - 64 - 8 - 76 - 6 - 76, conArea.getY() + 4, 76, 18);
    {
        auto b = body (conArea);
        auto top = b.removeFromTop (b.getHeight() / 2);
        layoutRow (con, 0, 6, top);
        layoutRow (con, 6, 12, b);
    }
    power (ptOn, ptArea);
    {
        auto b = body (ptArea);
        auto top = b.removeFromTop (b.getHeight() / 2);
        layoutRow (pt, 0, 3, top.removeFromLeft (top.getWidth() * 3 / 5));
        layoutRow (pt, 3, 8, b);
    }
    power (airOn, airArea);
    {
        auto b = body (airArea);
        layoutRow (air, 0, 1, b.removeFromTop (b.getHeight() / 2));
        layoutRow (air, 1, 2, b);
    }
    {
        auto b = globArea.reduced (8, 6);
        auto top = b.removeFromTop (22);
        autoGain.setBounds (top.removeFromLeft (98));
        top.removeFromLeft (6);
        rackPos.setBounds (top);
        b.removeFromTop (4);
        auto row1 = b.removeFromTop (b.getHeight() / 2);
        layoutRow (glob, 0, 2, row1);
        layoutRow (glob, 2, 4, b);
    }
}

void RackPanel::timerCallback()
{
    const float ag = proc.meters.autoGainDb.load();
    if (std::abs (ag - agDb) > 0.05f)
    {
        agDb = ag;
        repaint (globArea);
    }
    for (auto* c : { &indOn, &conOn, &ptOn, &airOn })
        (void) c;
    auto alpha = [] (std::vector<K>& l, bool on)
    {
        for (auto& k : l)
            k.knob->setAlpha (on ? 1.0f : 0.45f);
    };
    alpha (ind, indOn.getToggleState());
    alpha (con, conOn.getToggleState());
    alpha (pt, ptOn.getToggleState());
    alpha (air, airOn.getToggleState());
    repaint();
}

void RackPanel::paint (juce::Graphics& g)
{
    drawModulePanel (g, indArea.toFloat(), col::inductor, "", "CLASS-A INDUCTOR EQ", indOn.getToggleState());
    drawModulePanel (g, conArea.toFloat(), col::console, "", "", conOn.getToggleState());
    drawModulePanel (g, ptArea.toFloat(), col::tube, "", "PASSIVE LC + TUBE", ptOn.getToggleState());
    drawModulePanel (g, airArea.toFloat(), col::air, "", "", airOn.getToggleState());
    // global strip
    const auto r = globArea.toFloat();
    g.setGradientFill (juce::ColourGradient (col::panelTop, 0.0f, r.getY(), col::panel, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::global.withAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
    if (autoGain.getToggleState())
    {
        g.setColour (col::dim);
        g.setFont (font (10.5f, 1));
        g.drawText ("auto " + juce::String (agDb >= 0 ? "+" : "") + juce::String (agDb, 1) + " dB",
                    globArea.reduced (10, 4).removeFromBottom (14), juce::Justification::centredRight);
    }
}
