#include "MatchPanel.h"

using namespace ui;

MatchPanel::Spectrum& MatchPanel::sharedReference()
{
    static Spectrum s; // every instance in the host process sees the last reference captured anywhere
    return s;
}

MatchPanel::MatchPanel (PentaProcessor& p, EqGraph& g) : proc (p), graph (g)
{
    refIn.setTooltip ("Play the reference (a track you like) through THIS instance and capture it. 10-30 s of the "
                      "loudest section works best. Other instances in the session can use it too.");
    refSc.setTooltip ("Capture the side-chain input as the reference (route the reference track into the side-chain).");
    refShared.setTooltip ("Use the reference captured last in another instance of the EQ in this session.");
    trackBtn.setTooltip ("Play this track and capture its spectrum (same section length as the reference is ideal).");
    applyBtn.setTooltip ("Fit up to 12 bands to the match curve (replaces the current bands; Ctrl+Z undoes).");
    closeBtn.setTooltip ("Close (Esc).");
    for (auto* b : { &refIn, &refSc, &refShared, &trackBtn, &applyBtn, &closeBtn })
        addAndMakeVisible (b);
    applyBtn.setColour (juce::TextButton::buttonColourId, col::accent.withAlpha (0.85f));
    refIn.onClick = [this] { toggleCapture (1); };
    refSc.onClick = [this] { toggleCapture (2); };
    trackBtn.onClick = [this] { toggleCapture (3); };
    refShared.onClick = [this]
    {
        if (sharedReference().valid)
            ref = sharedReference();
        updateButtons();
        repaint();
    };
    applyBtn.onClick = [this] { if (apply() > 0 && onClose) onClose(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& caption, double lo, double hi, double step,
                         double def, const juce::String& suffix, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.5f));
        s.setColour (juce::Slider::textBoxTextColourId, col::text);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff22262d));
        s.setTooltip (tip);
        s.onValueChange = [this] { repaint(); };
        l.setText (caption, juce::dontSendNotification);
        l.setFont (caps (10.5f, 1));
        l.setColour (juce::Label::textColourId, col::dim);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setup (amount, amountL, "AMOUNT", 0.0, 100.0, 1.0, 70.0, " %", "How far to move towards the reference (70 % is a good start).");
    setup (smoothing, smoothingL, "SMOOTHING", 0.1, 1.0, 0.05, 0.33, " oct",
           "Width of the averaging: wider = broad tonal balance only, narrower = more detail.");
    setup (maxDb, maxL, "MAX", 3.0, 18.0, 1.0, 9.0, " dB", "Largest correction allowed at any frequency.");
    setWantsKeyboardFocus (true);
}

MatchPanel::~MatchPanel()
{
    if (capturing != 0)
        graph.stopCapture();
}

void MatchPanel::open()
{
    setVisible (true);
    updateButtons();
    startTimerHz (10);
    grabKeyboardFocus();
}

void MatchPanel::toggleCapture (int what)
{
    if (capturing == what)
    {
        finishCapture();
        return;
    }
    if (capturing != 0)
        finishCapture();
    capturing = what;
    graph.startCapture (what == 2 ? EqGraph::Capture::sidechain : EqGraph::Capture::input);
    updateButtons();
}

void MatchPanel::finishCapture()
{
    if (capturing == 0)
        return;
    Spectrum s;
    s.db = graph.capturedDb();
    s.seconds = graph.capturedSeconds();
    s.valid = s.seconds > 0.5;
    graph.stopCapture();
    if (capturing == 3)
    {
        s.origin = "this track";
        if (s.valid)
            track = s;
    }
    else
    {
        s.origin = capturing == 2 ? "side-chain" : "input of this instance";
        if (s.valid)
        {
            ref = s;
            sharedReference() = s;
        }
    }
    capturing = 0;
    updateButtons();
    repaint();
}

