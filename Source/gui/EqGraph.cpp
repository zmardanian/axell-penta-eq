#include "EqGraph.h"
#include "../dsp/CurveFit.h"

using namespace ui;

namespace
{
constexpr double kMinHz = 10.0, kMaxHz = 30000.0;
constexpr double kSpecTop = 6.0, kSpecBottom = -96.0;

const char* kTypeShort[] = { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH", "BAND PASS", "TILT" };
const char* kPlaceShort[] = { "", "L", "R", "M", "S" };

juce::String hzText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}

// the band clipboard: shared by every instance in the host process (copy in one, paste in another)
const penta::BandParam kClipParams[] = { penta::bpType, penta::bpFreq, penta::bpGain, penta::bpQ, penta::bpSlope, penta::bpMode,
                                         penta::bpPlace, penta::bpDyn, penta::bpThr, penta::bpRange, penta::bpRatio,
                                         penta::bpAtk, penta::bpRel, penta::bpSrc, penta::bpDynMode };
constexpr int kClipCount = (int) (sizeof (kClipParams) / sizeof (kClipParams[0]));
struct Clip
{
    bool valid = false;
    std::array<float, kClipCount> v {};
};
Clip& clipboard()
{
    static Clip c;
    return c;
}
} // namespace

//==============================================================================
void EqGraph::Analyzer::prepare (double sampleRate)
{
    fs = sampleRate;
    order = sampleRate > 100000.0 ? 14 : (sampleRate > 50000.0 ? 13 : 12);
    size = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    ring.assign ((size_t) size, 0.0f);
    work.assign ((size_t) (2 * size), 0.0f);
    window.resize ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (size - 1));
    binDb.assign ((size_t) (size / 2 + 1), -200.0f);
    ringPos = filled = sinceFrame = 0;
}

void EqGraph::Analyzer::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) ringPos] = x[i];
        ringPos = (ringPos + 1) % size;
    }
    filled = std::min (size, filled + n);
    sinceFrame += n;
}

bool EqGraph::Analyzer::compute()
{
    if (filled < size || sinceFrame < size / 4)
        return false;
    sinceFrame = 0;
    for (int i = 0; i < size; ++i)
        work[(size_t) i] = ring[(size_t) ((ringPos + i) % size)] * window[(size_t) i];
    std::fill (work.begin() + size, work.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (work.data(), true);
    // Hann window: coherent gain 0.5 -> a full-scale sine reads 0 dBFS
    const float norm = 4.0f / (float) size;
    for (int k = 0; k <= size / 2; ++k)
        binDb[(size_t) k] = juce::Decibels::gainToDecibels (work[(size_t) k] * norm, -200.0f);
    return true;
}

//==============================================================================
EqGraph::EqGraph (PentaProcessor& p, std::function<void (int)> onSelect) : proc (p), onSelectFn (std::move (onSelect))
{
    values = proc.readValues();
    freqs.resize (kPoints);
    for (int i = 0; i < kPoints; ++i)
        freqs[(size_t) i] = kMinHz * std::pow (kMaxHz / kMinHz, i / (double) (kPoints - 1));
    for (auto& s : slotDb)
        s.assign (kPoints, 0.0f);
    for (auto& s : slotDynDb)
        s.assign (kPoints, 0.0f);
    totalDb.assign (kPoints, 0.0f);
    totalStaticDb.assign (kPoints, 0.0f);
    moduleDb.assign (kPoints, 0.0f);
    dispPre.assign (kPoints, -200.0f);
    dispPost.assign (kPoints, -200.0f);
    dispSc.assign (kPoints, -200.0f);
    dispPeak.assign (kPoints, -200.0f);
    drawDb.assign (kPoints, std::numeric_limits<float>::quiet_NaN());
    capSum.assign (kPoints, 0.0);
    fifoTmp.resize (16384);
    setTooltip ("Click empty space to add a band; drag on empty space to select several. Drag a dot to move it "
                "(a selection moves together; Shift = fine, Ctrl = Q). Wheel = Q. Double-click = gain to 0. "
                "Alt+click = remove (on empty space: a new band on the spectrum peak). Arrows nudge, Del removes, "
                "Ctrl+C/V copy/paste a band, I inverts. Right-click for more.");
    setWantsKeyboardFocus (true);
    for (auto* b : { &drawApply, &drawClear, &drawCancel })
    {
        addChildComponent (b);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xf0262a32));
    }
    drawApply.setColour (juce::TextButton::buttonColourId, col::accent.withAlpha (0.85f));
    drawApply.setTooltip ("Fit up to 12 bands to the curve you drew (replaces the current bands; Ctrl+Z undoes).");
    drawClear.setTooltip ("Erase the drawing.");
    drawCancel.setTooltip ("Leave draw mode without changing anything (Esc).");
    drawApply.onClick = [this] { applyDrawing(); };
    drawClear.onClick = [this] { clearDrawing(); };
    drawCancel.onClick = [this] { setDrawMode (false); };
    proc.engine().setAnalyzerEnabled (true);
}

EqGraph::~EqGraph()
{
    proc.engine().setAnalyzerEnabled (false);
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().reduced (2.0f).withTrimmedLeft (30.0f).withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
    auto bar = plot.toNearestInt().withTrimmedTop (36).removeFromTop (24).withSizeKeepingCentre (250, 24);
    drawApply.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (6);
    drawClear.setBounds (bar.removeFromLeft (74));
    bar.removeFromLeft (6);
    drawCancel.setBounds (bar);
}

float EqGraph::xForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / 20.0) / std::log (20000.0 / 20.0);
    return plot.getX() + (float) t * plot.getWidth();
}

double EqGraph::freqForX (float x) const
{
    const double t = (x - plot.getX()) / plot.getWidth();
    return juce::jlimit (kMinHz, kMaxHz, 20.0 * std::pow (1000.0, t));
}

float EqGraph::yForDb (double db) const
{
    const double r = options.rangeDb;
    return plot.getCentreY() - (float) (db / r) * plot.getHeight() * 0.5f;
}

double EqGraph::dbForY (float y) const
{
    return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * options.rangeDb;
}

float EqGraph::yForSpec (double db) const
{
    const double t = (juce::jlimit (kSpecBottom, kSpecTop, db) - kSpecBottom) / (kSpecTop - kSpecBottom);
    return plot.getBottom() - (float) t * plot.getHeight();
}

juce::String EqGraph::noteName (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (! (hz > 0.0))
        return {};
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int n = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - n) * 100.0);
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    if (cents != 0)
        s << (cents > 0 ? " +" : " ") << cents << "ct";
    return s;
}

double EqGraph::snapped (double hz) const
{
    if (! options.snapNotes)
        return hz;
    return 440.0 * std::exp2 (std::round (12.0 * std::log2 (hz / 440.0)) / 12.0);
}

std::string EqGraph::slotParam (int slot, const char* what) const
{
    const std::string w (what);
    if (slot == 0 || slot == penta::kNumSlots - 1)
    {
        const std::string pre = slot == 0 ? "hp" : "lp";
        if (w == "freq") return pre + "Freq";
        if (w == "q") return pre + "Q";
        if (w == "slope") return pre + "Slope";
        if (w == "used") return pre + "On";
        return {};
    }
    return "b" + std::to_string (slot) + "_" + w;
}

float EqGraph::getParam (const std::string& id) const
{
    const int i = penta::paramIndex (id);
    return i < 0 ? 0.0f : values[(size_t) i];
}

