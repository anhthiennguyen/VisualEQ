#pragma once
#include <JuceHeader.h>
#include "SpectrumAnalyser.h"

static constexpr int kNumBands = 5;

enum class BandType { LowShelf, Peak, HighShelf };
static constexpr BandType kBandTypes[kNumBands] = {
    BandType::LowShelf, BandType::Peak, BandType::Peak, BandType::Peak, BandType::HighShelf
};
static constexpr float kDefaultFreqs[kNumBands] = { 80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f };
static constexpr float kDefaultQs[kNumBands]    = { 0.7f,  1.0f,   1.0f,    1.0f,    0.7f };

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
    SpectrumAnalyser& getSpectrumAnalyser()            { return spectrumAnalyser_; }
    double getPluginSampleRate() const                 { return sampleRate_; }
    int    getSlotIndex() const                        { return slotIndex_; }

private:
    double sampleRate_ = 44100.0;

    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

    using Filter      = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
    std::array<Filter, kNumBands> filtersL_, filtersR_;

    void updateFilters();

    SpectrumAnalyser spectrumAnalyser_;
    int slotIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualEQProcessor)
};
