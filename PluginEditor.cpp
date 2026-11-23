#include "PluginEditor.h"
#include "Tooltips.h"

using namespace juce;

namespace
{
    constexpr float kArcStart = -MathConstants<float>::pi * 0.75f;
    constexpr float kArcSpan  =  MathConstants<float>::pi * 1.5f;

    Point<float> onCircle (Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    void arc (Graphics& g, Point<float> c, float r, float from, float to, float thick, Colour col)
    {
        if (to - from < 0.002f) return;
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, kArcStart + from * kArcSpan, kArcStart + to * kArcSpan, true);
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    }

    double nowMs() { return Time::getMillisecondCounterHiRes(); }

    void setPlain (RangedAudioParameter* p, float plain)
    {
        if (p == nullptr) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }

    float plainOf (RangedAudioParameter* p) { return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; }

    void text (Graphics& g, const String& s, Rectangle<int> r, const Font& f, Colour c, Justification j = Justification::centredLeft)
    {
        g.setFont (f); g.setColour (c); g.drawFittedText (s, r, j, 1, 0.85f);
    }

    String tip (const String& id)
    {
        auto t = Tooltips::get (id);
        return t;
    }

    // Right-click menu shared by knobs, bars and faders
    void learnMenu (GlizzarineProcessor& proc, Component* target, const String& learnId, RangedAudioParameter* param)
    {
        const int cc = proc.ccFor (learnId);
        PopupMenu m;
        m.addSectionHeader (cc >= 0 ? "MIDI CC " + String (cc) : "No MIDI controller assigned");
        m.addItem (1, "MIDI Learn (move a knob or fader on your keyboard next)");
        m.addItem (2, "Forget MIDI controller", cc >= 0);
        m.addItem (3, "Reset to default", param != nullptr);
        Component::SafePointer<Component> safe (target);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [&proc, safe, learnId, param] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) proc.startLearn (learnId);
            if (r == 2) proc.forgetMapping (learnId);
            if (r == 3 && param != nullptr) setPlain (param, param->convertFrom0to1 (param->getDefaultValue()));
        });
    }

    String shortType (int filterType)
    {
        static const char* n[] = { "Ladder", "LP 12", "LP 24", "Band", "High", "Notch", "Formant", "Comb" };
        return n[jlimit (0, 7, filterType)];
    }
}

//==============================================================================
Font Theme::sans (float size, bool bold) { return Font (FontOptions (size, bold ? Font::bold : Font::plain)); }
Font Theme::mono (float size, bool bold) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), size, bold ? Font::bold : Font::plain)); }
void Theme::label (Graphics& g, const String& caps, Rectangle<int> r, Colour c, Justification j)
{
    auto f = sans (11.0f);
    f.setExtraKerningFactor (0.16f);
    g.setFont (f); g.setColour (c); g.drawText (caps, r, j, true);
}
void Theme::panel (Graphics& g, Rectangle<int> r, float radius, Colour c) { g.setColour (c); g.fillRoundedRectangle (r.toFloat(), radius); }

GlzLookAndFeel::GlzLookAndFeel()
{
    setColour (ComboBox::backgroundColourId, Theme::card2);
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::outlineColourId, Theme::edge);
    setColour (ComboBox::arrowColourId, Theme::dim);
    setColour (PopupMenu::backgroundColourId, Theme::card2);
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::headerTextColourId, Theme::dim);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent);
    setColour (PopupMenu::highlightedTextColourId, Theme::bg);
    setColour (TextButton::buttonColourId, Theme::card2);
    setColour (TextButton::textColourOffId, Theme::soft);
    setColour (TextButton::textColourOnId, Theme::bg);
    setColour (TextEditor::backgroundColourId, Theme::hub);
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
    setColour (TextEditor::outlineColourId, Theme::edge);
    setColour (TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (CaretComponent::caretColourId, Theme::accent);
    setColour (TooltipWindow::textColourId, Theme::text);
}

void GlzLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Theme::card2); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::edge); g.drawRoundedRectangle (r, 10.0f, 1.0f);
    Path p; const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
    p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
    g.setColour (Theme::dim); g.strokePath (p, PathStrokeType (1.6f));
}

void GlzLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void GlzLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (on || b.findColour (TextButton::buttonColourId) == Theme::accent)
    {
        g.setColour (down ? Theme::accent.darker (0.2f) : (over ? Theme::accent.brighter (0.15f) : Theme::accent));
        g.fillRoundedRectangle (r, 10.0f);
        return;
    }
    g.setColour (down ? Theme::accent.withAlpha (0.25f) : (over ? Theme::edge : Theme::card2));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::edge); g.drawRoundedRectangle (r, 10.0f, 1.0f);
}

void GlzLookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&)
{
    g.setColour (Theme::hub); g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h), 10.0f);
}

void GlzLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& t)
{
    g.setColour (t.hasKeyboardFocus (true) ? Theme::accent : Theme::edge);
    g.drawRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 10.0f, 1.0f);
}

void GlzLookAndFeel::drawTooltip (Graphics& g, const String& t, int w, int h)
{
    g.setColour (Theme::card2); g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h), 8.0f);
    g.setColour (Theme::edge); g.drawRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 8.0f, 1.0f);
    g.setFont (Theme::sans (13.0f)); g.setColour (Theme::text);
    g.drawFittedText (t, Rectangle<int> (0, 0, w, h).reduced (10, 6), Justification::centredLeft, 3);
}

Rectangle<int> GlzLookAndFeel::getTooltipBounds (const String& t, Point<int> pos, Rectangle<int> area)
{
    const int w = jmin (340, (int) GlyphArrangement::getStringWidth (Theme::sans (13.0f), t) + 24);
    const int lines = (int) std::ceil (GlyphArrangement::getStringWidth (Theme::sans (13.0f), t) / (float) jmax (1, w - 20));
    const int h = 14 + 17 * jlimit (1, 3, lines);
    return Rectangle<int> (pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 18, pos.y + 18, w, h).constrainedWithin (area);
}

//==============================================================================
AnimKnob::AnimKnob (GlizzarineProcessor& p, const String& paramId, const String& n, Style s)
    : proc (p), name (n), style (s)
{
    setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kArcStart + MathConstants<float>::twoPi, kArcStart + kArcSpan + MathConstants<float>::twoPi, true);
    setMouseDragSensitivity (220);
    setVelocityModeParameters (0.6, 1, 0.08, true, ModifierKeys::shiftModifier);
    bind (paramId, n);
    first = false;
}

void AnimKnob::bind (const String& paramId, const String& n)
{
    first = true;
    attachment.reset();
    id = paramId; name = n;
    if (learnTarget.isEmpty() || ! learnTarget.startsWith ("E")) learnTarget = paramId;
    param = proc.apvts.getParameter (id);
    if (param != nullptr)
    {
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *this);
        setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    }
    setTooltip (tip (id));
    disp = (float) valueToProportionOfLength (getValue());
    velo = 0.0f; trailFrom = disp; trailAlpha = 0.0f;
    first = false;
    repaint();
}

String AnimKnob::valueText (RangedAudioParameter* p)
{
    if (p == nullptr) return "-";
    if (auto* f = dynamic_cast<AudioParameterFloat*> (p))
    {
        const float v = f->get();
        const auto& r = f->range;
        const String lab = f->getLabel();
        if (lab == "s") return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s";
        if (lab == "ct") return (v > 0.05f ? "+" : "") + String (v, 1) + " ct";
        if (lab == "Hz" && v >= 19500.0f) return "open";
        if (lab == "Hz" && v >= 1000.0f) return String (v / 1000.0f, 1) + " kHz";
        if (lab == "Hz") return String (v, v < 10.0f ? 2 : 0) + " Hz";
        if (lab == "BPM") return String (v, 1) + " BPM";
        if (r.start == -1.0f && r.end == 1.0f) return (v > 0.005f ? "+" : "") + String (v, 2);
        if (r.start >= 0.0f && r.end <= 2.0f && lab.isEmpty()) return String (roundToInt (v * 100.0f)) + " %";
        return String (v, 2) + (lab.isEmpty() ? "" : " " + lab);
    }
    if (auto* i = dynamic_cast<AudioParameterInt*> (p))
    {
        const int v = i->get();
        if (i->paramID.endsWith ("_pitch")) return (v > 0 ? "+" : "") + String (v) + " st";
        return String (v);
    }
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p)) return c->getCurrentChoiceName();
    if (auto* b = dynamic_cast<AudioParameterBool*> (p)) return b->get() ? "On" : "Off";
    return p->getCurrentValueAsText();
}

void AnimKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { learnMenu (proc, this, learnTarget, param); return; }
    Slider::mouseDown (e);
}

void AnimKnob::valueChanged()
{
    if (first) return;
    if (trailAlpha < 0.03f) trailFrom = disp;
    trailAlpha = 0.16f;                  // trails: only very subtle
    activeUntil = nowMs() + 1000.0;
}

void AnimKnob::tick (float dt)
{
    const float target = (float) valueToProportionOfLength (getValue());
    bool changed = false;
    if (std::abs (target - disp) > 0.0004f || std::abs (velo) > 0.001f)
    {
        // soft spring with a tiny overshoot: the knob glides instead of jumping
        const float k = 320.0f, c = 23.0f;
        const float steps = jlimit (1.0f, 8.0f, std::ceil (dt / 0.004f));
        const float h = dt / steps;
        for (int i = 0; i < (int) steps; ++i) { velo += (k * (target - disp) - c * velo) * h; disp += velo * h; }
        if (std::abs (target - disp) < 0.0004f && std::abs (velo) < 0.001f) { disp = target; velo = 0.0f; }
        changed = true;
    }
    if (trailAlpha > 0.0f) { trailAlpha = jmax (0.0f, trailAlpha - dt * 0.2f); changed = true; }
    const bool active = nowMs() < activeUntil;
    const float gT = active ? 1.0f : 0.0f;
    if (std::abs (glow - gT) > 0.001f) { glow += (gT - glow) * jmin (1.0f, dt * (active ? 14.0f : 4.0f)); changed = true; }
    if (std::abs (bubble - gT) > 0.001f) { bubble += (gT - bubble) * jmin (1.0f, dt * (active ? 18.0f : 6.0f)); changed = true; }
    if (proc.learnTarget() == learnTarget) changed = true;
    if (changed) repaint();
}