void EqGraph::setParam (const std::string& id, float plainValue)
{
    const int i = penta::paramIndex (id);
    if (i < 0)
        return;
    auto* p = proc.param (i);
    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    values[(size_t) i] = p->convertFrom0to1 (p->getValue());
}

bool EqGraph::slotActive (int slot) const
{
    return slot >= 0 && slot < penta::kNumSlots && getParam (slotParam (slot, "used")) > 0.5f;
}

bool EqGraph::isGainType (int slot) const
{
    if (slot <= 0 || slot >= penta::kNumSlots - 1)
        return false;
    return penta::dsp::isGainType ((int) std::lround (getParam (slotParam (slot, "type"))));
}

float EqGraph::gainScale() const { return getParam ("gainScale") * 0.01f; }

double EqGraph::effGain (int slot) const { return getParam (slotParam (slot, "gain")) * gainScale(); }

void EqGraph::setEffGain (int slot, double db)
{
    const double s = gainScale();
    if (s < 0.01)
        return; // Gain Scale 0 %: every band is flat, nothing to move
    setParam (slotParam (slot, "gain"), (float) juce::jlimit (-30.0, 30.0, db / s));
}

juce::Point<float> EqGraph::nodePos (int slot) const
{
    const double f = getParam (slotParam (slot, "freq"));
    double db = 0.0;
    if (isGainType (slot))
    {
        const int type = (int) std::lround (getParam (slotParam (slot, "type")));
        db = type == penta::kTilt ? 0.0 : effGain (slot);
        if (slot >= 1 && slot <= penta::kNumBands && ! spectral[(size_t) (slot - 1)])
            db += delta[(size_t) (slot - 1)];
    }
    else
        db = 10.0 * std::log10 (std::max (curves.slotMagSq (slot, f), 1.0e-12));
    const double r = options.rangeDb;
    return { xForFreq (f), yForDb (juce::jlimit (-r, r, db)) };
}

int EqGraph::hitNode (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 12.0f;
    for (int s = 0; s < penta::kNumSlots; ++s)
    {
        if (! slotActive (s))
            continue;
        const float d = nodePos (s).getDistanceFrom (p);
        if (d < bestD)
        {
            bestD = d;
            best = s;
        }
    }
    return best;
}

//== selection ==================================================================
std::vector<int> EqGraph::selection() const
{
    std::vector<int> v;
    for (int s = 0; s < penta::kNumSlots; ++s)
        if (sel[(size_t) s] && slotActive (s))
            v.push_back (s);
    return v;
}

void EqGraph::notifySelect (int slot)
{
    selected = slot;
    if (onSelectFn)
    {
        // the editor answers with setSelected(): keep the multi-selection meanwhile
        const auto keep = sel;
        onSelectFn (slot);
        sel = keep;
        if (slot >= 0)
            sel[(size_t) slot] = true;
    }
    repaint();
}

void EqGraph::setSelected (int slot)
{
    selected = slot;
    sel.fill (false);
    if (slot >= 0 && slot < penta::kNumSlots)
        sel[(size_t) slot] = true;
    repaint();
}

void EqGraph::toggleSelected (int slot)
{
    if (slot < 0 || slot >= penta::kNumSlots)
        return;
    sel[(size_t) slot] = ! sel[(size_t) slot];
    if (sel[(size_t) slot])
        notifySelect (slot);
    else if (selected == slot)
    {
        const auto rest = selection();
        notifySelect (rest.empty() ? -1 : rest.front());
        sel[(size_t) slot] = false;
    }
    repaint();
}

void EqGraph::selectAll()
{
    int first = -1;
    for (int s = 1; s <= penta::kNumBands; ++s)
        if (slotActive (s))
        {
            sel[(size_t) s] = true;
            if (first < 0)
                first = s;
        }
    if (first >= 0 && ! isSelected (selected))
        notifySelect (first);
    repaint();
}

void EqGraph::clearSelection()
{
    sel.fill (false);
    notifySelect (-1);
}

//== edits ======================================================================
void EqGraph::beginGesture (const std::vector<int>& slots)
{
    endGesture();
    for (int s : slots)
        for (const char* w : { "freq", "gain", "q" })
        {
            const auto id = slotParam (s, w);
            const int i = id.empty() ? -1 : penta::paramIndex (id);
            if (i >= 0)
            {
                proc.param (i)->beginChangeGesture();
                gestureIds.push_back (i);
            }
        }
}

void EqGraph::endGesture()
{
    for (int i : gestureIds)
        proc.param (i)->endChangeGesture();
    gestureIds.clear();
}

void EqGraph::nudge (double octaves, double db)
{
    auto group = selection();
    if (group.empty())
        return;
    static juce::uint32 lastNudge = 0;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastNudge > 800) // a burst of key repeats is one undo step
        proc.pushUndo();
    lastNudge = now;
    beginGesture (group);
    for (int s : group)
    {
        if (! juce::exactlyEqual (octaves, 0.0))
        {
            const double f = getParam (slotParam (s, "freq")) * std::exp2 (octaves);
            setParam (slotParam (s, "freq"), (float) juce::jlimit (10.0, 30000.0, snapped (f)));
        }
        if (! juce::exactlyEqual (db, 0.0))
        {
            if (isGainType (s))
                setEffGain (s, effGain (s) + db);
            else // cuts, notch, band-pass: up/down = Q
                setParam (slotParam (s, "q"), (float) juce::jlimit (0.1, 24.0, getParam (slotParam (s, "q")) * std::exp2 (db / 3.0)));
        }
    }
    endGesture();
    refresh();
}

void EqGraph::invertSelected()
{
    auto group = selection();
    bool any = false;
    for (int s : group)
        any = any || isGainType (s);
    if (! any)
        return;
    proc.pushUndo();
    beginGesture (group);
    for (int s : group)
        if (isGainType (s))
            setParam (slotParam (s, "gain"), -getParam (slotParam (s, "gain")));
    endGesture();
    refresh();
}

void EqGraph::removeSelected()
{
    auto group = selection();
    if (group.empty())
        return;
    proc.pushUndo();
    for (int s : group)
        setParam (slotParam (s, "used"), 0.0f);
    sel.fill (false);
    notifySelect (-1);
    refresh();
}

void EqGraph::copyBand (int slot)
{
    if (slot < 1 || slot > penta::kNumBands)
        return;
    auto& c = clipboard();
    for (int i = 0; i < kClipCount; ++i)
        c.v[(size_t) i] = getParam (penta::bandId (slot - 1, kClipParams[i]));
    c.valid = true;
}

bool EqGraph::hasClipboard() { return clipboard().valid; }

int EqGraph::firstFreeBand() const
{
    for (int b = 1; b <= penta::kNumBands; ++b)
        if (getParam (slotParam (b, "used")) < 0.5f)
            return b;
    return -1;
}

void EqGraph::pasteToSelection()
{
    const auto& c = clipboard();
    if (! c.valid)
        return;
    std::vector<int> targets;
    for (int s : selection())
        if (s >= 1 && s <= penta::kNumBands)
            targets.push_back (s);
    if (targets.empty())
    {
        const int b = firstFreeBand();
        if (b < 0)
            return;
        targets.push_back (b);
    }
    proc.pushUndo();
    for (int s : targets)
    {
        for (int i = 0; i < kClipCount; ++i)
            setParam (penta::bandId (s - 1, kClipParams[i]), c.v[(size_t) i]);
        setParam (slotParam (s, "byp"), 0.0f);
        setParam (slotParam (s, "used"), 1.0f);
    }
    if (targets.size() == 1)
        setSelected (targets.front());
    notifySelect (targets.front());
    refresh();
}

