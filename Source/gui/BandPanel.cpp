#include "BandPanel.h"

using namespace ui;

namespace tip
{
const char* type = "Band shape. BELL boosts/cuts around a frequency; SHELVES lift/lower everything below or above; "
                   "LOW/HIGH CUT remove lows/highs; NOTCH removes a narrow frequency; BAND PASS keeps only a region; "
                   "TILT tips the whole spectrum around the frequency.";
const char* slope = "Steepness of cuts and band-pass (6-96 dB/oct). Shelves and tilt: 6 dB/oct = gentle 1st order, "
                    "12 dB/oct and up = 2nd order with Q.";
const char* mode = "CLEAN: exact analog-matched curve (no cramping near 20 kHz), boost and cut are mirror images. "
                   "CHARACTER: analog-console behaviour - the bandwidth narrows as you boost more (proportional Q), cuts are "
                   "tighter than boosts, and boosts saturate gently with DRIVE.";
const char* place = "Where the band works: ST = both channels, L or R only, M = the centre (Mid), S = the sides (Side). "
                    "Mid/Side is great on beats and masters; on a mono vocal use ST.";
const char* freq = "Band frequency. Drag the dot in the graph or type a value (e.g. 3.2k).";
const char* gain = "Boost or cut in dB. Double-click to reset.";
const char* q = "Bandwidth: low Q = wide and musical, high Q = narrow and surgical. Mouse wheel on the dot also works.";
const char* dyn = "Makes this band DYNAMIC: it only moves when the signal in its region (or the detector source) gets "
                  "louder than THRESHOLD. Perfect for de-essing, harshness, mud and 808 boom.";
const char* dmode = "BROAD: the whole band moves when its region gets loud. SPECTRAL: every frequency inside the band "
                    "reacts on its own - only what is too loud gets cut (a resonance, a whistle, a harsh partial, a sharp "
                    "sibilant), the rest of the region stays untouched. Zero latency in every phase mode.";
const char* thr = "Level (dBFS) where the band starts to move. BROAD: the level in the detector's region. SPECTRAL: per "
                  "frequency - a steady tone at this level just starts it. Loud music reads about -45 to -25 dBFS per "
                  "frequency (noise lower), so -30 dB catches ringing peaks while the rest passes.";
const char* range = "How far the band can move: negative = cuts when loud (de-ess, tame), positive = boosts when loud.";
const char* ratio = "How fast the band reaches its range above the threshold (like a compressor ratio).";
const char* atk = "How fast the band reacts (ms). 0.5-2 ms for sibilance, 10-30 ms to keep transients.";
const char* rel = "How fast the band returns (ms). 50-150 ms for de-essing, 150-400 ms for low end.";
const char* src = "What the detector listens to: SELF = this band's region; BAND n = another band's region (e.g. cut "
                  "3 kHz when 9 kHz gets loud); EXTERNAL = the side-chain input (e.g. the kick to carve the 808); "
                  "ESS = this band's region, but it moves only on esses (S, SH, Z, CH: Axell Velvet's sibilance "
                  "detector) - a de-esser band that never dulls the rest of the voice.";
const char* solo = "Hold to hear only this band's region (makes it easy to find a problem frequency).";
const char* bypass = "Bypass this band (it stays in place, greyed out).";
const char* remove = "Remove this band.";
const char* chip = "Select a band. Grey = unused: click to switch it on as a flat bell.";
const char* cutOn = "Switch the dedicated cut filter on/off.";
} // namespace tip