void AnimKnob::drawKnob (Graphics& g, Point<float> c, float r, float th)
{
    const bool learning = proc.learnTarget() == learnTarget && learnTarget.isNotEmpty();
    arc (g, c, r, 0.0f, 1.0f, th, Theme::track);
    if (trailAlpha > 0.004f && ! dimmed)
    {
        const float a = jmin (trailFrom, disp), b = jmax (trailFrom, disp);
        arc (g, c, r, a, b, th, Theme::accent.withAlpha (trailAlpha));
    }
    const Colour valueCol = dimmed ? Theme::mute : Theme::knob.interpolatedWith (Theme::accent, glow);
    if (glow > 0.01f && ! dimmed) arc (g, c, r, 0.0f, disp, th * 2.2f, Theme::accent.withAlpha (0.2f * glow));
    arc (g, c, r, 0.0f, jlimit (0.0f, 1.0f, disp), th, valueCol);
    const float hubR = r - th * 1.6f;
    g.setColour (Theme::hub); g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2);
    g.setColour (learning ? Theme::orange : Colour (0xff2c2c3a));
    g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2, learning ? 2.0f : 1.0f);
    const float a = kArcStart + jlimit (-0.05f, 1.05f, disp) * kArcSpan;
    g.setColour (dimmed ? Theme::mute : Theme::text);
    g.drawLine (Line<float> (c, onCircle (c, hubR * 0.8f, a)), th * 0.45f);
}

void AnimKnob::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const String val = valueText (param);
    if (glow > 0.01f && ! dimmed)
    {
        g.setColour (Theme::accent.withAlpha (0.06f * glow)); g.fillRoundedRectangle (b.reduced (1.0f), 16.0f);
        g.setColour (Theme::accent.withAlpha (0.8f * glow)); g.drawRoundedRectangle (b.reduced (1.0f), 16.0f, 1.0f);
    }
    const bool big = style == Big;
    const float top = big ? 22.0f : 8.0f, textH = big ? 40.0f : 34.0f;
    const float d = jmax (22.0f, jmin (b.getWidth() - 14.0f, b.getHeight() - top - textH));
    const Point<float> c (b.getCentreX(), top + d * 0.5f);
    drawKnob (g, c, d * 0.42f, jmax (3.0f, d * (big ? 0.075f : 0.085f)));
    const int ty = (int) (top + d + 2);
    text (g, name, { 2, ty, getWidth() - 4, 18 }, Theme::sans (big ? 14.0f : 13.0f), dimmed ? Theme::mute : Theme::text, Justification::centred);
    text (g, caption.isNotEmpty() ? caption + String::fromUTF8 (" · ") + val : val, { 2, ty + 17, getWidth() - 4, 15 }, Theme::mono (11.0f),
          Theme::dim, Justification::centred);
    if (big && bubble > 0.01f && ! dimmed)
    {
        const String s = val;
        const auto f = Theme::mono (12.5f, true);
        const float w = GlyphArrangement::getStringWidth (f, s) + 18.0f;
        auto r = Rectangle<float> (w, 22.0f).withCentre ({ b.getCentreX(), 11.0f + (1.0f - bubble) * 6.0f });
        g.setColour (Theme::accent.withAlpha (bubble)); g.fillRoundedRectangle (r, 7.0f);
        g.setFont (f); g.setColour (Theme::bg.withAlpha (bubble)); g.drawText (s, r, Justification::centred);
    }
}

//==============================================================================
Segments::Segments (GlizzarineProcessor& p, const String& paramId, StringArray l, bool j)
    : param (p.apvts.getParameter (paramId)), labels (std::move (l)), joined (j)
{
    setTooltip (tip (paramId));
}

int Segments::current() const
{
    if (param == nullptr) return 0;
    return roundToInt (plainOf (param) - param->getNormalisableRange().start);
}

void Segments::tick (float) { const int c = current(); if (c != shown) { shown = c; repaint(); } }

void Segments::paint (Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;
    const float gap = joined ? 3.0f : 5.0f;
    auto b = getLocalBounds().toFloat();
    if (joined) { g.setColour (Theme::hub); g.fillRoundedRectangle (b, 10.0f); b = b.reduced (3.0f); }
    const float w = (b.getWidth() - gap * (float) (n - 1)) / (float) n;
    const int cur = current();
    for (int i = 0; i < n; ++i)
    {
        auto r = Rectangle<float> (b.getX() + (float) i * (w + gap), b.getY(), w, b.getHeight());
        const bool on = i == cur;
        if (on) { g.setColour (dimmed ? Theme::mute : Theme::accent); g.fillRoundedRectangle (r, 8.0f); }
        else if (! joined) { g.setColour (Theme::edge); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f); }
        g.setFont (Theme::sans (w < 54.0f ? 12.0f : 13.0f));
        g.setColour (on ? Theme::bg : (dimmed ? Theme::mute : Theme::soft));
        g.drawFittedText (labels[i], r.toNearestInt().reduced (2, 0), Justification::centred, 1, 0.75f);
    }
}

void Segments::mouseDown (const MouseEvent& e)
{
    const int n = labels.size();
    if (n == 0 || param == nullptr || getWidth() <= 0) return;
    const int i = jlimit (0, n - 1, (int) ((float) e.x / (float) getWidth() * (float) n));
    setPlain (param, param->getNormalisableRange().start + (float) i);
    repaint();
}

//==============================================================================
Choice::Choice (GlizzarineProcessor& p, const String& paramId)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p.apvts.getParameter (paramId)))
    {
        addItemList (c->choices, 1);
        attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, paramId, *this);
    }
    setTooltip (tip (paramId));
}

//==============================================================================
Switch::Switch (GlizzarineProcessor& p, const String& paramId) : param (p.apvts.getParameter (paramId))
{
    pos = isOn() ? 1.0f : 0.0f;
    setTooltip (tip (paramId));
}
bool Switch::isOn() const { return param != nullptr && param->getValue() > 0.5f; }
void Switch::tick (float dt)
{
    const float t = isOn() ? 1.0f : 0.0f;
    if (std::abs (t - pos) > 0.001f) { pos += (t - pos) * jmin (1.0f, dt * 16.0f); if (std::abs (t - pos) < 0.01f) pos = t; repaint(); }
}
void Switch::paint (Graphics& g)
{
    auto r = Rectangle<float> (40.0f, 22.0f).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (Theme::edge.interpolatedWith (Theme::accent, pos)); g.fillRoundedRectangle (r, 11.0f);
    g.setColour (Theme::text); g.fillEllipse (r.getX() + 3.0f + pos * 18.0f, r.getY() + 3.0f, 16.0f, 16.0f);
}
void Switch::mouseDown (const MouseEvent&) { if (param != nullptr) setPlain (param, isOn() ? 0.0f : 1.0f); }

//==============================================================================
Bar::Bar (GlizzarineProcessor& p, const String& paramId, const String& n) : proc (p), id (paramId), name (n)
{
    setSliderStyle (Slider::LinearHorizontal);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    param = p.apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
        setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    }
    setTooltip (tip (paramId));
    first = false;
}
void Bar::valueChanged() { if (! first) activeUntil = nowMs() + 900.0; }
void Bar::tick (float dt)
{
    const float gT = nowMs() < activeUntil ? 1.0f : 0.0f;
    if (std::abs (glow - gT) > 0.001f) { glow += (gT - glow) * jmin (1.0f, dt * (gT > 0 ? 14.0f : 4.0f)); repaint(); }
}
void Bar::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { learnMenu (proc, this, id, param); return; }
    Slider::mouseDown (e);
}
void Bar::paint (Graphics& g)
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (18);
    text (g, name, top.withTrimmedRight (60), Theme::sans (12.5f), dimmed ? Theme::mute : Theme::soft);
    text (g, AnimKnob::valueText (param), top.removeFromRight (70), Theme::mono (11.0f),
          glow > 0.5f && ! dimmed ? Theme::accent : Theme::dim, Justification::centredRight);
    auto r = b.toFloat().withSizeKeepingCentre ((float) b.getWidth(), 6.0f);
    g.setColour (Theme::track); g.fillRoundedRectangle (r, 3.0f);
    const float p = (float) valueToProportionOfLength (getValue());
    const Colour col = dimmed ? Colour (0xff3a3850) : Theme::knob.interpolatedWith (Theme::accent, jmax (glow, isMouseOverOrDragging() ? 1.0f : 0.0f));
    g.setColour (col); g.fillRoundedRectangle (r.withWidth (jmax (3.0f, r.getWidth() * p)), 3.0f);
    if (isMouseOverOrDragging() || glow > 0.3f)
    {
        g.setColour (Theme::text);
        g.fillEllipse (r.getX() + r.getWidth() * p - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
}

//==============================================================================
VFader::VFader (GlizzarineProcessor& p, int i) : proc (p), index (i)
{
    setSliderStyle (Slider::LinearVertical);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    param = p.apvts.getParameter (KeyLab::faderParam (i));
    if (param != nullptr)
    {
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, KeyLab::faderParam (i), *this);
        setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    }
    setTooltip ("KeyLab fader " + String (i + 1) + ": " + tip (KeyLab::faderParam (i)));
    first = false;
}
void VFader::valueChanged() { if (! first) activeUntil = nowMs() + 900.0; }
void VFader::tick (float dt)
{
    const float gT = nowMs() < activeUntil ? 1.0f : 0.0f;
    if (std::abs (glow - gT) > 0.001f) { glow += (gT - glow) * jmin (1.0f, dt * (gT > 0 ? 14.0f : 4.0f)); repaint(); }
}
void VFader::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { learnMenu (proc, this, "F" + String (index + 1), param); return; }
    Slider::mouseDown (e);
}
void VFader::paint (Graphics& g)
{
    auto b = getLocalBounds();
    auto bottom = b.removeFromBottom (36);
    const float p = (float) valueToProportionOfLength (getValue());
    auto tr = b.reduced (0, 6).withSizeKeepingCentre (6, b.getHeight() - 12).toFloat();
    g.setColour (Theme::track); g.fillRoundedRectangle (tr, 3.0f);
    const float fillH = jmax (3.0f, tr.getHeight() * p);
    const Colour col = Theme::knob.interpolatedWith (Theme::accent, jmax (glow, isMouseOverOrDragging() ? 1.0f : 0.0f));
    g.setColour (col); g.fillRoundedRectangle (tr.withTop (tr.getBottom() - fillH), 3.0f);
    auto cap = Rectangle<float> (26.0f, 10.0f).withCentre ({ tr.getCentreX(), tr.getBottom() - fillH });
    g.setColour (Theme::hub); g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (col); g.drawRoundedRectangle (cap, 4.0f, 1.2f);
    const bool learning = proc.learnTarget() == "F" + String (index + 1);
    text (g, KeyLab::faderName (index), bottom.removeFromTop (18), Theme::sans (12.5f), learning ? Theme::orange : Theme::soft, Justification::centred);
    text (g, AnimKnob::valueText (param), bottom, Theme::mono (10.5f), glow > 0.5f ? Theme::accent : Theme::dim, Justification::centred);
}