int EqGraph::peakNear (double hz) const
{
    const auto& d = options.post ? dispPost : dispPre;
    int best = -1;
    float bestV = -150.0f;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = freqs[(size_t) i];
        if (f < hz * 0.79 || f > hz * 1.26 || f < 20.0 || f > 20000.0) // +-1/3 octave
            continue;
        const float v = d[(size_t) i];
        const bool localMax = (i == 0 || v >= d[(size_t) (i - 1)]) && (i + 1 == kPoints || v >= d[(size_t) (i + 1)]);
        if (localMax && v > bestV)
        {
            bestV = v;
            best = i;
        }
    }
    return best;
}

double EqGraph::peakHzNear (double hz) const
{
    const int i = peakNear (hz);
    if (i < 0)
        return -1.0;
    // refine on the FFT bins (+-1/6 octave around the display peak), parabolic in dB
    const auto& a = options.post ? anPost : anPre;
    if (a.fft == nullptr || a.binDb.empty())
        return freqs[(size_t) i];
    const double binHz = a.fs / a.size, f0 = freqs[(size_t) i];
    const int nb = (int) a.binDb.size();
    const int k0 = juce::jlimit (1, nb - 2, (int) std::floor (f0 / 1.122 / binHz));
    const int k1 = juce::jlimit (k0, nb - 2, (int) std::ceil (f0 * 1.122 / binHz));
    int k = k0;
    for (int j = k0; j <= k1; ++j)
        if (a.binDb[(size_t) j] > a.binDb[(size_t) k])
            k = j;
    const double ym = a.binDb[(size_t) (k - 1)], y0 = a.binDb[(size_t) k], yp = a.binDb[(size_t) (k + 1)];
    const double den = ym - 2.0 * y0 + yp;
    const double off = den < 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (ym - yp) / den) : 0.0;
    return (k + off) * binHz;
}

int EqGraph::grabPeakAt (juce::Point<float> p)
{
    const int b = firstFreeBand();
    if (b < 0)
        return -1;
    const double pk = peakHzNear (freqForX (p.x));
    const double hz = pk > 0.0 ? pk : freqForX (p.x);
    proc.pushUndo();
    setParam (slotParam (b, "type"), (float) penta::kBell);
    setParam (slotParam (b, "freq"), (float) hz);
    setParam (slotParam (b, "gain"), 0.0f);
    setParam (slotParam (b, "q"), 4.0f); // narrow: a peak, not a region
    setParam (slotParam (b, "byp"), 0.0f);
    setParam (slotParam (b, "used"), 1.0f);
    setSelected (b);
    notifySelect (b);
    refresh();
    return b;
}

int EqGraph::fitBandsTo (const std::vector<double>& hz, const std::vector<double>& db, const std::vector<double>& weight)
{
    penta::dsp::CurveFitter cf;
    cf.setTarget (hz, db, weight);
    const auto bands = cf.fit (penta::kNumBands);
    proc.pushUndo();
    for (int b = 1; b <= penta::kNumBands; ++b)
    {
        setParam (slotParam (b, "used"), 0.0f);
        setParam (slotParam (b, "dyn"), 0.0f);
    }
    const double scale = std::max (0.01, (double) gainScale());
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const int s = (int) i + 1;
        const auto& fb = bands[i];
        setParam (slotParam (s, "type"), (float) fb.type);
        setParam (slotParam (s, "freq"), (float) fb.freqHz);
        setParam (slotParam (s, "gain"), (float) juce::jlimit (-30.0, 30.0, fb.gainDb / scale));
        setParam (slotParam (s, "q"), (float) fb.q);
        setParam (slotParam (s, "slope"), 1.0f); // shelves: 12 dB/oct (2nd order)
        setParam (slotParam (s, "mode"), 0.0f);
        setParam (slotParam (s, "place"), (float) penta::kStereo);
        setParam (slotParam (s, "byp"), 0.0f);
        setParam (slotParam (s, "used"), 1.0f);
    }
    sel.fill (false);
    notifySelect (-1);
    refresh();
    return (int) bands.size();
}

//== draw mode ==================================================================
void EqGraph::setDrawMode (bool on)
{
    if (on == drawMode)
        return;
    drawMode = on;
    drawing = false;
    clearDrawing();
    for (auto* b : { &drawApply, &drawClear, &drawCancel })
        b->setVisible (on);
    if (onDrawModeChanged)
        onDrawModeChanged();
    repaint();
}

void EqGraph::clearDrawing()
{
    std::fill (drawDb.begin(), drawDb.end(), std::numeric_limits<float>::quiet_NaN());
    repaint();
}

void EqGraph::drawSegment (juce::Point<float> a, juce::Point<float> b)
{
    if (b.x < a.x)
        std::swap (a, b);
    const float r = options.rangeDb;
    for (int i = 0; i < kPoints; ++i)
    {
        const float x = xForFreq (freqs[(size_t) i]);
        if (x < a.x - 0.5f || x > b.x + 0.5f)
            continue;
        const float t = b.x - a.x > 0.5f ? juce::jlimit (0.0f, 1.0f, (x - a.x) / (b.x - a.x)) : 0.0f;
        drawDb[(size_t) i] = juce::jlimit (-r, r, (float) dbForY (a.y + t * (b.y - a.y)));
    }
    repaint();
}

void EqGraph::applyDrawing()
{
    // drawn points count fully; gaps between strokes are bridged; outside the strokes the curve is flat
    std::vector<double> hz, db, w;
    int lastDrawn = -1;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = freqs[(size_t) i];
        if (f < 20.0 || f > 20000.0)
            continue;
        hz.push_back (f);
        if (! std::isnan (drawDb[(size_t) i]))
        {
            db.push_back (drawDb[(size_t) i]);
            w.push_back (1.0);
            lastDrawn = i;
            continue;
        }
        int next = i + 1;
        while (next < kPoints && std::isnan (drawDb[(size_t) next]))
            ++next;
        if (lastDrawn >= 0 && next < kPoints)
        {
            const double t = (double) (i - lastDrawn) / (double) (next - lastDrawn);
            db.push_back ((1.0 - t) * drawDb[(size_t) lastDrawn] + t * drawDb[(size_t) next]);
            w.push_back (0.6);
        }
        else
        {
            db.push_back (0.0);
            w.push_back (0.3);
        }
    }
    bool any = false;
    for (float v : drawDb)
        any = any || ! std::isnan (v);
    if (any)
        fitBandsTo (hz, db, w);
    setDrawMode (false);
}

//== capture (Match) ============================================================
void EqGraph::startCapture (Capture what)
{
    capture = what;
    std::fill (capSum.begin(), capSum.end(), 0.0);
    capFrames = 0;
}

void EqGraph::stopCapture() { capture = Capture::none; }

double EqGraph::capturedSeconds() const
{
    const auto& a = anPre;
    return a.fs > 0.0 ? capFrames * (a.size / 4) / a.fs : 0.0;
}

std::vector<double> EqGraph::capturedDb() const
{
    std::vector<double> d ((size_t) kPoints, -200.0);
    if (capFrames > 0)
        for (int i = 0; i < kPoints; ++i)
            d[(size_t) i] = 10.0 * std::log10 (capSum[(size_t) i] / capFrames + 1.0e-20);
    return d;
}

