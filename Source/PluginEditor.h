#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQComponent.h"

class VisualEQEditor : public juce::AudioProcessorEditor
{
public:
    explicit VisualEQEditor (VisualEQProcessor&);
    ~VisualEQEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    VisualEQProcessor& proc_;
    EQComponent        eq_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualEQEditor)
};