//==============================================================================
NumberBox::NumberBox (GlizzarineProcessor& p, const String& paramId, const String& s) : suffix (s)
{
    setSliderStyle (Slider::LinearBarVertical);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (300);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
    setTooltip (tip (paramId));
}
void NumberBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Theme::hub); g.fillRoundedRectangle (r, 9.0f);
    g.setColour (isMouseOverOrDragging() ? Theme::accent : Theme::edge); g.drawRoundedRectangle (r, 9.0f, 1.0f);
    const double v = getValue();
    const String s = std::abs (v - std::round (v)) < 0.001 ? String ((int) std::round (v)) : String (v, 1);
    text (g, (v > 0 && suffix == "st" ? "+" : "") + s + " " + suffix, getLocalBounds(), Theme::mono (14.0f), Theme::text, Justification::centred);
}

//==============================================================================
PresetDialog::PresetDialog()
{
    setWantsKeyboardFocus (true);
    name.setFont (Theme::sans (16.0f));
    name.setIndents (12, 10);
    name.setInputRestrictions (60);
    name.onReturnKey = [this] { ok(); };
    name.onEscapeKey = [this] { close(); };
    okButton.setColour (TextButton::buttonColourId, Theme::accent);
    okButton.setColour (TextButton::textColourOffId, Theme::bg);
    okButton.onClick = [this] { ok(); };
    cancelButton.onClick = [this] { close(); };
    addChildComponent (name);
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
    setVisible (false);
}

void PresetDialog::askName (const String& suggestion, std::function<void (const String&)> onSave)
{
    title = "SAVE PRESET"; message = "Saved in Documents / Glizzarine Presets. Name it so you can find it again.";
    asking = true; nameFn = std::move (onSave); okFn = nullptr;
    okButton.setButtonText ("Save");
    name.setVisible (true); name.setText (suggestion, false); name.selectAll();
    setVisible (true); toFront (true); resized();
    name.grabKeyboardFocus();
}

void PresetDialog::askConfirm (const String& t, const String& m, const String& okText, std::function<void()> onOk)
{
    title = t; message = m; asking = false; okFn = std::move (onOk); nameFn = nullptr;
    okButton.setButtonText (okText);
    name.setVisible (false);
    setVisible (true); toFront (true); resized();
    grabKeyboardFocus();
}

void PresetDialog::ok()
{
    if (asking) { const auto n = name.getText().trim(); if (n.isEmpty()) return; auto fn = nameFn; close(); if (fn) fn (n); }
    else { auto fn = okFn; close(); if (fn) fn(); }
}

bool PresetDialog::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::returnKey) { ok(); return true; }
    return false;
}

void PresetDialog::resized()
{
    card = Rectangle<int> (460, asking ? 214 : 176).withCentre (getLocalBounds().getCentre());
    auto r = card.reduced (26, 22);
    r.removeFromTop (16 + 10 + 36);
    if (asking) { name.setBounds (r.removeFromTop (42)); r.removeFromTop (16); }
    auto b = r.removeFromBottom (40);
    okButton.setBounds (b.removeFromRight (120));
    b.removeFromRight (10);
    cancelButton.setBounds (b.removeFromRight (110));
}

void PresetDialog::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.6f));
    Theme::panel (g, card, 20.0f, Theme::card2);
    g.setColour (Theme::edge); g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 20.0f, 1.0f);
    auto r = card.reduced (26, 22);
    Theme::label (g, title, r.removeFromTop (16), Theme::accent);
    r.removeFromTop (10);
    g.setFont (Theme::sans (13.0f)); g.setColour (Theme::soft);
    g.drawFittedText (message, r.removeFromTop (36), Justification::topLeft, 3, 1.0f);
}

//==============================================================================
PresetBrowser::PresetBrowser (GlizzarineProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    search.setFont (Theme::sans (14.0f));
    search.setIndents (34, 9);
    search.setTextToShowWhenEmpty ("Search presets", Theme::dim);
    search.onTextChange = [this] { scroll = 0; rebuild(); repaint(); };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (search);
    setVisible (false);
}

void PresetBrowser::open()
{
    proc.scanUserPresets();
    scroll = 0; rebuild();
    setVisible (true); toFront (true);
    search.grabKeyboardFocus();
}

void PresetBrowser::rebuild()
{
    items.clear();
    const auto q = search.getText().trim().toLowerCase();
    const auto& f = factoryPresets();
    auto want = [&] (const String& c, const String& n, const String& tags)
    {
        if (cat != 0 && cats[cat] != c) return false;
        return q.isEmpty() || n.toLowerCase().contains (q) || tags.toLowerCase().contains (q) || c.toLowerCase().contains (q);
    };
    for (size_t i = 0; i < f.size(); ++i)
        if (want (f[i].category, f[i].name, f[i].tags)) items.push_back ({ f[i].name, f[i].category, f[i].tags, (int) i, -1 });
    const auto& u = proc.getUserPresets();
    for (size_t i = 0; i < u.size(); ++i)
        if (want ("MY PRESETS", u[i].name, {})) items.push_back ({ u[i].name, "MY PRESETS", "your preset", -1, (int) i });
}

void PresetBrowser::resized()
{
    search.setBounds (getWidth() - 30 - 320, 16, 320, 40);
    grid = getLocalBounds().withTrimmedTop (128).reduced (30, 0).withTrimmedBottom (20);
}

Rectangle<int> PresetBrowser::cardBounds (int i) const
{
    const int cols = 4, gap = 12, h = 112;
    const int w = (grid.getWidth() - gap * (cols - 1)) / cols;
    return { grid.getX() + (i % cols) * (w + gap), grid.getY() + (i / cols) * (h + gap) - scroll, w, h };
}

void PresetBrowser::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    text (g, "PRESETS", { 30, 18, 200, 36 }, Theme::sans (22.0f, true), Theme::accent);
    text (g, "Click a sound to load it. Esc closes.", { 170, 22, 400, 28 }, Theme::sans (13.0f), Theme::dim);
    // categories
    int x = 30;
    for (int i = 0; i < cats.size(); ++i)
    {
        auto f = Theme::sans (13.0f); f.setExtraKerningFactor (0.1f);
        const int w = (int) GlyphArrangement::getStringWidth (f, cats[i]) + 4;
        g.setFont (f); g.setColour (i == cat ? Theme::text : Theme::dim);
        g.drawText (cats[i], x, 70, w, 36, Justification::centred);
        if (i == cat) { g.setColour (Theme::accent); g.fillRect (x, 104, w, 2); }
        x += w + 26;
    }
    g.setColour (Theme::line); g.fillRect (0, 106, getWidth(), 1);

    g.saveState();
    g.reduceClipRegion (grid.expanded (0, 8));
    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto r = cardBounds (i);
        if (r.getBottom() < grid.getY() - 10 || r.getY() > grid.getBottom() + 10) continue;
        const auto& it = items[(size_t) i];
        const bool sel = (it.factory >= 0 && it.factory == proc.getCurrentFactory()) || (it.user >= 0 && it.user == proc.getCurrentUser());
        Theme::panel (g, r, 16.0f, sel ? Colour (0xff1a1422) : (i == hover ? Theme::card2 : Theme::card));
        if (sel) { g.setColour (Theme::accent); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 16.0f, 1.0f); }
        // a small wave, different per sound
        Path p; auto wr = r.reduced (14, 0).withTop (r.getY() + 12).withHeight (40).toFloat();
        const int seed = it.name.hashCode();
        const float f1 = 3.0f + (float) ((seed >> 3) & 7), f2 = 11.0f + (float) ((seed >> 7) & 15);
        for (int k = 0; k <= 60; ++k)
        {
            const float t = (float) k / 60.0f;
            const float y = std::sin (t * f1 + (float) (seed & 15)) * 0.7f * std::sin (MathConstants<float>::pi * t) + std::sin (t * f2) * 0.15f;
            const float px = wr.getX() + t * wr.getWidth(), py = wr.getCentreY() - y * wr.getHeight() * 0.45f;
            if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (sel ? Theme::accent : Colour (0xff6f6d85)); g.strokePath (p, PathStrokeType (2.0f));
        text (g, it.name, { r.getX() + 14, r.getY() + 58, r.getWidth() - 28, 22 }, Theme::sans (15.0f), Theme::text);
        text (g, it.category + String::fromUTF8 ("  ·  ") + it.tags, { r.getX() + 14, r.getY() + 82, r.getWidth() - 28, 16 }, Theme::mono (11.0f), Theme::dim);
    }
    g.restoreState();
    if (items.empty())
        text (g, cat == 7 ? "No presets of your own yet. Use SAVE in the top bar." : "Nothing found.", grid.withHeight (60), Theme::sans (15.0f), Theme::dim, Justification::centred);
}

