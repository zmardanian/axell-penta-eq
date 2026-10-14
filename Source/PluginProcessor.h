#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

// Thin JUCE wrapper around penta::Engine (all DSP lives in the JUCE-free headers).
class PentaProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PentaProcessor();
    ~PentaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void setNonRealtime (bool isNonRealtime) noexcept override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    //== undo / redo: snapshots of user edits only (host automation is never recorded) ==
    void pushUndo();               // call before a user edit (gesture start, preset load, A/B)
    // after a user changed a band's type: a cut gets the neutral 0.71 resonance (no corner peak), the
    // other types Q 1 - only while Q still holds the other family's neutral value (a tuned Q is kept)
    void applyNeutralQ (int band);
    bool undoStep();
    bool redoStep();
    bool canUndo() const { return ! undoPast.empty(); }
    bool canRedo() const { return ! undoFuture.empty(); }

    //== presets, A/B, undo (message thread) =====================================
    std::vector<float> readValues() const;            // current plain values (table order)
    void applyValues (const std::vector<float>& v);   // every parameter, notifying the host (one undo step)
    void applyFactoryPreset (int index);
    static juce::File userPresetFolder();
    bool saveUserPreset (const juce::String& name, const juce::String& description = {});
    bool loadUserPreset (const juce::File& file);
    juce::String getPresetName() const;
    void setPresetName (const juce::String& n);
    bool isPresetModified() const;
    void markPresetReference();

    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();

    // non-automatable UI state, saved with the session
    juce::ValueTree uiState() { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    //== metering (audio thread writes, GUI reads) ===============================
    struct Meters
    {
        std::atomic<float> inPeak[2] {}, outPeak[2] {};
        std::atomic<float> delta[penta::kNumBands] {};
        std::atomic<float> autoGainDb { 0.0f };
    };
    Meters meters;
    penta::Engine& engine() { return dsp; }
    bool sidechainConnected() const { return scConnected.load(); }

    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }

private:
    void timerCallback() override;
    void restoreTree (const juce::ValueTree& tree);
    void runEngine (juce::AudioBuffer<float>& buffer);
    static void storeMax (std::atomic<float>& a, float v) noexcept;

    penta::Engine dsp;
    int preparedChannels = 0;
    std::vector<juce::RangedAudioParameter*> params;
    // typed views: the parameters' own plain values (exact, lock-free). APVTS's raw cache skips
    // "approximately equal" updates, so it can keep a rounded value after a state restore.
    std::vector<juce::AudioParameterFloat*> pFloat;
    std::vector<juce::AudioParameterChoice*> pChoice;
    std::vector<juce::AudioParameterBool*> pBool;
    float plain (size_t i) const noexcept
    {
        if (pFloat[i] != nullptr) return pFloat[i]->get();
        if (pChoice[i] != nullptr) return (float) pChoice[i]->getIndex();
        return pBool[i] != nullptr && pBool[i]->get() ? 1.0f : 0.0f;
    }
    std::vector<float> values;  // audio thread scratch (sized once)
    std::atomic<bool> scConnected { false };

    std::atomic<int> currentProgram { 0 };
    std::atomic<int> pendingProgram { -1 };
    juce::CriticalSection nameLock;
    juce::String presetName { "Init (Flat)" };
    std::vector<float> presetReference;
    int activeSlot = 0;
    juce::ValueTree slots[2];
    std::vector<std::vector<float>> undoPast, undoFuture;
    static constexpr size_t kUndoDepth = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PentaProcessor)
};
