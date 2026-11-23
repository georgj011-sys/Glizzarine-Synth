// Fuzz test: random parameters, presets, block sizes 0-9000, sample rates 22-192 kHz, MIDI storms,
// state save/restore and preset changes, two instances side by side. Run under ASan/UBSan.
//   test_fuzz [seconds=60] [seed=1]
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <random>
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const double seconds = argc > 1 ? atof (argv[1]) : 60.0;
    std::mt19937 g ((unsigned) (argc > 2 ? atoi (argv[2]) : 1));
    auto ri = [&] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (g); };
    auto rf = [&] { return std::uniform_real_distribution<float> (0.0f, 1.0f) (g); };
    std::unique_ptr<GlizzarineProcessor> inst[2] { std::make_unique<GlizzarineProcessor>(), std::make_unique<GlizzarineProcessor>() };
    const double rates[] = { 22050, 44100, 48000, 88200, 96000, 192000 };
    long bad = 0, blocks = 0; double rendered = 0;
    const double t0 = Time::getMillisecondCounterHiRes();
    int round = 0;
    while (Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0)
    {
        ++round;
        const double sr = rates[ri (0, 5)];
        const int maxBlock = ri (0, 3) == 0 ? ri (1, 64) : ri (64, 4096);
        for (auto& p : inst) { p->setNonRealtime (ri (0, 1) == 1); p->prepareToPlay (sr, maxBlock); p->loadFactory (ri (0, p->getNumPrograms() - 1)); }
        auto& params = inst[0]->getParameters();
        AudioBuffer<float> buf (2, 9000);
        for (int b = 0; b < 400; ++b)
        {
            const int n = ri (0, 9) == 0 ? ri (0, 9000) : ri (1, maxBlock);
            for (int k = 0; k < 2; ++k)
            {
                auto& p = inst[k];
                buf.setSize (2, n, false, false, true);
                MidiBuffer m;
                const int events = ri (0, 9) == 0 ? ri (10, 80) : ri (0, 3);   // sometimes a MIDI storm
                for (int e = 0; e < events; ++e)
                {
                    const int pos = n > 0 ? ri (0, n - 1) : 0;
                    switch (ri (0, 9))
                    {
                        case 0: case 1: case 2: m.addEvent (MidiMessage::noteOn (1, ri (0, 127), (uint8) ri (1, 127)), pos); break;
                        case 3: case 4: m.addEvent (MidiMessage::noteOff (1, ri (0, 127)), pos); break;
                        case 5: m.addEvent (MidiMessage::pitchWheel (1, ri (0, 16383)), pos); break;
                        case 6: m.addEvent (MidiMessage::channelPressureChange (1, ri (0, 127)), pos); break;
                        case 7: m.addEvent (MidiMessage::controllerEvent (1, ri (0, 127), ri (0, 127)), pos); break;
                        case 8: m.addEvent (MidiMessage::controllerEvent (1, 64, ri (0, 1) * 127), pos); break;
                        default: m.addEvent (MidiMessage::allNotesOff (1), pos); break;
                    }
                }
                if (ri (0, 4) == 0)
                    for (int q = 0; q < ri (1, 12); ++q)
                        params[(size_t) ri (0, params.size() - 1)]->setValueNotifyingHost (ri (0, 5) == 0 ? (float) ri (0, 1) : rf());
                if (k == 1 && ri (0, 4) == 0)   // the second instance gets the same random moves via its own parameters
                    for (auto* pp : p->getParameters()) if (ri (0, 30) == 0) pp->setValueNotifyingHost (rf());
                p->processBlock (buf, m);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = buf.getSample (ch, i);
                        if (! std::isfinite (v) || std::abs (v) > 1.0f) ++bad;
                    }
                ++blocks; rendered += n / sr;
            }
            if (ri (0, 60) == 0) { MemoryBlock mb; inst[0]->getStateInformation (mb); inst[1]->setStateInformation (mb.getData(), (int) mb.getSize()); }
            if (ri (0, 80) == 0) inst[ri (0, 1)]->loadFactory (ri (0, inst[0]->getNumPrograms() - 1));
            if (ri (0, 150) == 0) inst[0]->switchAB (ri (0, 1));
        }
    }
    printf ("fuzz %.0f s: %d rounds, %ld blocks, %.0f s of audio, bad samples %ld, repairs %d / %d\n",
            seconds, round, blocks, rendered, bad, inst[0]->heals.load(), inst[1]->heals.load());
    return bad ? 1 : 0;
}
