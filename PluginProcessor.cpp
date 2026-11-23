#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    bool isGlobalParam (const juce::String& id) { return Params::isGlobal (id); }

    juce::String targetLabel (const juce::String& t)
    {
        if (t.length() == 2 && (t[0] == 'E' || t[0] == 'F') && t[1] >= '1' && t[1] <= '8')
            return (t[0] == 'E' ? "Knob " : "Fader ") + juce::String::charToString (t[1]);
        return {};
    }
}

//==============================================================================
GlizzarineProcessor::GlizzarineProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "PARAMS", Params::createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            paramIndex[rp->paramID.toStdString()] = (int) params.size();
            params.push_back (rp);
            ids.push_back (rp->paramID.toStdString());
        }
    // string_views point into "ids", which never changes again
    for (auto& id : ids)
        rawById[std::string_view (id)] = apvts.getRawParameterValue (id);

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto sw = Params::switchFor (params[i]->paramID);
        if (sw.isNotEmpty())
        {
            const auto it = paramIndex.find (sw.toStdString());
            if (it != paramIndex.end())
                switched.push_back ({ (int) i, it->second, params[i]->getValue() });
        }
    }

    for (int i = 0; i < 128; ++i) { ccRawValue[(size_t) i].store (0); ccEvents[(size_t) i].store (0); ccLast[(size_t) i] = -1.0f; }

    loadControllerMap();
    scanUserPresets();
    loadFactory (0);
    abState[0] = abState[1] = soundState();
    undoManager.clearUndoHistory();
    startTimerHz (60);
}

GlizzarineProcessor::~GlizzarineProcessor() { stopTimer(); }

//==============================================================================
float GlizzarineProcessor::raw (const char* id) const noexcept
{
    const auto it = rawById.find (std::string_view (id));
    return it != rawById.end() ? it->second->load() : 0.0f;
}

float GlizzarineProcessor::valueOf (const juce::String& id) const
{
    const auto it = paramIndex.find (id.toStdString());
    if (it == paramIndex.end()) return 0.0f;
    auto* p = params[(size_t) it->second];
    return p->convertFrom0to1 (p->getValue());
}

void GlizzarineProcessor::setParam (const juce::String& id, float realValue)
{
    const auto it = paramIndex.find (id.toStdString());
    if (it == paramIndex.end()) return;
    auto* p = params[(size_t) it->second];
    p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

//==============================================================================
void GlizzarineProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    engine.prepare (sampleRate, juce::jlimit (64, 2048, samplesPerBlock), &(*waveBank));
    setLatencySamples (engine.getLatencySamples());
    keyboardState.reset();
    ownBeat = 0.0;
}

bool GlizzarineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GlizzarineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);   // adds notes played on the on-screen keyboard

    // KeyLab controllers: remember the values, the timer applies them (soft takeover, learn)
    for (const auto md : midi)
    {
        const auto m = md.getMessage();
        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc != 1 && cc != 64 && cc < 120)
            {
                ccRawValue[(size_t) cc].store (m.getControllerValue());
                ccEvents[(size_t) cc].fetch_add (1);
                lastCC.store (cc);
            }
        }
        else if (m.isProgramChange()) pendingProgram.store (m.getProgramChangeNumber());
    }

    // tempo and time signature
    double bpm = 120.0, ppq = 0.0; bool playing = false; int hostNum = 4, hostDen = 4;
    const bool ownClock = raw ("clock_mode") > 0.5f;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) { hostNum = juce::jmax (1, ts->numerator); hostDen = juce::jmax (1, ts->denominator); }
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) { ppq = *q; playing = true; }
        }
    if (ownClock)
    {
        bpm = raw ("own_bpm");
        ppq = ownBeat; playing = true;
        ownBeat += n * bpm / 60.0 / sampleRate;
    }
    if (! std::isfinite (bpm) || bpm < 20.0 || bpm > 400.0) bpm = 120.0;
    bpmShown.store ((float) bpm);

    readSettings ([this] (const char* id) { return raw (id); }, settings, bpm, engine.getModWheel());
    const int tsChoice = (int) std::lround (raw ("time_sig"));
    if (tsChoice == 0) { settings.tsNum = hostNum; settings.tsDen = hostDen; }
    else Params::timeSig (tsChoice, settings.tsNum, settings.tsDen);
    settings.fp.ppq = ppq; settings.fp.hasPpq = playing;

    engine.process (buffer, midi, settings);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    if (buffer.getNumChannels() >= 2)
    {
        int w = scopeWrite.load();
        const float* L = buffer.getReadPointer (0); const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < n; i += 2) { scope[(size_t) w] = 0.5f * (L[i] + R[i]); w = (w + 1) & 2047; }
        scopeWrite.store (w);
    }
    arpStepShown.store (settings.arp.on ? engine.arpStepInBar() : -1);
    arpBarSteps.store (engine.arpStepsPerBar());
    voicesShown.store (engine.activeVoices());
    heals.store (engine.heals());
    midi.clear();
}

