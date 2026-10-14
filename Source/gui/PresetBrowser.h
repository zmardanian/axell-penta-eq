#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

// Overlay preset browser: categories, search, descriptions, factory + user presets.
class PresetBrowser : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser (PentaProcessor& p);
    void open();                         // refresh lists and show
    std::function<void()> onClose, onLoaded, onSaveRequest;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Entry
    {
        juce::String name, category, description;
        int factory = -1;
        juce::File file;
    };
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void textEditorTextChanged (juce::TextEditor&) override;
    void rebuild();
    void load (int row);

    PentaProcessor& proc;
    std::vector<Entry> all, shown;
    juce::String category { "All" };
    juce::OwnedArray<juce::TextButton> cats;
    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::TextButton loadBtn { "LOAD" }, saveBtn { "SAVE AS..." }, folderBtn { "FOLDER" }, closeBtn { "CLOSE" };
    juce::Rectangle<int> descArea;
};
