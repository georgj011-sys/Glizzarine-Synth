#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// "Dark Clean" look (same as the Glizzarine Harmonizer): one dark ground, cards, pink for whatever is active.
namespace Theme
{
    inline const juce::Colour bg      { 0xff09090d };
    inline const juce::Colour card    { 0xff101017 };
    inline const juce::Colour card2   { 0xff14141c };
    inline const juce::Colour line    { 0xff1c1c26 };
    inline const juce::Colour edge    { 0xff2a2a38 };
    inline const juce::Colour track   { 0xff242432 };
    inline const juce::Colour hub     { 0xff17171f };
    inline const juce::Colour text    { 0xffecebf2 };
    inline const juce::Colour soft    { 0xffc9c7d8 };
    inline const juce::Colour dim     { 0xff8f8da3 };
    inline const juce::Colour mute    { 0xff4d4b60 };
    inline const juce::Colour knob    { 0xffbdbacd };
    inline const juce::Colour accent  { 0xffff5fd2 };
    inline const juce::Colour violet  { 0xff8f7bff };
    inline const juce::Colour orange  { 0xffffb547 };

    juce::Font sans (float size, bool bold = false);
    juce::Font mono (float size, bool bold = false);
    void label (juce::Graphics&, const juce::String& caps, juce::Rectangle<int> r, juce::Colour c = dim,
                juce::Justification j = juce::Justification::centredLeft);
    void panel (juce::Graphics&, juce::Rectangle<int> r, float radius = 20.0f, juce::Colour c = card);
}

class GlzLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlzLookAndFeel();
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::sans (14.0f); }
    juce::Font getPopupMenuFont() override { return Theme::sans (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::sans (13.5f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

//==============================================================================
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick (float dt) = 0;
};

// A knob that glides to its value with a soft spring, lights up and shows a value bubble while it moves
// (also when you turn the knob on the KeyLab). Right click = MIDI learn. Can be re-bound (E1-E8).
class AnimKnob : public juce::Slider, public Animated
{
public:
    enum Style { Big, Small };
    AnimKnob (GlizzarineProcessor& p, const juce::String& paramId, const juce::String& name, Style s = Big);
    void bind (const juce::String& paramId, const juce::String& name);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void valueChanged() override;
    void tick (float dt) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    void setCaption (const juce::String& c) { caption = c; repaint(); }
    void setLearnTarget (const juce::String& t) { learnTarget = t; }
    void setLabel (const juce::String& n) { if (n != name) { name = n; repaint(); } }
    static juce::String valueText (juce::RangedAudioParameter*);
    const juce::String& paramId() const { return id; }
private:
    void drawKnob (juce::Graphics&, juce::Point<float> c, float radius, float thick);
    GlizzarineProcessor& proc;
    juce::String id, name, caption, learnTarget;
    Style style;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float disp = 0.0f, velo = 0.0f, trailFrom = 0.0f, trailAlpha = 0.0f, glow = 0.0f, bubble = 0.0f;
    double activeUntil = 0.0;
    bool dimmed = false, first = true;
};

// A row of choices for a choice / bool / whole-number parameter
class Segments : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    Segments (GlizzarineProcessor& p, const juce::String& paramId, juce::StringArray labels, bool joined = true);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float) override;
    int current() const;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
private:
    juce::RangedAudioParameter* param = nullptr;
    juce::StringArray labels;
    bool joined, dimmed = false;
    int shown = -1;
};

// A dropdown for long choice lists (oscillator models, LFO waves, mod sources ...)
class Choice : public juce::ComboBox
{
public:
    Choice (GlizzarineProcessor& p, const juce::String& paramId);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// A pill on/off switch bound to a bool / on-off parameter
class Switch : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    Switch (GlizzarineProcessor& p, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float dt) override;
    bool isOn() const;
private:
    juce::RangedAudioParameter* param = nullptr;
    float pos = 0.0f;
};

