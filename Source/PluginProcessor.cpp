#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const juce::Identifier kPresetNameId ("presetName");
const juce::Identifier kAbId ("AB");
const juce::Identifier kPresetDescId ("presetDescription");

juce::NormalisableRange<float> rangeFor (const penta::ParamSpec& s)
{
    juce::NormalisableRange<float> r (s.min, s.max, s.step);
    if (s.type == penta::PType::Float && s.centre > s.min && s.centre < s.max)
        r.setSkewForCentre (s.centre);
    return r;
}
} // namespace

APVTS::ParameterLayout PentaProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> out;
    for (const auto& s : penta::paramTable())
    {
        const ParameterID pid { s.id, s.version };
        switch (s.type)
        {
            case penta::PType::Bool:
                out.push_back (std::make_unique<AudioParameterBool> (pid, s.name, s.def > 0.5f));
                break;
            case penta::PType::Choice:
            {
                StringArray c;
                for (const auto& x : s.choices)
                    c.add (x);
                out.push_back (std::make_unique<AudioParameterChoice> (pid, s.name, c, (int) s.def));
                break;
            }
            case penta::PType::Float:
            default:
            {
                const auto spec = s;
                auto attrs = AudioParameterFloatAttributes()
                                 .withLabel (s.unit)
                                 .withStringFromValueFunction ([spec] (float v, int) { return String (penta::formatValue (spec, v)); })
                                 .withValueFromStringFunction ([spec] (const String& t)
                                 {
                                     auto x = t.trim();
                                     float v = x.getFloatValue();
                                     if (spec.unit == "Hz" && x.containsIgnoreCase ("k"))
                                         v *= 1000.0f;
                                     return v;
                                 });
                out.push_back (std::make_unique<AudioParameterFloat> (pid, s.name, rangeFor (s), s.def, attrs));
                break;
            }
        }
    }
    return { out.begin(), out.end() };
}

PentaProcessor::PentaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    const auto& t = penta::paramTable();
    params.resize (t.size());
    pFloat.resize (t.size());
    pChoice.resize (t.size());
    pBool.resize (t.size());
    values.resize (t.size());
    for (size_t i = 0; i < t.size(); ++i)
    {
        params[i] = apvts.getParameter (t[i].id);
        jassert (params[i] != nullptr);
        pFloat[i] = dynamic_cast<juce::AudioParameterFloat*> (params[i]);
        pChoice[i] = dynamic_cast<juce::AudioParameterChoice*> (params[i]);
        pBool[i] = dynamic_cast<juce::AudioParameterBool*> (params[i]);
        values[i] = t[i].def;
    }
    presetReference = readValues();
    startTimerHz (50); // also reports latency changes: the engine stays muted until then
}

PentaProcessor::~PentaProcessor()
{
    stopTimer();
}

double PentaProcessor::getTailLengthSeconds() const
{
    return 0.25; // longest resonant band ring-out
}

bool PentaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (out != layouts.getMainInputChannelSet())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

std::vector<float> PentaProcessor::readValues() const
{
    std::vector<float> v (params.size());
    for (size_t i = 0; i < params.size(); ++i)
        v[i] = plain (i);
    return v;
}

void PentaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
    for (size_t i = 0; i < params.size(); ++i)
        values[i] = plain (i);
    dsp.setParams (values);
    dsp.setNonRealtime (isNonRealtime());
    dsp.prepare (sampleRate, preparedChannels, samplesPerBlock);
    setLatencySamples (dsp.getLatencySamples());
    dsp.setHostLatency (getLatencySamples());
}

void PentaProcessor::reset()
{
    if (preparedChannels > 0)
        dsp.reset();
}

void PentaProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    AudioProcessor::setNonRealtime (isNonRealtime);
    dsp.setNonRealtime (isNonRealtime);
}

void PentaProcessor::storeMax (std::atomic<float>& a, float v) noexcept
{
    float cur = a.load (std::memory_order_relaxed);
    while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed))
    {
    }
}