void PresetBrowser::paintOverChildren (Graphics& g)
{
    // magnifier inside the search box
    auto s = search.getBounds().toFloat();
    g.setColour (Theme::dim);
    g.drawEllipse (s.getX() + 12, s.getCentreY() - 7, 11, 11, 1.6f);
    g.drawLine (s.getX() + 21.5f, s.getCentreY() + 2.5f, s.getX() + 26, s.getCentreY() + 7, 1.6f);
}

void PresetBrowser::mouseDown (const MouseEvent& e)
{
    if (e.y >= 66 && e.y <= 108)
    {
        int x = 30;
        for (int i = 0; i < cats.size(); ++i)
        {
            auto f = Theme::sans (13.0f); f.setExtraKerningFactor (0.1f);
            const int w = (int) GlyphArrangement::getStringWidth (f, cats[i]) + 4;
            if (e.x >= x - 13 && e.x < x + w + 13) { cat = i; scroll = 0; rebuild(); repaint(); return; }
            x += w + 26;
        }
        return;
    }
    for (int i = 0; i < (int) items.size(); ++i)
        if (cardBounds (i).contains (e.getPosition()) && grid.contains (e.getPosition()))
        {
            const auto it = items[(size_t) i];
            if (it.factory >= 0) proc.loadFactory (it.factory); else proc.loadUser (it.user);
            if (onClose) onClose();
            return;
        }
}

void PresetBrowser::mouseMove (const MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < (int) items.size(); ++i) if (cardBounds (i).contains (e.getPosition()) && grid.contains (e.getPosition())) h = i;
    if (h != hover) { hover = h; repaint(); }
}

void PresetBrowser::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int rows = ((int) items.size() + 3) / 4;
    const int maxScroll = jmax (0, rows * 124 - grid.getHeight());
    scroll = jlimit (0, maxScroll, scroll - roundToInt (w.deltaY * 240.0f));
    repaint();
}

bool PresetBrowser::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

//==============================================================================
// Pages
namespace
{
    const Rectangle<int> kContent { 30, 124, 1220, 616 };   // everything below the tabs

    void card (Graphics& g, Rectangle<int> r, const String& title, const String& hint = {})
    {
        Theme::panel (g, r);
        if (title.isNotEmpty()) Theme::label (g, title, { r.getX() + 22, r.getY() + 16, r.getWidth() - 44, 16 });
        if (hint.isNotEmpty()) text (g, hint, { r.getX() + 22, r.getY() + 14, r.getWidth() - 44, 18 }, Theme::sans (12.0f), Theme::mute, Justification::centredRight);
    }

    //--------------------------------------------------------------------------
    // PLAY: what you see is what you hear. Scope, the 8 KeyLab knobs (per preset) and the 8 faders.
    class PlayPage : public Page
    {
    public:
        explicit PlayPage (GlizzarineProcessor& p) : Page (p)
        {
            for (int i = 0; i < 8; ++i)
            {
                macroKnobs[i] = knob ("cutoff", "-");
                macroKnobs[i]->setLearnTarget ("E" + String (i + 1));
                macroKnobs[i]->setCaption ("E" + String (i + 1));
            }
            for (int i = 0; i < 8; ++i) faders[i] = add<VFader> (p, i);
            presetChanged();
        }
        void presetChanged() override
        {
            for (int i = 0; i < 8; ++i)
            {
                const auto id = proc.macroParam (i);
                macroKnobs[i]->bind (id, shortName (id));
                macroKnobs[i]->setLearnTarget ("E" + String (i + 1));
                macroKnobs[i]->setTooltip ("KeyLab knob " + String (i + 1) + ": " + tip (id));
            }
            repaint();
        }
        static String shortName (const String& id)
        {
            static const std::pair<const char*, const char*> n[] = {
                { "cutoff", "Cutoff" }, { "reso", "Resonance" }, { "fenv_amt", "Filter Env" }, { "m1_amt", "Mod 1" }, { "m2_amt", "Mod 2" },
                { "m3_amt", "Mod 3" }, { "m4_amt", "Mod 4" }, { "o1_shape", "Osc 1 Shape" }, { "o2_shape", "Osc 2 Shape" }, { "o3_shape", "Osc 3 Shape" },
                { "o1_color", "Osc 1 Color" }, { "o2_detune", "Detune" }, { "o3_detune", "Detune 3" }, { "o2_level", "Osc 2" }, { "o3_level", "Osc 3" },
                { "chorus_mix", "Chorus" }, { "at_cutoff", "AT Filter" }, { "vib_at", "AT Vibrato" }, { "wide", "Width" }, { "amp_decay", "Decay" },
                { "reverb_size", "Room Size" }, { "reverb_shimmer", "Shimmer" }, { "cloud_mix", "Grains" }, { "cloud_size", "Grain Size" },
                { "cloud_spray", "Spray" }, { "cloud_density", "Density" }, { "arp_gate", "Arp Length" }, { "arp_octaves", "Octaves" },
                { "delay_fb", "Feedback" }, { "delay_tone", "Echo Tone" }, { "tremolo", "Auto-Pan" }, { "lfo2_sync", "Step Rate" },
                { "flt_attack", "Filter Attack" }, { "orbit", "Orbit" }, { "entropy", "Entropy" }, { "ring", "Ring Mod" }, { "drift", "Drift" },
                { "glide", "Glide" }, { "xfm", "Cross FM" }, { "vib_wheel", "Vibrato" }
            };
            for (auto& kv : n) if (id == kv.first) return kv.second;
            return id;
        }
        void resized() override
        {
            scopeRect = { kContent.getX(), kContent.getY(), kContent.getWidth(), 170 };
            knobRect = { kContent.getX(), scopeRect.getBottom() + 16, 838, kContent.getBottom() - scopeRect.getBottom() - 16 };
            faderRect = { knobRect.getRight() + 16, knobRect.getY(), kContent.getRight() - knobRect.getRight() - 16, knobRect.getHeight() };
            auto k = knobRect.reduced (16, 0).withTrimmedTop (48).withTrimmedBottom (18);
            const int w = k.getWidth() / 4, h = k.getHeight() / 2;
            for (int i = 0; i < 8; ++i) macroKnobs[i]->setBounds (k.getX() + (i % 4) * w + 4, k.getY() + (i / 4) * h, w - 8, h - 6);
            auto f = faderRect.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (14);
            const int fw = f.getWidth() / 8;
            for (int i = 0; i < 8; ++i) faders[i]->setBounds (f.getX() + i * fw, f.getY(), fw, f.getHeight());
        }
        void paint (Graphics& g) override
        {
            Theme::panel (g, scopeRect);
            // live output scope
            const int w = proc.scopeWrite.load();
            auto sr = scopeRect.reduced (20, 30).toFloat();
            Path p; float peak = 0.0f;
            const int n = 700;
            for (int i = 0; i < n; ++i) peak = jmax (peak, std::abs (proc.scope[(size_t) ((w - n + i) & 2047)]));
            const float gain = peak > 0.02f ? 0.9f / peak : 1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float v = jlimit (-1.0f, 1.0f, proc.scope[(size_t) ((w - n + i) & 2047)] * gain);
                const float x = sr.getX() + sr.getWidth() * (float) i / (float) (n - 1), y = sr.getCentreY() - v * sr.getHeight() * 0.5f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (Theme::accent.withAlpha (0.18f)); g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Theme::accent); g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            String desc = proc.getPresetCategory() + String::fromUTF8 ("  ·  ") + proc.getPresetName();
            if (proc.getCurrentFactory() >= 0) desc << String::fromUTF8 ("  ·  ") << factoryPresets()[(size_t) proc.getCurrentFactory()].tags;
            Theme::label (g, desc.toUpperCase(), { scopeRect.getX() + 20, scopeRect.getY() + 12, 700, 16 });
            text (g, String (proc.voicesShown.load()) + " voices", { scopeRect.getRight() - 160, scopeRect.getY() + 10, 140, 18 },
                  Theme::mono (11.0f), Theme::dim, Justification::centredRight);

