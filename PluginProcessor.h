#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>
#include "Engine.h"
#include "FactoryPresets.h"

#ifndef GLZ_VERSION
 #define GLZ_VERSION "v1.0"
#endif

// The KeyLab: 8 encoders control E1-E8 (the preset's character parameters), 8 faders are always the same.
namespace KeyLab
{
    inline const char* faderParam (int i)
    {
        static const char* f[8] = { "o1_level", "o2_level", "o3_level", "amp_attack", "amp_release", "delay_mix", "reverb_mix", "master" };
        return f[juce::jlimit (0, 7, i)];
    }
    inline const char* faderName (int i)
    {
        static const char* f[8] = { "Osc 1", "Osc 2", "Osc 3", "Attack", "Release", "Delay", "Reverb", "Volume" };
        return f[juce::jlimit (0, 7, i)];
    }
}

class GlizzarineProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GlizzarineProcessor();
    ~GlizzarineProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Glizzarine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentFactory.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    float valueOf (const juce::String& id) const;
    void setParam (const juce::String& id, float realValue);

    // presets
    struct UserPreset { juce::String name; juce::File file; };
    void loadFactory (int index);
    void loadUser (int index);
    bool saveUser (const juce::String& name);
    bool deleteUser (int index);
    void scanUserPresets();
    juce::File userPresetFolder() const;
    const std::vector<UserPreset>& getUserPresets() const { return userPresets; }
    juce::String getPresetName() const { return presetName; }
    juce::String getPresetCategory() const { return presetCategory; }
    int getCurrentFactory() const { return currentFactory.load(); }
    int getCurrentUser() const { return currentUser; }
    void stepPreset (int delta);
    std::atomic<int> presetVersion { 0 };   // bumps whenever presets, macros or mappings change

    // E1-E8: which parameter each encoder controls in the current sound
    juce::String macroParam (int i) const { return macros[(size_t) juce::jlimit (0, 7, i)]; }
    juce::String macroName (int i) const;

    // A/B compare
    void switchAB (int slot);
    void copyToOtherSlot();
    int getABSlot() const { return abSlot; }

    // KeyLab / MIDI learn (mapping is global: saved for all projects)
    void startLearn (const juce::String& target);     // param id, or "E1".."E8", "F1".."F8"
    void startWizard();
    void cancelLearn();
    void forgetMapping (const juce::String& target);
    void clearMappings();
    int ccFor (const juce::String& target) const;
    juce::String learnTarget() const { return learning; }
    int wizardStep() const { return wizard; }
    juce::String wizardText() const;
    juce::String describeTarget (const juce::String& target) const;
    bool encodersRelative() const { return relativeEncoders; }
    void setEncodersRelative (bool r);

    // shown in the UI
    std::array<float, 2048> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<int> arpStepShown { -1 }, arpBarSteps { 16 }, voicesShown { 0 }, heals { 0 };
    std::atomic<float> bpmShown { 120.0f };
    void tap();
    void triggerNote (int note, bool on, float vel = 0.8f);   // on-screen keyboard / preset audition

private:
    void timerCallback() override;
    void applyValues (const std::vector<std::pair<const char*, float>>& values);
    void resetToDefaults();
    void afterPresetLoad();
    void autoSwitchEffects();
    void syncSwitched();
    juce::ValueTree soundState() const;
    void restoreSound (const juce::ValueTree& t);
    juce::File dataDir() const;
    void loadControllerMap();
    void saveControllerMap();
    void assignCC (int cc, const juce::String& target);
    juce::String resolveTarget (const juce::String& target) const;
    float raw (const char* id) const noexcept;

    juce::SharedResourcePointer<WaveBank> waveBank;
    SynthEngine engine;
    EngineSettings settings;
    double sampleRate = 44100.0;
    double ownBeat = 0.0;

    // audio-thread parameter access without allocation
    std::vector<std::string> ids;
    std::unordered_map<std::string_view, std::atomic<float>*> rawById;

    std::vector<juce::RangedAudioParameter*> params;
    std::unordered_map<std::string, int> paramIndex;
    struct Switched { int param, sw; float last; };
    std::vector<Switched> switched;

    std::atomic<int> currentFactory { 0 };
    int currentUser = -1;
    juce::String presetName, presetCategory;
    std::array<juce::String, 8> macros;
    std::vector<UserPreset> userPresets;

    juce::ValueTree abState[2];
    int abSlot = 0;

    // controller map: cc -> "E1".."E8", "F1".."F8" or a parameter id
    std::array<juce::String, 128> ccMap;
    std::array<std::atomic<int>, 128> ccRawValue;   // last 7-bit value from the audio thread
    std::array<std::atomic<int>, 128> ccEvents;     // number of messages since the timer last looked
    std::array<bool, 128> ccCaught {};
    std::array<float, 128> ccLast {};
    std::atomic<int> lastCC { -1 };
    juce::String learning;
    int wizard = -1;
    bool relativeEncoders = false;
    std::atomic<int> pendingProgram { -1 };
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlizzarineProcessor)
};