//==============================================================================
void GlizzarineProcessor::triggerNote (int note, bool on, float vel)
{
    if (on) keyboardState.noteOn (1, note, vel);
    else    keyboardState.noteOff (1, note, 0.0f);
}

void GlizzarineProcessor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        if (avg > 0.0) setParam ("own_bpm", (float) juce::jlimit (40.0, 240.0, 60000.0 / avg));
        setParam ("clock_mode", 1.0f);
    }
}

//==============================================================================
// Presets
juce::String GlizzarineProcessor::macroName (int i) const
{
    const auto id = macroParam (i);
    if (id.isEmpty()) return "-";
    if (auto* p = apvts.getParameter (id)) return p->getName (32);
    return id;
}

void GlizzarineProcessor::resetToDefaults()
{
    for (auto* p : params)
        if (! isGlobalParam (p->paramID))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void GlizzarineProcessor::applyValues (const std::vector<std::pair<const char*, float>>& values)
{
    for (auto& kv : values)
        if (auto* p = apvts.getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
}

void GlizzarineProcessor::afterPresetLoad()
{
    for (int i = 0; i < 128; ++i) { ccCaught[(size_t) i] = false; ccLast[(size_t) i] = -1.0f; }   // soft takeover again
    syncSwitched();
    undoManager.clearUndoHistory();
    ++presetVersion;
}

void GlizzarineProcessor::loadFactory (int index)
{
    const auto& all = factoryPresets();
    if (all.empty()) return;
    index = juce::jlimit (0, (int) all.size() - 1, index);
    const auto& pr = all[(size_t) index];
    resetToDefaults();
    applyValues (pr.values);
    for (int i = 0; i < 8; ++i) macros[(size_t) i] = pr.macros[i] != nullptr ? pr.macros[i] : "";
    presetName = pr.name; presetCategory = pr.category;
    currentFactory.store (index); currentUser = -1;
    afterPresetLoad();
}

juce::File GlizzarineProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Glizzarine Presets");
}

void GlizzarineProcessor::scanUserPresets()
{
    userPresets.clear();
    for (auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.glzpreset"))
        userPresets.push_back ({ f.getFileNameWithoutExtension(), f });
    std::sort (userPresets.begin(), userPresets.end(), [] (const UserPreset& a, const UserPreset& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

juce::ValueTree GlizzarineProcessor::soundState() const
{
    juce::ValueTree t ("SOUND");
    for (auto* p : params)
        if (! isGlobalParam (p->paramID))
            t.setProperty (p->paramID, p->convertFrom0to1 (p->getValue()), nullptr);
    for (int i = 0; i < 8; ++i) t.setProperty ("E" + juce::String (i + 1), macros[(size_t) i], nullptr);
    t.setProperty ("presetName", presetName, nullptr);
    t.setProperty ("presetCategory", presetCategory, nullptr);
    return t;
}

void GlizzarineProcessor::restoreSound (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    resetToDefaults();
    for (auto* p : params)
        if (! isGlobalParam (p->paramID) && t.hasProperty (p->paramID))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) t.getProperty (p->paramID)));
    for (int i = 0; i < 8; ++i)
    {
        const auto k = "E" + juce::String (i + 1);
        if (t.hasProperty (k)) macros[(size_t) i] = t.getProperty (k).toString();
    }
    if (t.hasProperty ("presetName")) presetName = t.getProperty ("presetName").toString();
    if (t.hasProperty ("presetCategory")) presetCategory = t.getProperty ("presetCategory").toString();
}

bool GlizzarineProcessor::saveUser (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    presetName = name.trim(); presetCategory = "MY PRESETS";
    auto t = soundState();
    auto xml = t.createXml();
    const auto f = folder.getChildFile (clean + ".glzpreset");
    if (xml == nullptr || ! xml->writeTo (f)) return false;
    scanUserPresets();
    for (size_t i = 0; i < userPresets.size(); ++i) if (userPresets[i].file == f) currentUser = (int) i;
    currentFactory.store (-1);
    ++presetVersion;
    return true;
}

void GlizzarineProcessor::loadUser (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size())) return;
    if (auto xml = juce::XmlDocument::parse (userPresets[(size_t) index].file))
    {
        restoreSound (juce::ValueTree::fromXml (*xml));
        presetName = userPresets[(size_t) index].name; presetCategory = "MY PRESETS";
        currentUser = index; currentFactory.store (-1);
        afterPresetLoad();
    }
}