            card (g, knobRect, "KEYLAB KNOBS E1-E8", String::fromUTF8 ("they change with the sound · right-click = MIDI learn"));
            card (g, faderRect, "KEYLAB FADERS", "always the same");
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            if (++frame % 2 == 0) repaint (scopeRect);
        }
    private:
        AnimKnob* macroKnobs[8] {};
        VFader* faders[8] {};
        Rectangle<int> scopeRect, knobRect, faderRect;
        int frame = 0;
    };

    //--------------------------------------------------------------------------
    // OSC: three oscillators side by side, voice settings below
    class OscPage : public Page
    {
    public:
        explicit OscPage (GlizzarineProcessor& p) : Page (p)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto& o = osc[i];
                o.type = add<Choice> (p, Params::osc (i, "type"));
                o.shape = knob (Params::osc (i, "shape"), "Shape");
                o.color = knob (Params::osc (i, "color"), "Color");
                o.detune = knob (Params::osc (i, "detune"), "Detune", AnimKnob::Small);
                o.level = knob (Params::osc (i, "level"), "Level", AnimKnob::Small);
                o.pitch = add<NumberBox> (p, Params::osc (i, "pitch"), "st");
                o.typeParam = p.apvts.getParameter (Params::osc (i, "type"));
                o.levelParam = p.apvts.getParameter (Params::osc (i, "level"));
            }
            xfm = knob ("xfm", "Cross FM", AnimKnob::Small);
            ring = knob ("ring", "Ring Mod", AnimKnob::Small);
            glide = knob ("glide", "Glide", AnimKnob::Small);
            mode = add<Segments> (p, "mode", StringArray { "Mono", "Legato", "Poly" });
        }
        void resized() override
        {
            const int gap = 16, w = (kContent.getWidth() - 2 * gap) / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto r = Rectangle<int> (kContent.getX() + i * (w + gap), kContent.getY(), w, 450);
                cardRect[i] = r;
                auto in = r.reduced (20, 0).withTrimmedTop (46);
                osc[i].type->setBounds (in.removeFromTop (38));
                in.removeFromTop (12);
                auto big = in.removeFromTop (172);
                osc[i].shape->setBounds (big.removeFromLeft (big.getWidth() / 2).reduced (4, 0));
                osc[i].color->setBounds (big.reduced (4, 0));
                in.removeFromTop (12);
                auto row = in.removeFromTop (130);
                const int cw = row.getWidth() / 3;
                pitchRect[i] = row.removeFromLeft (cw);
                osc[i].pitch->setBounds (pitchRect[i].withSizeKeepingCentre (cw - 20, 40).withY (pitchRect[i].getY() + 30));
                osc[i].detune->setBounds (row.removeFromLeft (cw));
                osc[i].level->setBounds (row);
            }
            bottomRect = { kContent.getX(), kContent.getY() + 466, kContent.getWidth(), kContent.getHeight() - 466 };
            auto b = bottomRect.reduced (20, 0).withTrimmedTop (36).withTrimmedBottom (10);
            xfm->setBounds (b.removeFromLeft (120)); ring->setBounds (b.removeFromLeft (120));
            b.removeFromLeft (60);
            modeRect = b.removeFromLeft (360);
            mode->setBounds (modeRect.withSizeKeepingCentre (modeRect.getWidth(), 40).translated (0, 6));
            b.removeFromLeft (30);
            glide->setBounds (b.removeFromLeft (120));
        }
        void paint (Graphics& g) override
        {
            for (int i = 0; i < 3; ++i)
            {
                card (g, cardRect[i], "OSCILLATOR " + String (i + 1), osc[i].levelParam != nullptr && osc[i].levelParam->getValue() < 0.001f ? "off (level 0)" : String());
                text (g, "Pitch", pitchRect[i].withHeight (20).withY (pitchRect[i].getY() + 80), Theme::sans (13.0f), Theme::text, Justification::centred);
                text (g, "semitones", pitchRect[i].withHeight (16).withY (pitchRect[i].getY() + 98), Theme::mono (11.0f), Theme::dim, Justification::centred);
            }
            card (g, bottomRect, "MIX & VOICES");
            Theme::label (g, "VOICE MODE", modeRect.withHeight (16).translated (0, -24 + 30));
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            for (int i = 0; i < 3; ++i)
            {
                const int t = roundToInt (plainOf (osc[i].typeParam));
                if (t != osc[i].shownType)
                {
                    osc[i].shownType = t;
                    osc[i].shape->setLabel (Params::shapeName (t));
                    osc[i].color->setLabel (Params::colorName (t));
                }
                const bool off = osc[i].levelParam != nullptr && osc[i].levelParam->getValue() < 0.001f;
                osc[i].shape->setDimmed (off); osc[i].color->setDimmed (off); osc[i].detune->setDimmed (off);
                if (off != osc[i].wasOff) { osc[i].wasOff = off; repaint (cardRect[i]); }
            }
        }
    private:
        struct O { Choice* type; AnimKnob *shape, *color, *detune, *level; NumberBox* pitch; RangedAudioParameter *typeParam, *levelParam; int shownType = -1; bool wasOff = false; };
        O osc[3];
        AnimKnob *xfm, *ring, *glide; Segments* mode;
        Rectangle<int> cardRect[3], pitchRect[3], bottomRect, modeRect;
    };

    //--------------------------------------------------------------------------
    // FILTER & ENV: the filter on the left, both envelopes on the right (with a small curve)
    class FilterPage : public Page
    {
    public:
        explicit FilterPage (GlizzarineProcessor& p) : Page (p)
        {
            type = add<Segments> (p, "filter_type", StringArray { "Ladder", "LP 12", "LP 24", "Band", "High", "Notch", "Formant", "Comb" });
            cutoff = knob ("cutoff", "Cutoff"); reso = knob ("reso", "Resonance");
            envAmt = knob ("fenv_amt", "Env Amount"); drive = knob ("filter_drive", "Drive");
            const char* a[4] = { "amp_attack", "amp_decay", "amp_sustain", "amp_release" };
            const char* f[4] = { "flt_attack", "flt_decay", "flt_sustain", "flt_release" };
            const char* n[4] = { "Attack", "Decay", "Sustain", "Release" };
            for (int i = 0; i < 4; ++i) { amp[i] = knob (a[i], n[i], AnimKnob::Small); flt[i] = knob (f[i], n[i], AnimKnob::Small); }
        }
        void resized() override
        {
            filterRect = { kContent.getX(), kContent.getY(), 560, kContent.getHeight() };
            auto in = filterRect.reduced (20, 0).withTrimmedTop (46);
            type->setBounds (in.removeFromTop (40));
            in.removeFromTop (30);
            auto row1 = in.removeFromTop (220);
            cutoff->setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (20, 0)); reso->setBounds (row1.reduced (20, 0));
            in.removeFromTop (20);
            auto row2 = in.removeFromTop (220);
            envAmt->setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (20, 0)); drive->setBounds (row2.reduced (20, 0));
            const int x = filterRect.getRight() + 16, w = kContent.getRight() - x, h = (kContent.getHeight() - 16) / 2;
            ampRect = { x, kContent.getY(), w, h };
            fltRect = { x, kContent.getY() + h + 16, w, h };
            layoutEnv (ampRect, amp); layoutEnv (fltRect, flt);
        }
        void layoutEnv (Rectangle<int> r, AnimKnob** k)
        {
            auto in = r.reduced (20, 0).withTrimmedTop (40).withTrimmedBottom (14);
            in.removeFromLeft (290);   // curve
            const int w = in.getWidth() / 4, h = jmin (in.getHeight(), w + 46);
            for (int i = 0; i < 4; ++i) k[i]->setBounds (in.getX() + i * w, in.getCentreY() - h / 2 + 8, w, h);
        }
        void drawEnv (Graphics& g, Rectangle<int> r, const char* pre, Colour col)
        {
            auto c = r.reduced (22, 0).withTrimmedTop (50).withTrimmedBottom (26).withWidth (260).toFloat();
            g.setColour (Theme::hub); g.fillRoundedRectangle (c, 10.0f);
            c = c.reduced (12, 14);
            const float A = proc.valueOf (String (pre) + "attack"), D = proc.valueOf (String (pre) + "decay"),
                        S = proc.valueOf (String (pre) + "sustain"), R = proc.valueOf (String (pre) + "release");
            auto len = [] (float t) { return std::sqrt (jmax (0.001f, t)); };
            const float total = len (A) + len (D) + 0.6f + len (R);
            const float xa = c.getX() + c.getWidth() * len (A) / total, xd = xa + c.getWidth() * len (D) / total, xs = xd + c.getWidth() * 0.6f / total;
            Path p;
            p.startNewSubPath (c.getX(), c.getBottom());
            p.lineTo (xa, c.getY());
            p.quadraticTo (xa + (xd - xa) * 0.3f, c.getBottom() - (c.getHeight() * S), xd, c.getBottom() - c.getHeight() * S);
            p.lineTo (xs, c.getBottom() - c.getHeight() * S);
            p.quadraticTo (xs + (c.getRight() - xs) * 0.3f, c.getBottom(), c.getRight(), c.getBottom());
            g.setColour (col.withAlpha (0.15f)); g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (col); g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        void paint (Graphics& g) override
        {
            card (g, filterRect, "FILTER", shortType (type->current()) + String::fromUTF8 (" · key tracking is automatic"));
            card (g, ampRect, "AMP ENVELOPE", "volume over time");
            card (g, fltRect, "FILTER ENVELOPE", "Env Amount sets how far it opens");
            drawEnv (g, ampRect, "amp_", Theme::accent);
            drawEnv (g, fltRect, "flt_", Theme::violet);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            if (++frame % 6 == 0) repaint();
            const bool noEnv = std::abs (proc.valueOf ("fenv_amt")) < 0.005f;
            for (auto* k : flt) k->setDimmed (noEnv);
        }
    private:
        Segments* type; AnimKnob *cutoff, *reso, *envAmt, *drive; AnimKnob* amp[4]; AnimKnob* flt[4];
        Rectangle<int> filterRect, ampRect, fltRect;
        int frame = 0;
    };

    //--------------------------------------------------------------------------
    // MOD: two LFOs, the mod matrix, playing (wheel / aftertouch) and strange physics
    class ModPage : public Page
    {
    public:
        explicit ModPage (GlizzarineProcessor& p) : Page (p)
        {
            for (int i = 0; i < 2; ++i)
            {
                const String pre = "lfo" + String (i + 1) + "_";
                lfoWave[i] = add<Choice> (p, pre + "wave"); lfoSync[i] = add<Choice> (p, pre + "sync");
                lfoRate[i] = knob (pre + "rate", "Speed", AnimKnob::Small);
            }
            for (int i = 0; i < 4; ++i)
            {
                src[i] = add<Choice> (p, Params::slot (i, "src")); dst[i] = add<Choice> (p, Params::slot (i, "dst"));
                amt[i] = add<Bar> (p, Params::slot (i, "amt"), "Amount");
            }
            vibWheel = knob ("vib_wheel", "Wheel Vibrato", AnimKnob::Small);
            vibAt = knob ("vib_at", "AT Vibrato", AnimKnob::Small);
            atCut = knob ("at_cutoff", "AT Filter", AnimKnob::Small);
            physOn = add<Switch> (p, "physics_on");
            const char* ph[4] = { "gravity", "entropy", "drift", "orbit" };
            const char* pn[4] = { "Gravity", "Entropy", "Drift", "Orbit" };
            for (int i = 0; i < 4; ++i) phys[i] = knob (ph[i], pn[i], AnimKnob::Small);
        }
        void resized() override
        {
            const int x0 = kContent.getX(), y0 = kContent.getY();
            for (int i = 0; i < 2; ++i)
            {
                lfoRect[i] = { x0, y0 + i * 216, 380, 200 };
                auto in = lfoRect[i].reduced (20, 0).withTrimmedTop (46).withTrimmedBottom (14);
                auto left = in.removeFromLeft (200);
                lfoWave[i]->setBounds (left.removeFromTop (36)); left.removeFromTop (14);
                lfoSync[i]->setBounds (left.removeFromTop (36));
                lfoRate[i]->setBounds (in.reduced (10, 0));
            }
            matrixRect = { x0 + 396, y0, kContent.getRight() - x0 - 396, 416 };
            auto m = matrixRect.reduced (20, 0).withTrimmedTop (64);
            for (int i = 0; i < 4; ++i)
            {
                auto row = m.removeFromTop (78);
                auto r = row.withTrimmedBottom (20);
                src[i]->setBounds (r.removeFromLeft (220).withSizeKeepingCentre (220, 36));
                r.removeFromLeft (40);
                dst[i]->setBounds (r.removeFromLeft (220).withSizeKeepingCentre (220, 36));
                r.removeFromLeft (30);
                amt[i]->setBounds (r.withSizeKeepingCentre (r.getWidth(), 40));
            }
            playRect = { x0, y0 + 432, 380, kContent.getBottom() - y0 - 432 };
            auto pl = playRect.reduced (14, 0).withTrimmedTop (36).withTrimmedBottom (6);
            const int pw = pl.getWidth() / 3;
            vibWheel->setBounds (pl.removeFromLeft (pw)); vibAt->setBounds (pl.removeFromLeft (pw)); atCut->setBounds (pl);
            physRect = { x0 + 396, y0 + 432, kContent.getRight() - x0 - 396, playRect.getHeight() };
            physOn->setBounds (physRect.getRight() - 20 - 56, physRect.getY() + 8, 56, 32);
            auto ps = physRect.reduced (20, 0).withTrimmedTop (36).withTrimmedBottom (6).withTrimmedLeft (200);
            const int qw = ps.getWidth() / 4;
            for (int i = 0; i < 4; ++i) phys[i]->setBounds (ps.removeFromLeft (qw));
        }
        void paint (Graphics& g) override
        {
            for (int i = 0; i < 2; ++i) card (g, lfoRect[i], "LFO " + String (i + 1), i == 1 ? "synced = restarts with every note" : "");
            card (g, matrixRect, "MOD MATRIX", String::fromUTF8 ("source → target · amount can be negative"));
            auto m = matrixRect.reduced (20, 0).withTrimmedTop (40);
            Theme::label (g, "SOURCE", m.withHeight (16)); Theme::label (g, "TARGET", m.withHeight (16).translated (260, 0));
            card (g, playRect, "PLAYING", String::fromUTF8 ("mod wheel · aftertouch"));
            card (g, physRect, "STRANGE PHYSICS");
            g.setFont (Theme::sans (12.5f)); g.setColour (Theme::dim);
            g.drawFittedText ("Notes sink or rise, every note comes out different, tuning wanders, oscillators circle in stereo.",
                              { physRect.getX() + 22, physRect.getY() + 50, 176, 110 }, Justification::topLeft, 6, 1.0f);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            const bool on = physOn->isOn();
            for (auto* k : phys) k->setDimmed (! on);
            for (int i = 0; i < 4; ++i) amt[i]->setDimmed (src[i]->getSelectedItemIndex() <= 0);
            for (int i = 0; i < 2; ++i) lfoRate[i]->setDimmed (lfoSync[i]->getSelectedItemIndex() > 0);
        }
    private:
        Choice *lfoWave[2], *lfoSync[2]; AnimKnob* lfoRate[2];
        Choice *src[4], *dst[4]; Bar* amt[4];
        AnimKnob *vibWheel, *vibAt, *atCut; Switch* physOn; AnimKnob* phys[4];
        Rectangle<int> lfoRect[2], matrixRect, playRect, physRect;
    };

    //--------------------------------------------------------------------------
    // The 16 custom steps plus a live view of the bar (the 1 is marked)
    class StepGrid : public Component, public SettableTooltipClient
    {
    public:
        explicit StepGrid (GlizzarineProcessor& p) : proc (p)
        {
            for (int i = 0; i < 16; ++i) steps[i] = p.apvts.getParameter (Params::arpStep (i));
            setTooltip (Tooltips::get ("arp_step1"));
        }
        void paint (Graphics& g) override
        {
            const int barSteps = jlimit (1, 48, proc.arpBarSteps.load()), cur = proc.arpStepShown.load();
            const int shownSteps = jmin (16, barSteps);
            const float w = (float) getWidth() / 16.0f;
            const bool custom = roundToInt (proc.valueOf ("arp_rhythm")) == Params::customRhythm;
            for (int i = 0; i < 16; ++i)
            {
                auto r = Rectangle<float> ((float) i * w + 2.0f, 0.0f, w - 4.0f, (float) getHeight() - 16.0f);
                const bool on = steps[i] != nullptr && steps[i]->getValue() > 0.5f;
                const bool inBar = i < shownSteps;
                const bool now = cur >= 0 && (cur % 16) == i;
                g.setColour (now ? Theme::accent : (on ? (custom ? Theme::knob : Theme::mute) : Theme::hub));
                if (! inBar) g.setColour (Theme::hub.withAlpha (0.4f));
                g.fillRoundedRectangle (r, 6.0f);
                if (i == 0)   // the 1 of the bar
                { g.setColour (Theme::accent); g.fillRoundedRectangle (r.getX(), (float) getHeight() - 8.0f, r.getWidth(), 3.0f, 1.5f); }
                text (g, String (i + 1), Rectangle<int> ((int) r.getX(), getHeight() - 14, (int) r.getWidth(), 12), Theme::mono (9.5f),
                      i == 0 ? Theme::accent : Theme::mute, Justification::centred);
            }
        }
        void mouseDown (const MouseEvent& e) override
        {
            const int i = jlimit (0, 15, (int) ((float) e.x / ((float) getWidth() / 16.0f)));
            if (steps[i] != nullptr) setPlain (steps[i], steps[i]->getValue() > 0.5f ? 0.0f : 1.0f);
            repaint();
        }
    private:
        GlizzarineProcessor& proc;
        RangedAudioParameter* steps[16] {};
    };

    class ArpPage : public Page
    {
    public:
        explicit ArpPage (GlizzarineProcessor& p) : Page (p)
        {
            on = add<Switch> (p, "arp_on");
            pattern = add<Choice> (p, "arp_mode");
            speed = add<Segments> (p, "arp_speed", StringArray { "1x", "2x", "4x" });
            octaves = add<Segments> (p, "arp_octaves", StringArray { "1", "2", "3", "4" });
            gate = knob ("arp_gate", "Length", AnimKnob::Small); swing = knob ("arp_swing", "Swing", AnimKnob::Small);
            mutate = knob ("arp_mutate", "Mutation", AnimKnob::Small); glitch = knob ("arp_glitch", "Glitch", AnimKnob::Small);
            hold = add<Switch> (p, "arp_hold"); topLine = add<Switch> (p, "arp_topline");
            rhythm = add<Choice> (p, "arp_rhythm");
            grid = add<StepGrid> (p);
        }
        void resized() override
        {
            const int x0 = kContent.getX(), y0 = kContent.getY();
            mainRect = { x0, y0, 600, 300 };
            on->setBounds (mainRect.getX() + 80, mainRect.getY() + 8, 56, 32);
            auto in = mainRect.reduced (22, 0).withTrimmedTop (56);
            auto labels = in.removeFromLeft (110); (void) labels;
            pattern->setBounds (in.removeFromTop (38).withWidth (300)); in.removeFromTop (22);
            speed->setBounds (in.removeFromTop (38).withWidth (300)); in.removeFromTop (22);
            octaves->setBounds (in.removeFromTop (38).withWidth (300)); in.removeFromTop (22);
            auto hl = in.removeFromTop (38);
            hold->setBounds (hl.removeFromLeft (56)); hl.removeFromLeft (150);
            topLine->setBounds (hl.removeFromLeft (56));
            feelRect = { x0 + 616, y0, kContent.getRight() - x0 - 616, 300 };
            auto f = feelRect.reduced (16, 0).withTrimmedTop (60).withTrimmedBottom (20);
            const int w = f.getWidth() / 4;
            gate->setBounds (f.removeFromLeft (w)); swing->setBounds (f.removeFromLeft (w)); mutate->setBounds (f.removeFromLeft (w)); glitch->setBounds (f);
            rhythmRect = { x0, y0 + 316, kContent.getWidth(), kContent.getHeight() - 316 };
            rhythm->setBounds (rhythmRect.getX() + 22, rhythmRect.getY() + 52, 300, 38);
            grid->setBounds (rhythmRect.reduced (22, 0).withTrimmedTop (120).withTrimmedBottom (40));
        }
        void paint (Graphics& g) override
        {
            card (g, mainRect, "ARP");
            auto in = mainRect.reduced (22, 0).withTrimmedTop (56);
            const char* lab[4] = { "Pattern", "Speed", "Octaves", "Hold" };
            for (int i = 0; i < 4; ++i) text (g, lab[i], { in.getX(), in.getY() + i * 60, 110, 38 }, Theme::sans (14.0f), Theme::soft);
            text (g, "Top line", { in.getX() + 110 + 56 + 24, in.getY() + 180, 120, 38 }, Theme::sans (14.0f), Theme::soft);
            text (g, String::fromUTF8 ("1x = one step per beat · the 1 of the bar is accented"), { mainRect.getX() + 22, mainRect.getBottom() - 30, 560, 18 },
                  Theme::sans (12.0f), Theme::mute);
            card (g, feelRect, "FEEL");
            card (g, rhythmRect, "RHYTHM", String::fromUTF8 ("Custom uses the 16 steps · pink = the step playing now"));
            text (g, String (proc.bpmShown.load(), 1) + " BPM", { rhythmRect.getX() + 340, rhythmRect.getY() + 52, 200, 38 }, Theme::mono (13.0f), Theme::dim);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            const bool active = on->isOn();
            for (auto* k : { gate, swing, mutate, glitch }) k->setDimmed (! active);
            speed->setDimmed (! active); octaves->setDimmed (! active);
            const int s = proc.arpStepShown.load();
            if (s != lastStep || ++frame % 15 == 0) { lastStep = s; grid->repaint(); }
        }
    private:
        Switch* on; Choice* pattern; Segments *speed, *octaves; AnimKnob *gate, *swing, *mutate, *glitch;
        Switch *hold, *topLine; Choice* rhythm; StepGrid* grid;
        Rectangle<int> mainRect, feelRect, rhythmRect;
        int lastStep = -2, frame = 0;
    };

    //--------------------------------------------------------------------------
    // EFFECTS: every effect is one row with its switch and all its controls
    class EffectsPage : public Page
    {
    public:
        explicit EffectsPage (GlizzarineProcessor& p) : Page (p)
        {
            row ("DRIVE", "drive_on", "dist_type", { { "dist_amt", "Amount" } });
            row ("TAPE", "tape_on", {}, { { "tape", "Age" } });
            row ("CHORUS", "chorus_on", "chorus_type", { { "chorus_mix", "Mix" }, { "chorus_rate", "Speed" }, { "chorus_depth", "Depth" } });
            row ("PHASER", "phaser_on", {}, { { "phaser_mix", "Mix" }, { "phaser_rate", "Speed" }, { "phaser_depth", "Depth" } });
            row ("DELAY", "delay_on", "delay_sync", { { "delay_mix", "Mix" }, { "delay_fb", "Feedback" }, { "delay_time", "Time (Free)" }, { "delay_tone", "Tone" } });
            row ("GRAINS", "grains_on", {}, { { "cloud_mix", "Mix" }, { "cloud_size", "Size" }, { "cloud_density", "Density" }, { "cloud_spray", "Spray" } });
            row ("REVERB", "reverb_on", {}, { { "reverb_mix", "Mix" }, { "reverb_size", "Size" } });
            row ("SHIMMER", "shimmer_on", {}, { { "reverb_shimmer", "Amount" } });
            row ("GATE", "gate_on", "gate_pattern", { { "gate_depth", "Depth" } });
            row ("STEREO", "stereo_on", "stereo_style", { { "wide", "Width" }, { "tremolo", "Auto-Pan" }, { "tremolo_rate", "Pan Speed" } });
        }
        void row (const char* name, const char* sw, const String& choiceId, std::vector<std::pair<const char*, const char*>> bars)
        {
            Row r; r.name = name; r.sw = add<Switch> (proc, sw);
            if (choiceId.isNotEmpty()) r.choice = add<Choice> (proc, choiceId);
            for (auto& b : bars) r.bars.push_back (add<Bar> (proc, b.first, b.second));
            rows.push_back (r);
        }
        void resized() override
        {
            const int colW = (kContent.getWidth() - 16) / 2, h = (kContent.getHeight() - 4 * 12) / 5;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const int col = (int) i / 5, k = (int) i % 5;
                auto r = Rectangle<int> (kContent.getX() + col * (colW + 16), kContent.getY() + k * (h + 12), colW, h);
                rows[i].rect = r;
                rows[i].sw->setBounds (r.getX() + 14, r.getY() + 12, 56, 30);
                if (rows[i].choice != nullptr) rows[i].choice->setBounds (r.getRight() - 20 - 150, r.getY() + 10, 150, 34);
                auto in = r.reduced (20, 0).withTrimmedTop (54).withTrimmedBottom (10);
                const int n = (int) rows[i].bars.size();
                const int bw = n > 0 ? (in.getWidth() - (n - 1) * 18) / n : 0;
                for (int b = 0; b < n; ++b) rows[i].bars[(size_t) b]->setBounds (in.getX() + b * (bw + 18), in.getY(), bw, 40);
            }
        }
        void paint (Graphics& g) override
        {
            for (auto& r : rows)
            {
                Theme::panel (g, r.rect, 18.0f);
                text (g, r.name, { r.rect.getX() + 80, r.rect.getY() + 12, 120, 30 }, Theme::sans (13.0f, true), r.sw->isOn() ? Theme::text : Theme::dim);
            }
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            for (auto& r : rows)
            {
                const bool on = r.sw->isOn();
                for (auto* b : r.bars) b->setDimmed (! on);
                if (on != r.wasOn) { r.wasOn = on; repaint (r.rect); }
            }
        }
    private:
        struct Row { String name; Switch* sw = nullptr; Choice* choice = nullptr; std::vector<Bar*> bars; Rectangle<int> rect; bool wasOn = false; };
        std::vector<Row> rows;
    };

    //--------------------------------------------------------------------------
    // SETUP: KeyLab learning, tempo and time signature
    class SetupPage : public Page
    {
    public:
        explicit SetupPage (GlizzarineProcessor& p) : Page (p)
        {
            learnBtn.setButtonText ("Learn KeyLab");
            learnBtn.setColour (TextButton::buttonColourId, Theme::accent);
            learnBtn.setColour (TextButton::textColourOffId, Theme::bg);
            learnBtn.onClick = [this] { if (proc.wizardStep() >= 0) proc.cancelLearn(); else proc.startWizard(); };
            clearBtn.setButtonText ("Forget all");
            clearBtn.onClick = [this] { proc.clearMappings(); };
            absBtn.setButtonText ("Absolute"); relBtn.setButtonText ("Relative");
            absBtn.setClickingTogglesState (false); relBtn.setClickingTogglesState (false);
            absBtn.onClick = [this] { proc.setEncodersRelative (false); };
            relBtn.onClick = [this] { proc.setEncodersRelative (true); };
            for (auto* b : { &learnBtn, &clearBtn, &absBtn, &relBtn, &tapBtn, &folderBtn }) addAndMakeVisible (b);
            clock = add<Segments> (p, "clock_mode", StringArray { "Follow project", "Own tempo" });
            bpm = add<NumberBox> (p, "own_bpm", "BPM");
            tapBtn.setButtonText ("Tap"); tapBtn.onClick = [this] { proc.tap(); };
            ts = add<Choice> (p, "time_sig");
            folderBtn.setButtonText ("Open preset folder");
            folderBtn.onClick = [this] { auto f = proc.userPresetFolder(); f.createDirectory(); f.startAsProcess(); };
        }
        void resized() override
        {
            const int x0 = kContent.getX(), y0 = kContent.getY();
            keyRect = { x0, y0, 760, kContent.getHeight() };
            learnBtn.setBounds (keyRect.getX() + 22, keyRect.getY() + 48, 170, 40);
            clearBtn.setBounds (keyRect.getX() + 204, keyRect.getY() + 48, 120, 40);
            absBtn.setBounds (keyRect.getRight() - 22 - 220, keyRect.getY() + 48, 108, 40);
            relBtn.setBounds (keyRect.getRight() - 22 - 108, keyRect.getY() + 48, 108, 40);
            tempoRect = { x0 + 776, y0, kContent.getRight() - x0 - 776, 300 };
            auto t = tempoRect.reduced (22, 0).withTrimmedTop (50);
            clock->setBounds (t.removeFromTop (40)); t.removeFromTop (18);
            auto r = t.removeFromTop (40); bpm->setBounds (r.removeFromLeft (150)); r.removeFromLeft (10); tapBtn.setBounds (r.removeFromLeft (90));
            t.removeFromTop (40);
            ts->setBounds (t.removeFromTop (38).withTrimmedLeft (130));
            infoRect = { tempoRect.getX(), y0 + 316, tempoRect.getWidth(), kContent.getHeight() - 316 };
            folderBtn.setBounds (infoRect.getX() + 22, infoRect.getBottom() - 60, 200, 38);
        }
        void paint (Graphics& g) override
        {
            card (g, keyRect, "KEYLAB", "saved for all projects");
            const String wiz = proc.wizardText();
            const String learn = proc.learnTarget();
            String status = wiz.isNotEmpty() ? wiz : (learn.isNotEmpty() ? "Move a knob or fader for: " + proc.describeTarget (learn) : String ("Encoders send:"));
            text (g, status, { keyRect.getX() + 340, keyRect.getY() + 48, wiz.isNotEmpty() || learn.isNotEmpty() ? 400 : 160, 40 },
                  Theme::sans (14.0f), wiz.isNotEmpty() || learn.isNotEmpty() ? Theme::orange : Theme::dim,
                  wiz.isNotEmpty() || learn.isNotEmpty() ? Justification::centredLeft : Justification::centredRight);
            // mapping table
            auto tbl = keyRect.reduced (22, 0).withTrimmedTop (110).withTrimmedBottom (20);
            const int colW = tbl.getWidth() / 2;
            for (int c = 0; c < 2; ++c)
            {
                Theme::label (g, c == 0 ? "KNOBS E1-E8 (change with the sound)" : "FADERS (always the same)", { tbl.getX() + c * colW, tbl.getY(), colW, 16 });
                for (int i = 0; i < 8; ++i)
                {
                    const String target = (c == 0 ? "E" : "F") + String (i + 1);
                    const int cc = proc.ccFor (target);
                    auto r = Rectangle<int> (tbl.getX() + c * colW, tbl.getY() + 28 + i * 48, colW - 16, 40);
                    const bool now = proc.wizardStep() == c * 8 + i || learn == target;
                    Theme::panel (g, r, 10.0f, now ? Colour (0xff221a10) : Theme::hub);
                    if (now) { g.setColour (Theme::orange); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f); }
                    text (g, (c == 0 ? "Knob " : "Fader ") + String (i + 1), r.withTrimmedLeft (14).withWidth (80), Theme::sans (13.5f), Theme::text);
                    text (g, c == 0 ? proc.macroName (i) : String (KeyLab::faderName (i)), r.withTrimmedLeft (100).withWidth (180), Theme::sans (13.0f), Theme::dim);
                    text (g, cc >= 0 ? "CC " + String (cc) : "not learned", r.withTrimmedRight (14), Theme::mono (12.0f), cc >= 0 ? Theme::soft : Theme::mute, Justification::centredRight);
                }
            }
            card (g, tempoRect, "TEMPO");
            auto t = tempoRect.reduced (22, 0).withTrimmedTop (50);
            text (g, "Time signature", { t.getX(), t.getY() + 138, 130, 38 }, Theme::sans (14.0f), Theme::soft);
            text (g, String (proc.bpmShown.load(), 1) + " BPM now", { t.getX() + 260, t.getY() + 58, 200, 40 }, Theme::mono (12.0f), Theme::dim);
            card (g, infoRect, "HOW TO SET UP THE KEYLAB");
            const char* lines[] = {
                "1. KeyLab in User mode, MIDI track in Sonar with Input = KeyLab, Output = Glizzarine.",
                "2. Click Learn KeyLab, then turn knob 1 to 8 and move fader 1 to 8, one after the other.",
                "3. Knobs now control E1-E8 of each sound, faders the levels, attack, release, delay, reverb, volume.",
                "4. A control only takes over once it passes the current value, so nothing jumps.",
                "5. Right-click any control to give it its own knob. Learned once, works in every project." };
            for (int i = 0; i < 5; ++i)
                g.setFont (Theme::sans (12.5f)), g.setColour (Theme::soft),
                g.drawFittedText (lines[i], { infoRect.getX() + 22, infoRect.getY() + 46 + i * 38, infoRect.getWidth() - 44, 36 }, Justification::topLeft, 2, 1.0f);
            text (g, String ("Glizzarine ") + GLZ_VERSION + String::fromUTF8 (" · ") + String (__DATE__), { infoRect.getX() + 240, infoRect.getBottom() - 60, infoRect.getWidth() - 262, 38 },
                  Theme::mono (11.0f), Theme::mute, Justification::centredRight);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            absBtn.setToggleState (! proc.encodersRelative(), dontSendNotification);
            relBtn.setToggleState (proc.encodersRelative(), dontSendNotification);
            learnBtn.setButtonText (proc.wizardStep() >= 0 ? "Stop" : "Learn KeyLab");
            if (++frame % 6 == 0) repaint();
        }
    private:
        TextButton learnBtn, clearBtn, absBtn, relBtn, tapBtn, folderBtn;
        Segments* clock; NumberBox* bpm; Choice* ts;
        Rectangle<int> keyRect, tempoRect, infoRect;
        int frame = 0;
    };
}

