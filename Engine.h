#pragma once
// The complete synth without any plugin code: voices, voice allocation, arpeggiator, effects.
// The plugin and the offline renderer both use exactly this class, so what the renderer
// produces is what you hear in Sonar.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstring>
#include "Voice.h"
#include "FX.h"
#include "Arp.h"
#include "Mapping.h"

struct ArpSettings
{
    bool on = false, hold = false;
    bool topLine = false;   // the highest held note plays its own line, a little after the main pattern
    int mode = 0, speed = 0, octaves = 1, rhythm = 0;
    float gate = 0.6f, swing = 0.0f, mutate = 0.0f, glitch = 0.0f;
    bool steps[16] {};
};

struct EngineSettings
{
    VoiceParams vp;
    FxParams fp;
    ArpSettings arp;
    int mode = 2;                     // 0 mono, 1 legato, 2 poly
    double bpm = 120.0;
    int tsNum = 4, tsDen = 4;
    double ppq = 0.0;                 // host position in quarter notes (only used when playing)
    bool playing = false;
};

// Reads every engine setting from a "get (id) -> real value" function (APVTS or a plain map)
template <typename Getter>
void readSettings (Getter&& get, EngineSettings& s, double bpm, float modWheel)
{
    mapParams (get, s.vp, s.fp, bpm, modWheel);
    auto geti = [&get] (const char* id) { return (int) std::lround (get (id)); };
    s.mode = juce::jlimit (0, 2, geti ("mode"));
    s.bpm = bpm;
    auto& a = s.arp;
    a.on = geti ("arp_on") == 1;
    a.hold = geti ("arp_hold") == 1;
    a.topLine = geti ("arp_topline") == 1;
    a.mode = juce::jlimit (0, 11, geti ("arp_mode"));
    a.speed = juce::jlimit (0, 2, geti ("arp_speed"));
    a.octaves = juce::jlimit (1, 4, geti ("arp_octaves"));
    a.rhythm = juce::jlimit (0, Params::customRhythm, geti ("arp_rhythm"));
    a.gate = get ("arp_gate");
    a.swing = get ("arp_swing");
    a.mutate = get ("arp_mutate");
    a.glitch = get ("arp_glitch");
    for (int i = 0; i < 16; ++i) a.steps[i] = get (Params::arpStepKey (i)) > 0.5f;
}

class SynthEngine
{
public:
   #ifndef GLZ_NUM_VOICES
    #define GLZ_NUM_VOICES 10
   #endif
    static constexpr int numVoices = GLZ_NUM_VOICES;

    void prepare (double sampleRate, int maxBlock, const WaveBank* bank)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        blockMax = juce::jlimit (16, 8192, maxBlock > 0 ? maxBlock : 512);
        for (auto& v : voices) v.prepare (sr, bank);
        fx.prepare (sr, blockMax);
        resetState();
    }

    void setSeed (juce::int64 s)   // makes renders repeatable (tests); the plugin never calls it
    {
        for (int i = 0; i < numVoices; ++i) voices[(size_t) i].setSeed (s * 31 + i);
        fx.setSeed (s * 7 + 3);
        arpRng.setSeed (s * 13 + 5);
    }

    int getLatencySamples() const noexcept { return fx.getLatencySamples(); }
    int heals() const noexcept { int h = fx.heals; for (auto& v : voices) h += v.heals; return h; }
    int fxHeals() const noexcept { return fx.heals; }
    int activeVoices() const noexcept { int n = 0; for (auto& v : voices) if (v.isActive()) ++n; return n; }
    int arpStepInBar() const noexcept { return arpDisplay; }
    int arpStepsPerBar() const noexcept { return arpBarSteps; }

    void resetState()
    {
        for (auto& v : voices) v.kill();
        held.clear(); physical.clear(); sustained.clear(); arpNotes.clear(); noteStack.clear();
        pendingCount = 0; arpRunning = false; lastNote = -1; clock = 0.0;
        pedal = false; bend = 0.0f; modWheel = 0.0f; aftertouch = 0.0f;
        lastMode = -1; arpWasOn = false; arpDisplay = -1;
        fx.reset();
    }

    float getModWheel() const noexcept { return modWheel; }

    // Renders one host block. Any block size works: it is cut into pieces of at most blockMax.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, EngineSettings& s)
    {
        const int total = buffer.getNumSamples();
        buffer.clear();
        if (buffer.getNumChannels() < 2 || total <= 0) { clock += juce::jmax (0, total); return; }
        if (! std::isfinite (s.bpm) || s.bpm < 20.0 || s.bpm > 400.0) s.bpm = 120.0;

        auto it = midi.begin();
        for (int done = 0; done < total; )
        {
            const int n = juce::jmin (blockMax, total - done);
            processPiece (buffer, done, n, midi, it, s);
            done += n;
        }
    }

    // direct note control (renderer, UI keyboard)
    void noteOn (int note, float vel, const EngineSettings& s)  { handleNoteOn (note, vel, s, clock); }
    void noteOff (int note, const EngineSettings& s)            { handleNoteOff (note, s); }
    void setAftertouch (float a) noexcept { aftertouch = juce::jlimit (0.0f, 1.0f, a); }