BandPanel::BandPanel (PentaProcessor& p, std::function<void (int)> onSelect) : proc (p), onSelectFn (std::move (onSelect))
{
    for (int s = 0; s < penta::kNumSlots; ++s)
    {
        const juce::String name = s == 0 ? "HP" : (s == penta::kNumSlots - 1 ? "LP" : juce::String (s));
        auto* b = chips.add (new juce::TextButton (name));
        b->setTooltip (tip::chip);
        b->setColour (juce::TextButton::buttonOnColourId, col::band (s == 0 ? 12 : (s == penta::kNumSlots - 1 ? 13 : s - 1)));
        b->onClick = [this, s]
        {
            const bool cut = s == 0 || s == penta::kNumSlots - 1;
            const std::string used = cut ? (s == 0 ? "hpOn" : "lpOn") : penta::bandId (s - 1, penta::bpUsed);
            if (! cut && getParam (used) < 0.5f)
            {
                proc.pushUndo();
                setParam (penta::bandId (s - 1, penta::bpGain), 0.0f);
                setParam (penta::bandId (s - 1, penta::bpBypass), 0.0f);
                setParam (used, 1.0f);
            }
            if (onSelectFn)
                onSelectFn (s);
        };
        addAndMakeVisible (b);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &type, &slope, &source, &mode, &place, &freq, &gain, &q, &dyn,
                                                            &dmode, &thr, &range, &ratio, &atk, &rel, &cutOn, &solo, &bypass,
                                                            &remove })
        addAndMakeVisible (c);
    solo.setTooltip (tip::solo);
    bypass.setTooltip (tip::bypass);
    remove.setTooltip (tip::remove);
    bypass.setClickingTogglesState (false);
    bypass.setColour (juce::TextButton::buttonOnColourId, col::faint.brighter (0.4f));
    solo.setColour (juce::TextButton::buttonOnColourId, col::warn);
    solo.addMouseListener (this, false);
    solo.onStateChange = [this]
    {
        const bool down = solo.isDown();
        if (down != soloing && slot >= 1 && slot <= penta::kNumBands)
        {
            soloing = down;
            proc.engine().setSoloBand (down ? slot - 1 : -1);
            solo.setToggleState (down, juce::dontSendNotification);
        }
    };
    bypass.onClick = [this]
    {
        if (slot < 1 || slot > penta::kNumBands)
            return;
        proc.pushUndo();
        const auto id = penta::bandId (slot - 1, penta::bpBypass);
        setParam (id, getParam (id) > 0.5f ? 0.0f : 1.0f);
    };
    remove.onClick = [this]
    {
        if (slot < 0)
            return;
        proc.pushUndo();
        if (slot == 0 || slot == penta::kNumSlots - 1)
            setParam (slot == 0 ? "hpOn" : "lpOn", 0.0f);
        else
            setParam (penta::bandId (slot - 1, penta::bpUsed), 0.0f);
        if (onSelectFn)
            onSelectFn (-1);
    };
    rebind();
    startTimerHz (15);
}

BandPanel::~BandPanel()
{
    if (typeParam != nullptr)
        typeParam->removeListener (this);
    proc.engine().setSoloBand (-1);
}

void BandPanel::parameterValueChanged (int, float)
{
    if (! typeGesture.load())
        return;
    juce::Component::SafePointer<BandPanel> safe (this);
    const int band = typeBand;
    juce::MessageManager::callAsync ([safe, band]
    {
        if (safe != nullptr)
            safe->proc.applyNeutralQ (band);
    });
}

float BandPanel::getParam (const std::string& id) const
{
    const int i = penta::paramIndex (id);
    if (i < 0)
        return 0.0f;
    return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue());
}

void BandPanel::setParam (const std::string& id, float v)
{
    const int i = penta::paramIndex (id);
    if (i < 0)
        return;
    auto* p = proc.param (i);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    p->endChangeGesture();
}

void BandPanel::setSlot (int s)
{
    if (s == slot)
        return;
    if (soloing)
    {
        proc.engine().setSoloBand (-1);
        soloing = false;
    }
    slot = s;
    rebind();
    repaint();
}

