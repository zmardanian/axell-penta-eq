#pragma once
#include "EqGraph.h"

// Match: capture a reference spectrum and this track's spectrum while the audio plays, then fit bands
// that move this track's tonal balance towards the reference.
//  - the reference comes from the side-chain, from this instance's input, or from another instance in
//    the same session (a captured reference is shared by all instances in the host process)
//  - the overall level difference is removed (it matches tone, not loudness); amount, smoothing and the
//    largest correction are adjustable; APPLY replaces the bands (one undo step)
class MatchPanel : public juce::Component, private juce::Timer
{
public:
    MatchPanel (PentaProcessor& p, EqGraph& g);
    ~MatchPanel() override;
    void open();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // used by tests: set spectra directly, compute and apply
    struct Spectrum
    {
        std::vector<double> db;  // on EqGraph::gridHz()
        double seconds = 0.0;
        juce::String origin;
        bool valid = false;
    };
    void setReference (const Spectrum& s) { ref = s; }
    void setTrack (const Spectrum& s) { track = s; }
    std::vector<double> matchCurve() const;  // the correction to apply, on the 20 Hz .. 20 kHz part of the grid
    std::vector<double> matchHz() const;
    int apply();                             // returns the number of bands used (0: nothing to apply)
    void setAmount (double pct) { amount.setValue (pct); }

private:
    static Spectrum& sharedReference();
    void timerCallback() override;
    void toggleCapture (int what);           // 1 reference (input), 2 reference (side-chain), 3 this track
    void finishCapture();
    void updateButtons();

    PentaProcessor& proc;
    EqGraph& graph;
    Spectrum ref, track;
    int capturing = 0;
    juce::TextButton refIn { "REFERENCE FROM INPUT" }, refSc { "REFERENCE FROM SIDE-CHAIN" }, refShared { "USE SHARED REFERENCE" },
        trackBtn { "CAPTURE THIS TRACK" }, applyBtn { "APPLY" }, closeBtn { "CLOSE" };
    juce::Slider amount, smoothing, maxDb;
    juce::Label amountL, smoothingL, maxL;
    juce::Rectangle<int> plotArea, textArea;
};
