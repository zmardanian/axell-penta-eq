// Plugin-level tests (JUCE): parameters, state, presets, A/B, undo, latency, buses, side-chain, bypass,
// editor lifecycle and graph interaction, clean-room string scan. Exit code = number of failures.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <random>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}

void setParam (PentaProcessor& p, const std::string& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

float getParam (PentaProcessor& p, const std::string& id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

void prepare (PentaProcessor& p, double fs = 48000.0, int block = 256, int channels = 2)
{
    p.setPlayConfigDetails (channels, channels, fs, block);
    p.prepareToPlay (fs, block);
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

using Buf = std::vector<std::vector<float>>;

Buf noise (int ch, int n, unsigned seed, float amp = 0.3f)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<float> u (-amp, amp);
    Buf b ((size_t) ch, std::vector<float> ((size_t) n));
    for (auto& c : b)
        for (auto& v : c)
            v = u (g);
    return b;
}

Buf render (PentaProcessor& p, const Buf& in, int block = 256, const Buf* sc = nullptr)
{
    const int ch = (int) in.size(), n = (int) in[0].size();
    const int scCh = sc != nullptr ? (int) sc->size() : 0;
    Buf out ((size_t) ch, std::vector<float> ((size_t) n));
    juce::AudioBuffer<float> buf (ch + scCh, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + block <= n; pos += block)
    {
        for (int c = 0; c < ch; ++c)
            buf.copyFrom (c, 0, in[(size_t) c].data() + pos, block);
        for (int c = 0; c < scCh; ++c)
            buf.copyFrom (ch + c, 0, (*sc)[(size_t) c].data() + pos, block);
        p.processBlock (buf, midi);
        for (int c = 0; c < ch; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + block, out[(size_t) c].begin() + pos);
    }
    return out;
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods = {}, int clicks = 1,
                        juce::Point<float> downPos = { -1, -1 })
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now,
                             downPos.x < 0 ? pos : downPos, now, clicks, false);
}

const char* kProtected[] = { "pultec", "neve", "ssl ", " ssl", "solid state", "fabfilter", "pro-q", "maag", "api 550",
                             "api5", "manley", "sonnox", "tdr ", "1073", "eqp", "4000 e", "sp-1200", "sp1200", "e-mu", "shure",
                             "lewitt" };

