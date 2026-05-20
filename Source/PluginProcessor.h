#pragma once
#include <JuceHeader.h>
#include "SpectrumAnalyser.h"

static constexpr int kNumEQBands = 7;

enum class BandType { Bell=0, LowShelf=1, HighShelf=2, LowCut=3, HighCut=4, Notch=5, BandPass=6 };
static constexpr float kEQDefaultFreqs[kNumEQBands] = { 80.0f, 200.0f, 500.0f, 1000.0f, 3000.0f, 8000.0f, 12000.0f };
static constexpr float kEQDefaultQs[kNumEQBands]    = { 0.7f,  1.0f,   1.0f,   1.0f,    1.0f,    1.0f,    0.7f };

class VisualEQProcessor : public juce::AudioProcessor
{
public:
    VisualEQProcessor();
    ~VisualEQProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override  { return "VisualEQ"; }
    bool acceptsMidi()  const override           { return false; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return "Default"; }
    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS()    { return apvts_; }
    SpectrumAnalyser& getEQAnalyser()                 { return eqAnalyser_; }
    SpectrumAnalyser& getHintsAnalyser()              { return hintsAnalyser_; }
    double getPluginSampleRate() const                { return sampleRate_; }
    int    getSlotIndex() const                       { return slotIndex_; }

private:
    double sampleRate_ = 44100.0;

    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

    using Filter      = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
    std::array<Filter, kNumEQBands> filtersL_, filtersR_;

    void updateEQFilters();

    SpectrumAnalyser eqAnalyser_;
    SpectrumAnalyser hintsAnalyser_ { 0.2f };
    int slotIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualEQProcessor)
};