// A labelled horizontal amount bar with its value (effects page, mod amounts)
class Bar : public juce::Slider, public Animated
{
public:
    Bar (GlizzarineProcessor& p, const juce::String& paramId, const juce::String& name);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void valueChanged() override;
    void tick (float dt) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
private:
    GlizzarineProcessor& proc;
    juce::String id, name;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool dimmed = false;
    float glow = 0.0f; double activeUntil = 0.0; bool first = true;
};

// A vertical fader (the KeyLab fader strip on the PLAY page)
class VFader : public juce::Slider, public Animated
{
public:
    VFader (GlizzarineProcessor& p, int faderIndex);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void valueChanged() override;
    void tick (float dt) override;
private:
    GlizzarineProcessor& proc;
    int index;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float glow = 0.0f; double activeUntil = 0.0; bool first = true;
};

// A number you drag up / down (pitch, BPM)
class NumberBox : public juce::Slider
{
public:
    NumberBox (GlizzarineProcessor& p, const juce::String& paramId, const juce::String& suffix);
    void paint (juce::Graphics&) override;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String suffix;
};

// Name / confirm questions shown inside the plugin window
class PresetDialog : public juce::Component
{
public:
    PresetDialog();
    void askName (const juce::String& suggestion, std::function<void (const juce::String&)> onSave);
    void askConfirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
    bool keyPressed (const juce::KeyPress&) override;
private:
    void close() { setVisible (false); nameFn = nullptr; okFn = nullptr; }
    void ok();
    juce::TextEditor name;
    juce::TextButton okButton, cancelButton { "Cancel" };
    juce::String title, message;
    bool asking = false;
    std::function<void (const juce::String&)> nameFn;
    std::function<void()> okFn;
    juce::Rectangle<int> card;
};

class GlizzarineEditor;

// Preset browser: categories, search and cards
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (GlizzarineProcessor& p);
    void open();
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::function<void()> onClose;
private:
    struct Item { juce::String name, category, tags; int factory = -1, user = -1; };
    void rebuild();
    juce::Rectangle<int> cardBounds (int i) const;
    GlizzarineProcessor& proc;
    juce::TextEditor search;
    juce::StringArray cats { "ALL", "PADS", "ARPS", "LEADS", "KEYS", "BASS", "VIDEO", "MY PRESETS" };
    int cat = 0, hover = -1, scroll = 0;
    std::vector<Item> items;
    juce::Rectangle<int> grid;
};

// A tab page: owns its controls and ticks them
class Page : public juce::Component
{
public:
    explicit Page (GlizzarineProcessor& p) : proc (p) { setInterceptsMouseClicks (false, true); }
    virtual void tickPage (float dt) { for (auto* a : animated) a->tick (dt); }
    virtual void presetChanged() {}
protected:
    template <typename T, typename... Args> T* add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        if (auto* a = dynamic_cast<Animated*> (c)) animated.push_back (a);
        return c;
    }
    AnimKnob* knob (const juce::String& id, const juce::String& name, AnimKnob::Style s = AnimKnob::Big)
    { return add<AnimKnob> (proc, id, name, s); }
    GlizzarineProcessor& proc;
    juce::OwnedArray<juce::Component> owned;
    std::vector<Animated*> animated;
};

//==============================================================================
class GlizzarineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlizzarineEditor (GlizzarineProcessor&);
    ~GlizzarineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int i);
    int numTabs() const { return tabNames.size(); }
    void openBrowser();

    // the whole UI is drawn at this size and scaled to the window
    static constexpr int baseW = 1280, baseH = 760;

private:
    class Content;
    void timerCallback() override;
    void savePreset();
    GlizzarineProcessor& proc;
    GlzLookAndFeel lnf;
    std::unique_ptr<Content> content;
    juce::StringArray tabNames { "PLAY", "OSC", "FILTER & ENV", "MOD", "ARP", "EFFECTS", "SETUP" };
    juce::TooltipWindow tooltips { nullptr, 600 };
    double lastTick = 0.0;
    int lastPresetVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlizzarineEditor)
};