bool GlizzarineProcessor::deleteUser (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size())) return false;
    const bool ok = userPresets[(size_t) index].file.deleteFile();
    scanUserPresets();
    currentUser = -1;
    ++presetVersion;
    return ok;
}

void GlizzarineProcessor::stepPreset (int delta)
{
    const int nf = (int) factoryPresets().size(), nu = (int) userPresets.size(), total = nf + nu;
    if (total == 0) return;
    int cur = currentFactory.load() >= 0 ? currentFactory.load() : (currentUser >= 0 ? nf + currentUser : 0);
    cur = ((cur + delta) % total + total) % total;
    if (cur < nf) loadFactory (cur); else loadUser (cur - nf);
}

int GlizzarineProcessor::getNumPrograms() { return juce::jmax (1, (int) factoryPresets().size()); }
void GlizzarineProcessor::setCurrentProgram (int index) { if (index >= 0 && index != currentFactory.load()) loadFactory (index); }
const juce::String GlizzarineProcessor::getProgramName (int index)
{
    const auto& all = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) all.size()) ? juce::String (all[(size_t) index].name) : juce::String();
}

//==============================================================================
// A/B: two complete sounds; switching keeps what you did in each
void GlizzarineProcessor::switchAB (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot) return;
    abState[abSlot] = soundState();
    abSlot = slot;
    restoreSound (abState[abSlot]);
    afterPresetLoad();
}

void GlizzarineProcessor::copyToOtherSlot() { abState[1 - abSlot] = soundState(); ++presetVersion; }

//==============================================================================
// Effects switch themselves on when you turn up one of their knobs
void GlizzarineProcessor::autoSwitchEffects()
{
    for (auto& w : switched)
    {
        const float v = params[(size_t) w.param]->getValue();
        if (v > w.last + 1.0e-4f && params[(size_t) w.sw]->getValue() < 0.5f)
            params[(size_t) w.sw]->setValueNotifyingHost (1.0f);
        w.last = v;
    }
}

void GlizzarineProcessor::syncSwitched() { for (auto& w : switched) w.last = params[(size_t) w.param]->getValue(); }

//==============================================================================
// KeyLab and MIDI learn
juce::String GlizzarineProcessor::resolveTarget (const juce::String& t) const
{
    if (t.length() == 2 && t[0] == 'E') return macroParam (t.getTrailingIntValue() - 1);
    if (t.length() == 2 && t[0] == 'F') return KeyLab::faderParam (t.getTrailingIntValue() - 1);
    return t;
}

void GlizzarineProcessor::timerCallback()
{
    autoSwitchEffects();

    const int program = pendingProgram.exchange (-1);
    if (program >= 0) loadFactory (program);

    // learning: the next controller you move gets the target
    if (learning.isNotEmpty() || wizard >= 0)
    {
        const int cc = lastCC.exchange (-1);
        if (cc >= 0)
        {
            if (wizard >= 0)
            {
                assignCC (cc, (wizard < 8 ? "E" : "F") + juce::String (wizard % 8 + 1));
                if (++wizard >= 16) wizard = -1;
            }
            else { assignCC (cc, learning); learning.clear(); }
            ccEvents[(size_t) cc].store (0);
            ++presetVersion;
        }
        return;
    }

    for (int cc = 0; cc < 128; ++cc)
    {
        const int events = ccEvents[(size_t) cc].exchange (0);
        if (events == 0 || ccMap[(size_t) cc].isEmpty()) continue;
        const auto id = resolveTarget (ccMap[(size_t) cc]);
        const auto it = paramIndex.find (id.toStdString());
        if (it == paramIndex.end()) continue;
        auto* p = params[(size_t) it->second];
        const int v7 = ccRawValue[(size_t) cc].load();
        const bool isEncoder = ccMap[(size_t) cc].startsWith ("E");

        if (isEncoder && relativeEncoders)
        {
            // relative encoders: 1..63 = up, 65..127 = down (two's complement style)
            const int steps = v7 < 64 ? v7 : v7 - 128;
            const float nv = juce::jlimit (0.0f, 1.0f, p->getValue() + (float) (steps * juce::jmax (1, events)) / 200.0f);
            p->beginChangeGesture(); p->setValueNotifyingHost (nv); p->endChangeGesture();
            continue;
        }

        const float val = (float) v7 / 127.0f;
        const float current = p->getValue();
        // soft takeover: the control only grabs the value once it passes the current setting
        if (! ccCaught[(size_t) cc])
        {
            const float last = ccLast[(size_t) cc];
            if (std::abs (val - current) < 0.03f || (last >= 0.0f && (last - current) * (val - current) <= 0.0f))
                ccCaught[(size_t) cc] = true;
        }
        ccLast[(size_t) cc] = val;
        if (ccCaught[(size_t) cc])
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (val); p->endChangeGesture();
        }
    }
}

