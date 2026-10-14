#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

// Character rack (Inductor, Console E/G, Passive Tube, Air) + global strip.
class RackPanel : public juce::Component, private juce::Timer
{
public:
    explicit RackPanel (PentaProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct K
    {
        std::unique_ptr<ui::Knob> knob;
        juce::String id;
    };
    ui::Knob& knob (std::vector<K>& list, const juce::String& caption, const juce::String& id, juce::Colour c,
                    const juce::String& tooltip, int d = 40);
    void layoutRow (std::vector<K>& list, int from, int to, juce::Rectangle<int> area);

    PentaProcessor& proc;
    ui::Toggle indOn { "INDUCTOR", ui::col::inductor, "power" }, conOn { "CONSOLE", ui::col::console, "power" },
               ptOn { "PASSIVE TUBE", ui::col::tube, "power" }, airOn { "AIR", ui::col::air, "power" };
    ui::Segmented conMode { { "E", "G" }, ui::col::console };
    ui::Toggle conLfBell { "LF BELL", ui::col::console, "pill" }, conHfBell { "HF BELL", ui::col::console, "pill" };
    ui::Toggle autoGain { "AUTO GAIN", ui::col::global, "pill" };
    ui::Segmented rackPos { { "POST", "PRE" }, ui::col::global };
    std::vector<K> ind, con, pt, air, glob;
    juce::Rectangle<int> indArea, conArea, ptArea, airArea, globArea;
    float agDb = 0.0f;
};
