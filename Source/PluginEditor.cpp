#include "PluginEditor.h"

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth"), kSelected ("selected"), kAnPre ("anPre"), kAnPost ("anPost"),
    kAnSc ("anSc"), kAnPeak ("anPeak"), kAnTilt ("anTilt"), kAnSpeed ("anSpeed"), kRange ("eqRange");

namespace tip
{
const char* presets = "Presets: 40 factory presets for vocals, beats, mix bus/master and Axell's own voice. "
                      "Click to open the browser (search, categories, descriptions). Your presets live in "
                      "~/.local/share/AxellPentaEQ/Presets.";
const char* ab = "A/B compare: switch between two complete settings.";
const char* copy = "Copy the current slot into the other slot.";
const char* undo = "Undo the last change (Ctrl+Z).";
const char* redo = "Redo (Ctrl+Shift+Z or Ctrl+Y).";
const char* phase = "ZERO LATENCY: analog-matched curves, no delay - use while recording. "
                    "NATURAL PHASE: also matches the analog phase, 2.7 ms latency. "
                    "LINEAR PHASE: no phase shift at all (mastering, parallel/stacked signals); higher latency, "
                    "can pre-ring on steep low-frequency moves.";
const char* quality = "Linear Phase resolution: higher = more accurate at low frequencies, more latency "
                      "(Low ~45 ms, Medium ~90 ms, High ~360 ms at 48 kHz).";
const char* os = "Oversampling for the character stages and Character bands (cleaner drive at high settings). "
                 "1x = no extra latency.";
const char* analyzer = "Analyzer and graph options: pre/post/side-chain spectrum, peak hold, tilt, speed, EQ scale.";
const char* size = "Interface size (75-200 %). You can also drag the bottom-right corner.";
const char* meterIn = "Input peak level (dBFS). Click to reset the peak number.";
const char* meterOut = "Output peak level (dBFS). Click to reset the peak number.";
} // namespace tip
} // namespace