void PentaProcessor::runEngine (juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    for (int c = mainIn.getNumChannels(); c < mainOut.getNumChannels(); ++c)
        mainOut.clear (c, 0, n);
    if (preparedChannels == 0 || n == 0)
        return;

    for (size_t i = 0; i < params.size(); ++i)
        values[i] = plain (i);
    dsp.setParams (values);

    const float* sc[2] { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        scCh = juce::jmin (2, scBuf.getNumChannels());
        for (int c = 0; c < scCh; ++c)
            sc[c] = scBuf.getReadPointer (c);
    }
    scConnected.store (scCh > 0, std::memory_order_relaxed);
    dsp.process (mainOut.getArrayOfWritePointers(), juce::jmin (preparedChannels, mainOut.getNumChannels()), n,
                 scCh > 0 ? sc : nullptr, scCh);

    const auto& m = dsp.getMeters();
    for (int c = 0; c < 2; ++c)
    {
        storeMax (meters.inPeak[c], m.inPeak[c]);
        storeMax (meters.outPeak[c], m.outPeak[c]);
    }
    for (int b = 0; b < penta::kNumBands; ++b)
        meters.delta[b].store (m.delta[(size_t) b], std::memory_order_relaxed);
    meters.autoGainDb.store (m.autoGainDb, std::memory_order_relaxed);
    dsp.clearMeters();
}

void PentaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    runEngine (buffer);
}

// host bypass: pass the input through a delay of the reported latency, so PDC stays aligned
void PentaProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // JUCE's default implementation does not delay; a latency-matched bypass keeps the host's
    // compensation correct. Simplest exact solution: run the engine with Mix = 0 (dry path is the
    // latency-aligned input, bit-exact) and output gain 0 dB.
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainOut = getBusBuffer (buffer, false, 0);
    if (preparedChannels == 0 || n == 0)
        return;
    for (size_t i = 0; i < params.size(); ++i)
        values[i] = plain (i);
    const auto& x = penta::idx();
    values[(size_t) x.mix] = 0.0f;
    values[(size_t) x.outGain] = 0.0f;
    values[(size_t) x.inGain] = 0.0f;
    dsp.setParams (values);
    dsp.process (mainOut.getArrayOfWritePointers(), juce::jmin (preparedChannels, mainOut.getNumChannels()), n);
    dsp.clearMeters();
}

void PentaProcessor::timerCallback()
{
    const int pp = pendingProgram.exchange (-1);
    if (pp >= 0)
    {
        const auto& list = penta::factoryPresets();
        setPresetName (list[(size_t) pp].name);
        presetReference = list[(size_t) pp].resolve();
    }
    // latency handoff: the engine publishes a new latency when it switches (muted) and stays muted
    // until this has told the host (VST3: restartComponent, synchronous on the message thread)
    if (preparedChannels > 0)
    {
        const int lat = dsp.getLatencySamples();
        if (lat != getLatencySamples())
            setLatencySamples (lat);
        dsp.setHostLatency (lat);
    }
}

//==============================================================================
void PentaProcessor::applyNeutralQ (int band)
{
    if (band < 0 || band >= penta::kNumBands)
        return;
    const int t = (int) std::lround (plain ((size_t) penta::idx().band[band][penta::bpType]));
    const bool cut = t == penta::kLowCut || t == penta::kHighCut;
    auto* q = params[(size_t) penta::idx().band[band][penta::bpQ]];
    const float cur = q->convertFrom0to1 (q->getValue());
    const float from = cut ? 1.0f : 0.70710678f, to = cut ? 0.70710678f : 1.0f;
    if (std::abs (cur - from) > 1.0e-3f)
        return;
    q->beginChangeGesture();
    q->setValueNotifyingHost (q->convertTo0to1 (to));
    q->endChangeGesture();
}

void PentaProcessor::pushUndo()
{
    auto now = readValues();
    if (! undoPast.empty() && undoPast.back() == now)
        return;
    undoPast.push_back (std::move (now));
    if (undoPast.size() > kUndoDepth)
        undoPast.erase (undoPast.begin());
    undoFuture.clear();
}

bool PentaProcessor::undoStep()
{
    if (undoPast.empty())
        return false;
    undoFuture.push_back (readValues());
    auto v = undoPast.back();
    undoPast.pop_back();
    applyValues (v);
    return true;
}

bool PentaProcessor::redoStep()
{
    if (undoFuture.empty())
        return false;
    undoPast.push_back (readValues());
    auto v = undoFuture.back();
    undoFuture.pop_back();
    applyValues (v);
    return true;
}

