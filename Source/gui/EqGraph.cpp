#include "EqGraph.h"

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
    totalDb.assign (kPoints, 0.0f);
    totalStaticDb.assign (kPoints, 0.0f);
    moduleDb.assign (kPoints, 0.0f);
    dispPre.assign (kPoints, -200.0f);
    dispPost.assign (kPoints, -200.0f);
    dispSc.assign (kPoints, -200.0f);
    dispPeak.assign (kPoints, -200.0f);
    fifoTmp.resize (16384);
    setTooltip ("Click empty space to add a band. Drag a dot to move it (cuts: up/down = resonance). "
                "Mouse wheel on a dot = Q (Shift = fine). Double-click = gain to 0. Alt+click = remove. "
                "Right-click a dot for type, slope, placement and more.");
    proc.engine().setAnalyzerEnabled (true);
}

EqGraph::~EqGraph()
{
    proc.engine().setAnalyzerEnabled (false);
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().reduced (2.0f).withTrimmedLeft (30.0f).withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
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
    if (slot == 0 || slot == penta::kNumSlots - 1)
        return getParam (slotParam (slot, "used")) > 0.5f;
    return getParam (slotParam (slot, "used")) > 0.5f;
}

bool EqGraph::isGainType (int slot) const
{
    if (slot == 0 || slot == penta::kNumSlots - 1)
        return false;
    return penta::dsp::isGainType ((int) std::lround (getParam (slotParam (slot, "type"))));
}

juce::Point<float> EqGraph::nodePos (int slot) const
{
    const double f = getParam (slotParam (slot, "freq"));
    double db = 0.0;
    if (isGainType (slot))
    {
        const int type = (int) std::lround (getParam (slotParam (slot, "type")));
        db = type == penta::kTilt ? 0.0 : getParam (slotParam (slot, "gain"));
        if (slot >= 1 && slot <= penta::kNumBands)
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
        buildDisplay (anPre, dispPre, nullptr, rel);
    if (anPost.compute())
        buildDisplay (anPost, dispPost, &dispPeak, rel);
    scActive = proc.sidechainConnected();
    if (scActive && anSc.compute())
        buildDisplay (anSc, dispSc, nullptr, rel);
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
    for (int b = 0; b < penta::kNumBands; ++b)
        delta[(size_t) b] = proc.meters.delta[b].load (std::memory_order_relaxed);
    fs = proc.engine().getSampleRate();
    curves.build (values, fs, delta.data());
    curvesStatic.build (values, fs);
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
    }
    pullAnalyzers();
    repaint();
}

