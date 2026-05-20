#include "PluginEditor.h"
#include "SharedAnalyserState.h"

VisualEQEditor::VisualEQEditor (VisualEQProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), eq_(p)
{
    addAndMakeVisible(eq_);
    setSize(820, 420);
    setResizable(true, true);
    setResizeLimits(600, 300, 1600, 800);
    SharedAnalyserState::getInstance()->registerEditor(p.getSlotIndex(), this);
}

VisualEQEditor::~VisualEQEditor()
{
    SharedAnalyserState::getInstance()->unregisterEditor(proc_.getSlotIndex());
}

void VisualEQEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff070b10));
}

void VisualEQEditor::resized()
{
    eq_.setBounds(getLocalBounds());
}