void BandPanel::rebind()
{
    auto& a = proc.apvts;
    auto g = [this] { proc.pushUndo(); };
    for (auto* k : { &freq, &gain, &q, &thr, &range, &ratio, &atk, &rel })
        k->unbind();
    type.unbind();
    if (typeParam != nullptr)
        typeParam->removeListener (this);
    typeParam = nullptr;
    typeBand = -1;
    typeGesture = false;
    slope.unbind();
    source.unbind();
    mode.unbind();
    place.unbind();
    dyn.unbind();
    dmode.unbind();
    cutOn.unbind();
    const bool cut = slot == 0 || slot == penta::kNumSlots - 1;
    const auto c = col::band (slot <= 0 ? 12 : (slot == penta::kNumSlots - 1 ? 13 : slot - 1));
    for (auto* k : { &freq, &gain, &q })
        k->setAccent (c);
    mode.setAccent (c);
    if (slot < 0)
    {
        updateEnablement();
        return;
    }
    if (cut)
    {
        const std::string pre = slot == 0 ? "hp" : "lp";
        freq.bind (a, pre + "Freq", tip::freq, g);
        q.bind (a, pre + "Q", "Resonance at the corner (0.71 = no peak). Applies to slopes of 12 dB/oct and up.", g);
        q.setCaption ("RESO");
        slope.bind (a, pre + "Slope", tip::slope, g);
        cutOn.bind (a, pre + "On", tip::cutOn, g);
    }
    else
    {
        const int b = slot - 1;
        using namespace penta;
        freq.bind (a, bandId (b, bpFreq), tip::freq, g);
        gain.bind (a, bandId (b, bpGain), tip::gain, g);
        q.bind (a, bandId (b, bpQ), tip::q, g);
        q.setCaption ("Q");
        type.bind (a, bandId (b, bpType), tip::type, g);
        typeParam = a.getParameter (bandId (b, bpType));
        typeBand = b;
        typeParam->addListener (this);
        slope.bind (a, bandId (b, bpSlope), tip::slope, g);
        source.bind (a, bandId (b, bpSrc), tip::src, g);
        mode.bind (*a.getParameter (bandId (b, bpMode)), tip::mode, g);
        place.bind (*a.getParameter (bandId (b, bpPlace)), tip::place, g);
        dyn.bind (a, bandId (b, bpDyn), tip::dyn, g);
        dmode.bind (*a.getParameter (bandId (b, bpDynMode)), tip::dmode, g);
        thr.bind (a, bandId (b, bpThr), tip::thr, g);
        range.bind (a, bandId (b, bpRange), tip::range, g);
        ratio.bind (a, bandId (b, bpRatio), tip::ratio, g);
        atk.bind (a, bandId (b, bpAtk), tip::atk, g);
        rel.bind (a, bandId (b, bpRel), tip::rel, g);
    }
    updateEnablement();
    resized();
}

void BandPanel::updateEnablement()
{
    const bool none = slot < 0;
    const bool cut = slot == 0 || slot == penta::kNumSlots - 1;
    const bool band = ! none && ! cut;
    int t = 0;
    bool gainType = false, isDyn = false, shelfOrCut = false;
    if (band)
    {
        t = (int) std::lround (getParam (penta::bandId (slot - 1, penta::bpType)));
        gainType = penta::dsp::isGainType (t);
        isDyn = getParam (penta::bandId (slot - 1, penta::bpDyn)) > 0.5f;
        shelfOrCut = t != penta::kBell && t != penta::kNotch;
    }
    for (auto* c : std::initializer_list<juce::Component*> { &type, &mode, &place, &gain, &dyn, &dmode, &thr, &range, &ratio,
                                                            &atk, &rel, &source, &solo, &bypass })
        c->setVisible (band);
    cutOn.setVisible (cut);
    freq.setVisible (! none);
    q.setVisible (! none);
    slope.setVisible (! none);
    remove.setVisible (! none);
    gain.setEnabled (gainType);
    slope.setEnabled (cut || shelfOrCut);
    mode.setEnabled (band && (t == penta::kBell || t == penta::kLowShelf || t == penta::kHighShelf));
    dyn.setEnabled (gainType);
    for (auto* c : std::initializer_list<juce::Component*> { &thr, &range, &ratio, &atk, &rel, &source, &dmode })
        c->setEnabled (gainType && isDyn);
    if (band)
        bypass.setToggleState (getParam (penta::bandId (slot - 1, penta::bpBypass)) > 0.5f, juce::dontSendNotification);
}

