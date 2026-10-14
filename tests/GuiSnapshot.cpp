// Headless GUI snapshot tool: builds the real processor + editor, feeds programme material so the
// analyzer and meters move, and saves PNGs.   PentaGuiSnapshot [outputDir]
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <functional>
#include <random>

namespace
{
void feed (PentaProcessor& proc, PentaEditor* editor, double fs, int seconds)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 g (3);
    std::normal_distribution<float> n (0.0f, 1.0f);
    float lp1 = 0, lp2 = 0;
    long t = 0;
    for (int blk = 0; blk < (int) (fs * seconds / 512); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++t)
        {
            // pink-ish noise + a kick-like low thump + a vocal-ish formant tone
            const float w = n (g) * 0.08f;
            lp1 += 0.05f * (w - lp1);
            lp2 += 0.3f * (w - lp2);
            const double ph = std::fmod ((double) t / fs, 0.5);
            const float kick = (float) (0.5 * std::exp (-ph * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * ph));
            const float voc = (float) (0.08 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) t / fs)
                                       * (0.6 + 0.4 * std::sin ((double) t / fs * 3.0)));
            const float x = lp1 * 2.0f + lp2 + kick + voc;
            buf.setSample (0, i, x);
            buf.setSample (1, i, x * 0.9f + w * 0.2f);
        }
        proc.processBlock (buf, midi);
        if (editor != nullptr && blk % 3 == 0)
            editor->refreshNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

void shoot (const juce::File& dir, const juce::String& name, float uiScale, float renderScale,
            const std::function<void (PentaProcessor&)>& setup, const std::function<void (PentaEditor&)>& after = nullptr)
{
    PentaProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setup (proc);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<PentaEditor*> (ed.get());
    ed->setSize (juce::roundToInt (ui::kWidth * uiScale), juce::roundToInt (ui::kHeight * uiScale));
    feed (proc, editor, 48000.0, 3);
    if (after)
        after (*editor);
    editor->refreshNow();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, renderScale);
    const auto file = dir.getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    if (os.openedOk() && png.writeImageToStream (img, os))
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    else
        std::printf ("FAILED to write %s\n", file.getFullPathName().toRawUTF8());
}

int presetIndex (const std::string& name)
{
    const auto& l = penta::factoryPresets();
    for (int i = 0; i < (int) l.size(); ++i)
        if (l[(size_t) i].name == name)
            return i;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/screenshots"));
    dir.createDirectory();
    auto preset = [] (const std::string& n) { return [n] (PentaProcessor& p) { p.applyFactoryPreset (presetIndex (n)); }; };
    shoot (dir, "penta-100pct", 1.0f, 1.0f, preset ("Axell Signature - Booth Fix"), [] (PentaEditor& e) { e.selectSlot (2); });
    shoot (dir, "penta-75pct-min", 0.75f, 1.0f, preset ("Rap Lead - Clean & Forward"), [] (PentaEditor& e) { e.selectSlot (3); });
    shoot (dir, "penta-150pct", 1.5f, 1.0f, preset ("Drum Bus - Boom Bap Thump"), [] (PentaEditor& e) { e.selectSlot (-1); });
    shoot (dir, "penta-100pct-hidpi2x", 1.0f, 2.0f, preset ("Master - M/S Widen Highs"), [] (PentaEditor& e) { e.selectSlot (1); });
    shoot (dir, "penta-browser", 1.0f, 1.0f, preset ("Master - Gentle Polish"), [] (PentaEditor& e) { e.openBrowser(); });
    shoot (dir, "penta-hp-selected", 1.0f, 1.0f, preset ("Kick - Punch & Knock"), [] (PentaEditor& e) { e.selectSlot (0); });
    return 0;
}
