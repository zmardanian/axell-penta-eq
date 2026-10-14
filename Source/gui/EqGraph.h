#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

#include <juce_dsp/juce_dsp.h>

// The big interactive curve + spectrum analyzer.
//  - click on empty space: add a band there        - drag on empty space: select bands with a frame
//  - drag a node: frequency / gain (cuts: Q)       - Ctrl/Cmd+click: add / remove a node from the selection
//  - dragging a selected node moves the whole selection; Shift = fine; Ctrl/Cmd+drag = Q
//  - mouse wheel on a node: Q (Shift = fine)       - double-click a node: gain to 0 dB
//  - Alt+click a node: remove                      - Alt+click empty space: new band on the spectrum peak
//  - right-click: band menu (node) or graph menu (empty space)
//  - keys: arrows nudge frequency / gain, Del removes, Ctrl+A all, Esc none, Ctrl+C/V copy/paste a band,
//          I inverts gains, B bypasses, Tab / Shift+Tab next / previous band
//  - DRAW mode: paint the curve you want, APPLY fits bands to it
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
    bool keyPressed (const juce::KeyPress&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;

    void refresh();                 // 30 Hz from the editor: analyzer + curves
    void setSelected (int slot);    // 0 = HP, 1..12 bands, 13 = LP, -1 none (selects only this one)
    int getSelected() const { return selected; }

    //== multi-selection ========================================================
    bool isSelected (int slot) const { return slot >= 0 && slot < penta::kNumSlots && sel[(size_t) slot]; }
    std::vector<int> selection() const;
    void toggleSelected (int slot);
    void selectAll();
    void clearSelection();

    //== editing helpers (also used by tests) ===================================
    void nudge (double octaves, double db);           // move the selection
    void invertSelected();                            // flip the gains of the selection
    void removeSelected();
    void copyBand (int slot);                         // to the clipboard (shared by all instances)
    void pasteToSelection();                          // onto the selected bands, or a new band
    static bool hasClipboard();
    int grabPeakAt (juce::Point<float> p);            // new band on the nearest spectrum peak; returns its slot
    // replace the parametric bands by a fit of a target curve (dB on hz); returns the number of bands used
    int fitBandsTo (const std::vector<double>& hz, const std::vector<double>& db, const std::vector<double>& weight = {});

    //== draw mode ==============================================================
    void setDrawMode (bool on);
    bool isDrawMode() const { return drawMode; }
    void applyDrawing();
    void clearDrawing();
    void drawSegment (juce::Point<float> a, juce::Point<float> b); // paint the target curve (tests)
    std::function<void()> onDrawModeChanged;

    //== spectrum capture (Match) ===============================================
    enum class Capture { none, input, sidechain };
    void startCapture (Capture what);
    void stopCapture();
    Capture capturing() const { return capture; }
    double capturedSeconds() const;
    std::vector<double> capturedDb() const;        // averaged power spectrum on gridHz() (dB)
    const std::vector<double>& gridHz() const { return freqs; }

    // analyzer options (stored in the UI state by the editor)
    struct Options
    {
        bool pre = true, post = true, sc = true, peakHold = true;
        float rangeDb = 18.0f;   // EQ scale +-dB
        float tilt = 4.5f;       // analyzer slope dB/oct
        int speed = 1;           // 0 slow, 1 medium, 2 fast
        bool snapNotes = false;  // frequency drags snap to semitones (A4 = 440 Hz)
        bool freeze = false;     // hold the analyzer display
    };
    Options options;
    void showOptionsMenu();
    std::function<void()> onOptionsChanged;

    static juce::String noteName (double hz); // e.g. "A4 +3c"

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
    void accumulateCapture (const Analyzer& a);
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    float yForSpec (double db) const;
    juce::Point<float> nodePos (int slot) const;
    int hitNode (juce::Point<float> p) const;
    bool slotActive (int slot) const;
    bool isGainType (int slot) const;
    float gainScale() const;
    double effGain (int slot) const;                 // gain as heard (Gain Scale applied)
    void setEffGain (int slot, double db);
    void setParam (const std::string& id, float plainValue);
    float getParam (const std::string& id) const;
    std::string slotParam (int slot, const char* what) const; // "freq", "gain", "q", "type" ...
    int addBandAt (juce::Point<float> p);
    int firstFreeBand() const;
    int peakNear (double hz) const;                  // index into freqs of the spectrum peak near hz, or -1
    double peakHzNear (double hz) const;             // that peak's frequency, refined on the analyzer bins
    void showNodeMenu (int slot);
    void showGraphMenu (juce::Point<float> p);
    void beginGesture (const std::vector<int>& slots);
    void endGesture();
    void notifySelect (int slot);
    double snapped (double hz) const;

    PentaProcessor& proc;
    std::function<void (int)> onSelectFn;
    std::vector<float> values;
    penta::CurveModel curves, curvesStatic;
    std::array<float, penta::kNumBands> delta {};
    std::array<bool, penta::kNumBands> spectral {};
    double fs = 48000.0;

    static constexpr int kPoints = 320;
    std::vector<double> freqs;
    std::array<std::vector<float>, penta::kNumSlots> slotDb, slotDynDb;
    std::vector<float> totalDb, totalStaticDb, moduleDb;

    Analyzer anPre, anPost, anSc;
    std::vector<float> dispPre, dispPost, dispSc, dispPeak;
    std::vector<float> fifoTmp;
    bool scActive = false;

    juce::Rectangle<float> plot;
    int selected = -1, hover = -1, dragging = -1;
    std::array<bool, penta::kNumSlots> sel {};
    juce::Point<float> dragStart;
    struct DragStart { double f = 0, g = 0, q = 0; };
    std::array<DragStart, penta::kNumSlots> dragFrom {};
    std::vector<int> dragGroup, gestureIds;
    bool dragMoved = false, dragQ = false;
    // frame selection on empty space
    bool lassoing = false;
    juce::Rectangle<float> lasso;
    // Alt held: show the spectrum peak a click would grab
    bool altHeld = false;
    juce::Point<float> mousePos;
    // draw mode
    bool drawMode = false, drawing = false;
    std::vector<float> drawDb;      // target curve on freqs, NaN = not drawn
    juce::Point<float> drawLast;
    juce::TextButton drawApply { "APPLY" }, drawClear { "CLEAR" }, drawCancel { "CANCEL" };
    // capture
    Capture capture = Capture::none;
    std::vector<double> capSum;
    int capFrames = 0, capHop = 1;
};