void EqGraph::accumulateCapture (const Analyzer& a)
{
    // 1/6-octave power, no tilt (the match compares spectra, not displays)
    const int nb = a.size / 2 + 1;
    const double binHz = a.fs / a.size;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = freqs[(size_t) i];
        int k0 = (int) std::floor (f / std::pow (2.0, 1.0 / 12.0) / binHz), k1 = (int) std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz);
        k0 = juce::jlimit (1, nb - 1, k0);
        k1 = juce::jlimit (k0, nb - 1, k1);
        double p = 0.0;
        for (int k = k0; k <= k1; ++k)
            p += std::pow (10.0, a.binDb[(size_t) k] / 10.0);
        capSum[(size_t) i] += p / (k1 - k0 + 1);
    }
    ++capFrames;
}

//==============================================================================
void EqGraph::pullAnalyzers()
{
    auto& e = proc.engine();
    if (anPre.fft == nullptr || ! juce::exactlyEqual (anPre.fs, e.getSampleRate()))
    {
        anPre.prepare (e.getSampleRate());
        anPost.prepare (e.getSampleRate());
        anSc.prepare (e.getSampleRate());
    }
    auto drain = [this] (penta::SpscFifo& f, Analyzer& a)
    {
        int n;
        while ((n = f.pull (fifoTmp.data(), (int) fifoTmp.size())) > 0)
            a.push (fifoTmp.data(), n);
    };
    drain (e.preAnalyzer(), anPre);
    drain (e.postAnalyzer(), anPost);
    drain (e.scAnalyzer(), anSc);
    const float rel = options.speed == 0 ? 0.25f : (options.speed == 1 ? 0.6f : 1.4f); // dB per frame fall
    if (anPre.compute())
    {
        if (! options.freeze)
            buildDisplay (anPre, dispPre, nullptr, rel);
        if (capture == Capture::input)
            accumulateCapture (anPre);
    }
    if (anPost.compute() && ! options.freeze)
        buildDisplay (anPost, dispPost, &dispPeak, rel);
    scActive = proc.sidechainConnected();
    if (scActive && anSc.compute())
    {
        if (! options.freeze)
            buildDisplay (anSc, dispSc, nullptr, rel);
        if (capture == Capture::sidechain)
            accumulateCapture (anSc);
    }
}

void EqGraph::buildDisplay (const Analyzer& a, std::vector<float>& out, std::vector<float>* peak, float release)
{
    // 1/6-octave power smoothing, display tilt (pivot 1 kHz), fast rise, slow fall
    const int nb = a.size / 2 + 1;
    const double binHz = a.fs / a.size;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = freqs[(size_t) i];
        const double lo = f / std::pow (2.0, 1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        int k0 = (int) std::floor (lo / binHz), k1 = (int) std::ceil (hi / binHz);
        k0 = juce::jlimit (1, nb - 1, k0);
        k1 = juce::jlimit (k0, nb - 1, k1);
        double p = 0.0;
        for (int k = k0; k <= k1; ++k)
            p += std::pow (10.0, a.binDb[(size_t) k] / 10.0);
        p /= (k1 - k0 + 1);
        float db = (float) (10.0 * std::log10 (p + 1.0e-20)) + options.tilt * (float) std::log2 (f / 1000.0);
        if (f > a.fs * 0.5)
            db = -200.0f;
        float& o = out[(size_t) i];
        o = db > o ? db : std::max (db, o - release);
        if (peak != nullptr)
        {
            float& pk = (*peak)[(size_t) i];
            pk = db > pk ? db : pk - 0.05f;
        }
    }
}

void EqGraph::refresh()
{
    values = proc.readValues();
    std::array<float, penta::kNumBands> curveDelta {};
    for (int b = 0; b < penta::kNumBands; ++b)
    {
        delta[(size_t) b] = proc.meters.delta[b].load (std::memory_order_relaxed);
        const auto& bi = penta::idx().band[b];
        spectral[(size_t) b] = values[(size_t) bi[penta::bpDyn]] > 0.5f && values[(size_t) bi[penta::bpDynMode]] > 0.5f;
        curveDelta[(size_t) b] = spectral[(size_t) b] ? 0.0f : delta[(size_t) b];
    }
    fs = proc.engine().getSampleRate();
    curves.build (values, fs, curveDelta.data());
    curvesStatic.build (values, fs);
    constexpr int nd = penta::dsp::SpectralDynamics::kDisplayPoints;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = freqs[(size_t) i];
        for (int s = 0; s < penta::kNumSlots; ++s)
            slotDb[(size_t) s][(size_t) i] = curves.on[(size_t) s]
                                                 ? (float) (10.0 * std::log10 (std::max (curves.slotMagSq (s, f), 1.0e-12)))
                                                 : 0.0f;
        double m = 1.0;
        for (int k = 0; k < penta::dsp::kNumModules; ++k)
            if (curves.modOn[(size_t) k])
                m *= curves.moduleMagSq (k, f);
        moduleDb[(size_t) i] = (float) (10.0 * std::log10 (std::max (m, 1.0e-12)));
        totalDb[(size_t) i] = (float) curves.totalDb (f);
        totalStaticDb[(size_t) i] = (float) curvesStatic.totalDb (f);
        // spectral bands: their live per-frequency gain on top of the static shape
        const double pos = std::log (f / 20.0) / std::log (1000.0) * (nd - 1);
        for (int b = 0; b < penta::kNumBands; ++b)
        {
            const int s = b + 1;
            float dyn = 0.0f;
            if (spectral[(size_t) b] && curves.on[(size_t) s] && pos >= 0.0 && pos <= nd - 1)
            {
                const int k0 = juce::jlimit (0, nd - 2, (int) pos);
                const float t = (float) (pos - k0);
                dyn = proc.engine().spectralDisplayDb (b, k0) * (1.0f - t) + proc.engine().spectralDisplayDb (b, k0 + 1) * t;
            }
            slotDynDb[(size_t) s][(size_t) i] = slotDb[(size_t) s][(size_t) i] + dyn;
            if (curves.place[(size_t) s] != penta::kSide)
                totalDb[(size_t) i] += dyn;
        }
    }
    pullAnalyzers();
    repaint();
}

