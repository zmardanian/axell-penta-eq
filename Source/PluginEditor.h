#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/BandPanel.h"
#include "gui/EqGraph.h"
#include "gui/LookAndFeel.h"
#include "gui/MatchPanel.h"
#include "gui/PresetBrowser.h"
#include "gui/RackPanel.h"

class PentaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PentaEditor (PentaProcessor&);
    ~PentaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // used by the headless snapshot tool and tests
    void refreshNow() { timerCallback(); }
    void selectSlot (int slot);
    void openBrowser();
    EqGraph& getGraph() { return graph; }
    MatchPanel& getMatch() { return match; }

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (PentaEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        PentaEditor& owner;
    };

    void timerCallback() override;
    void layout();
    void stepPreset (int delta);
    void promptSavePreset();
    void showSizeMenu();
    void setUiScale (float scale);
    void saveUiState();

    PentaProcessor& proc;
    ui::LookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 650 };

    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" };
    ui::MenuButton presetName;
    juce::TextButton slotA { "A" }, slotB { "B" }, slotCopy { "COPY" }, undoBtn { "UNDO" }, redoBtn { "REDO" };
    ui::Choice phase, quality, os;
    ui::MenuButton analyzerBtn, sizeBtn;
    juce::TextButton drawBtn { "DRAW" }, matchBtn { "MATCH" };
    juce::Slider scaleSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scaleAttachment;

    EqGraph graph;
    ui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    BandPanel bandPanel;
    RackPanel rack;
    PresetBrowser browser;
    MatchPanel match;

    juce::Rectangle<int> headerArea, graphArea;
    juce::String latencyText;
    bool constructed = false;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PentaEditor)
};
