// Export test, like a Fast Bounce in Sonar: offline (non-realtime), huge and odd block sizes, sample-rate change,
// 3 minutes of an arp and a pad per preset. Fails if the output goes silent while notes are held,
// if anything is not finite, or if the output clips.
//   test_bounce [seconds per preset=40]
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const double secs = argc > 1 ? atof (argv[1]) : 40.0;
    int problems = 0;
    const int blocks[] = { 8192, 4096, 1000, 441, 32, 16384 };
    const double rates[] = { 44100, 48000, 96000 };
    GlizzarineProcessor p;
    for (int preset = 0; preset < p.getNumPrograms(); ++preset)
    {
        const double sr = rates[preset % 3];
        const int bs = blocks[preset % 6];
        p.setNonRealtime (true);
        p.prepareToPlay (sr, bs);
        p.loadFactory (preset);
        AudioBuffer<float> buf (2, bs);
        const int total = (int) (secs * sr);
        double silentRun = 0, worstSilent = 0, peak = 0; long bad = 0;
        int chord = 0; int changedAt = -1;
        const int prog[4][4] = { { 50, 57, 60, 65 }, { 46, 53, 57, 62 }, { 41, 48, 57, 64 }, { 43, 50, 55, 62 } };
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos);
            buf.setSize (2, n, false, false, true);
            MidiBuffer m;
            // a new chord every 2 s, held for 1.9 s
            const int period = (int) (2.0 * sr), hold = (int) (1.9 * sr);
            for (int i = 0; i < n; ++i)
            {
                const int t = pos + i;
                if (t % period == 0) { for (int k : prog[chord % 4]) m.addEvent (MidiMessage::noteOn (1, k, (uint8) 90), i); }
                if (t % period == hold) { for (int k : prog[chord % 4]) m.addEvent (MidiMessage::noteOff (1, k), i); ++chord; }
            }
            if (pos > total / 2 && pos - bs <= total / 2) { p.prepareToPlay (sr == 48000 ? 44100 : 48000, bs); changedAt = pos; }   // sample rate change mid-way (hosts only do this while stopped: held notes end)
            p.processBlock (buf, m);
            double blockPeak = 0;
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) ++bad;
                blockPeak = std::max (blockPeak, (double) std::abs (v));
            }
            peak = std::max (peak, blockPeak);
            const bool afterChange = changedAt >= 0 && pos - changedAt < (int) (2.5 * sr);
            if (blockPeak < 1.0e-5 && pos > sr && ! afterChange) silentRun += n / sr; else silentRun = 0;
            worstSilent = std::max (worstSilent, silentRun);
        }
        const bool ok = bad == 0 && worstSilent < 0.2 && peak <= 1.0;
        if (! ok) ++problems;
        printf ("%s %-20s sr %6.0f block %5d: peak %.2f, longest silence %.2f s, bad %ld, repairs %d\n", ok ? "OK  " : "FAIL",
                p.getProgramName (preset).toRawUTF8(), sr, bs, peak, worstSilent, bad, p.heals.load());
    }
    printf ("\n%d problem(s)\n", problems);
    return problems ? 1 : 0;
}