//==============================================================================
void EqGraph::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto full = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (full, 8.0f);

    // grid
    g.setFont (font (10.0f, 1));
    for (int fi : { 20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800, 1000, 2000, 3000, 4000, 5000, 6000, 8000,
                    10000, 20000 })
    {
        const double f = fi;
        const float x = xForFreq (f);
        const bool major = fi == 100 || fi == 1000 || fi == 10000;
        g.setColour (major ? Colour (0xff2c313a) : col::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        const bool labelled = fi == 20 || fi == 50 || fi == 100 || fi == 200 || fi == 500 || fi == 1000 || fi == 2000
                           || fi == 5000 || fi == 10000 || fi == 20000;
        if (labelled)
        {
            g.setColour (col::faint);
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f), Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                        Justification::centred);
        }
    }
    const float r = options.rangeDb;
    const float step = r >= 24.0f ? 6.0f : (r >= 12.0f ? 3.0f : 1.0f);
    for (float db = -r; db <= r + 0.01f; db += step)
    {
        const float y = yForDb (db);
        g.setColour (std::abs (db) < 0.01f ? Colour (0xff3a404b) : col::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        if (std::abs (std::fmod (db, step * 2.0f)) < 0.01f || std::abs (db) < 0.01f)
        {
            g.setColour (col::faint);
            g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (plot.getX() - 30.0f, y - 7.0f, 26.0f, 14.0f),
                        Justification::centredRight);
        }
    }
    // spectrum scale (right side)
    for (double db : { 0.0, -24.0, -48.0, -72.0 })
    {
        g.setColour (col::faint.withAlpha (0.7f));
        g.drawText (String ((int) db), Rectangle<float> (plot.getRight() + 3.0f, yForSpec (db) - 7.0f, 30.0f, 14.0f), Justification::centredLeft);
    }

    // analyzers
    auto specPath = [&] (const std::vector<float>& d, bool close)
    {
        Path p;
        bool started = false;
        for (int i = 0; i < kPoints; ++i)
        {
            const double f = freqs[(size_t) i];
            if (f < 18.0 || f > 22000.0)
                continue;
            const float x = xForFreq (f), y = yForSpec (d[(size_t) i]);
            if (! started)
            {
                p.startNewSubPath (x, close ? plot.getBottom() : y);
                if (close)
                    p.lineTo (x, y);
                started = true;
            }
            else
                p.lineTo (x, y);
        }
        if (close && started)
        {
            p.lineTo (xForFreq (22000.0), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        const float specAlpha = drawMode ? 0.4f : 1.0f;
        if (options.pre)
        {
            g.setColour (Colour (0x18c3c8d2).withMultipliedAlpha (specAlpha));
            g.fillPath (specPath (dispPre, true));
        }
        if (options.post)
        {
            auto p = specPath (dispPost, true);
            g.setColour (col::teal.withAlpha (0.07f * specAlpha));
            g.fillPath (p);
            g.setColour (col::teal.withAlpha (0.38f * specAlpha));
            g.strokePath (specPath (dispPost, false), PathStrokeType (1.0f));
            if (options.peakHold)
            {
                g.setColour (col::text.withAlpha (0.25f * specAlpha));
                g.strokePath (specPath (dispPeak, false), PathStrokeType (0.8f));
            }
        }
        if (options.sc && scActive)
        {
            g.setColour (col::warn.withAlpha (0.6f * specAlpha));
            g.strokePath (specPath (dispSc, false), PathStrokeType (1.0f));
        }

        // band curves
        auto curvePath = [&] (const std::vector<float>& d)
        {
            Path p;
            for (int i = 0; i < kPoints; ++i)
            {
                const float x = xForFreq (freqs[(size_t) i]), y = yForDb (juce::jlimit (-r * 1.5f, r * 1.5f, d[(size_t) i]));
                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }
            return p;
        };
        const float zeroY = yForDb (0.0);
        for (int s = 0; s < penta::kNumSlots; ++s)
        {
            if (! curves.on[(size_t) s])
                continue;
            auto p = curvePath (slotDb[(size_t) s]);
            auto fill = p;
            fill.lineTo (xForFreq (kMaxHz), zeroY);
            fill.lineTo (xForFreq (kMinHz), zeroY);
            fill.closeSubPath();
            const auto c = col::band (s == 0 ? 12 : (s == penta::kNumSlots - 1 ? 13 : s - 1));
            const bool hi = isSelected (s);
            g.setColour (c.withAlpha (hi ? 0.24f : 0.10f));
            g.fillPath (fill);
            // outline only where the band actually does something (no line along 0 dB)
            Path active;
            bool inSeg = false;
            const auto& d = slotDb[(size_t) s];
            for (int i = 0; i < kPoints; ++i)
            {
                const bool on = std::abs (d[(size_t) i]) > 0.05f;
                const float x = xForFreq (freqs[(size_t) i]), y = yForDb (juce::jlimit (-r * 1.5f, r * 1.5f, d[(size_t) i]));
                if (on && ! inSeg)
                    active.startNewSubPath (x, y);
                else if (on)
                    active.lineTo (x, y);
                inSeg = on;
            }
            g.setColour (c.withAlpha (hi ? 0.95f : 0.45f));
            g.strokePath (active, PathStrokeType (hi ? 1.6f : 1.0f));
            // spectral band: the live per-frequency gain, filled against its static shape
            if (s >= 1 && s <= penta::kNumBands && spectral[(size_t) (s - 1)])
            {
                Path sp;
                const auto& dd = slotDynDb[(size_t) s];
                for (int i = 0; i < kPoints; ++i)
                {
                    const float x = xForFreq (freqs[(size_t) i]), y = yForDb (juce::jlimit (-r * 1.5f, r * 1.5f, dd[(size_t) i]));
                    if (i == 0)
                        sp.startNewSubPath (x, y);
                    else
                        sp.lineTo (x, y);
                }
                for (int i = kPoints - 1; i >= 0; --i)
                    sp.lineTo (xForFreq (freqs[(size_t) i]), yForDb (juce::jlimit (-r * 1.5f, r * 1.5f, d[(size_t) i])));
                sp.closeSubPath();
                g.setColour (c.withAlpha (0.45f));
                g.fillPath (sp);
            }
        }
        bool anyModule = false;
        for (bool m : curves.modOn)
            anyModule = anyModule || m;
        if (anyModule)
        {
            g.setColour (col::tube.withAlpha (0.45f));
            g.strokePath (curvePath (moduleDb), PathStrokeType (1.0f, PathStrokeType::curved, PathStrokeType::butt));
        }
        // total: static (faint) and live (dynamic bands moving)
        g.setColour (col::text.withAlpha (0.25f));
        g.strokePath (curvePath (totalStaticDb), PathStrokeType (1.2f));
        g.setColour (col::text.withAlpha (drawMode ? 0.4f : 1.0f));
        g.strokePath (curvePath (totalDb), PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));

        // the curve being drawn
        if (drawMode)
        {
            Path dp;
            bool started = false;
            for (int i = 0; i < kPoints; ++i)
            {
                const float v = drawDb[(size_t) i];
                if (std::isnan (v))
                {
                    started = false;
                    continue;
                }
                const float x = xForFreq (freqs[(size_t) i]), y = yForDb (v);
                if (! started)
                    dp.startNewSubPath (x, y);
                else
                    dp.lineTo (x, y);
                started = true;
            }
            g.setColour (col::accent);
            g.strokePath (dp, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // nodes
    for (int s = 0; s < penta::kNumSlots && ! drawMode; ++s)
    {
        if (! slotActive (s))
            continue;
        const auto pos = nodePos (s);
        const auto c = col::band (s == 0 ? 12 : (s == penta::kNumSlots - 1 ? 13 : s - 1));
        const bool byp = s >= 1 && s <= penta::kNumBands && getParam (slotParam (s, "byp")) > 0.5f;
        const bool dyn = s >= 1 && s <= penta::kNumBands && getParam (slotParam (s, "dyn")) > 0.5f && isGainType (s);
        if (dyn)
        {
            // dynamic range bar: static gain -> static gain + range
            const float g0 = (float) effGain (s), rg = getParam (slotParam (s, "range"));
            const float y0 = yForDb (jlimit (-r, r, g0)), y1 = yForDb (jlimit (-r, r, g0 + rg));
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (pos.x - 2.0f, std::min (y0, y1), 4.0f, std::abs (y1 - y0), 2.0f);
        }
        const bool hi = isSelected (s);
        const float rad = s == selected ? 8.5f : (hi || s == hover ? 8.0f : 7.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (pos.x - rad - 1.0f, pos.y - rad + 1.0f, 2 * rad + 2.0f, 2 * rad + 2.0f);
        g.setColour (byp ? col::faint : c);
        g.fillEllipse (pos.x - rad, pos.y - rad, 2 * rad, 2 * rad);
        if (hi)
        {
            g.setColour (s == selected ? col::text : col::text.withAlpha (0.6f));
            g.drawEllipse (pos.x - rad - 2.5f, pos.y - rad - 2.5f, 2 * rad + 5.0f, 2 * rad + 5.0f, s == selected ? 1.4f : 1.0f);
        }
        if (dyn)
        {
            g.setColour (col::text.withAlpha (0.9f));
            g.drawEllipse (pos.x - rad + 2.0f, pos.y - rad + 2.0f, 2 * rad - 4.0f, 2 * rad - 4.0f, 1.0f);
        }
        g.setColour (Colour (0xff121418));
        g.setFont (font (9.5f, 2));
        const String label = s == 0 ? "HP" : (s == penta::kNumSlots - 1 ? "LP" : String (s));
        g.drawText (label, Rectangle<float> (pos.x - rad, pos.y - rad, 2 * rad, 2 * rad), Justification::centred);
        if (s >= 1 && s <= penta::kNumBands)
        {
            const int place = (int) std::lround (getParam (slotParam (s, "place")));
            String tag = place > 0 ? String (kPlaceShort[place]) : String();
            if (dyn && spectral[(size_t) (s - 1)])
                tag << (tag.isEmpty() ? "" : " ") << "SP";
            if (tag.isNotEmpty())
            {
                g.setColour (c);
                g.setFont (font (9.0f, 2));
                g.drawText (tag, Rectangle<float> (pos.x + rad, pos.y - rad - 8.0f, 30.0f, 10.0f), Justification::centredLeft);
            }
        }
    }

    // frame selection
    if (lassoing)
    {
        g.setColour (col::accent.withAlpha (0.08f));
        g.fillRect (lasso);
        g.setColour (col::accent.withAlpha (0.7f));
        g.drawRect (lasso, 1.0f);
    }

    // Alt held over empty space: the spectrum peak a click would grab
    if (altHeld && ! drawMode && hover < 0 && plot.contains (mousePos))
    {
        const int i = peakNear (freqForX (mousePos.x));
        if (i >= 0)
        {
            const double pk = peakHzNear (freqForX (mousePos.x));
            const float x = xForFreq (pk);
            const float y = yForSpec ((options.post ? dispPost : dispPre)[(size_t) i]);
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawVerticalLine ((int) x, y, plot.getBottom());
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.setFont (font (11.0f, 1));
            g.drawText ("grab " + hzText (pk) + "  " + noteName (pk),
                        Rectangle<float> (x + 6.0f, y - 16.0f, 160.0f, 14.0f).constrainedWithin (plot), Justification::centredLeft);
        }
    }

    // hover / drag readout
    const int info = dragging >= 0 ? dragging : hover;
    if (info >= 0 && slotActive (info) && ! drawMode)
    {
        const auto pos = nodePos (info);
        const double f = getParam (slotParam (info, "freq"));
        String t = hzText (f) + "  " + noteName (f);
        if (isGainType (info))
        {
            const double ge = effGain (info);
            t << "   " << (ge >= 0 ? "+" : "") << String (ge, 1) << " dB";
        }
        t << "   Q " << String (getParam (slotParam (info, "q")), 2);
        if (info >= 1 && info <= penta::kNumBands)
            t = String (kTypeShort[(int) std::lround (getParam (slotParam (info, "type")))]) + "   " + t;
        if (dragGroup.size() > 1 && dragging >= 0)
            t << "   (" << (int) dragGroup.size() << " bands)";
        const float w = GlyphArrangement::getStringWidth (font (12.0f, 1), t) + 16.0f;
        auto box = Rectangle<float> (pos.x - w * 0.5f, pos.y - 38.0f, w, 22.0f);
        if (box.getY() < plot.getY())
            box.setY (pos.y + 16.0f);
        box = box.constrainedWithin (plot);
        g.setColour (Colour (0xf0262a32));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (col::text);
        g.setFont (font (12.0f, 1));
        g.drawText (t, box, Justification::centred);
    }

    // status line: draw mode, capture, freeze
    String status;
    if (drawMode)
        status = "DRAW THE CURVE YOU WANT, THEN APPLY  (ESC = CANCEL)";
    else if (capture != Capture::none)
        status = String (capture == Capture::input ? "CAPTURING INPUT  " : "CAPTURING SIDE-CHAIN  ") + String (capturedSeconds(), 1) + " s";
    else if (options.freeze)
        status = "ANALYZER FROZEN";
    if (status.isNotEmpty())
    {
        g.setFont (caps (11.0f, 2));
        g.setColour (drawMode || capture != Capture::none ? col::accent : col::dim);
        g.drawText (status, plot.toNearestInt().withTrimmedTop (8).removeFromTop (22).withTrimmedLeft (12), Justification::centredLeft);
    }
    g.setColour (col::track);
    g.drawRoundedRectangle (full.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int h = drawMode ? -1 : hitNode (e.position);
    if (h != hover || altHeld)
    {
        hover = h;
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    mousePos = { -1000.0f, -1000.0f };
    repaint();
}

void EqGraph::modifierKeysChanged (const juce::ModifierKeys& m)
{
    if (m.isAltDown() != altHeld)
    {
        altHeld = m.isAltDown();
        repaint();
    }
}

int EqGraph::addBandAt (juce::Point<float> p)
{
    const int b = firstFreeBand();
    if (b < 0)
        return -1;
    proc.pushUndo();
    const double f = snapped (freqForX (p.x));
    const double g = juce::jlimit (-30.0, 30.0, dbForY (p.y));
    setParam (slotParam (b, "type"), (float) penta::kBell);
    setParam (slotParam (b, "freq"), (float) f);
    setParam (slotParam (b, "gain"), 0.0f);
    setParam (slotParam (b, "q"), 1.0f);
    setParam (slotParam (b, "byp"), 0.0f);
    setParam (slotParam (b, "used"), 1.0f);
    setEffGain (b, std::abs (g) < 0.5 ? 0.0 : g);
    return b;
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = e.position;
    dragMoved = false;
    lassoing = false;
    dragGroup.clear();
    if (drawMode)
    {
        if (! e.mods.isPopupMenu())
        {
            drawing = true;
            drawLast = e.position;
            drawSegment (e.position, e.position);
        }
        return;
    }
    const int h = hitNode (e.position);
    if (h < 0)
    {
        dragging = -1;
        if (e.mods.isPopupMenu())
        {
            showGraphMenu (e.position);
            return;
        }
        if (e.mods.isAltDown() && plot.contains (e.position))
        {
            // new band on the spectrum peak, then drag its gain right away
            const int b = grabPeakAt (e.position);
            if (b > 0)
            {
                dragging = b;
                dragStart = e.position;
                dragGroup = { b };
                dragFrom[(size_t) b] = { getParam (slotParam (b, "freq")), effGain (b), getParam (slotParam (b, "q")) };
                dragQ = false;
                beginGesture (dragGroup);
            }
        }
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (! isSelected (h))
            setSelected (h);
        notifySelect (h);
        showNodeMenu (h);
        return;
    }
    if (e.mods.isAltDown() && h >= 1 && h <= penta::kNumBands)
    {
        if (isSelected (h) && selection().size() > 1)
            removeSelected();
        else
        {
            proc.pushUndo();
            setParam (slotParam (h, "used"), 0.0f);
            sel[(size_t) h] = false;
            notifySelect (-1);
        }
        return;
    }
    if (! isSelected (h) && ! e.mods.isCommandDown())
        setSelected (h);
    if (! e.mods.isCommandDown())
        notifySelect (h);
    proc.pushUndo();
    dragging = h;
    dragStart = e.position;
    dragQ = e.mods.isCommandDown();
    dragGroup = isSelected (h) ? selection() : std::vector<int> { h };
    for (int s : dragGroup)
        dragFrom[(size_t) s] = { getParam (slotParam (s, "freq")), isGainType (s) ? effGain (s) : 0.0, getParam (slotParam (s, "q")) };
    beginGesture (dragGroup);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (drawMode)
    {
        if (drawing)
        {
            drawSegment (drawLast, e.position);
            drawLast = e.position;
        }
        return;
    }
    if (dragging < 0)
    {
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 4 || e.mods.isAltDown())
            return;
        // frame selection (Ctrl/Shift: add to the selection)
        lassoing = true;
        lasso = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        const bool add = e.mods.isCommandDown() || e.mods.isShiftDown();
        int first = -1;
        for (int s = 0; s < penta::kNumSlots; ++s)
        {
            const bool inside = slotActive (s) && lasso.contains (nodePos (s));
            if (! add)
                sel[(size_t) s] = inside;
            else if (inside)
                sel[(size_t) s] = true;
            if (inside && first < 0)
                first = s;
        }
        if (first >= 0 && ! isSelected (selected))
            notifySelect (first);
        repaint();
        return;
    }
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto d = (e.position - dragStart) * fine;
    if (d.getDistanceFromOrigin() > 1.0f)
        dragMoved = true;
    if (! dragMoved)
        return;
    const auto& p0 = dragFrom[(size_t) dragging];
    const double fNew = snapped (freqForX (xForFreq (p0.f) + d.x));
    const double ratio = fNew / std::max (1.0, p0.f);
    const double dg = -d.y / (plot.getHeight() * 0.5f) * options.rangeDb;
    const bool single = dragGroup.size() == 1;
    for (int s : dragGroup)
    {
        const auto& from = dragFrom[(size_t) s];
        setParam (slotParam (s, "freq"), (float) juce::jlimit (10.0, 30000.0, s == dragging ? fNew : from.f * ratio));
        if (dragQ || (! isGainType (s) && single))
            setParam (slotParam (s, "q"), (float) juce::jlimit (0.1, 24.0, from.q * std::pow (2.0, -d.y / 60.0)));
        else if (isGainType (s))
        {
            const double lim = 30.0 * gainScale();
            setEffGain (s, juce::jlimit (-lim, lim, from.g + dg));
        }
    }
    refresh();
}

void EqGraph::mouseUp (const juce::MouseEvent& e)
{
    if (drawMode)
    {
        drawing = false;
        return;
    }
    if (lassoing)
    {
        lassoing = false;
        repaint();
        return;
    }
    if (dragging >= 0)
    {
        endGesture();
        if (e.mods.isCommandDown() && ! dragMoved)
            toggleSelected (dragging);
        dragging = -1;
        dragGroup.clear();
        repaint();
        return;
    }
    // a click (no drag) on empty space adds a band
    if (! e.mods.isPopupMenu() && ! e.mods.isAltDown() && e.getDistanceFromDragStart() < 3 && plot.contains (e.position)
        && e.getNumberOfClicks() == 1)
    {
        const int b = addBandAt (e.position);
        if (b > 0)
        {
            refresh();
            setSelected (b);
            notifySelect (b);
        }
    }
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (drawMode)
        return;
    const int h = hitNode (e.position);
    if (h >= 1 && h <= penta::kNumBands && isGainType (h))
    {
        proc.pushUndo();
        setParam (slotParam (h, "gain"), 0.0f);
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (drawMode)
        return;
    int h = hitNode (e.position);
    if (h < 0)
        h = selected;
    if (h < 0 || ! slotActive (h))
        return;
    const auto group = isSelected (h) ? selection() : std::vector<int> { h };
    const float step = (e.mods.isShiftDown() ? 0.02f : 0.12f) * (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    beginGesture (group);
    for (int s : group)
    {
        const auto id = slotParam (s, "q");
        setParam (id, juce::jlimit (0.1f, 24.0f, getParam (id) * std::pow (2.0f, step * 4.0f)));
    }
    endGesture();
    refresh();
}

bool EqGraph::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey)
    {
        if (drawMode)
            setDrawMode (false);
        else
            clearSelection();
        return true;
    }
    if (drawMode)
    {
        if (code == juce::KeyPress::returnKey)
        {
            applyDrawing();
            return true;
        }
        return false;
    }
    const double fineF = m.isShiftDown() ? 1.0 / 96.0 : 1.0 / 12.0, fineG = m.isShiftDown() ? 0.1 : 0.5;
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        nudge ((code == juce::KeyPress::rightKey ? 1.0 : -1.0) * fineF, 0.0);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        nudge (0.0, (code == juce::KeyPress::upKey ? 1.0 : -1.0) * fineG);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        removeSelected();
        return true;
    }
    if (m.isCommandDown() && (code == 'A' || code == 'a'))
    {
        selectAll();
        return true;
    }
    if (m.isCommandDown() && (code == 'C' || code == 'c'))
    {
        copyBand (selected);
        return true;
    }
    if (m.isCommandDown() && (code == 'V' || code == 'v'))
    {
        pasteToSelection();
        return true;
    }
    if (! m.isAnyModifierKeyDown() || m == juce::ModifierKeys::shiftModifier)
    {
        if (code == 'I' || code == 'i')
        {
            invertSelected();
            return true;
        }
        if (code == 'B' || code == 'b')
        {
            const auto group = selection();
            if (! group.empty())
            {
                proc.pushUndo();
                const bool to = getParam (slotParam (group.front(), "byp")) < 0.5f;
                for (int s : group)
                    if (s >= 1 && s <= penta::kNumBands)
                        setParam (slotParam (s, "byp"), to ? 1.0f : 0.0f);
                refresh();
            }
            return true;
        }
        if (code == juce::KeyPress::tabKey)
        {
            std::vector<int> active;
            for (int s = 0; s < penta::kNumSlots; ++s)
                if (slotActive (s))
                    active.push_back (s);
            if (! active.empty())
            {
                auto it = std::find (active.begin(), active.end(), selected);
                int idx = it == active.end() ? -1 : (int) (it - active.begin());
                idx = m.isShiftDown() ? (idx <= 0 ? (int) active.size() - 1 : idx - 1) : (idx + 1) % (int) active.size();
                setSelected (active[(size_t) idx]);
                notifySelect (active[(size_t) idx]);
            }
            return true;
        }
    }
    return false;
}

//==============================================================================
void EqGraph::showNodeMenu (int slot)
{
    using namespace juce;
    PopupMenu m, type, slope, place, mode;
    const bool band = slot >= 1 && slot <= penta::kNumBands;
    const int nSel = (int) selection().size();
    if (band)
    {
        const int t = (int) std::lround (getParam (slotParam (slot, "type")));
        for (int i = 0; i < 8; ++i)
            type.addItem (100 + i, penta::choice::bandType[(size_t) i], true, i == t);
        m.addSubMenu ("Type", type);
        const int pl = (int) std::lround (getParam (slotParam (slot, "place")));
        for (int i = 0; i < 5; ++i)
            place.addItem (200 + i, penta::choice::place[(size_t) i], true, i == pl);
        m.addSubMenu ("Placement", place);
        const int md = (int) std::lround (getParam (slotParam (slot, "mode")));
        mode.addItem (300, "Clean", true, md == 0);
        mode.addItem (301, "Character", true, md == 1);
        m.addSubMenu ("Mode", mode);
    }
    const int sl = (int) std::lround (getParam (slotParam (slot, "slope")));
    for (int i = 0; i < 8; ++i)
        slope.addItem (400 + i, penta::choice::slope[(size_t) i], true, i == sl);
    m.addSubMenu ("Slope", slope);
    if (band)
    {
        m.addSeparator();
        const bool dyn = getParam (slotParam (slot, "dyn")) > 0.5f;
        m.addItem (500, "Dynamic", isGainType (slot), dyn);
        m.addItem (504, "Spectral dynamics", isGainType (slot), dyn && getParam (slotParam (slot, "dmode")) > 0.5f);
        m.addItem (501, "Bypass", true, getParam (slotParam (slot, "byp")) > 0.5f);
        m.addItem (502, "Reset gain to 0 dB", isGainType (slot));
        m.addItem (505, nSel > 1 ? "Invert gains (" + String (nSel) + " bands)" : String ("Invert gain"), isGainType (slot));
        m.addSeparator();
        m.addItem (601, "Copy band");
        m.addItem (602, nSel > 1 ? "Paste onto " + String (nSel) + " bands" : String ("Paste onto this band"), hasClipboard());
    }
    m.addSeparator();
    m.addItem (503, nSel > 1 ? "Remove " + String (nSel) + " bands" : (band ? String ("Remove band") : String ("Turn off")));
    SafePointer<EqGraph> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (Desktop::getMousePosition(), Desktop::getMousePosition())),
                     [safe, slot, nSel] (int id)
    {
        if (safe == nullptr || id == 0)
            return;
        auto& self = *safe;
        if (id == 503 && nSel > 1)
        {
            self.removeSelected();
            return;
        }
        if (id == 505)
        {
            if (nSel <= 1)
                self.setSelected (slot);
            self.invertSelected();
            return;
        }
        if (id == 601)
        {
            self.copyBand (slot);
            return;
        }
        if (id == 602)
        {
            if (nSel <= 1)
                self.setSelected (slot);
            self.pasteToSelection();
            return;
        }
        self.proc.pushUndo();
        if (id >= 100 && id < 108)
        {
            self.setParam (self.slotParam (slot, "type"), (float) (id - 100));
            self.proc.applyNeutralQ (slot - 1);
        }
        else if (id >= 200 && id < 205)
            self.setParam (self.slotParam (slot, "place"), (float) (id - 200));
        else if (id == 300 || id == 301)
            self.setParam (self.slotParam (slot, "mode"), (float) (id - 300));
        else if (id >= 400 && id < 408)
            self.setParam (self.slotParam (slot, "slope"), (float) (id - 400));
        else if (id == 500)
            self.setParam (self.slotParam (slot, "dyn"), self.getParam (self.slotParam (slot, "dyn")) > 0.5f ? 0.0f : 1.0f);
        else if (id == 504)
        {
            const bool on = self.getParam (self.slotParam (slot, "dyn")) > 0.5f && self.getParam (self.slotParam (slot, "dmode")) > 0.5f;
            self.setParam (self.slotParam (slot, "dmode"), on ? 0.0f : 1.0f);
            if (! on)
                self.setParam (self.slotParam (slot, "dyn"), 1.0f);
        }
        else if (id == 501)
            self.setParam (self.slotParam (slot, "byp"), self.getParam (self.slotParam (slot, "byp")) > 0.5f ? 0.0f : 1.0f);
        else if (id == 502)
            self.setParam (self.slotParam (slot, "gain"), 0.0f);
        else if (id == 503)
        {
            self.setParam (self.slotParam (slot, "used"), 0.0f);
            self.sel[(size_t) slot] = false;
            self.notifySelect (-1);
        }
        self.refresh();
    });
}

void EqGraph::showGraphMenu (juce::Point<float> p)
{
    using namespace juce;
    PopupMenu m;
    m.addItem (1, "Add band here");
    m.addItem (2, "Add band on the spectrum peak here (Alt+click)");
    m.addItem (3, "Paste band", hasClipboard());
    m.addSeparator();
    m.addItem (4, "Select all bands (Ctrl+A)");
    m.addItem (5, "Draw curve...");
    m.addSeparator();
    m.addItem (6, "Snap frequency to notes", true, options.snapNotes);
    m.addItem (7, "Freeze analyzer", true, options.freeze);
    SafePointer<EqGraph> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (Desktop::getMousePosition(), Desktop::getMousePosition())),
                     [safe, p] (int id)
    {
        if (safe == nullptr || id == 0)
            return;
        auto& self = *safe;
        switch (id)
        {
            case 1:
            {
                const int b = self.addBandAt (p);
                if (b > 0)
                {
                    self.setSelected (b);
                    self.notifySelect (b);
                }
                break;
            }
            case 2: self.grabPeakAt (p); break;
            case 3: self.clearSelection(); self.pasteToSelection(); break;
            case 4: self.selectAll(); break;
            case 5: self.setDrawMode (true); break;
            case 6: self.options.snapNotes = ! self.options.snapNotes; break;
            case 7: self.options.freeze = ! self.options.freeze; break;
            default: break;
        }
        if ((id == 6 || id == 7) && self.onOptionsChanged)
            self.onOptionsChanged();
        self.refresh();
    });
}