//==============================================================================
// Everything is laid out at 1280 x 760 inside this component, which the editor scales to the window size
class GlizzarineEditor::Content : public Component
{
public:
    Content (GlizzarineProcessor& p, GlizzarineEditor& e) : proc (p), owner (e), browser (p)
    {
        pages.add (new PlayPage (p)); pages.add (new OscPage (p)); pages.add (new FilterPage (p));
        pages.add (new ModPage (p)); pages.add (new ArpPage (p)); pages.add (new EffectsPage (p)); pages.add (new SetupPage (p));
        for (auto* pg : pages) addChildComponent (pg);

        auto setup = [this] (TextButton& b, const String& t, const String& tipText)
        { b.setButtonText (t); b.setTooltip (tipText); addAndMakeVisible (b); };
        setup (prevBtn, "<", "Previous preset"); setup (nextBtn, ">", "Next preset");
        setup (saveBtn, "SAVE", "Save the current sound as your own preset");
        setup (undoBtn, "Undo", "Undo the last change"); setup (redoBtn, "Redo", "Redo");
        setup (aBtn, "A", "Sound A (compare two versions)"); setup (bBtn, "B", "Sound B: switch to compare. Your changes stay in each slot.");
        saveBtn.setComponentID ("save");
        prevBtn.onClick = [this] { proc.stepPreset (-1); };
        nextBtn.onClick = [this] { proc.stepPreset (1); };
        undoBtn.onClick = [this] { proc.undoManager.undo(); };
        redoBtn.onClick = [this] { proc.undoManager.redo(); };
        aBtn.onClick = [this] { proc.switchAB (0); };
        bBtn.onClick = [this] { proc.switchAB (1); };
        saveBtn.onClick = [this] { owner.savePreset(); };

        browser.onClose = [this] { browser.setVisible (false); };
        addChildComponent (browser);
        addChildComponent (dialog);
        setSize (baseW, baseH);
        showTab (0);
    }

