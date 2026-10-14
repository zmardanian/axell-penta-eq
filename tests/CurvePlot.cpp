// Renders the MEASURED frequency response of every factory preset (engine impulse response, Zero
// Latency/Natural/Linear as the preset specifies, offline) together with the analog model curve.
//   PentaCurvePlot [outputDir]      -> <dir>/<preset>.png, <dir>/contact-sheet.png, <dir>/README.md
#include "../Source/PluginProcessor.h"
#include "../Source/gui/LookAndFeel.h"

#include <complex>

namespace
{
juce::String slug (const juce::String& s)
{
    juce::String out;
    for (auto c : s.toLowerCase())
        out << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-"));
    while (out.contains ("--"))
        out = out.replace ("--", "-");
    return out.trimCharactersAtEnd ("-");
}

struct Measured
{
    std::vector<double> hz, dbL, dbR, model;
    int latency = 0;
};

Measured measure (const penta::FactoryPreset& p, double fs)
{
    Measured m;
    penta::Engine e;
    auto v = p.resolve();
    e.setNonRealtime (true);
    e.setParams (v);
    e.prepare (fs, 2, 512);
    e.setParams (v);
    e.reset();
    m.latency = e.getLatencySamples();
    const int settle = 8192 + (int) (fs * 0.5), len = 1 << 17;
    std::vector<float> l ((size_t) (settle + len), 0.0f), r = l;
    const float amp = 1.0e-3f; // -60 dBFS: below every dynamic threshold, no drive colour
    // first half: Mid impulse (L = R), the curve a centred source hears
    l[(size_t) settle] = amp;
    r[(size_t) settle] = amp;
    for (int pos = 0; pos < (int) l.size(); pos += 512)
    {
        const int n = std::min (512, (int) l.size() - pos);
        float* ch[2] = { l.data() + pos, r.data() + pos };
        e.process (ch, 2, n);
    }
    penta::dsp::RealFft<double> fft;
    fft.prepare (len);
    std::vector<double> x ((size_t) len);
    std::vector<std::complex<double>> X ((size_t) (len / 2 + 1));
    auto spectrum = [&] (const std::vector<float>& s, std::vector<double>& out)
    {
        for (int i = 0; i < len; ++i)
            x[(size_t) i] = s[(size_t) (settle + i)] / amp;
        fft.forward (x.data(), X.data());
        out.clear();
        for (double f : m.hz)
        {
            const double b = f / fs * len;
            const int k = (int) b;
            const double t = b - k;
            const double a0 = std::abs (X[(size_t) k]), a1 = std::abs (X[(size_t) std::min (k + 1, len / 2)]);
            out.push_back (20.0 * std::log10 (std::max (a0 * (1.0 - t) + a1 * t, 1.0e-9)));
        }
    };
    for (int i = 0; i < 480; ++i)
        m.hz.push_back (20.0 * std::pow (1000.0, i / 479.0));
    // Mid response = (L + R) / 2 of the Mid impulse
    std::vector<float> mid (l.size());
    for (size_t i = 0; i < l.size(); ++i)
        mid[i] = 0.5f * (l[i] + r[i]);
    spectrum (mid, m.dbL);
    // Side response: a second, fresh run with a Side impulse (L = -R), output (L - R) / 2
    {
        penta::Engine e2;
        e2.setNonRealtime (true);
        e2.setParams (v);
        e2.prepare (fs, 2, 512);
        e2.setParams (v);
        e2.reset();
        std::vector<float> l2 ((size_t) (settle + len), 0.0f), r2 = l2;
        l2[(size_t) settle] = amp;
        r2[(size_t) settle] = -amp;
        for (int pos = 0; pos < (int) l2.size(); pos += 512)
        {
            const int n = std::min (512, (int) l2.size() - pos);
            float* ch[2] = { l2.data() + pos, r2.data() + pos };
            e2.process (ch, 2, n);
        }
        std::vector<float> side (l2.size());
        for (size_t i = 0; i < l2.size(); ++i)
            side[i] = 0.5f * (l2[i] - r2[i]);
        spectrum (side, m.dbR);
    }
    penta::CurveModel cm;
    cm.build (v, fs);
    for (double f : m.hz)
        m.model.push_back (cm.totalDb (f) + v[(size_t) penta::idx().outGain]);
    return m;
}

void drawPlot (juce::Graphics& g, juce::Rectangle<float> area, const penta::FactoryPreset& p, const Measured& m, bool big)
{
    using namespace juce;
    g.setColour (ui::col::well);
    g.fillRoundedRectangle (area, 6.0f);
    auto plot = area.reduced (big ? 46.0f : 8.0f, big ? 40.0f : 22.0f).withTrimmedBottom (big ? 10.0f : 0.0f);
    if (! big)
        plot = plot.withTrimmedTop (4.0f);
    const double range = 18.0;
    auto xF = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    auto yD = [&] (double d) { return plot.getCentreY() - (float) (jlimit (-range, range, d) / range) * plot.getHeight() * 0.5f; };
    g.setFont (ui::font (big ? 11.0f : 8.0f));
    for (int f : { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 })
    {
        g.setColour (ui::col::grid);
        g.drawVerticalLine ((int) xF (f), plot.getY(), plot.getBottom());
        if (big)
        {
            g.setColour (ui::col::faint);
            g.drawText (f >= 1000 ? String (f / 1000) + "k" : String (f), Rectangle<float> (xF (f) - 20, plot.getBottom() + 4, 40, 14), Justification::centred);
        }
    }
    for (int d = -18; d <= 18; d += 6)
    {
        g.setColour (d == 0 ? Colour (0xff3a404b) : ui::col::grid);
        g.drawHorizontalLine ((int) yD (d), plot.getX(), plot.getRight());
        if (big)
        {
            g.setColour (ui::col::faint);
            g.drawText ((d > 0 ? "+" : "") + String (d), Rectangle<float> (plot.getX() - 40, yD (d) - 7, 34, 14), Justification::centredRight);
        }
    }
    auto path = [&] (const std::vector<double>& d)
    {
        Path pa;
        for (size_t i = 0; i < d.size(); ++i)
        {
            const auto pt = Point<float> (xF (m.hz[i]), yD (d[i]));
            if (i == 0)
                pa.startNewSubPath (pt);
            else
                pa.lineTo (pt);
        }
        return pa;
    };
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        Path model = path (m.model);
        float dashes[] = { 5.0f, 4.0f };
        Path dashed;
        PathStrokeType (1.2f).createDashedStroke (dashed, model, dashes, 2);
        g.setColour (ui::col::accent.withAlpha (0.9f));
        g.fillPath (dashed);
        bool stereoDiff = false;
        for (size_t i = 0; i < m.dbL.size(); ++i)
            stereoDiff = stereoDiff || std::abs (m.dbL[i] - m.dbR[i]) > 0.05;
        g.setColour (ui::col::text);
        g.strokePath (path (m.dbL), PathStrokeType (big ? 2.0f : 1.4f));
        if (stereoDiff)
        {
            g.setColour (ui::col::teal);
            g.strokePath (path (m.dbR), PathStrokeType (big ? 1.4f : 1.0f));
        }
    }
    g.setColour (ui::col::text);
    g.setFont (ui::font (big ? 15.0f : 9.5f, 1));
    g.drawText (String (p.name), area.reduced (big ? 14.0f : 6.0f, 2.0f).withHeight (big ? 26.0f : 16.0f), Justification::centredLeft);
    if (big)
    {
        g.setColour (ui::col::dim);
        g.setFont (ui::font (11.0f));
        g.drawText (String (p.category) + "   |   measured Mid (white), Side (teal, where different), model (dashed)   |   latency "
                        + String (m.latency) + " samples @ 48 kHz",
                    area.reduced (14.0f, 0.0f).withTrimmedTop (22.0f).withHeight (18.0f), Justification::centredLeft);
    }
}