void EqGraph::setSelected (int slot)
{
    selected = slot;
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
        if (options.pre)
        {
            g.setColour (Colour (0x18c3c8d2));
            g.fillPath (specPath (dispPre, true));
        }
        if (options.post)
        {
            auto p = specPath (dispPost, true);
            g.setColour (col::teal.withAlpha (0.07f));
            g.fillPath (p);
            g.setColour (col::teal.withAlpha (0.38f));
            g.strokePath (specPath (dispPost, false), PathStrokeType (1.0f));
            if (options.peakHold)
            {
                g.setColour (col::text.withAlpha (0.25f));
                g.strokePath (specPath (dispPeak, false), PathStrokeType (0.8f));
            }
        }
        if (options.sc && scActive)
        {
            g.setColour (col::warn.withAlpha (0.6f));
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
            g.setColour (c.withAlpha (s == selected ? 0.24f : 0.10f));
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
            g.setColour (c.withAlpha (s == selected ? 0.95f : 0.45f));
            g.strokePath (active, PathStrokeType (s == selected ? 1.6f : 1.0f));
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
        g.setColour (col::text);
        g.strokePath (curvePath (totalDb), PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // nodes
    for (int s = 0; s < penta::kNumSlots; ++s)
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
            const float g0 = getParam (slotParam (s, "gain")), rg = getParam (slotParam (s, "range"));
            const float y0 = yForDb (jlimit (-r, r, g0)), y1 = yForDb (jlimit (-r, r, g0 + rg));
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (pos.x - 2.0f, std::min (y0, y1), 4.0f, std::abs (y1 - y0), 2.0f);
        }
        const float rad = s == selected ? 8.5f : (s == hover ? 8.0f : 7.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (pos.x - rad - 1.0f, pos.y - rad + 1.0f, 2 * rad + 2.0f, 2 * rad + 2.0f);
        g.setColour (byp ? col::faint : c);
        g.fillEllipse (pos.x - rad, pos.y - rad, 2 * rad, 2 * rad);
        if (s == selected)
        {
            g.setColour (col::text);
            g.drawEllipse (pos.x - rad - 2.5f, pos.y - rad - 2.5f, 2 * rad + 5.0f, 2 * rad + 5.0f, 1.4f);
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
            if (place > 0)
            {
                g.setColour (c);
                g.setFont (font (9.0f, 2));
                g.drawText (kPlaceShort[place], Rectangle<float> (pos.x + rad, pos.y - rad - 8.0f, 14.0f, 10.0f), Justification::centredLeft);
            }
        }
    }

    // hover / drag readout
    const int info = dragging >= 0 ? dragging : hover;
    if (info >= 0 && slotActive (info))
    {
        const auto pos = nodePos (info);
        const double f = getParam (slotParam (info, "freq"));
        String t = hzText (f);
        if (isGainType (info))
            t << "   " << (getParam (slotParam (info, "gain")) >= 0 ? "+" : "") << String (getParam (slotParam (info, "gain")), 1) << " dB";
        t << "   Q " << String (getParam (slotParam (info, "q")), 2);
        if (info >= 1 && info <= penta::kNumBands)
            t = String (kTypeShort[(int) std::lround (getParam (slotParam (info, "type")))]) + "   " + t;
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
    g.setColour (col::track);
    g.drawRoundedRectangle (full.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

int EqGraph::addBandAt (juce::Point<float> p)
{
    for (int b = 1; b <= penta::kNumBands; ++b)
    {
        if (getParam (slotParam (b, "used")) > 0.5f)
            continue;
        proc.pushUndo();
        const double f = freqForX (p.x);
        const double g = juce::jlimit (-30.0, 30.0, dbForY (p.y));
        setParam (slotParam (b, "type"), (float) penta::kBell);
        setParam (slotParam (b, "freq"), (float) f);
        setParam (slotParam (b, "gain"), (float) (std::abs (g) < 0.5 ? 0.0 : g));
        setParam (slotParam (b, "q"), 1.0f);
        setParam (slotParam (b, "byp"), 0.0f);
        setParam (slotParam (b, "used"), 1.0f);
        return b;
    }
    return -1;
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    dragMoved = false;
    if (h < 0)
    {
        dragging = -1;
        return;
    }
    if (e.mods.isPopupMenu())
    {
        setSelected (h);
        if (onSelectFn)
            onSelectFn (h);
        showNodeMenu (h);
        return;
    }
    if (e.mods.isAltDown() && h >= 1 && h <= penta::kNumBands)
    {
        proc.pushUndo();
        setParam (slotParam (h, "used"), 0.0f);
        if (onSelectFn)
            onSelectFn (-1);
        return;
    }
    proc.pushUndo();
    dragging = h;
    dragStart = e.position;
    dragF0 = getParam (slotParam (h, "freq"));
    dragG0 = isGainType (h) ? getParam (slotParam (h, "gain")) : 0.0;
    dragQ0 = getParam (slotParam (h, "q"));
    for (const char* w : { "freq", "gain", "q" })
    {
        const auto id = slotParam (h, w);
        if (! id.empty() && penta::paramIndex (id) >= 0)
            proc.param (penta::paramIndex (id))->beginChangeGesture();
    }
    setSelected (h);
    if (onSelectFn)
        onSelectFn (h);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto d = (e.position - dragStart) * fine;
    if (d.getDistanceFromOrigin() > 1.0f)
        dragMoved = true;
    const double f = freqForX (xForFreq (dragF0) + d.x);
    setParam (slotParam (dragging, "freq"), (float) f);
    if (isGainType (dragging))
    {
        const int type = (int) std::lround (getParam (slotParam (dragging, "type")));
        if (type != penta::kTilt || true)
            setParam (slotParam (dragging, "gain"), (float) juce::jlimit (-30.0, 30.0, dragG0 - d.y / (plot.getHeight() * 0.5f) * options.rangeDb));
    }
    else
        setParam (slotParam (dragging, "q"), (float) juce::jlimit (0.1, 24.0, dragQ0 * std::pow (2.0, -d.y / 60.0)));
    refresh();
}

void EqGraph::mouseUp (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        for (const char* w : { "freq", "gain", "q" })
        {
            const auto id = slotParam (dragging, w);
            if (! id.empty() && penta::paramIndex (id) >= 0)
                proc.param (penta::paramIndex (id))->endChangeGesture();
        }
        dragging = -1;
        repaint();
        return;
    }
    // a click (no drag) on empty space adds a band
    if (! e.mods.isPopupMenu() && e.getDistanceFromDragStart() < 3 && plot.contains (e.position)
        && e.getNumberOfClicks() == 1)
    {
        const int b = addBandAt (e.position);
        if (b > 0)
        {
            refresh();
            setSelected (b);
            if (onSelectFn)
                onSelectFn (b);
        }
    }
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 1 && h <= penta::kNumBands && isGainType (h))
    {
        proc.pushUndo();
        setParam (slotParam (h, "gain"), 0.0f);
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int h = hitNode (e.position);
    if (h < 0)
        h = selected;
    if (h < 0 || ! slotActive (h))
        return;
    const auto id = slotParam (h, "q");
    const float q = getParam (id);
    const float step = (e.mods.isShiftDown() ? 0.02f : 0.12f) * (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    const int idx = penta::paramIndex (id);
    auto* p = proc.param (idx);
    p->beginChangeGesture();
    setParam (id, juce::jlimit (0.1f, 24.0f, q * std::pow (2.0f, step * 4.0f)));
    p->endChangeGesture();
    refresh();
}

//==============================================================================
void EqGraph::showNodeMenu (int slot)
{
    using namespace juce;
    PopupMenu m, type, slope, place, mode;
    const bool band = slot >= 1 && slot <= penta::kNumBands;
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
        m.addItem (500, "Dynamic", isGainType (slot), getParam (slotParam (slot, "dyn")) > 0.5f);
        m.addItem (501, "Bypass", true, getParam (slotParam (slot, "byp")) > 0.5f);
        m.addItem (502, "Reset gain to 0 dB", isGainType (slot));
    }
    m.addSeparator();
    m.addItem (503, band ? "Remove band" : "Turn off");
    SafePointer<EqGraph> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (Desktop::getMousePosition(), Desktop::getMousePosition())),
                     [safe, slot, band] (int id)
    {
        if (safe == nullptr || id == 0)
            return;
        auto& self = *safe;
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
        else if (id == 501)
            self.setParam (self.slotParam (slot, "byp"), self.getParam (self.slotParam (slot, "byp")) > 0.5f ? 0.0f : 1.0f);
        else if (id == 502)
            self.setParam (self.slotParam (slot, "gain"), 0.0f);
        else if (id == 503)
        {
            self.setParam (self.slotParam (slot, "used"), 0.0f);
            if (self.onSelectFn)
                self.onSelectFn (-1);
        }
        (void) band;
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
    speed.addItem (10, "Slow", true, options.speed == 0);
    speed.addItem (11, "Medium", true, options.speed == 1);
    speed.addItem (12, "Fast", true, options.speed == 2);
    m.addSubMenu ("Speed", speed);
    m.addSeparator();
    m.addSectionHeader ("EQ SCALE");
    for (int rr : { 6, 12, 18, 30 })
        range.addItem (20 + rr, "+-" + String (rr) + " dB", true, std::abs (options.rangeDb - (float) rr) < 0.1f);
    m.addSubMenu ("Range", range);
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
            case 10: case 11: case 12: o.speed = id - 10; break;
            default: if (id >= 26) o.rangeDb = (float) (id - 20); break;
        }
        if (safe->onOptionsChanged)
            safe->onOptionsChanged();
        safe->repaint();
    });
}
