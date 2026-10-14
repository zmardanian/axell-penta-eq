#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

// Editor for the selected band (or the dedicated HP / LP cut). Controls re-bind on selection.
class BandPanel : public juce::Component, private juce::Timer, private juce::AudioProcessorParameter::Listener
{
public:
    BandPanel (PentaProcessor& p, std::function<void (int)> onSelect);
    ~BandPanel() override;
    void setSlot (int slot);   // 0 HP, 1..12 bands, 13 LP, -1 none
    int getSlot() const { return slot; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    // the selected band's type: a user change (it comes as a gesture; automation does not) resets a
    // neutral Q to the new type's neutral value
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool starting) override { typeGesture = starting; }
    void rebind();
    void updateEnablement();
    void setParam (const std::string& id, float v);
    float getParam (const std::string& id) const;

    PentaProcessor& proc;
    std::function<void (int)> onSelectFn;
    int slot = -1;

    juce::OwnedArray<juce::TextButton> chips;
    ui::Choice type, slope, source;
    ui::Segmented mode { { "CLEAN", "CHARACTER" }, ui::col::accent };
    ui::Segmented place { { "ST", "L", "R", "M", "S" }, ui::col::teal };
    ui::Knob freq { "FREQ", ui::col::accent, 52 }, gain { "GAIN", ui::col::accent, 52 }, q { "Q", ui::col::accent, 52 };
    ui::Toggle dyn { "DYNAMIC", ui::col::warn, "pill" };
    ui::Segmented dmode { { "BROAD", "SPECTRAL" }, ui::col::warn };
    ui::Knob thr { "THRESH", ui::col::warn, 44 }, range { "RANGE", ui::col::warn, 44 }, ratio { "RATIO", ui::col::warn, 44 },
             atk { "ATTACK", ui::col::warn, 44 }, rel { "RELEASE", ui::col::warn, 44 };
    ui::Toggle cutOn { "ON", ui::col::accent, "power" };
    juce::TextButton solo { "SOLO" }, bypass { "BYPASS" }, remove { "REMOVE" };
    juce::Rectangle<int> titleArea, dynArea, mainArea;
    bool soloing = false;
    juce::RangedAudioParameter* typeParam = nullptr;
    int typeBand = -1;
    std::atomic<bool> typeGesture { false };
};