void PentaProcessor::applyValues (const std::vector<float>& v)
{
    for (size_t i = 0; i < params.size() && i < v.size(); ++i)
    {
        auto* p = params[i];
        const float norm = p->convertTo0to1 (v[i]);
        if (std::abs (p->getValue() - norm) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
}

void PentaProcessor::applyFactoryPreset (int index)
{
    const auto& list = penta::factoryPresets();
    if (index < 0 || index >= (int) list.size())
        return;
    currentProgram.store (index);
    const auto v = list[(size_t) index].resolve();
    pushUndo();
    applyValues (v);
    setPresetName (list[(size_t) index].name);
    presetReference = v;
}

int PentaProcessor::getNumPrograms()
{
    return (int) penta::factoryPresets().size();
}

void PentaProcessor::setCurrentProgram (int index)
{
    const auto& list = penta::factoryPresets();
    if (index < 0 || index >= (int) list.size())
        return;
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyFactoryPreset (index);
        return;
    }
    // VST3 hosts may change programs from the audio thread: set the values (atomics + async tree
    // updates inside APVTS), resolve the display name on the message thread
    currentProgram.store (index);
    const auto& preset = list[(size_t) index];
    for (size_t i = 0; i < params.size(); ++i)
        params[i]->setValueNotifyingHost (params[i]->convertTo0to1 (penta::paramTable()[i].def));
    for (const auto& kv : preset.values)
    {
        const int i = penta::paramIndex (kv.first);
        if (i >= 0)
            params[(size_t) i]->setValueNotifyingHost (params[(size_t) i]->convertTo0to1 (kv.second));
    }
    pendingProgram.store (index);
}

const juce::String PentaProcessor::getProgramName (int index)
{
    const auto& list = penta::factoryPresets();
    if (index < 0 || index >= (int) list.size())
        return {};
    return list[(size_t) index].name;
}

juce::String PentaProcessor::getPresetName() const
{
    const juce::ScopedLock sl (nameLock);
    return presetName;
}

void PentaProcessor::setPresetName (const juce::String& n)
{
    const juce::ScopedLock sl (nameLock);
    presetName = n;
}

bool PentaProcessor::isPresetModified() const
{
    const auto v = readValues();
    for (size_t i = 0; i < v.size() && i < presetReference.size(); ++i)
        if (std::abs (v[i] - presetReference[i]) > 1.0e-3f * std::max (1.0f, std::abs (presetReference[i])))
            return true;
    return false;
}

void PentaProcessor::markPresetReference()
{
    presetReference = readValues();
}

juce::File PentaProcessor::userPresetFolder()
{
    auto base = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    juce::File root = base.isNotEmpty() ? juce::File (base)
                                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    return root.getChildFile ("AxellPentaEQ").getChildFile ("Presets");
}

bool PentaProcessor::saveUserPreset (const juce::String& name, const juce::String& description)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory())
        return false;
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("UI"), nullptr);
    state.setProperty (kPresetNameId, name.trim(), nullptr);
    state.setProperty (kPresetDescId, description, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".pentapreset")))
        return false;
    setPresetName (name.trim());
    markPresetReference();
    return true;
}

bool PentaProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    // keep this session's UI state
    tree.removeChild (tree.getChildWithName ("UI"), nullptr);
    const auto ui = uiState().createCopy();
    pushUndo();
    restoreTree (tree);
    apvts.state.removeChild (apvts.state.getChildWithName ("UI"), nullptr);
    apvts.state.appendChild (ui, nullptr);
    setPresetName (tree.getProperty (kPresetNameId, file.getFileNameWithoutExtension()).toString());
    markPresetReference();
    return true;
}

void PentaProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    pushUndo();
    const auto current = exactState();
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (slotLock);
        slots[activeSlot] = current;
        if (slots[slot].isValid())
            target = slots[slot].createCopy();
        else
            slots[slot] = current.createCopy();
        activeSlot = slot;
    }
    if (target.isValid())
    {
        const auto ui = uiState().createCopy();
        restoreTree (target);
        apvts.state.removeChild (apvts.state.getChildWithName ("UI"), nullptr);
        apvts.state.appendChild (ui, nullptr);
    }
}

void PentaProcessor::copyActiveToOther()
{
    const auto current = exactState();
    const juce::ScopedLock sl (slotLock);
    slots[1 - activeSlot] = current;
}