PentaEditor::PentaEditor (PentaProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      graph (p, [this] (int s) { selectSlot (s); }),
      bandPanel (p, [this] (int s) { selectSlot (s); }),
      rack (p),
      browser (p)
{
    auto ui = proc.uiState();
    const int savedWidth = (int) ui.getProperty (kUiWidth, kWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (auto* c : std::initializer_list<juce::Component*> { &presetPrev, &presetNext, &presetName, &presetSave, &slotA, &slotB,
                                                            &slotCopy, &undoBtn, &redoBtn, &phase, &quality, &os, &sizeBtn,
                                                            &graph, &analyzerBtn, &inMeter, &outMeter, &bandPanel, &rack })
        content.addAndMakeVisible (c);
    content.addChildComponent (browser);

    auto g = [this] { proc.pushUndo(); };
    phase.bind (proc.apvts, "phase", tip::phase, g);
    quality.bind (proc.apvts, "lpQuality", tip::quality, g);
    os.bind (proc.apvts, "os", tip::os, g);
    analyzerBtn.setButtonText ("ANALYZER");
    sizeBtn.setButtonText ("SIZE");
    analyzerBtn.setTooltip (tip::analyzer);
    sizeBtn.setTooltip (tip::size);
    presetName.setTooltip (tip::presets);
    presetPrev.setTooltip ("Previous factory preset");
    presetNext.setTooltip ("Next factory preset");
    presetSave.setTooltip ("Save the current settings as a user preset");
    for (auto* b : { &slotA, &slotB })
    {
        b->setColour (juce::TextButton::buttonOnColourId, col::global);
        b->setTooltip (tip::ab);
    }
    slotCopy.setTooltip (tip::copy);
    undoBtn.setTooltip (tip::undo);
    redoBtn.setTooltip (tip::redo);
    inMeter.setTooltip (tip::meterIn);
    outMeter.setTooltip (tip::meterOut);

    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { openBrowser(); };
    presetSave.onClick = [this] { promptSavePreset(); };
    slotA.onClick = [this] { proc.selectSlot (0); timerCallback(); };
    slotB.onClick = [this] { proc.selectSlot (1); timerCallback(); };
    slotCopy.onClick = [this] { proc.copyActiveToOther(); };
    undoBtn.onClick = [this] { proc.undoStep(); timerCallback(); };
    redoBtn.onClick = [this] { proc.redoStep(); timerCallback(); };
    analyzerBtn.onClick = [this] { graph.showOptionsMenu(); };
    sizeBtn.onClick = [this] { showSizeMenu(); };
    browser.onClose = [this] { browser.setVisible (false); };
    browser.onLoaded = [this] { browser.setVisible (false); selectSlot (-1); timerCallback(); };
    browser.onSaveRequest = [this] { promptSavePreset(); };

    // restore UI state
    auto& o = graph.options;
    o.pre = ui.getProperty (kAnPre, true);
    o.post = ui.getProperty (kAnPost, true);
    o.sc = ui.getProperty (kAnSc, true);
    o.peakHold = ui.getProperty (kAnPeak, true);
    o.tilt = (float) ui.getProperty (kAnTilt, 4.5);
    o.speed = (int) ui.getProperty (kAnSpeed, 1);
    o.rangeDb = (float) ui.getProperty (kRange, 18.0);
    graph.onOptionsChanged = [this] { saveUiState(); };

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    const int w = juce::jlimit (kWidth * 3 / 4, kWidth * 2, savedWidth);
    setSize (w, juce::roundToInt (w * (double) kHeight / kWidth));
    constructed = true;
    selectSlot ((int) ui.getProperty (kSelected, -1));
    setWantsKeyboardFocus (true);
    timerCallback();
    startTimerHz (30);
}

PentaEditor::~PentaEditor()
{
    stopTimer();
    saveDialog.reset();
    setLookAndFeel (nullptr);
}

void PentaEditor::saveUiState()
{
    auto ui = proc.uiState();
    const auto& o = graph.options;
    ui.setProperty (kAnPre, o.pre, nullptr);
    ui.setProperty (kAnPost, o.post, nullptr);
    ui.setProperty (kAnSc, o.sc, nullptr);
    ui.setProperty (kAnPeak, o.peakHold, nullptr);
    ui.setProperty (kAnTilt, o.tilt, nullptr);
    ui.setProperty (kAnSpeed, o.speed, nullptr);
    ui.setProperty (kRange, o.rangeDb, nullptr);
}

void PentaEditor::selectSlot (int slot)
{
    graph.setSelected (slot);
    bandPanel.setSlot (slot);
    proc.uiState().setProperty (kSelected, slot, nullptr);
}

void PentaEditor::openBrowser()
{
    browser.open();
}

void PentaEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void PentaEditor::resized()
{
    const float scale = (float) getWidth() / (float) kWidth;
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.uiState().setProperty (kUiWidth, getWidth(), nullptr);
}

void PentaEditor::setUiScale (float scale)
{
    const int w = juce::roundToInt (kWidth * juce::jlimit (0.75f, 2.0f, scale));
    setSize (w, juce::roundToInt (w * (double) kHeight / kWidth));
}

void PentaEditor::Content::resized() { owner.layout(); }

void PentaEditor::layout()
{
    using juce::Rectangle;
    const int m = 12;
    headerArea = { 0, 0, kWidth, 56 };
    {
        auto pb = Rectangle<int> (284, 13, 336, 30);
        presetPrev.setBounds (pb.removeFromLeft (26));
        pb.removeFromLeft (3);
        presetSave.setBounds (pb.removeFromRight (54));
        pb.removeFromRight (3);
        presetNext.setBounds (pb.removeFromRight (26));
        pb.removeFromRight (3);
        presetName.setBounds (pb);

        auto r = Rectangle<int> (632, 13, kWidth - 632 - m, 30);
        slotA.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (3);
        slotB.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (3);
        slotCopy.setBounds (r.removeFromLeft (54));
        r.removeFromLeft (8);
        undoBtn.setBounds (r.removeFromLeft (50));
        r.removeFromLeft (3);
        redoBtn.setBounds (r.removeFromLeft (50));
        sizeBtn.setBounds (r.removeFromRight (66));
        r.removeFromRight (4);
        os.setBounds (r.removeFromRight (52));
        r.removeFromRight (4);
        quality.setBounds (r.removeFromRight (84));
        r.removeFromRight (4);
        phase.setBounds (r.removeFromRight (124));
    }
    graphArea = { m, 64, kWidth - 2 * m - 62, 378 };
    graph.setBounds (graphArea);
    analyzerBtn.setBounds (graphArea.getRight() - 136, graphArea.getY() + 6, 96, 22);
    inMeter.setBounds (graphArea.getRight() + 8, graphArea.getY(), 26, graphArea.getHeight() - 14);
    outMeter.setBounds (graphArea.getRight() + 36, graphArea.getY(), 26, graphArea.getHeight() - 14);
    bandPanel.setBounds (m, 450, kWidth - 2 * m, 140);
    rack.setBounds (m, 598, kWidth - 2 * m, kHeight - 598 - m);
    browser.setBounds (graphArea.reduced (40, 6).withTrimmedRight (-62));
}

void PentaEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    auto& e = owner;
    g.fillAll (col::bg);
    g.setColour (col::header);
    g.fillRect (e.headerArea);
    g.setColour (Colour (0xff23272e));
    g.drawHorizontalLine (e.headerArea.getBottom() - 1, 0.0f, (float) kWidth);
    g.setColour (col::text);
    g.setFont (caps (21.0f, 2));
    g.drawText ("AXELL PENTA EQ", 18, 7, 280, 26, Justification::centredLeft);
    {
        const auto f = caps (9.0f, 1);
        g.setFont (f);
        float x = 19.0f;
        auto word = [&] (const String& t, Colour c, float gapAfter)
        {
            g.setColour (c);
            const float w = GlyphArrangement::getStringWidth (f, t);
            g.drawText (t, Rectangle<float> (x, 33.0f, w + 2.0f, 14.0f), Justification::centredLeft);
            x += w + gapAfter;
        };
        word ("PARAMETRIC", col::accent, 5.0f);
        word ("INDUCTOR", col::inductor, 5.0f);
        word ("CONSOLE", col::console, 5.0f);
        word ("TUBE", col::tube, 5.0f);
        word ("AIR", col::air, 0.0f);
    }
    g.setFont (font (10.5f));
    g.setColour (col::faint);
    g.drawText (e.latencyText, e.graphArea.getRight() + 4, e.graphArea.getBottom() - 12, 70, 12, Justification::centred);
}

void PentaEditor::timerCallback()
{
    graph.refresh();
    auto& mv = proc.meters;
    const int nCh = juce::jmax (1, proc.getMainBusNumOutputChannels());
    inMeter.push (mv.inPeak[0].exchange (0.0f), mv.inPeak[1].exchange (0.0f), nCh);
    outMeter.push (mv.outPeak[0].exchange (0.0f), mv.outPeak[1].exchange (0.0f), nCh);
    presetName.setButtonText (proc.getPresetName() + (proc.isPresetModified() ? " *" : ""));
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    slotCopy.setTooltip (proc.getActiveSlot() == 0 ? "Copy A into B" : "Copy B into A");
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    quality.setEnabled (phase.getSelectedItemIndex() == penta::kLinear);
    const int lat = proc.getLatencySamples();
    const auto lt = lat == 0 ? juce::String ("0 lat") : juce::String (lat) + " smp";
    if (lt != latencyText)
    {
        latencyText = lt;
        content.repaint();
    }
}

bool PentaEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown())
            proc.redoStep();
        else
            proc.undoStep();
        timerCallback();
        return true;
    }
    if (mods.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y'))
    {
        proc.redoStep();
        timerCallback();
        return true;
    }
    return false;
}

