// CPU check: 10 voices, every effect on (drive at 0 so nothing distorts), long held chord + arp.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
using namespace juce;
int main()
{
    ScopedJuceInitialiser_GUI init;
    GlizzarineProcessor p;
    p.prepareToPlay (48000, 256);
    p.loadFactory (12);   // Monolith: swarm, physics, ring mod
    for (auto* id : { "tape_on", "chorus_on", "phaser_on", "delay_on", "grains_on", "reverb_on", "shimmer_on", "gate_on", "stereo_on" }) p.setParam (id, 1.0f);
    p.setParam ("tape", 0.3f); p.setParam ("phaser_mix", 0.4f); p.setParam ("cloud_mix", 0.4f); p.setParam ("reverb_shimmer", 0.4f); p.setParam ("gate_depth", 0.3f);
    p.setParam ("chorus_type", 1.0f); p.setParam ("amp_release", 6.0f);
    AudioBuffer<float> b (2, 256);
    const int blocks = (int) (60.0 * 48000 / 256);
    const double t0 = Time::getMillisecondCounterHiRes();
    int maxVoices = 0;
    for (int k = 0; k < blocks; ++k)
    {
        MidiBuffer m;
        if (k % 200 == 0) for (int i = 0; i < 10; ++i) m.addEvent (MidiMessage::noteOn (1, 40 + i * 4 + (k / 200) % 3, (uint8) 100), 0);
        if (k % 200 == 150) for (int i = 0; i < 10; ++i) m.addEvent (MidiMessage::noteOff (1, 40 + i * 4 + (k / 200) % 3), 0);
        p.processBlock (b, m);
        maxVoices = jmax (maxVoices, p.voicesShown.load());
    }
    const double ms = Time::getMillisecondCounterHiRes() - t0;
    printf ("60 s of audio with %d voices + all effects in %.0f ms: %.1f %% of one CPU core (x%.0f realtime)\n", maxVoices, ms, ms / 600.0, 60000.0 / ms);
    return 0;
}