private:
    struct Pending { double time; int note; float vel; bool on; float swell; };
    struct NoteSet   // small fixed-size note list, never allocates
    {
        int n[128] {}; int count = 0;
        void clear() noexcept { count = 0; }
        bool contains (int v) const noexcept { for (int i = 0; i < count; ++i) if (n[i] == v) return true; return false; }
        void add (int v) noexcept { if (! contains (v) && count < 128) n[count++] = v; }
        void push (int v) noexcept { remove (v); if (count < 128) n[count++] = v; }
        void remove (int v) noexcept { int k = 0; for (int i = 0; i < count; ++i) if (n[i] != v) n[k++] = n[i]; count = k; }
        bool empty() const noexcept { return count == 0; }
        int last() const noexcept { return count > 0 ? n[count - 1] : -1; }
    };

    void processPiece (juce::AudioBuffer<float>& buffer, int offset, int n, const juce::MidiBuffer& midi,
                       juce::MidiBufferIterator& it, EngineSettings& s)
    {
        // voice mode change: release everything, mono modes only use voice 0
        if (s.mode != lastMode)
        {
            if (lastMode != -1) { releaseAll(); for (int i = 1; i < numVoices; ++i) voices[(size_t) i].release(); }
            lastMode = s.mode;
        }
        if (s.arp.on != arpWasOn)
        {
            stopArp(); releaseAll();
            if (s.arp.on) { arpNotes = physical; if (! arpNotes.empty()) startArp (clock, s); }
            arpWasOn = s.arp.on;
        }

        outL = buffer.getWritePointer (0) + offset;
        outR = buffer.getWritePointer (1) + offset;
        pieceStart = clock;

        int cur = 0;
        for (; it != midi.end(); ++it)
        {
            const auto md = *it;
            const int t = md.samplePosition - offset;
            if (t >= n) break;
            const int at = juce::jlimit (0, n, t);
            advanceTo (cur, at, s);
            handleMidi (md.getMessage(), pieceStart + at, s);
        }
        advanceTo (cur, n, s);
        clock += n;

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, offset, n);
        fx.process (view, s.fp);
    }

    void advanceTo (int& cur, int target, const EngineSettings& s)
    {
        const double absTarget = pieceStart + target;
        for (int guard = 0; guard < 4096; ++guard)
        {
            double next = 1.0e300; int which = -2;
            if (arpRunning) { next = arpNext; which = -1; if (s.arp.topLine && topNext < next) { next = topNext; which = -3; } }
            for (int k = 0; k < pendingCount; ++k)
                if (pending[(size_t) k].time < next) { next = pending[(size_t) k].time; which = k; }
            if (which == -2 || next >= absTarget) break;
            const int off = juce::jlimit (cur, target, (int) std::floor (next - pieceStart));
            renderVoices (cur, off - cur, s);
            cur = off;
            if (which == -1) doArpStep (arpNext, s);
            else if (which == -3) doTopStep (topNext, s);
            else
            {
                const Pending ev = pending[(size_t) which];
                pending[(size_t) which] = pending[(size_t) (--pendingCount)];
                if (ev.on) engineNoteOn (ev.note, ev.vel, s, ev.swell);
                else       engineNoteOff (ev.note, s);
            }
        }
        renderVoices (cur, target - cur, s);
        cur = target;
    }

    void renderVoices (int start, int num, const EngineSettings& s)
    {
        if (num <= 0) return;
        const int count = s.mode == 2 ? numVoices : 1;
        for (int i = 0; i < count; ++i)
            voices[(size_t) i].render (outL, outR, start, num, s.vp, bend * 2.0f, modWheel, aftertouch);
        // mono modes: the other voices may still be ringing out from before a mode change
        if (count == 1) for (int i = 1; i < numVoices; ++i) if (voices[(size_t) i].isActive())
            voices[(size_t) i].render (outL, outR, start, num, s.vp, bend * 2.0f, modWheel, aftertouch);
    }

    void handleMidi (const juce::MidiMessage& m, double t, const EngineSettings& s)
    {
        if (m.isNoteOn()) handleNoteOn (m.getNoteNumber(), m.getFloatVelocity(), s, t);
        else if (m.isNoteOff()) handleNoteOff (m.getNoteNumber(), s);
        else if (m.isPitchWheel()) bend = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
        else if (m.isChannelPressure()) aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch()) aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(), val = m.getControllerValue();
            if (cc == 1) modWheel = (float) val / 127.0f;
            else if (cc == 64)
            {
                const bool down = val >= 64;
                if (pedal && ! down)
                {
                    if (s.arp.on)
                    {
                        if (! s.arp.hold) { pruneArp(); }
                    }
                    else
                    {
                        for (int i = 0; i < sustained.count; ++i)
                            if (! physical.contains (sustained.n[i])) engineNoteOff (sustained.n[i], s);
                        sustained.clear();
                    }
                }
                pedal = down;
            }
            else if (cc == 120 || cc == 123)
            {
                physical.clear(); sustained.clear(); arpNotes.clear(); stopArp(); releaseAll();
            }
        }
    }

    bool arpLatched (const EngineSettings& s) const noexcept { return s.arp.hold || pedal; }

    void pruneArp()
    {
        for (int i = arpNotes.count; --i >= 0;)
            if (! physical.contains (arpNotes.n[i])) arpNotes.remove (arpNotes.n[i]);
        if (arpNotes.empty()) stopArp();
    }

    void handleNoteOn (int n, float vel, const EngineSettings& s, double t)
    {
        n = juce::jlimit (0, 127, n);
        vel = std::isfinite (vel) ? juce::jlimit (0.0f, 1.0f, vel) : 0.8f;
        if (s.arp.on)
        {
            if (arpLatched (s) && physical.empty()) arpNotes.clear();
            physical.add (n);
            if (! arpNotes.contains (n) && arpNotes.count < Arp::maxHeld) arpNotes.add (n);
            arpVelocity = vel;
            if (! arpRunning && ! arpNotes.empty()) startArp (t, s);
        }
        else
        {
            physical.add (n);
            sustained.remove (n);
            engineNoteOn (n, vel, s, 0.0f);
        }
    }

    void handleNoteOff (int n, const EngineSettings& s)
    {
        physical.remove (n);
        if (s.arp.on)
        {
            if (! arpLatched (s)) { arpNotes.remove (n); if (arpNotes.empty()) stopArp(); }
        }
        else if (pedal) sustained.add (n);
        else engineNoteOff (n, s);
    }

    void startArp (double t, const EngineSettings& s)
    {
        arpRunning = true;
        arpStep = arpNoteIndex = walkPos = 0;
        arpGrid = arpNext = t;
        // top line: starts a fifth of a beat later (100 ms at 120 BPM), so the two lines interlock
        topStep = topIndex = topWalk = 0;
        topGrid = topNext = t + 0.2 * 60.0 / s.bpm * sr;
        std::fill (std::begin (mutRest), std::end (mutRest), false);
        std::fill (std::begin (mutOct), std::end (mutOct), 0);
    }

    void stopArp()
    {
        arpRunning = false;
        for (int k = 0; k < pendingCount; ++k)
            if (! pending[(size_t) k].on) releaseNote (pending[(size_t) k].note);
        pendingCount = 0;
        arpDisplay = -1;
    }

    void schedule (double t, int note, float vel, bool on, float swell = 0.0f)
    {
        if (pendingCount < (int) pending.size()) pending[(size_t) (pendingCount++)] = { t, note, vel, on, swell };
    }

    void doArpStep (double t, const EngineSettings& s)
    {
        if (arpNotes.empty()) { stopArp(); return; }
        Arp::NoteList sorted, played;
        const int top = topNote (s);
        for (int i = 0; i < arpNotes.count && i < Arp::maxHeld; ++i)
            if (arpNotes.n[i] != top) { played.add (arpNotes.n[i]); sorted.add (arpNotes.n[i]); }
        std::sort (sorted.n, sorted.n + sorted.count);

        // speed 1x = one step per beat of the time signature, 2x / 4x subdivide
        const auto& a = s.arp;
        const int speed = 1 << juce::jlimit (0, 2, a.speed);
        const int den = juce::jlimit (1, 32, s.tsDen), num = juce::jlimit (1, 32, s.tsNum);
        const double stepLen = juce::jmax (16.0, (4.0 / (double) den) / (double) speed * 60.0 / s.bpm * sr);
        const int stepsPerBar = juce::jlimit (1, 48, num * speed);
        const int posInBar = arpStep % stepsPerBar;
        const int idx16 = posInBar % 16;
        arpBarSteps = stepsPerBar;

        bool hit, accent;
        if (a.rhythm == Params::customRhythm) { hit = a.steps[idx16]; accent = false; }
        else
        {
            const char* pattern = Params::rhythmPattern (a.rhythm);
            const int len = juce::jmax (1, (int) std::strlen (pattern));
            hit = pattern[posInBar % len] != '.';
            accent = pattern[posInBar % len] == 'X';
        }
        const bool downbeat = posInBar % 4 == 0;   // the 1 of the bar, and every 4th step after it
        arpDisplay = posInBar;

        if (a.mutate > 0.001f && arpRng.nextFloat() < a.mutate * 0.15f)
        {
            const int k = arpRng.nextInt (16), action = arpRng.nextInt (3);
            int rests = 0; for (auto r : mutRest) rests += r ? 1 : 0;
            if (action == 0 && (mutRest[k] || rests < 10)) mutRest[k] = ! mutRest[k];
            else if (action == 1) { static constexpr int sh[4] = { -12, 0, 12, 7 }; mutOct[k] = sh[arpRng.nextInt (4)]; }
            else { mutRest[k] = false; mutOct[k] = 0; }
        }

        if (hit && ! mutRest[idx16])
        {
            Arp::NoteList result;
            const bool ok = Arp::pick (a.mode, sorted, played, a.octaves, arpNoteIndex, walkPos,
                                       [this] (int m) { return arpRng.nextInt (juce::jmax (1, m)); }, result);
            if (ok)
            {
                ++arpNoteIndex;
                // your playing velocity carries through; the 1 of the bar (and pattern accents) is a bit louder
                float vel = arpVelocity * ((downbeat || accent) ? 1.0f : 0.85f);
                int shift = mutOct[idx16], count = 1;
                double gateFactor = a.gate; float swell = 0.0f; bool drop = false;
                if (a.glitch > 0.001f && arpRng.nextFloat() < a.glitch * 0.45f)
                {
                    switch (arpRng.nextInt (5))
                    {
                        case 0:  count = arpRng.nextBool() ? 3 : 4; gateFactor = 0.5; break;
                        case 1:  gateFactor *= 0.15; break;
                        case 2:  drop = true; break;
                        case 3:  { static constexpr int j[3] = { 12, -12, 7 }; shift += j[arpRng.nextInt (3)]; break; }
                        default: swell = (float) (stepLen * 1.5 / sr); gateFactor = juce::jmax (gateFactor, 1.2); vel *= 0.9f; break;
                    }
                }
                if (! drop)
                {
                    const double sub = stepLen / count;
                    const double gateLen = juce::jmax (0.002 * sr, gateFactor * sub);
                    for (int r = 0; r < count; ++r)
                    {
                        const double tOn = t + r * sub;
                        for (int j = 0; j < result.count; ++j)
                        {
                            const int note = juce::jlimit (0, 127, result.n[j] + shift);
                            if (r == 0) engineNoteOn (note, vel, s, swell);
                            else        schedule (tOn, note, vel * 0.85f, true);
                            schedule (tOn + gateLen, note, 0.0f, false);
                        }
                    }
                }
            }
        }
        ++arpStep;
        arpGrid += stepLen;
        arpNext = arpGrid + ((arpStep % 2 == 1) ? (double) a.swing * 0.33 * stepLen : 0.0);
    }

    // the note the top line plays (-1 = none): the highest held note, once at least two are held
    int topNote (const EngineSettings& s) const noexcept
    {
        if (! s.arp.topLine || arpNotes.count < 2) return -1;
        int m = -1; for (int i = 0; i < arpNotes.count; ++i) m = juce::jmax (m, arpNotes.n[i]);
        return m;
    }

    void doTopStep (double t, const EngineSettings& s)
    {
        const auto& a = s.arp;
        const int speed = 1 << juce::jlimit (0, 2, a.speed);
        const int den = juce::jlimit (1, 32, s.tsDen);
        const double stepLen = juce::jmax (16.0, (4.0 / (double) den) / (double) speed * 60.0 / s.bpm * sr);
        const int top = topNote (s);
        if (top >= 0)
        {
            Arp::NoteList one, result; one.add (top);
            const int mode = a.mode == Arp::Chord ? Arp::Up : a.mode;
            if (Arp::pick (mode, one, one, a.octaves, topIndex, topWalk, [this] (int m) { return arpRng.nextInt (juce::jmax (1, m)); }, result))
            {
                ++topIndex;
                const float vel = arpVelocity * (topStep % 4 == 0 ? 1.0f : 0.85f);
                const int note = juce::jlimit (0, 127, result.n[0]);
                engineNoteOn (note, vel, s, 0.0f);
                schedule (t + juce::jmax (0.002 * sr, (double) a.gate * stepLen), note, 0.0f, false);
            }
        }
        ++topStep;
        topGrid += stepLen;
        topNext = topGrid + ((topStep % 2 == 1) ? (double) a.swing * 0.33 * stepLen : 0.0);
    }

    void engineNoteOn (int note, float vel, const EngineSettings& s, float swell)
    {
        static constexpr float spreadSeq[8] = { -1.0f, 1.0f, -0.5f, 0.5f, -0.75f, 0.75f, -0.25f, 0.25f };
        noteStack.push (note);
        if (s.mode == 2)
        {
            GlizzarineVoice* target = nullptr;
            for (auto& v : voices) if (v.isActive() && v.getNote() == note) target = &v;
            if (target == nullptr) for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
            if (target == nullptr)
                for (auto& v : voices)
                {
                    if (target == nullptr) { target = &v; continue; }
                    const bool better = (! v.isGateOn() && target->isGateOn()) || (v.isGateOn() == target->isGateOn() && v.age < target->age);
                    if (better) target = &v;
                }
            const float from = (s.vp.glide > 0.0005f && lastNote >= 0) ? (float) lastNote : (float) note;
            target->start (note, vel, from, s.vp, ++seed, spreadSeq[(spread++) & 7], swell);
            target->age = ++ageCounter;
        }
        else
        {
            auto& v = voices[0];
            if (s.mode == 1 && noteStack.count > 1 && v.isActive() && v.isGateOn()) v.glideTo (note);
            else
            {
                float from = (float) note;
                if (s.mode == 0) from = v.isActive() ? v.getPitch() : (lastNote >= 0 ? (float) lastNote : (float) note);
                v.start (note, vel, from, s.vp, ++seed, 0.0f, swell);
            }
        }
        lastNote = note;
    }

    void releaseNote (int note)
    {
        for (auto& v : voices) if (v.isGateOn() && v.getNote() == note) v.release();
        noteStack.remove (note);
    }

    void engineNoteOff (int note, const EngineSettings& s)
    {
        const bool wasTop = noteStack.last() == note;
        noteStack.remove (note);
        if (s.mode == 2) { for (auto& v : voices) if (v.isGateOn() && v.getNote() == note) v.release(); return; }
        auto& v = voices[0];
        if (noteStack.empty()) { if (v.isGateOn()) v.release(); }
        else if (wasTop && v.isGateOn()) { const int prev = noteStack.last(); v.glideTo (prev); lastNote = prev; }
    }

    void releaseAll()
    {
        for (auto& v : voices) if (v.isGateOn()) v.release();
        noteStack.clear();
    }

    double sr = 44100.0, clock = 0.0, pieceStart = 0.0;
    int blockMax = 512;
    std::array<GlizzarineVoice, numVoices> voices;
    GlizzarineFX fx;
    float* outL = nullptr; float* outR = nullptr;

    NoteSet held, physical, sustained, arpNotes, noteStack;
    std::array<Pending, 512> pending {};
    int pendingCount = 0;
    bool arpRunning = false, arpWasOn = false, pedal = false;
    double arpGrid = 0.0, arpNext = 0.0;
    int arpStep = 0, arpNoteIndex = 0, walkPos = 0, arpDisplay = -1, arpBarSteps = 16;
    float arpVelocity = 0.8f;
    double topGrid = 0.0, topNext = 0.0;
    int topStep = 0, topIndex = 0, topWalk = 0;
    bool mutRest[16] {}; int mutOct[16] {};
    juce::Random arpRng;
    float bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
    int lastMode = -1, lastNote = -1;
    uint32_t seed = 1; int spread = 0; juce::uint64 ageCounter = 0;
};