void PentaEditor::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    proc.applyFactoryPreset ((proc.getCurrentProgram() + delta + n) % n);
    selectSlot (-1);
    timerCallback();
}

void PentaEditor::showSizeMenu()
{
    juce::PopupMenu size;
    for (int pct : { 75, 90, 100, 125, 150, 175, 200 })
        size.addItem (pct, juce::String (pct) + " %", true, std::abs (getWidth() - kWidth * pct / 100) < 4);
    juce::Component::SafePointer<PentaEditor> safe (this);
    size.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeBtn), [safe] (int id)
    {
        if (safe != nullptr && id > 0)
            safe->setUiScale ((float) id / 100.0f);
    });
}

void PentaEditor::promptSavePreset()
{
    using namespace juce;
    saveDialog = std::make_unique<AlertWindow> ("Save preset", "Name and (optional) description:", MessageBoxIconType::NoIcon, this);
    auto* w = saveDialog.get();
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.getPresetName(), "Name");
    w->addTextEditor ("desc", {}, "Description");
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<PentaEditor> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->saveDialog == nullptr)
            return;
        const auto name = safe->saveDialog->getTextEditorContents ("name").trim();
        const auto desc = safe->saveDialog->getTextEditorContents ("desc").trim();
        if (result == 1 && name.isNotEmpty() && ! safe->proc.saveUserPreset (name, desc))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Save preset",
                                              "Could not write to " + PentaProcessor::userPresetFolder().getFullPathName());
        safe->timerCallback();
        if (safe->browser.isVisible())
            safe->browser.open();
        MessageManager::callAsync ([safe] { if (safe != nullptr) safe->saveDialog.reset(); });
    }), false);
}