    void showTab (int i)
    {
        current = jlimit (0, pages.size() - 1, i);
        for (int k = 0; k < pages.size(); ++k) pages[k]->setVisible (k == current);
        repaint (0, 64, getWidth(), 50);
    }

    void resized() override
    {
        for (auto* pg : pages) pg->setBounds (getLocalBounds());
        browser.setBounds (getLocalBounds());
        dialog.setBounds (getLocalBounds());
        auto h = Rectangle<int> (0, 0, getWidth(), 64).reduced (30, 12);
        nextBtn.setBounds (h.removeFromRight (40)); h.removeFromRight (6);
        presetRect = h.removeFromRight (250); h.removeFromRight (6);
        prevBtn.setBounds (h.removeFromRight (40)); h.removeFromRight (14);
        saveBtn.setBounds (h.removeFromRight (64)); h.removeFromRight (14);
        bBtn.setBounds (h.removeFromRight (36)); h.removeFromRight (4);
        aBtn.setBounds (h.removeFromRight (36)); h.removeFromRight (14);
        redoBtn.setBounds (h.removeFromRight (56)); h.removeFromRight (4);
        undoBtn.setBounds (h.removeFromRight (56));
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg);
        text (g, "GLIZZARINE", { 30, 18, 160, 28 }, Theme::sans (22.0f, true), Theme::accent);
        text (g, String (GLZ_VERSION) + "  " + String (__DATE__), { 172, 20, 130, 24 }, Theme::mono (11.0f), Theme::dim);
        if (proc.heals.load() > 0)
            text (g, "repaired " + String (proc.heals.load()) + "x", { 300, 20, 120, 24 }, Theme::sans (11.0f), Theme::orange);
        // preset name (click = browser)
        Theme::panel (g, presetRect, 10.0f, Theme::card2);
        g.setColour (Theme::edge); g.drawRoundedRectangle (presetRect.toFloat().reduced (0.5f), 10.0f, 1.0f);
        Theme::label (g, proc.getPresetCategory(), presetRect.withHeight (16).translated (0, 3), Theme::dim, Justification::centred);
        text (g, proc.getPresetName(), presetRect.withTrimmedTop (16).withTrimmedBottom (2), Theme::sans (15.0f), Theme::text, Justification::centred);
        // tabs
        int x = 30;
        for (int i = 0; i < owner.tabNames.size(); ++i)
        {
            auto f = Theme::sans (13.0f); f.setExtraKerningFactor (0.1f);
            const int w = (int) GlyphArrangement::getStringWidth (f, owner.tabNames[i]) + 4;
            g.setFont (f); g.setColour (i == current ? Theme::text : Theme::dim);
            g.drawText (owner.tabNames[i], x, 70, w, 36, Justification::centred);
            if (i == current) { g.setColour (Theme::accent); g.fillRect (x, 104, w, 2); }
            x += w + 28;
        }
        g.setColour (Theme::line); g.fillRect (0, 106, getWidth(), 1);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (presetRect.contains (e.getPosition())) { browser.open(); return; }
        if (e.y < 66 || e.y > 108) return;
        int x = 30;
        for (int i = 0; i < owner.tabNames.size(); ++i)
        {
            auto f = Theme::sans (13.0f); f.setExtraKerningFactor (0.1f);
            const int w = (int) GlyphArrangement::getStringWidth (f, owner.tabNames[i]) + 4;
            if (e.x >= x - 14 && e.x < x + w + 14) { showTab (i); return; }
            x += w + 28;
        }
    }

    void tick (float dt)
    {
        pages[current]->tickPage (dt);
        undoBtn.setEnabled (proc.undoManager.canUndo());
        redoBtn.setEnabled (proc.undoManager.canRedo());
        aBtn.setToggleState (proc.getABSlot() == 0, dontSendNotification);
        bBtn.setToggleState (proc.getABSlot() == 1, dontSendNotification);
        if (browser.isVisible()) browser.repaint();
    }

    void presetChanged()
    {
        for (auto* pg : pages) pg->presetChanged();
        repaint();
    }

    GlizzarineProcessor& proc;
    GlizzarineEditor& owner;
    OwnedArray<Page> pages;
    int current = 0;
    TextButton prevBtn, nextBtn, saveBtn, undoBtn, redoBtn, aBtn, bBtn;
    Rectangle<int> presetRect;
    PresetBrowser browser;
    PresetDialog dialog;
};

