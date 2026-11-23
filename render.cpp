// Offline renderer: plays the review phrase through the real engine (no host) and writes raw stereo float.
// Same phrase as the pad review in which Till approved the sounds:
//   Dm9 -> Bbmaj7 -> F at velocity 0.4 / 0.7 / 1.0 (3 s each), then a short high voicing.
// Arp presets play the same chords through the engine's own arpeggiator at 120 BPM.
//
//   render <outdir> [preset index | -1 = all] [aftertouch ramp 0..1] [sample rate] [seed] [extra id=value ...]
// Writes <outdir>/NN.f32 (interleaved stereo float) and prints the name of each preset.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "FactoryPresets.h"
#include <cstdio>
#include <map>

struct Holder : juce::AudioProcessor
{
    Holder() : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
               apvts (*this, nullptr, "P", Params::createLayout()) {}
    const juce::String getName() const override { return "r"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;

    void set (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        else fprintf (stderr, "unknown parameter %s\n", id.toRawUTF8());
    }
    float get (const juce::String& id) const
    {
        auto* p = apvts.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
    void loadDefaults()
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) rp->setValueNotifyingHost (rp->getDefaultValue());
    }
};

struct Ev { double t, dur; int note; float vel; };

static std::vector<Ev> phrase (double& length)
{
    std::vector<Ev> e; double t = 0;
    const int prog[3][5] = { { 38, 50, 57, 60, 65 }, { 34, 46, 53, 57, 62 }, { 41, 53, 57, 60, 64 } };
    const float vel[3] = { 0.4f, 0.7f, 1.0f };
    for (int c = 0; c < 3; ++c) { for (int j = 0; j < 5; ++j) e.push_back ({ t + j * 0.015, 2.8, prog[c][j], vel[c] }); t += 3.0; }
    const int hi[4] = { 62, 69, 72, 76 };
    for (int j = 0; j < 4; ++j) e.push_back ({ t + j * 0.02, 1.5, hi[j], 0.6f });
    length = t + 4.0;
    return e;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { fprintf (stderr, "usage: render <outdir> [index] [atRamp] [sr] [seed] [id=value ...]\n"); return 1; }
    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    outDir.createDirectory();
    const int only = argc > 2 ? atoi (argv[2]) : -1;
    const float atRamp = argc > 3 ? (float) atof (argv[3]) : 0.0f;
    const double sr = argc > 4 ? atof (argv[4]) : 44100.0;
    const int seedArg = argc > 5 ? atoi (argv[5]) : 1;
    std::vector<std::pair<juce::String, float>> extra;
    for (int i = 6; i < argc; ++i) { juce::String a (argv[i]); extra.push_back ({ a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false).getFloatValue() }); }

    juce::SharedResourcePointer<WaveBank> bank;
    const auto& presets = factoryPresets();
    for (size_t pi = 0; pi < presets.size(); ++pi)
    {
        if (only >= 0 && (int) pi != only) continue;
        Holder h; h.loadDefaults();
        for (auto& kv : presets[pi].values) h.set (kv.first, kv.second);
        for (auto& kv : extra) h.set (kv.first, kv.second);
        if (getenv ("GLZ_CHECK"))
            for (auto& kv : presets[pi].values)
                if (std::abs (h.get (kv.first) - kv.second) > 1.0e-3f * (1.0f + std::abs (kv.second)))
                    printf ("  %s: wanted %g, got %g\n", kv.first, kv.second, h.get (kv.first));

        SynthEngine eng; const int B = getenv ("GLZ_BLOCK") ? atoi (getenv ("GLZ_BLOCK")) : 512;
        eng.prepare (sr, B, &(*bank));
        eng.setSeed (seedArg);
        EngineSettings s;
        double len; auto evs = phrase (len);
        if (auto* ef = getenv ("GLZ_EVENTS"))   // test hook: play a given event list instead of the phrase
        {
            evs.clear();
            juce::StringArray lines; lines.addLines (juce::File (ef).loadFileAsString());
            for (auto& l : lines) { auto t = juce::StringArray::fromTokens (l, " ", ""); if (t.size() >= 4) evs.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), t[2].getIntValue(), t[3].getFloatValue() }); }
        }
        struct Act { double t; bool on; int note; float vel; };
        std::vector<Act> acts;
        for (auto& e : evs) { acts.push_back ({ e.t, true, e.note, e.vel }); acts.push_back ({ e.t + e.dur, false, e.note, 0 }); }
        std::sort (acts.begin(), acts.end(), [] (const Act& a, const Act& b) { return a.t < b.t || (a.t == b.t && ! a.on && b.on); });

        const int total = (int) (len * sr);
        std::vector<float> out ((size_t) total * 2);
        juce::AudioBuffer<float> buf (2, B);
        size_t ai = 0; int heldCount = 0; double heldSince = 0;
        for (int pos = 0; pos < total; pos += B)
        {
            const int n = std::min (B, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            while (ai < acts.size() && acts[ai].t * sr < pos + n)
            {
                auto& a = acts[ai++];
                const int off = juce::jlimit (0, n - 1, (int) (a.t * sr) - pos);
                if (a.on) { if (heldCount++ == 0) heldSince = a.t; midi.addEvent (juce::MidiMessage::noteOn (1, a.note, a.vel), off); }
                else { --heldCount; midi.addEvent (juce::MidiMessage::noteOff (1, a.note), off); }
            }
            if (atRamp > 0)   // aftertouch: pressure builds up over 1.5 s while a chord is held
            {
                const double t = pos / sr;
                const float at = heldCount > 0 ? (float) std::min (1.0, (t - heldSince) / 1.5) * atRamp : 0.0f;
                midi.addEvent (juce::MidiMessage::channelPressureChange (1, juce::jlimit (0, 127, (int) std::lround (at * 127))), 0);
            }
            readSettings ([&h] (const juce::String& id) { return h.get (id); }, s, 120.0, 0.0f);
            eng.process (buf, midi, s);
            for (int i = 0; i < n; ++i) { out[(size_t) (pos + i) * 2] = buf.getSample (0, i); out[(size_t) (pos + i) * 2 + 1] = buf.getSample (1, i); }
        }
        auto f = outDir.getChildFile (juce::String ((int) pi).paddedLeft ('0', 2) + ".f32");
        f.deleteFile();
        juce::FileOutputStream os (f); os.write (out.data(), out.size() * sizeof (float));
        printf ("%02d %s heals=%d\n", (int) pi, presets[pi].name, eng.heals());
    }
    return 0;
}