bool save (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    return os.openedOk() && png.writeImageToStream (img, os);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/preset-curves"));
    dir.createDirectory();
    ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    const auto& list = penta::factoryPresets();
    const int cols = 5, rows = ((int) list.size() + cols - 1) / cols, cw = 300, chh = 170;
    juce::Image sheet (juce::Image::RGB, cols * cw, rows * chh, true);
    juce::Graphics gs (sheet);
    gs.fillAll (ui::col::bg);
    juce::String md = "# Preset frequency responses\n\nMeasured from the engine (impulse at -60 dBFS so dynamic bands sit at "
                      "their static gain and drive adds no colour; 48 kHz; the preset's own phase mode). White = Mid (what a centred source hears), "
                      "teal = Side (only drawn where it differs from Mid, i.e. presets with Mid/Side bands), dashed orange = analog model incl. "
                      "output trim. Generated by `PentaCurvePlot`.\n\n![contact sheet](contact-sheet.png)\n\n";
    double worstDev = 0.0;
    juce::String worstName;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& p = list[(size_t) i];
        const auto m = measure (p, 48000.0);
        juce::Image img (juce::Image::RGB, 900, 420, true);
        juce::Graphics g (img);
        g.fillAll (ui::col::bg);
        drawPlot (g, img.getBounds().toFloat().reduced (6.0f), p, m, true);
        const auto file = dir.getChildFile (slug (p.name) + ".png");
        save (img, file);
        drawPlot (gs, juce::Rectangle<float> ((float) ((i % cols) * cw), (float) ((i / cols) * chh), (float) cw, (float) chh).reduced (4.0f),
                  p, m, false);
        // measured Mid vs model (the model is the Mid / centred-source curve)
        double dev = 0.0;
        for (size_t k = 0; k < m.hz.size(); ++k)
            if (m.hz[k] < 20000.0 && m.model[k] > -24.0)
                dev = std::max (dev, std::abs (m.dbL[k] - m.model[k]));
        if (dev > worstDev)
        {
            worstDev = dev;
            worstName = p.name;
        }
        md << "## " << p.name << "\n\n*" << p.category << "* - " << p.description << "\n\n![" << p.name << "](" << file.getFileName()
           << ")\n\n";
        std::printf ("%-40s latency %6d  max |measured-model| %.3f dB\n", p.name.c_str(), m.latency, dev);
    }
    save (sheet, dir.getChildFile ("contact-sheet.png"));
    dir.getChildFile ("README.md").replaceWithText (md);
    std::printf ("worst measured-vs-model deviation: %.3f dB (%s)\n", worstDev, worstName.toRawUTF8());
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
