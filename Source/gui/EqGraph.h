#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

#include <juce_dsp/juce_dsp.h>

// The big interactive curve + spectrum analyzer.
//  - click on empty space: add a band there        - drag a node: frequency / gain (cuts: frequency / Q)
//  - mouse wheel on a node: Q (Shift = fine)       - double-click a node: gain to 0 dB
//  - Alt+click a node: remove band                 - right-click a node: band menu
class EqGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    EqGraph (PentaProcessor& p, std::function<void (int slot)> onSelect);
    ~EqGraph() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void refresh();                 // 30 Hz from the editor: analyzer + curves
    void setSelected (int slot);    // 0 = HP, 1..12 bands, 13 = LP, -1 none
    int getSelected() const { return selected; }

    // analyzer options (stored in the UI state by the editor)
    struct Options
    {
        bool pre = true, post = true, sc = true, peakHold = true;
        float rangeDb = 18.0f;   // EQ scale +-dB
        float tilt = 4.5f;       // analyzer slope dB/oct
        int speed = 1;           // 0 slow, 1 medium, 2 fast
    };
    Options options;
    void showOptionsMenu();
    std::function<void()> onOptionsChanged;

private:
    struct Analyzer
    {
        void prepare (double fs);
        void push (const float* x, int n);
        bool compute();               // returns true if a new frame was produced
        std::vector<float> ring;
        int ringPos = 0, filled = 0, sinceFrame = 0;
        int order = 12, size = 4096;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, work;
        std::vector<float> binDb;     // magnitude per bin (dB, Hann-corrected)
        double fs = 48000.0;
    };

    void pullAnalyzers();
    void buildDisplay (const Analyzer& a, std::vector<float>& out, std::vector<float>* peak, float release);
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    float yForSpec (double db) const;
    juce::Point<float> nodePos (int slot) const;
    int hitNode (juce::Point<float> p) const;
    bool slotActive (int slot) const;
    bool isGainType (int slot) const;
    void setParam (const std::string& id, float plainValue);
    float getParam (const std::string& id) const;
    std::string slotParam (int slot, const char* what) const; // "freq", "gain", "q", "type" ...
    int addBandAt (juce::Point<float> p);
    void showNodeMenu (int slot);

    PentaProcessor& proc;
    std::function<void (int)> onSelectFn;
    std::vector<float> values;
    penta::CurveModel curves, curvesStatic;
    std::array<float, penta::kNumBands> delta {};
    double fs = 48000.0;

    static constexpr int kPoints = 320;
    std::vector<double> freqs;
    std::array<std::vector<float>, penta::kNumSlots> slotDb;
    std::vector<float> totalDb, totalStaticDb, moduleDb;

    Analyzer anPre, anPost, anSc;
    std::vector<float> dispPre, dispPost, dispSc, dispPeak;
    std::vector<float> fifoTmp;
    bool scActive = false;

    juce::Rectangle<float> plot;
    int selected = -1, hover = -1, dragging = -1;
    juce::Point<float> dragStart;
    double dragF0 = 0, dragG0 = 0, dragQ0 = 0;
    bool dragMoved = false;
};