//==============================================================================
GlizzarineEditor::GlizzarineEditor (GlizzarineProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p, *this);
    addAndMakeVisible (*content);
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    lastTick = Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

GlizzarineEditor::~GlizzarineEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlizzarineEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void GlizzarineEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content->setTransform (AffineTransform::scale (s));
    content->setBounds (0, 0, baseW, baseH);
}

void GlizzarineEditor::showTab (int i) { content->showTab (i); }
void GlizzarineEditor::openBrowser() { content->browser.open(); }

void GlizzarineEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    const int pv = proc.presetVersion.load();
    if (pv != lastPresetVersion) { lastPresetVersion = pv; content->presetChanged(); }
    content->tick (dt);
}

void GlizzarineEditor::savePreset()
{
    auto& dialog = content->dialog;
    const auto suggestion = proc.getCurrentUser() >= 0 ? proc.getPresetName() : "My " + proc.getPresetName();
    dialog.askName (suggestion, [this] (const String& name)
    {
        auto doSave = [this, name]
        {
            if (! proc.saveUser (name))
                content->dialog.askConfirm ("SAVE PRESET", "Could not write the file to Documents / Glizzarine Presets.", "OK", nullptr);
        };
        const auto f = proc.userPresetFolder().getChildFile (File::createLegalFileName (name) + ".glzpreset");
        if (f.existsAsFile())
            content->dialog.askConfirm ("OVERWRITE?", "A preset called \"" + name + "\" already exists. Replace it with the current sound?", "Overwrite", doSave);
        else doSave();
    });
}