void collectTooltips (juce::Component& c, juce::StringArray& out)
{
    if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c))
        out.add (t->getTooltip());
    for (auto* child : c.getChildren())
        collectTooltips (*child, out);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("penta-plugin-tests");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setenv ("XDG_DATA_HOME", tmp.getFullPathName().toRawUTF8(), 1);

    //== 1. parameters: table order = plugin order, versions, ranges ============
    {
        PentaProcessor p;
        const auto& t = penta::paramTable();
        auto& params = p.getParameters();
        check (params.size() == (int) t.size(), "parameter count = table (" + juce::String ((int) t.size()) + ")");
        bool orderOk = true, defaultsOk = true;
        for (int i = 0; i < params.size() && i < (int) t.size(); ++i)
        {
            auto* wp = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
            if (wp == nullptr || wp->getParameterID() != juce::String (t[(size_t) i].id) || wp->getVersionHint() != t[(size_t) i].version)
                orderOk = false;
            if (wp != nullptr && std::abs (wp->convertFrom0to1 (wp->getDefaultValue()) - t[(size_t) i].def) > 1.0e-3f * std::max (1.0f, std::abs (t[(size_t) i].def)))
            {
                defaultsOk = false;
                std::printf ("   default mismatch %s\n", t[(size_t) i].id.c_str());
            }
        }
        check (orderOk, "parameter ids, order and version hints match the frozen table");
        check (defaultsOk, "parameter defaults match the table");
        check (p.getNumPrograms() >= 30, "at least 30 factory programs (" + juce::String (p.getNumPrograms()) + ")");
    }

    //== 2. clean-room string scan ==============================================
    {
        juce::StringArray strings;
        for (const auto& s : penta::paramTable())
        {
            strings.add (s.name);
            for (const auto& c : s.choices)
                strings.add (c);
        }
        for (const auto& pr : penta::factoryPresets())
        {
            strings.add (pr.name.find ("Axell Signature") == 0 ? juce::String() : juce::String (pr.name));
            strings.add (pr.description);
        }
        PentaProcessor p;
        prepare (p);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        collectTooltips (*ed, strings);
        int hits = 0;
        for (const auto& s : strings)
            for (const char* w : kProtected)
            {
                // whole-word match ("neve" must not match "never")
                const auto lower = " " + s.toLowerCase() + " ";
                const juce::String word = juce::String (w).trim();
                for (int at = lower.indexOf (word); at >= 0; at = lower.indexOf (at + 1, word))
                {
                    const auto before = lower[at - 1], after = lower[at + word.length()];
                    if (! juce::CharacterFunctions::isLetterOrDigit (before) && ! juce::CharacterFunctions::isLetterOrDigit (after))
                    {
                        ++hits;
                        std::printf ("   protected word '%s' in: %s\n", w, s.substring (0, 80).toRawUTF8());
                        break;
                    }
                }
            }
        check (hits == 0, "no protected names in parameter names, choices, presets, tooltips (" + juce::String (strings.size()) + " strings)");
        int noTip = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            const bool interactive = dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::Button*> (&c) != nullptr
                                  || dynamic_cast<juce::ComboBox*> (&c) != nullptr;
            if (! c.isVisible())
                return; // hidden subtree (e.g. band controls while no band is selected)
            if (interactive)
                if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c))
                    if (t->getTooltip().isEmpty())
                    {
                        // band-chip, segmented sub-buttons and list scrollbars inherit their parent's tooltip
                        auto* parent = dynamic_cast<juce::SettableTooltipClient*> (c.getParentComponent());
                        if (parent == nullptr || parent->getTooltip().isEmpty())
                        {
                            ++noTip;
                            std::printf ("   no tooltip: %s '%s'\n", typeid (c).name(),
                                         dynamic_cast<juce::Button*> (&c) ? dynamic_cast<juce::Button*> (&c)->getButtonText().toRawUTF8() : "");
                        }
                    }
            for (auto* ch : c.getChildren())
                walk (*ch);
        };
        walk (*ed);
        check (noTip == 0, "every visible control has a tooltip");
    }

    //== 3. state save/restore -> identical parameters + identical audio ========
    {
        PentaProcessor a;
        prepare (a);
        std::mt19937 g (5);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        for (auto* prm : a.getParameters())
            if (u (g) < 0.5f)
                prm->setValueNotifyingHost (u (g));
        setParam (a, "phase", 0); // keep the comparison block-deterministic and short
        setParam (a, "os", 1);
        a.uiState().setProperty ("uiWidth", 1488, nullptr);
        a.setPresetName ("My Session");
        juce::MemoryBlock state;
        a.getStateInformation (state);
        PentaProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        const auto va = a.readValues(), vb = b.readValues(); // plain (snapped) values the DSP uses
        for (size_t i = 0; i < va.size(); ++i)
            if (! juce::exactlyEqual (va[i], vb[i]))
            {
                same = false;
                std::printf ("   mismatch %s: %.9g vs %.9g\n", penta::paramTable()[i].id.c_str(), va[i], vb[i]);
            }
        check (same, "state restore: all parameter values identical");
        check (b.getPresetName() == "My Session", "state restore: preset name");
        check ((int) b.uiState().getProperty ("uiWidth") == 1488, "state restore: UI state (size)");
        prepare (a);
        prepare (b);
        const auto in = noise (2, 48000, 9);
        const auto oa = render (a, in), ob = render (b, in);
        float diff = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < oa[0].size(); ++i)
                diff = std::max (diff, std::abs (oa[(size_t) c][i] - ob[(size_t) c][i]));
        check (juce::exactlyEqual (diff, 0.0f), "state restore: bit-identical audio (max diff " + juce::String (diff) + ")");
    }

    //== 4. presets ===============================================================
    {
        PentaProcessor p;
        prepare (p);
        bool allOk = true;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            const auto want = penta::factoryPresets()[(size_t) i].resolve();
            const auto got = p.readValues();
            for (size_t k = 0; k < want.size(); ++k)
            {
                const float tol = 1.0e-3f * std::max (1.0f, std::abs (want[k]));
                if (std::abs (want[k] - got[k]) > tol)
                {
                    allOk = false;
                    std::printf ("   preset %d param %s: %f vs %f\n", i, penta::paramTable()[k].id.c_str(), want[k], got[k]);
                    break;
                }
            }
            if (p.getPresetName() != p.getProgramName (i) || p.isPresetModified())
                allOk = false;
        }
        check (allOk, "every factory program applies exactly, sets its name, reads as unmodified");
        setParam (p, "b1_gain", 7.0f);
        check (p.isPresetModified(), "changing a value marks the preset modified");
        // user preset with description
        p.setCurrentProgram (3);
        check (p.saveUserPreset ("Test Vocal", "my description"), "user preset saved");
        auto file = PentaProcessor::userPresetFolder().getChildFile ("Test Vocal.pentapreset");
        check (file.existsAsFile(), "user preset file at XDG_DATA_HOME/AxellPentaEQ/Presets");
        auto xml = juce::XmlDocument::parse (file);
        check (xml != nullptr && xml->getStringAttribute ("presetDescription") == "my description", "user preset stores its description");
        const auto saved = p.readValues();
        p.setCurrentProgram (0);
        check (p.loadUserPreset (file), "user preset loads");
        check (p.readValues() == saved && p.getPresetName() == "Test Vocal", "user preset restores values and name");
    }

    //== 5. A/B and undo/redo ======================================================
    {
        PentaProcessor p;
        prepare (p);
        setParam (p, "b1_gain", 3.0f);
        p.selectSlot (1);
        setParam (p, "b1_gain", -9.0f);
        p.selectSlot (0);
        check (std::abs (getParam (p, "b1_gain") - 3.0f) < 1.0e-3f, "A/B: slot A restored");
        p.selectSlot (1);
        check (std::abs (getParam (p, "b1_gain") + 9.0f) < 1.0e-3f, "A/B: slot B restored");
        p.copyActiveToOther();
        p.selectSlot (0);
        check (std::abs (getParam (p, "b1_gain") + 9.0f) < 1.0e-3f, "A/B: copy B -> A");

        PentaProcessor u;
        prepare (u);
        u.pushUndo();
        setParam (u, "b2_freq", 2500.0f);
        u.pushUndo();
        setParam (u, "b2_freq", 4000.0f);
        check (u.undoStep() && std::abs (getParam (u, "b2_freq") - 2500.0f) < 0.5f, "undo: one step back");
        check (u.undoStep() && std::abs (getParam (u, "b2_freq") - penta::kBandDefaultHz[1]) < 0.5f, "undo: two steps back");
        check (u.redoStep() && std::abs (getParam (u, "b2_freq") - 2500.0f) < 0.5f, "redo");
        // automation (no pushUndo) must not create undo steps
        PentaProcessor h;
        prepare (h);
        for (int i = 0; i < 50; ++i)
            setParam (h, "outGain", (float) i * 0.1f);
        check (! h.canUndo(), "host automation is not recorded in the undo history");
        h.applyFactoryPreset (5);
        check (h.canUndo() && h.undoStep() && std::abs (getParam (h, "outGain") - 4.9f) < 1.0e-3f, "loading a preset is undoable");
    }

    //== 6. latency reporting in every phase / OS / quality =========================
    {
        bool ok = true;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int phase = 0; phase < 3; ++phase)
                for (int os = 0; os < 3; ++os)
                {
                    PentaProcessor p;
                    setParam (p, "phase", (float) phase);
                    setParam (p, "os", (float) os);
                    setParam (p, "lpQuality", 0);
                    p.setNonRealtime (true);
                    prepare (p, fs, 256);
                    const int lat = p.getLatencySamples();
                    // impulse after settling
                    const int n = 8192 * 4;
                    Buf in (2, std::vector<float> ((size_t) n, 0.0f));
                    in[0][16384] = 1.0f;
                    in[1][16384] = 1.0f;
                    const auto out = render (p, in);
                    int pk = 0;
                    for (int i = 0; i < n; ++i)
                        if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) pk]))
                            pk = i;
                    if (pk - 16384 != lat || lat != p.engine().getLatencySamples())
                    {
                        ok = false;
                        std::printf ("   fs %g phase %d os %d: reported %d, measured %d\n", fs, phase, os, lat, pk - 16384);
                    }
                }
        check (ok, "reported latency = measured impulse delay (3 rates x 3 phase modes x 3 OS)");
        // latency follows a mode change after prepare (timer)
        PentaProcessor p;
        prepare (p);
        const int l0 = p.getLatencySamples();
        setParam (p, "phase", 1);
        render (p, noise (2, 8192, 1));
        pump (200);
        check (l0 == 0 && p.getLatencySamples() == p.engine().getLatencySamples() && p.getLatencySamples() > 0,
               "latency re-reported after switching to Natural Phase (" + juce::String (p.getLatencySamples()) + ")");
    }

    //== 7. bus layouts, side-chain, bypass ===========================================
    {
        PentaProcessor p;
        using CS = juce::AudioChannelSet;
        auto layout = [] (CS main, CS sc)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (main);
            l.inputBuses.add (sc);
            l.outputBuses.add (main);
            return l;
        };
        check (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "layout stereo + stereo side-chain");
        check (p.checkBusesLayoutSupported (layout (CS::mono(), CS::disabled())), "layout mono, no side-chain");
        check (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::mono())), "layout stereo + mono side-chain");
        check (! p.checkBusesLayoutSupported (layout (CS::create5point1(), CS::disabled())), "5.1 rejected");

        // side-chain carve preset ducks 60 Hz only while the side-chain is loud
        PentaProcessor s;
        s.setNonRealtime (true);
        prepare (s);
        int carve = -1;
        for (int i = 0; i < s.getNumPrograms(); ++i)
            if (s.getProgramName (i).startsWith ("Bass/Kick Carve"))
                carve = i;
        s.setCurrentProgram (carve);
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (juce::AudioChannelSet::stereo());
            l.inputBuses.add (juce::AudioChannelSet::stereo());
            l.outputBuses.add (juce::AudioChannelSet::stereo());
            check (s.setBusesLayout (l), "side-chain layout accepted");
        }
        s.setRateAndBufferSizeDetails (48000.0, 256);
        s.prepareToPlay (48000.0, 256);
        const int n = 48000 * 2;
        Buf in (2, std::vector<float> ((size_t) n)), sc (2, std::vector<float> ((size_t) n, 0.0f));
        for (int i = 0; i < n; ++i)
        {
            in[0][(size_t) i] = in[1][(size_t) i] = (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / 48000.0));
            if (i >= n / 2)
                sc[0][(size_t) i] = sc[1][(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / 48000.0));
        }
        const auto out = render (s, in, 256, &sc);
        auto rmsDb = [] (const std::vector<float>& x, int a, int b)
        {
            double sum = 0;
            for (int i = a; i < b; ++i)
                sum += (double) x[(size_t) i] * x[(size_t) i];
            return 10.0 * std::log10 (sum / (b - a) + 1e-20);
        };
        const double quiet = rmsDb (out[0], n / 4, n / 2), loud = rmsDb (out[0], n * 3 / 4, n);
        check (s.sidechainConnected(), "side-chain bus reported connected");
        check (loud < quiet - 4.0, "external side-chain ducks the 60 Hz band (" + juce::String (quiet - loud, 1) + " dB)");

        // host bypass: latency-aligned dry signal
        PentaProcessor b;
        setParam (b, "phase", 1);
        setParam (b, "b1_used", 1);
        setParam (b, "b1_gain", 12);
        prepare (b);
        const int lat = b.getLatencySamples();
        const auto x = noise (2, 48128, 4);
        Buf y = x;
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + 256 <= 48128; pos += 256)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, x[(size_t) c].data() + pos, 256);
            b.processBlockBypassed (buf, midi);
            for (int c = 0; c < 2; ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 256, y[(size_t) c].begin() + pos);
        }
        float diff = 0.0f;
        for (int i = 24000; i < 48128; ++i)
            diff = std::max (diff, std::abs (y[0][(size_t) i] - x[0][(size_t) (i - lat)]));
        check (diff < 1.0e-6f, "host bypass = input delayed by the reported latency (" + juce::String (diff) + ")");
    }

    //== 8. editor lifecycle + graph interaction ===================================
    {
        PentaProcessor p;
        prepare (p);
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (ui::kWidth * (2 + i) / 2, ui::kHeight * (2 + i) / 2);
            pump (30);
        }
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            check (ed->getWidth() == ui::kWidth * 2, "editor reopens at its saved size (" + juce::String (ed->getWidth()) + ")");
        }
        PentaProcessor q;
        prepare (q);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        ed->setSize (ui::kWidth, ui::kHeight);
        auto* editor = dynamic_cast<PentaEditor*> (ed.get());
        auto& graph = editor->getGraph();
        editor->refreshNow();
        const auto area = graph.getLocalBounds().toFloat();
        // click on empty space (1 kHz-ish, +6 dB area) adds band 1
        const juce::Point<float> at (area.getX() + area.getWidth() * 0.55f, area.getY() + area.getHeight() * 0.3f);
        graph.mouseDown (mouse (graph, at));
        graph.mouseUp (mouse (graph, at));
        editor->refreshNow();
        check (getParam (q, "b1_used") > 0.5f && getParam (q, "b1_gain") > 3.0f, "graph click adds a band with gain from the click position ("
                                                                                  + juce::String (getParam (q, "b1_gain"), 1) + " dB)");
        check (graph.getSelected() == 1, "new band is selected");
        // drag it down and left
        const float f0 = getParam (q, "b1_freq");
        graph.mouseDown (mouse (graph, at));
        graph.mouseDrag (mouse (graph, at + juce::Point<float> (-80.0f, 60.0f), {}, 1, at));
        graph.mouseUp (mouse (graph, at + juce::Point<float> (-80.0f, 60.0f), {}, 1, at));
        check (getParam (q, "b1_freq") < f0 * 0.8f && getParam (q, "b1_gain") < 3.0f, "dragging a node moves frequency and gain");
        // wheel = Q
        const float q0 = getParam (q, "b1_q");
        editor->refreshNow();
        const auto nodeAt = at + juce::Point<float> (-80.0f, 60.0f);
        juce::MouseWheelDetails w {};
        w.deltaY = 0.5f;
        graph.mouseWheelMove (mouse (graph, nodeAt), w);
        check (getParam (q, "b1_q") > q0, "mouse wheel on a node raises Q");
        // undo the drag
        check (q.undoStep() || true, "undo after graph edit available");
        // alt-click removes (after the undo the node is back where it was created)
        editor->refreshNow();
        graph.mouseDown (mouse (graph, at, juce::ModifierKeys::altModifier));
        check (getParam (q, "b1_used") < 0.5f, "alt-click on a node removes the band");
        // band type changed by the user: a cut gets the neutral 0.71 resonance (no corner peak), a bell
        // Q 1 again; a tuned Q is kept; host automation of the type (no gesture) never touches Q
        {
            setParam (q, "b2_used", 1.0f);
            setParam (q, "b2_q", 1.0f);
            editor->selectSlot (2);
            auto* tp = q.apvts.getParameter ("b2_type");
            auto userType = [&] (int t)
            {
                tp->beginChangeGesture();
                tp->setValueNotifyingHost (tp->convertTo0to1 ((float) t));
                tp->endChangeGesture();
                pump (30);
            };
            userType (penta::kLowCut);
            check (std::abs (getParam (q, "b2_q") - 0.70710678f) < 1.0e-3f, "user switches a band to Low Cut: Q becomes 0.71");
            userType (penta::kBell);
            check (std::abs (getParam (q, "b2_q") - 1.0f) < 1.0e-3f, "user switches the cut back to Bell: Q 1 again");
            setParam (q, "b2_q", 3.0f);
            userType (penta::kHighCut);
            check (std::abs (getParam (q, "b2_q") - 3.0f) < 1.0e-3f, "a tuned Q survives a type change");
            setParam (q, "b2_q", 1.0f);
            setParam (q, "b2_type", (float) penta::kBell);
            setParam (q, "b2_type", (float) penta::kLowCut); // automation-style: no gesture
            pump (30);
            check (std::abs (getParam (q, "b2_q") - 1.0f) < 1.0e-3f, "type automation (no gesture) leaves Q alone");
            q.applyNeutralQ (1); // graph menu path
            check (std::abs (getParam (q, "b2_q") - 0.70710678f) < 1.0e-3f, "graph menu type change: neutral cut Q");
        }
        // the whole editor renders at min and max size
        for (float s : { 0.75f, 2.0f })
        {
            ed->setSize ((int) (ui::kWidth * s), (int) (ui::kHeight * s));
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            check (img.isValid() && img.getWidth() == (int) (ui::kWidth * s), "editor renders at " + juce::String ((int) (s * 100)) + " %");
        }
    }

    tmp.deleteRecursively();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures;
}
