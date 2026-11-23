// Renders every tab of the editor (and the preset browser / save dialog) to PNG files, with a chord playing.
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace juce;
static void shot (Component& c, const String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    File f (File::getCurrentWorkingDirectory().getChildFile (name + ".png"));
    f.deleteFile(); FileOutputStream os (f); PNGImageFormat png; png.writeImageToStream (img, os);
}
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    auto* p = new GlizzarineProcessor();
    const int preset = argc > 1 ? atoi (argv[1]) : 0;
    p->loadFactory (preset);
    p->prepareToPlay (48000, 512);
    AudioBuffer<float> b (2, 512);
    for (int blk = 0; blk < 160; ++blk)
    {
        MidiBuffer m;
        if (blk == 2) for (int n : { 50, 57, 60, 65 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
        p->processBlock (b, m);
    }
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    ed->setVisible (true);
    auto* ge = dynamic_cast<GlizzarineEditor*> (ed.get());
    const char* names[] = { "play", "osc", "filter", "mod", "arp", "effects", "setup" };
    for (int t = 0; t < 7; ++t)
    {
        ge->showTab (t);
        for (int k = 0; k < 20; ++k) { MidiBuffer m; p->processBlock (b, m); MessageManager::getInstance()->runDispatchLoopUntil (15); }
        if (t == 0) { p->setParam ("cutoff", p->valueOf ("cutoff") * 1.3f); MessageManager::getInstance()->runDispatchLoopUntil (250); }
        shot (*ed, String ("tab_") + names[t]);
    }
    ge->showTab (0);
    ge->openBrowser();
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    shot (*ed, "browser");
    ed = nullptr; delete p;
    return 0;
}