void MatchPanel::updateButtons()
{
    auto label = [this] (juce::TextButton& b, int what, const juce::String& idle)
    {
        b.setButtonText (capturing == what ? "STOP  (" + juce::String (graph.capturedSeconds(), 1) + " s)" : idle);
        b.setColour (juce::TextButton::buttonColourId, capturing == what ? col::clip.withAlpha (0.7f) : juce::Colour (0xff2a2e36));
    };
    label (refIn, 1, "REFERENCE FROM INPUT");
    label (refSc, 2, "REFERENCE FROM SIDE-CHAIN");
    label (trackBtn, 3, "CAPTURE THIS TRACK");
    refSc.setEnabled (proc.sidechainConnected() || capturing == 2);
    refShared.setEnabled (sharedReference().valid && capturing == 0);
    applyBtn.setEnabled (ref.valid && track.valid && capturing == 0);
}

void MatchPanel::timerCallback()
{
    if (! isVisible())
    {
        stopTimer();
        return;
    }
    updateButtons();
    repaint (plotArea);
    repaint (textArea);
}

std::vector<double> MatchPanel::matchHz() const
{
    std::vector<double> hz;
    for (double f : graph.gridHz())
        if (f >= 20.0 && f <= 20000.0)
            hz.push_back (f);
    return hz;
}

std::vector<double> MatchPanel::matchCurve() const
{
    const auto& grid = graph.gridHz();
    std::vector<double> diff, hz;
    std::vector<bool> ok;
    if (! ref.valid || ! track.valid || ref.db.size() != grid.size() || track.db.size() != grid.size())
        return {};
    for (size_t i = 0; i < grid.size(); ++i)
    {
        if (grid[i] < 20.0 || grid[i] > 20000.0)
            continue;
        hz.push_back (grid[i]);
        const bool valid = ref.db[i] > -110.0 && track.db[i] > -110.0; // silence carries no information
        ok.push_back (valid);
        diff.push_back (valid ? ref.db[i] - track.db[i] : 0.0);
    }
    // smoothing over +-half the width (octaves), valid points only
    const double half = 0.5 * smoothing.getValue();
    std::vector<double> sm (hz.size(), 0.0);
    for (size_t i = 0; i < hz.size(); ++i)
    {
        double s = 0.0, n = 0.0;
        for (size_t j = 0; j < hz.size(); ++j)
            if (ok[j] && std::abs (std::log2 (hz[j] / hz[i])) <= half)
            {
                s += diff[j];
                n += 1.0;
            }
        sm[i] = n > 0.0 ? s / n : 0.0;
    }
    // remove the level difference: mean over 100 Hz .. 10 kHz (tone, not loudness)
    double mean = 0.0, n = 0.0;
    for (size_t i = 0; i < hz.size(); ++i)
        if (hz[i] >= 100.0 && hz[i] <= 10000.0)
        {
            mean += sm[i];
            n += 1.0;
        }
    mean = n > 0.0 ? mean / n : 0.0;
    const double a = amount.getValue() * 0.01, lim = maxDb.getValue();
    for (auto& v : sm)
        v = juce::jlimit (-lim, lim, (v - mean) * a);
    return sm;
}

int MatchPanel::apply()
{
    const auto c = matchCurve();
    if (c.empty())
        return 0;
    return graph.fitBandsTo (matchHz(), c);
}

void MatchPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xf2171a1f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
    g.setColour (col::text);
    g.setFont (caps (15.0f, 2));
    g.drawText ("MATCH", getLocalBounds().reduced (16, 10).removeFromTop (22), Justification::centredLeft);

    // text: what was captured
    g.setFont (font (12.0f));
    auto t = textArea;
    auto line = [&] (const String& s, Colour c)
    {
        g.setColour (c);
        g.drawText (s, t.removeFromTop (18), Justification::centredLeft);
    };
    line ("Reference: " + (ref.valid ? String (ref.seconds, 1) + " s from the " + ref.origin : String ("not captured yet")),
          ref.valid ? col::teal : col::dim);
    line ("This track: " + (track.valid ? String (track.seconds, 1) + " s" : String ("not captured yet")),
          track.valid ? col::text : col::dim);
    line ("Play each source and press its button to start / stop capturing. Then APPLY.", col::faint);

    // preview: reference (teal), this track (grey), the correction (accent)
    const auto pa = plotArea.toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (pa, 6.0f);
    const auto& grid = graph.gridHz();
    auto xFor = [&] (double f) { return pa.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * pa.getWidth(); };
    g.setColour (col::grid);
    for (double f : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xFor (f), pa.getY(), pa.getBottom());
    g.drawHorizontalLine ((int) pa.getCentreY(), pa.getX(), pa.getRight());
    auto specPath = [&] (const Spectrum& s, Colour c)
    {
        if (! s.valid || s.db.size() != grid.size())
            return;
        double mx = -200.0;
        for (size_t i = 0; i < grid.size(); ++i)
            if (grid[i] >= 20.0 && grid[i] <= 20000.0)
                mx = std::max (mx, s.db[i]);
        Path p;
        bool started = false;
        for (size_t i = 0; i < grid.size(); ++i)
        {
            if (grid[i] < 20.0 || grid[i] > 20000.0)
                continue;
            const float y = pa.getY() + 6.0f + (float) jlimit (0.0, 1.0, (mx - s.db[i]) / 72.0) * (pa.getHeight() - 12.0f);
            if (! started)
                p.startNewSubPath (xFor (grid[i]), y);
            else
                p.lineTo (xFor (grid[i]), y);
            started = true;
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (1.2f));
    };
    specPath (track, col::dim.withAlpha (0.8f));
    specPath (ref, col::teal);
    const auto c = matchCurve();
    if (! c.empty())
    {
        const auto hz = matchHz();
        Path p;
        for (size_t i = 0; i < hz.size(); ++i)
        {
            const float y = pa.getCentreY() - (float) (c[i] / 12.0) * pa.getHeight() * 0.5f;
            if (i == 0)
                p.startNewSubPath (xFor (hz[i]), y);
            else
                p.lineTo (xFor (hz[i]), y);
        }
        g.setColour (col::accent);
        g.strokePath (p, PathStrokeType (2.2f));
    }
    g.setColour (col::faint);
    g.setFont (font (10.0f));
    g.drawText ("100", Rectangle<float> (xFor (100.0) + 3.0f, pa.getBottom() - 14.0f, 40.0f, 12.0f), Justification::centredLeft);
    g.drawText ("1k", Rectangle<float> (xFor (1000.0) + 3.0f, pa.getBottom() - 14.0f, 40.0f, 12.0f), Justification::centredLeft);
    g.drawText ("10k", Rectangle<float> (xFor (10000.0) + 3.0f, pa.getBottom() - 14.0f, 40.0f, 12.0f), Justification::centredLeft);
    g.setColour (col::accent);
    g.drawText ("correction (+-12 dB)", Rectangle<float> (pa.getRight() - 140.0f, pa.getY() + 4.0f, 134.0f, 12.0f), Justification::centredRight);
    g.setColour (col::teal);
    g.drawText ("reference", Rectangle<float> (pa.getX() + 6.0f, pa.getY() + 4.0f, 80.0f, 12.0f), Justification::centredLeft);
    g.setColour (col::dim);
    g.drawText ("this track", Rectangle<float> (pa.getX() + 6.0f, pa.getY() + 17.0f, 80.0f, 12.0f), Justification::centredLeft);
}

void MatchPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    r.removeFromTop (28);
    auto left = r.removeFromLeft (300);
    auto row = [&] (juce::Component& c) { c.setBounds (left.removeFromTop (28)); left.removeFromTop (6); };
    row (refIn);
    row (refSc);
    row (refShared);
    left.removeFromTop (8);
    row (trackBtn);
    left.removeFromTop (8);
    auto sl = [&] (juce::Label& l, juce::Slider& s)
    {
        auto a = left.removeFromTop (24);
        l.setBounds (a.removeFromLeft (90));
        s.setBounds (a);
        left.removeFromTop (6);
    };
    sl (amountL, amount);
    sl (smoothingL, smoothing);
    sl (maxL, maxDb);
    auto bottom = left.removeFromBottom (30);
    applyBtn.setBounds (bottom.removeFromLeft (140));
    bottom.removeFromLeft (8);
    closeBtn.setBounds (bottom);
    r.removeFromLeft (16);
    textArea = r.removeFromTop (56);
    r.removeFromTop (6);
    plotArea = r;
}

bool MatchPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}