void BandPanel::timerCallback()
{
    for (int s = 0; s < penta::kNumSlots; ++s)
    {
        const bool cut = s == 0 || s == penta::kNumSlots - 1;
        const std::string used = cut ? (s == 0 ? "hpOn" : "lpOn") : penta::bandId (s - 1, penta::bpUsed);
        const bool on = getParam (used) > 0.5f;
        auto* chip = chips[s];
        chip->setToggleState (on, juce::dontSendNotification);
        chip->setAlpha (s == slot ? 1.0f : 0.8f);
    }
    updateEnablement();
    repaint (titleArea);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto top = r.removeFromTop (24);
    for (auto* c : chips)
    {
        c->setBounds (top.removeFromLeft (30).reduced (1, 0));
        top.removeFromLeft (2);
    }
    top.removeFromLeft (10);
    remove.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    bypass.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    solo.setBounds (top.removeFromRight (64));
    titleArea = top;
    r.removeFromTop (6);

    // main: type/slope | mode/place | freq gain q || dynamic
    mainArea = r.removeFromLeft (r.getWidth() * 56 / 100);
    dynArea = r.withTrimmedLeft (10);
    auto m = mainArea;
    auto col1 = m.removeFromLeft (150);
    const int rowH = 26;
    auto c1 = col1.withSizeKeepingCentre (150, rowH * 2 + 10);
    if (slot == 0 || slot == penta::kNumSlots - 1)
    {
        cutOn.setBounds (c1.removeFromTop (rowH));
        c1.removeFromTop (10);
        slope.setBounds (c1.removeFromTop (rowH));
    }
    else
    {
        type.setBounds (c1.removeFromTop (rowH));
        c1.removeFromTop (10);
        slope.setBounds (c1.removeFromTop (rowH));
    }
    m.removeFromLeft (12);
    auto col2 = m.removeFromLeft (190);
    auto c2 = col2.withSizeKeepingCentre (190, rowH * 2 + 10);
    mode.setBounds (c2.removeFromTop (rowH));
    c2.removeFromTop (10);
    place.setBounds (c2.removeFromTop (rowH));
    m.removeFromLeft (8);
    const int kw = m.getWidth() / 3;
    freq.setBounds (m.removeFromLeft (kw));
    gain.setBounds (m.removeFromLeft (kw));
    q.setBounds (m);

    auto d = dynArea;
    auto dcol = d.removeFromLeft (140);
    const int dRow = 24;
    auto dc = dcol.withSizeKeepingCentre (140, dRow * 3 + 10);
    dyn.setBounds (dc.removeFromTop (dRow));
    dc.removeFromTop (5);
    dmode.setBounds (dc.removeFromTop (dRow));
    dc.removeFromTop (5);
    source.setBounds (dc.removeFromTop (dRow));
    d.removeFromLeft (6);
    const int dw = d.getWidth() / 5;
    for (auto* k : { &thr, &range, &ratio, &atk, &rel })
        k->setBounds (d.removeFromLeft (dw));
}

void BandPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (col::panelTop, 0.0f, r.getY(), col::panel, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    const auto c = col::band (slot <= 0 ? 12 : (slot == penta::kNumSlots - 1 ? 13 : slot - 1));
    g.setColour ((slot < 0 ? col::track : c).withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
    g.setFont (caps (12.0f, 2));
    String title;
    if (slot < 0)
        title = "NO BAND SELECTED - CLICK THE GRAPH TO ADD ONE";
    else if (slot == 0)
        title = "HIGH-PASS (LOW CUT)";
    else if (slot == penta::kNumSlots - 1)
        title = "LOW-PASS (HIGH CUT)";
    else
        title = "BAND " + String (slot);
    g.setColour (slot < 0 ? col::dim : col::text);
    g.drawText (title, titleArea, Justification::centredLeft);
    if (slot >= 1 && slot <= penta::kNumBands)
    {
        g.setColour (col::track);
        g.drawVerticalLine (dynArea.getX() - 5, (float) dynArea.getY() + 4.0f, (float) dynArea.getBottom() - 4.0f);
    }
}