void EqGraph::showOptionsMenu()
{
    using namespace juce;
    PopupMenu m, range, speed;
    m.addSectionHeader ("ANALYZER");
    m.addItem (1, "Input spectrum (pre)", true, options.pre);
    m.addItem (2, "Output spectrum (post)", true, options.post);
    m.addItem (3, "Side-chain spectrum", true, options.sc);
    m.addItem (4, "Peak hold", true, options.peakHold);
    m.addItem (5, "Tilt 4.5 dB/oct (pink = flat)", true, options.tilt > 0.0f);
    m.addItem (6, "Freeze", true, options.freeze);
    speed.addItem (10, "Slow", true, options.speed == 0);
    speed.addItem (11, "Medium", true, options.speed == 1);
    speed.addItem (12, "Fast", true, options.speed == 2);
    m.addSubMenu ("Speed", speed);
    m.addSeparator();
    m.addSectionHeader ("EQ SCALE");
    for (int rr : { 6, 12, 18, 30 })
        range.addItem (20 + rr, "+-" + String (rr) + " dB", true, std::abs (options.rangeDb - (float) rr) < 0.1f);
    m.addSubMenu ("Range", range);
    m.addItem (7, "Snap frequency to notes", true, options.snapNotes);
    SafePointer<EqGraph> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe] (int id)
    {
        if (safe == nullptr || id == 0)
            return;
        auto& o = safe->options;
        switch (id)
        {
            case 1: o.pre = ! o.pre; break;
            case 2: o.post = ! o.post; break;
            case 3: o.sc = ! o.sc; break;
            case 4: o.peakHold = ! o.peakHold; break;
            case 5: o.tilt = o.tilt > 0.0f ? 0.0f : 4.5f; break;
            case 6: o.freeze = ! o.freeze; break;
            case 7: o.snapNotes = ! o.snapNotes; break;
            case 10: case 11: case 12: o.speed = id - 10; break;
            default: if (id >= 26) o.rangeDb = (float) (id - 20); break;
        }
        if (safe->onOptionsChanged)
            safe->onOptionsChanged();
        safe->repaint();
    });
}