// normalised value that maps to exactly `plainValue` (plain -> normalised -> plain can be off by an
// ulp on skewed ranges; a few neighbouring floats contain an exact pre-image in practice)
static float exactNormalised (const juce::RangedAudioParameter& p, float plainValue)
{
    const float n0 = juce::jlimit (0.0f, 1.0f, p.convertTo0to1 (plainValue));
    float up = n0, down = n0;
    for (int k = 0; k < 64; ++k)
    {
        if (juce::exactlyEqual (p.convertFrom0to1 (up), plainValue))
            return up;
        if (juce::exactlyEqual (p.convertFrom0to1 (down), plainValue))
            return down;
        up = std::nextafter (up, 2.0f);
        down = std::nextafter (down, -1.0f);
    }
    return n0;
}

// APVTS::replaceState skips a parameter whose snapped value already matches, so a raw host value
// could survive a restore (found by pluginval in Duo Vocal Comp): re-apply every stored value, exactly.
void PentaProcessor::restoreTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = tree.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && (child.hasProperty ("p") || child.hasProperty ("value")))
            {
                const float stored = (float) child.getProperty (child.hasProperty ("p") ? "p" : "value");
                const float norm = exactNormalised (*rp, stored);
                if (! juce::exactlyEqual (rp->getValue(), norm))
                    rp->setValueNotifyingHost (norm);
            }
        }
}

//==============================================================================
// Session state is stored as a binary ValueTree (exact float round trip: a reopened project is
// bit-identical). XML state (older builds, hand-made sessions) is still accepted.
juce::ValueTree PentaProcessor::exactState()
{
    auto state = apvts.copyState();
    // the exact plain value next to APVTS's cached one: skewed ranges would otherwise lose the last
    // bit through plain -> normalised -> plain, and a reopened project would not be bit-identical
    for (size_t i = 0; i < params.size(); ++i)
    {
        auto child = state.getChildWithProperty ("id", params[i]->paramID);
        if (child.isValid())
            child.setProperty ("p", plain (i), nullptr); // exact plain value (float -> double is exact)
    }
    return state;
}

void PentaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = exactState();
    state.setProperty (kPresetNameId, getPresetName(), nullptr);
    // A/B: which slot is active, and the inactive slot's parameters (the active one is the state itself)
    {
        juce::ValueTree ab (kAbId);
        const juce::ScopedLock sl (slotLock);
        ab.setProperty ("active", activeSlot.load(), nullptr);
        const auto& other = slots[1 - activeSlot];
        if (other.isValid())
        {
            auto o = other.createCopy();
            o.removeChild (o.getChildWithName ("UI"), nullptr);
            ab.appendChild (o, nullptr);
        }
        state.appendChild (ab, nullptr);
    }
    juce::MemoryOutputStream os (destData, false);
    os.writeInt (0x50454e51); // 'PENQ' magic
    state.writeToStream (os);
}

void PentaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ValueTree tree;
    if (sizeInBytes > 4)
    {
        juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
        if (is.readInt() == 0x50454e51)
            tree = juce::ValueTree::readFromStream (is);
    }
    if (! tree.isValid())
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            tree = juce::ValueTree::fromXml (*xml);
    if (tree.isValid() && tree.hasType (apvts.state.getType()))
    {
        // A/B (older sessions have none: slot A active, B empty, as in a new instance)
        int act = 0;
        juce::ValueTree other;
        const auto ab = tree.getChildWithName (kAbId);
        if (ab.isValid())
        {
            act = juce::jlimit (0, 1, (int) ab.getProperty ("active", 0));
            if (ab.getNumChildren() > 0 && ab.getChild (0).hasType (apvts.state.getType()))
                other = ab.getChild (0).createCopy();
            tree.removeChild (ab, nullptr);
        }
        {
            const juce::ScopedLock sl (slotLock);
            activeSlot = act;
            slots[act] = {};
            slots[1 - act] = other;
        }
        setPresetName (tree.getProperty (kPresetNameId, getPresetName()).toString());
        restoreTree (tree);
        undoPast.clear();
        undoFuture.clear();
        markPresetReference();
    }
}

juce::AudioProcessorEditor* PentaProcessor::createEditor()
{
    return new PentaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PentaProcessor();
}