void GlizzarineProcessor::assignCC (int cc, const juce::String& target)
{
    for (auto& m : ccMap) if (m == target) m.clear();
    ccMap[(size_t) cc] = target;
    ccCaught[(size_t) cc] = false; ccLast[(size_t) cc] = -1.0f;
    saveControllerMap();
}

void GlizzarineProcessor::startLearn (const juce::String& target) { wizard = -1; learning = target; lastCC.store (-1); ++presetVersion; }
void GlizzarineProcessor::startWizard() { learning.clear(); wizard = 0; lastCC.store (-1); ++presetVersion; }
void GlizzarineProcessor::cancelLearn() { wizard = -1; learning.clear(); ++presetVersion; }
void GlizzarineProcessor::forgetMapping (const juce::String& target) { for (auto& m : ccMap) if (m == target) m.clear(); saveControllerMap(); ++presetVersion; }
void GlizzarineProcessor::clearMappings() { for (auto& m : ccMap) m.clear(); saveControllerMap(); ++presetVersion; }
void GlizzarineProcessor::setEncodersRelative (bool r) { relativeEncoders = r; saveControllerMap(); ++presetVersion; }

int GlizzarineProcessor::ccFor (const juce::String& target) const
{
    for (int i = 0; i < 128; ++i) if (ccMap[(size_t) i] == target) return i;
    return -1;
}

juce::String GlizzarineProcessor::wizardText() const
{
    if (wizard < 0) return {};
    return (wizard < 8 ? "Turn knob " + juce::String (wizard + 1) : "Move fader " + juce::String (wizard - 7))
           + " on your KeyLab  (" + juce::String (wizard + 1) + " of 16)";
}

juce::String GlizzarineProcessor::describeTarget (const juce::String& t) const
{
    const auto lab = targetLabel (t);
    if (lab.isNotEmpty())
    {
        const int i = t.getTrailingIntValue() - 1;
        return lab + "  (" + (t[0] == 'E' ? macroName (i) : juce::String (KeyLab::faderName (i))) + ")";
    }
    if (auto* p = apvts.getParameter (t)) return p->getName (40);
    return t;
}

juce::File GlizzarineProcessor::dataDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Glizzarine");
    d.createDirectory();
    return d;
}

void GlizzarineProcessor::loadControllerMap()
{
    if (auto xml = juce::XmlDocument::parse (dataDir().getChildFile ("controller.xml")))
    {
        relativeEncoders = xml->getBoolAttribute ("relative", false);
        for (auto* e : xml->getChildIterator())
        {
            const int cc = e->getIntAttribute ("cc", -1);
            if (cc >= 0 && cc < 128) ccMap[(size_t) cc] = e->getStringAttribute ("id");
        }
    }
}

void GlizzarineProcessor::saveControllerMap()
{
    juce::XmlElement root ("MAP");
    root.setAttribute ("relative", relativeEncoders);
    for (int i = 0; i < 128; ++i)
        if (ccMap[(size_t) i].isNotEmpty())
        {
            auto* e = root.createNewChildElement ("M");
            e->setAttribute ("cc", i);
            e->setAttribute ("id", ccMap[(size_t) i]);
        }
    root.writeTo (dataDir().getChildFile ("controller.xml"));
}

//==============================================================================
void GlizzarineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("EXTRA");
    for (int i = 0; i < 8; ++i) extra.setProperty ("E" + juce::String (i + 1), macros[(size_t) i], nullptr);
    extra.setProperty ("presetName", presetName, nullptr);
    extra.setProperty ("presetCategory", presetCategory, nullptr);
    extra.setProperty ("factory", currentFactory.load(), nullptr);
    juce::ValueTree root ("GLIZZARINE");
    root.setProperty ("version", GLZ_VERSION, nullptr);
    root.appendChild (state.createCopy(), nullptr);
    root.appendChild (extra, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void GlizzarineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("GLIZZARINE")) return;
    auto state = root.getChildWithName (apvts.state.getType());
    if (state.isValid()) apvts.replaceState (state);
    auto extra = root.getChildWithName ("EXTRA");
    if (extra.isValid())
    {
        for (int i = 0; i < 8; ++i) macros[(size_t) i] = extra.getProperty ("E" + juce::String (i + 1)).toString();
        presetName = extra.getProperty ("presetName").toString();
        presetCategory = extra.getProperty ("presetCategory").toString();
        currentFactory.store ((int) extra.getProperty ("factory", -1));
    }
    currentUser = -1;
    afterPresetLoad();
}

juce::AudioProcessorEditor* GlizzarineProcessor::createEditor() { return new GlizzarineEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlizzarineProcessor(); }
