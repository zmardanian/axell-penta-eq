#include "PresetBrowser.h"

using namespace ui;

PresetBrowser::PresetBrowser (PentaProcessor& p) : proc (p)
{
    juce::StringArray names { "All" };
    for (const auto& c : penta::presetCategories())
        names.add (c);
    names.add ("User");
    for (const auto& n : names)
    {
        auto* b = cats.add (new juce::TextButton (n));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, col::accent);
        b->setTooltip (n == "All" ? juce::String ("Show every preset") : (n == "User" ? juce::String ("Your saved presets")
                                                                                    : "Show the " + n + " presets"));
        b->onClick = [this, n]
        {
            category = n;
            rebuild();
        };
        addAndMakeVisible (b);
    }
    search.setTextToShowWhenEmpty ("Search presets (name or description)...", col::faint);
    search.setFont (font (14.0f));
    search.addListener (this);
    addAndMakeVisible (search);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, col::well);
    addAndMakeVisible (list);
    for (auto* b : { &loadBtn, &saveBtn, &folderBtn, &closeBtn })
        addAndMakeVisible (b);
    loadBtn.setTooltip ("Load the selected preset (or double-click it).");
    closeBtn.setTooltip ("Close the browser (Esc).");
    search.setTooltip ("Type to filter by name, description or category.");
    saveBtn.setTooltip ("Save the current settings as a user preset.");
    folderBtn.setTooltip ("Open the user preset folder (~/.local/share/AxellPentaEQ/Presets).");
    loadBtn.onClick = [this] { load (list.getSelectedRow()); };
    saveBtn.onClick = [this] { if (onSaveRequest) onSaveRequest(); };
    folderBtn.onClick = []
    {
        auto f = PentaProcessor::userPresetFolder();
        f.createDirectory();
        f.startAsProcess();
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    setWantsKeyboardFocus (true);
}

void PresetBrowser::open()
{
    all.clear();
    const auto& f = penta::factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
        all.push_back ({ f[(size_t) i].name, f[(size_t) i].category, f[(size_t) i].description, i, {} });
    auto files = PentaProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.pentapreset");
    files.sort();
    for (auto& file : files)
    {
        juce::String desc;
        if (auto xml = juce::XmlDocument::parse (file))
            desc = xml->getStringAttribute ("presetDescription");
        all.push_back ({ file.getFileNameWithoutExtension(), "User", desc.isEmpty() ? juce::String ("Your preset") : desc, -1, file });
    }
    rebuild();
    // preselect the current preset
    const auto cur = proc.getPresetName();
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].name == cur)
            list.selectRow (i);
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void PresetBrowser::rebuild()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (const auto& e : all)
    {
        if (category != "All" && e.category != category)
            continue;
        if (q.isNotEmpty() && ! e.name.toLowerCase().contains (q) && ! e.description.toLowerCase().contains (q)
            && ! e.category.toLowerCase().contains (q))
            continue;
        shown.push_back (e);
    }
    for (auto* b : cats)
        b->setToggleState (b->getButtonText() == category, juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    repaint (descArea);
}

int PresetBrowser::getNumRows() { return (int) shown.size(); }

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    const auto& e = shown[(size_t) row];
    if (selected)
    {
        g.setColour (col::accent.withAlpha (0.2f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h).reduced (2.0f, 1.0f), 5.0f);
    }
    g.setColour (e.name == proc.getPresetName() ? col::accent : col::text);
    g.setFont (font (14.0f, 1));
    g.drawText (e.name, 10, 3, w - 150, 18, juce::Justification::centredLeft);
    g.setColour (col::faint);
    g.setFont (caps (9.5f, 1));
    g.drawText (e.category.toUpperCase(), w - 150, 3, 140, 18, juce::Justification::centredRight);
    g.setColour (col::dim);
    g.setFont (font (11.5f));
    g.drawText (e.description, 10, 21, w - 20, 16, juce::Justification::centredLeft, true);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { load (row); }

void PresetBrowser::selectedRowsChanged (int) { repaint (descArea); }

void PresetBrowser::textEditorTextChanged (juce::TextEditor&) { rebuild(); }

void PresetBrowser::load (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    const auto& e = shown[(size_t) row];
    if (e.factory >= 0)
        proc.applyFactoryPreset (e.factory);
    else
        proc.loadUserPreset (e.file);
    list.repaint();
    if (onLoaded)
        onLoaded();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    if (k == juce::KeyPress::returnKey)
    {
        load (list.getSelectedRow());
        return true;
    }
    return false;
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (30);
    closeBtn.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    search.setBounds (top.removeFromRight (320));
    r.removeFromTop (10);
    auto left = r.removeFromLeft (180);
    for (auto* b : cats)
    {
        b->setBounds (left.removeFromTop (30));
        left.removeFromTop (6);
    }
    r.removeFromLeft (12);
    auto bottom = r.removeFromBottom (34);
    loadBtn.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (8);
    saveBtn.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    folderBtn.setBounds (bottom.removeFromRight (90));
    descArea = bottom;
    r.removeFromBottom (8);
    list.setBounds (r);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xf2121418));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (col::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.2f);
    g.setColour (col::text);
    g.setFont (caps (15.0f, 2));
    g.drawText ("PRESETS", 18, 14, 200, 30, juce::Justification::centredLeft);
    const int row = list.getSelectedRow();
    if (row >= 0 && row < (int) shown.size())
    {
        g.setColour (col::dim);
        g.setFont (font (12.0f));
        g.drawFittedText (shown[(size_t) row].description, descArea.reduced (4, 0), juce::Justification::centredLeft, 2, 0.9f);
    }
}
