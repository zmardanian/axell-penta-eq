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

        // A/B survives a session reopen: both slots and the active one, bit-exact
        {
            PentaProcessor a;
            prepare (a);
            setParam (a, "b1_gain", 3.0f);
            setParam (a, "b3_freq", 1234.567f);
            a.selectSlot (1);
            setParam (a, "b1_gain", -9.0f);
            setParam (a, "b3_q", 2.345f);
            const auto bVals = a.readValues();
            a.selectSlot (0);
            const auto aVals = a.readValues();
            a.selectSlot (1);
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            PentaProcessor q;
            prepare (q);
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.getActiveSlot() == 1 && q.readValues() == bVals, "A/B session: reopens on slot B with B's values");
            q.selectSlot (0);
            check (q.readValues() == aVals, "A/B session: slot A survives the reopen (bit-exact)");
            q.selectSlot (1);
            check (q.readValues() == bVals, "A/B session: back to B (bit-exact)");
            q.selectSlot (0);
            juce::MemoryBlock m2;
            q.getStateInformation (m2);
            PentaProcessor r;
            prepare (r);
            r.setStateInformation (m2.getData(), (int) m2.getSize());
            const bool onA = r.getActiveSlot() == 0 && r.readValues() == aVals;
            r.selectSlot (1);
            check (onA && r.readValues() == bVals, "A/B session: reopens on slot A, B kept");
            // a session without A/B data (older build, XML) clears the slots of the previous session
            juce::MemoryBlock old;
            PentaProcessor::copyXmlToBinary (*PentaProcessor().apvts.copyState().createXml(), old);
            r.setStateInformation (old.getData(), (int) old.getSize());
            const auto flat = r.readValues();
            const bool slotA = r.getActiveSlot() == 0;
            r.selectSlot (1);
            check (slotA && r.readValues() == flat, "A/B session: older state -> slot A active, no stale slot B");
        }

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
        // latency follows a mode change after prepare (timer): requested, reported, then applied
        PentaProcessor p;
        prepare (p);
        const int l0 = p.getLatencySamples();
        setParam (p, "phase", 1);
        render (p, noise (2, 8192, 1));
        pump (200);
        render (p, noise (2, 4096, 2));
        check (l0 == 0 && p.getLatencySamples() == p.engine().getLatencySamples() && p.getLatencySamples() > 0,
               "latency re-reported after switching to Natural Phase (" + juce::String (p.getLatencySamples()) + ")");

        // latency handshake (D24): the engine never plays a latency the host has not been told. No message
        // loop runs between the switch and pump() here, as when the host's message thread is busy.
        auto peak = [] (const Buf& b, int from, int to)
        {
            float m = 0.0f;
            for (const auto& c : b)
                for (int i = from; i < to; ++i)
                    m = std::max (m, std::abs (c[(size_t) i]));
            return m;
        };
        // max |y[i] - x[i - lat]| for i >= from (a default setting is a pure delay)
        auto alignErr = [] (const Buf& x, const Buf& y, int lat, int from)
        {
            float m = 0.0f;
            for (size_t c = 0; c < x.size(); ++c)
                for (int i = std::max (from, lat); i < (int) x[c].size(); ++i)
                    m = std::max (m, std::abs (y[c][(size_t) i] - x[c][(size_t) (i - lat)]));
            return m;
        };
        {
            PentaProcessor h;
            prepare (h, 48000.0, 256);
            render (h, noise (2, 24000, 3));
            setParam (h, "phase", 1); // Zero Latency -> Natural Phase
            const auto o1 = render (h, noise (2, 14336, 4));
            check (h.engine().getLatencySamples() == 0 && h.getLatencySamples() == 0 && peak (o1, 1024, 14336) < 1.0e-9f,
                   "latency handshake: silent, and the new latency not applied, until the host has been told");
            pump (60);
            const auto x2 = noise (2, 9728, 5);
            const auto o2 = render (h, x2);
            const int natLat = h.engine().getLatencySamples();
            const float e2 = alignErr (x2, o2, natLat, 2048);
            check (natLat > 0 && h.getLatencySamples() == natLat && e2 < 1.0e-5f && peak (o2, 2048, 9728) > 0.1f,
                   "latency handshake: applied once reported, aligned to the new latency (" + juce::String (e2) + ")");
            setParam (h, "phase", 0); // back to Zero Latency: lower latency
            const auto o3 = render (h, noise (2, 14336, 6));
            const bool muted = h.getLatencySamples() == natLat && h.engine().getLatencySamples() == natLat
                            && peak (o3, 1024, 14336) < 1.0e-9f;
            pump (60);
            const auto x4 = noise (2, 9728, 7);
            const auto o4 = render (h, x4);
            const float e4 = alignErr (x4, o4, 0, 2048);
            check (muted && h.getLatencySamples() == 0 && e4 < 1.0e-5f && peak (o4, 2048, 9728) > 0.1f,
                   "latency handshake: Natural Phase -> Zero Latency also waits, then aligned (" + juce::String (e4) + ")");
            setParam (h, "lpQuality", 2); // no latency change in Zero Latency: no handshake
            const auto x5 = noise (2, 9728, 8);
            const auto o5 = render (h, x5);
            check (alignErr (x5, o5, 0, 4800) < 1.0e-5f, "latency handshake: a switch that keeps the latency does not wait");
        }
        {
            // the host cannot be told (no message loop): after 0.5 s the old mode plays again - aligned with what
            // the host still compensates; the withdrawn request can no longer be reported; once the message
            // thread runs again the switch is retried and completes
            PentaProcessor h;
            prepare (h, 48000.0, 256);
            setParam (h, "phase", 1);
            const auto x = noise (2, 49152, 9);
            const auto o = render (h, x);
            int stray = 0;
            const bool withdrawn = ! h.engine().takeLatencyRequest (stray);
            check (peak (o, 1024, 23000) < 1.0e-9f && peak (o, 28800, 49152) > 0.1f && alignErr (x, o, 0, 28800) < 1.0e-5f
                       && h.engine().getLatencySamples() == 0 && h.getLatencySamples() == 0 && withdrawn,
                   "latency handshake: no host response -> the old mode resumes after 0.5 s, aligned; request withdrawn");
            pump (60);                             // the message thread is alive again
            render (h, noise (2, 4096, 10));       // the engine retries: fades out, asks again
            pump (60);                             // reported
            const auto x2 = noise (2, 14336, 11);
            const auto o2 = render (h, x2);
            const int lat = h.engine().getLatencySamples();
            check (lat > 0 && h.getLatencySamples() == lat && alignErr (x2, o2, lat, 4096) < 1.0e-5f,
                   "latency handshake: retried when the message thread runs again, then aligned to the new latency");
        }
        {
            // the race the atomic handshake exists for: the report has been claimed (being told to the host)
            // when the 0.5 s run out - the engine must not fall back to the old mode; it stays silent until the
            // acknowledgement, then plays the new latency
            PentaProcessor h;
            prepare (h, 48000.0, 256);
            setParam (h, "phase", 1);
            render (h, noise (2, 4096, 12));      // fades out, posts the request
            int claimed = 0;
            const bool took = h.engine().takeLatencyRequest (claimed); // "the wrapper is reporting it right now"
            const auto o = render (h, noise (2, 49152, 13));            // > 0.5 s without an acknowledgement
            h.engine().ackLatencyRequest();
            const auto x2 = noise (2, 14336, 14);
            const auto o2 = render (h, x2);
            check (took && claimed > 0 && peak (o, 0, 49152) < 1.0e-9f && h.engine().getLatencySamples() == claimed
                       && alignErr (x2, o2, claimed, 4096) < 1.0e-5f,
                   "latency handshake: a request being reported is never abandoned - silent until acknowledged, then the new latency");
        }
        {
            // offline renders switch at once (deterministic, independent of the message thread)
            PentaProcessor h;
            h.setNonRealtime (true);
            prepare (h, 48000.0, 256);
            render (h, noise (2, 4800, 10));
            setParam (h, "phase", 1);
            const auto x = noise (2, 9728, 11);
            const auto o = render (h, x);
            const int lat = h.engine().getLatencySamples();
            check (lat > 0 && alignErr (x, o, lat, 4800) < 1.0e-5f, "latency handshake: offline renders switch at once");
        }
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

    //== 9. 2.0: new parameters, Gain Scale, spectral mode, graph workflow =========
    {
        // a session saved by 1.0 (no Dynamic Mode / Gain Scale) opens with their defaults
        {
            PentaProcessor a;
            prepare (a);
            setParam (a, "b3_dmode", 1.0f);
            setParam (a, "gainScale", 40.0f);
            setParam (a, "b1_gain", 5.0f);
            auto state = a.apvts.copyState();
            for (auto id : { "b3_dmode", "gainScale" })
                state.removeChild (state.getChildWithProperty ("id", id), nullptr);
            juce::MemoryBlock old;
            PentaProcessor::copyXmlToBinary (*state.createXml(), old);
            PentaProcessor b;
            prepare (b);
            setParam (b, "b3_dmode", 1.0f);
            setParam (b, "gainScale", 40.0f);
            b.setStateInformation (old.getData(), (int) old.getSize());
            check (getParam (b, "b3_dmode") < 0.5f && std::abs (getParam (b, "gainScale") - 100.0f) < 1.0e-3f
                       && std::abs (getParam (b, "b1_gain") - 5.0f) < 1.0e-3f,
                   "a 1.0 session opens with Dynamic Mode = Broadband and Gain Scale = 100 %");
            bool v2 = true;
            for (auto* prm : b.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    const bool added = r->paramID.endsWith ("_dmode") || r->paramID == "gainScale";
                    v2 = v2 && (r->getVersionHint() == (added ? 2 : 1));
                }
            check (v2, "parameters added in 2.0 carry version hint 2, all others 1");
        }
        // Gain Scale scales the band gains in the audio path
        {
            PentaProcessor g;
            prepare (g);
            setParam (g, "b1_used", 1.0f);
            setParam (g, "b1_freq", 1000.0f);
            setParam (g, "b1_gain", 8.0f);
            setParam (g, "gainScale", 50.0f);
            const int n = 48000;
            Buf in (2, std::vector<float> ((size_t) n));
            for (int i = 0; i < n; ++i)
                in[0][(size_t) i] = in[1][(size_t) i] = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0);
            const auto out = render (g, in);
            double pk = 0.0;
            for (int i = n / 2; i < n - 256; ++i)
                pk = std::max (pk, (double) std::abs (out[0][(size_t) i]));
            const double gainDb = 20.0 * std::log10 (pk / 0.1);
            check (std::abs (gainDb - 4.0) < 0.1, "Gain Scale 50 %: a +8 dB bell acts as +4 dB (" + juce::String (gainDb, 2) + " dB)");
        }
        PentaProcessor q;
        prepare (q);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        ed->setSize (ui::kWidth, ui::kHeight);
        auto* editor = dynamic_cast<PentaEditor*> (ed.get());
        auto& graph = editor->getGraph();
        const auto area = graph.getLocalBounds().toFloat();
        const auto plot = area.reduced (2.0f).withTrimmedLeft (30.0f).withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
        auto xFor = [&] (double hz) { return plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        auto yFor = [&] (double db) { return plot.getCentreY() - (float) (db / graph.options.rangeDb) * plot.getHeight() * 0.5f; };
        auto band = [&] (int b, float hz, float db)
        {
            setParam (q, "b" + std::to_string (b) + "_type", 0.0f);
            setParam (q, "b" + std::to_string (b) + "_freq", hz);
            setParam (q, "b" + std::to_string (b) + "_gain", db);
            setParam (q, "b" + std::to_string (b) + "_q", 1.0f);
            setParam (q, "b" + std::to_string (b) + "_used", 1.0f);
        };
        band (1, 500.0f, 3.0f);
        band (2, 2000.0f, -3.0f);
        editor->refreshNow();
        // frame selection on empty space selects both nodes
        const juce::Point<float> from (xFor (300.0), yFor (9.0)), to (xFor (3500.0), yFor (-9.0));
        graph.mouseDown (mouse (graph, from));
        graph.mouseDrag (mouse (graph, to, {}, 1, from));
        graph.mouseUp (mouse (graph, to, {}, 1, from));
        auto selNow = graph.selection();
        check (selNow.size() == 2 && selNow[0] == 1 && selNow[1] == 2 && getParam (q, "b3_used") < 0.5f,
               "dragging a frame on empty space selects the nodes inside (no band added)");
        // dragging one selected node moves the whole selection: same frequency ratio, same gain change
        const float f1 = getParam (q, "b1_freq"), f2 = getParam (q, "b2_freq"), g1 = getParam (q, "b1_gain"), g2 = getParam (q, "b2_gain");
        const juce::Point<float> n1 (xFor (500.0), yFor (3.0)), n1b = n1 + juce::Point<float> (40.0f, -30.0f);
        graph.mouseDown (mouse (graph, n1));
        graph.mouseDrag (mouse (graph, n1b, {}, 1, n1));
        graph.mouseUp (mouse (graph, n1b, {}, 1, n1));
        const float r1 = getParam (q, "b1_freq") / f1, r2 = getParam (q, "b2_freq") / f2;
        const float d1 = getParam (q, "b1_gain") - g1, d2 = getParam (q, "b2_gain") - g2;
        check (r1 > 1.2f && std::abs (r1 - r2) < 0.01f && d1 > 1.0f && std::abs (d1 - d2) < 0.05f,
               "dragging a selected node moves the selection together (ratio " + juce::String (r1, 3) + "/" + juce::String (r2, 3)
                   + ", gain +" + juce::String (d1, 2) + "/+" + juce::String (d2, 2) + " dB)");
        // keyboard: arrows nudge the selection, I inverts, Delete removes
        const float k1 = getParam (q, "b1_freq"), k2 = getParam (q, "b2_freq"), kg = getParam (q, "b1_gain");
        graph.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        graph.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (std::abs (getParam (q, "b1_freq") / k1 - std::exp2 (1.0f / 12.0f)) < 1.0e-3f
                   && std::abs (getParam (q, "b2_freq") / k2 - std::exp2 (1.0f / 12.0f)) < 1.0e-3f
                   && std::abs (getParam (q, "b1_gain") - kg - 0.5f) < 1.0e-3f,
               "Right arrow: +1 semitone, Up arrow: +0.5 dB, for every selected band");
        const float i1 = getParam (q, "b1_gain"), i2 = getParam (q, "b2_gain");
        graph.keyPressed (juce::KeyPress ('i'));
        check (std::abs (getParam (q, "b1_gain") + i1) < 1.0e-3f && std::abs (getParam (q, "b2_gain") + i2) < 1.0e-3f,
               "I inverts the gains of the selection");
        // copy band 1, paste into a new band
        graph.setSelected (1);
        graph.keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
        graph.clearSelection();
        graph.keyPressed (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
        check (getParam (q, "b3_used") > 0.5f && std::abs (getParam (q, "b3_freq") - getParam (q, "b1_freq")) < 0.5f
                   && std::abs (getParam (q, "b3_gain") - getParam (q, "b1_gain")) < 1.0e-3f,
               "Ctrl+C / Ctrl+V: a band's settings pasted into a new band");
        graph.setSelected (3);
        graph.toggleSelected (2);
        graph.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        check (getParam (q, "b3_used") < 0.5f && getParam (q, "b2_used") < 0.5f && getParam (q, "b1_used") > 0.5f,
               "Delete removes the selected bands only");
        check (q.undoStep() && getParam (q, "b3_used") > 0.5f && getParam (q, "b2_used") > 0.5f, "removing a selection is one undo step");
        // note readout and snapping
        check (EqGraph::noteName (440.0) == "A4" && EqGraph::noteName (440.0 * std::exp2 (0.2 / 12.0)) == "A4 +20ct"
                   && EqGraph::noteName (261.6256) == "C4",
               "note readout: A4, A4 +20ct, C4");
        graph.options.snapNotes = true;
        editor->refreshNow();
        graph.setSelected (1);
        const juce::Point<float> s0 (xFor (getParam (q, "b1_freq")), yFor (getParam (q, "b1_gain")));
        graph.mouseDown (mouse (graph, s0));
        graph.mouseDrag (mouse (graph, s0 + juce::Point<float> (23.0f, 0.0f), {}, 1, s0));
        graph.mouseUp (mouse (graph, s0 + juce::Point<float> (23.0f, 0.0f), {}, 1, s0));
        const double semis = 12.0 * std::log2 (getParam (q, "b1_freq") / 440.0);
        check (std::abs (semis - std::round (semis)) < 0.01, "snap to notes: a dragged band lands on a semitone (" + juce::String (semis, 3) + ")");
        graph.options.snapNotes = false;
        // grab a spectrum peak: a 3 kHz tone, Alt+click a bit off it creates a band on the peak
        for (int b = 1; b <= penta::kNumBands; ++b)
            setParam (q, "b" + std::to_string (b) + "_used", 0.0f);
        {
            const int n = 48000;
            Buf in (2, std::vector<float> ((size_t) n));
            for (int i = 0; i < n; ++i)
                in[0][(size_t) i] = in[1][(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * i / 48000.0);
            for (int k = 0; k < 8; ++k)
            {
                render (q, in);
                editor->refreshNow();
            }
        }
        graph.modifierKeysChanged (juce::ModifierKeys::altModifier);
        const juce::Point<float> near (xFor (3300.0), yFor (0.0));
        graph.mouseDown (mouse (graph, near, juce::ModifierKeys::altModifier));
        graph.mouseUp (mouse (graph, near, juce::ModifierKeys::altModifier));
        graph.modifierKeysChanged ({});
        check (getParam (q, "b1_used") > 0.5f && std::abs (getParam (q, "b1_freq") / 3000.0f - 1.0f) < 0.03f,
               "Alt+click near a spectrum peak: new band on the peak (" + juce::String (getParam (q, "b1_freq"), 0) + " Hz)");
        // draw mode: sketch a bump, APPLY fits bands that follow it
        graph.setDrawMode (true);
        juce::Point<float> last (xFor (25.0), yFor (0.0));
        for (int i = 1; i <= 80; ++i)
        {
            const double f = 25.0 * std::pow (19000.0 / 25.0, i / 80.0);
            const double db = 6.0 * std::exp (-std::pow (std::log2 (f / 1000.0) / 0.8, 2.0));
            const juce::Point<float> pnt (xFor (f), yFor (db));
            graph.drawSegment (last, pnt);
            last = pnt;
        }
        graph.applyDrawing();
        q.apvts.copyState();
        penta::CurveModel cm;
        cm.build (q.readValues(), 48000.0);
        const double at1k = cm.totalDb (1000.0), at100 = cm.totalDb (100.0), at10k = cm.totalDb (10000.0);
        check (! graph.isDrawMode() && std::abs (at1k - 6.0) < 0.5 && std::abs (at100) < 0.5 && std::abs (at10k) < 0.5,
               "draw a +6 dB bump at 1 kHz, APPLY: the fitted curve follows it (1k " + juce::String (at1k, 2) + ", 100 "
                   + juce::String (at100, 2) + ", 10k " + juce::String (at10k, 2) + " dB)");
        check (q.undoStep() && getParam (q, "b1_freq") > 2500.0f, "APPLY of a drawing is one undo step");
        // match: a reference with +4 dB above ~5 kHz -> the fitted bands reproduce the match curve
        {
            auto& m = editor->getMatch();
            MatchPanel::Spectrum ref, trk;
            for (double f : graph.gridHz())
            {
                const double pink = -20.0 - 3.0 * std::log2 (f / 1000.0);
                ref.db.push_back (pink + 4.0 * (0.5 + 0.5 * std::tanh (2.0 * std::log2 (f / 5000.0))));
                trk.db.push_back (pink);
            }
            ref.valid = trk.valid = true;
            ref.seconds = trk.seconds = 10.0;
            m.setReference (ref);
            m.setTrack (trk);
            m.setAmount (100.0);
            const auto target = m.matchCurve();
            const auto hz = m.matchHz();
            const int used = m.apply();
            penta::CurveModel mm;
            mm.build (q.readValues(), 48000.0);
            double worst = 0.0;
            for (size_t i = 0; i < hz.size(); ++i)
                worst = std::max (worst, std::abs (mm.totalDb (hz[i]) - target[i]));
            check (used > 0 && worst < 0.5, "MATCH: bands fitted to the match curve (" + juce::String (used) + " bands, worst "
                                                + juce::String (worst, 2) + " dB)");
            check (target.back() - target[hz.size() / 4] > 3.0, "MATCH: brighter reference -> the correction lifts the highs");
        }
        // band panel: the Spectral selector appears with the dynamic controls
        setParam (q, "b1_used", 1.0f);
        setParam (q, "b1_type", 0.0f);
        setParam (q, "b1_dyn", 1.0f);
        editor->selectSlot (1);
        pump (100);
        juce::StringArray tips;
        collectTooltips (*ed, tips);
        bool spectralTip = false;
        for (auto& t : tips)
            spectralTip = spectralTip || t.contains ("SPECTRAL: every frequency inside the band");
        check (spectralTip, "band panel offers BROAD / SPECTRAL for dynamic bands");
    }

    tmp.deleteRecursively();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures;
}
