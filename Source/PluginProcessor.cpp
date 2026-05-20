#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SharedAnalyserState.h"

juce::AudioProcessorValueTreeState::ParameterLayout VisualEQProcessor::createParams()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray typeChoices { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "freq", 1),
            "Band " + juce::String(i + 1) + " Freq",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f),
            kEQDefaultFreqs[i]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "gain", 1),
            "Band " + juce::String(i + 1) + " Gain",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
            0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "q", 1),
            "Band " + juce::String(i + 1) + " Q",
            juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f),
            kEQDefaultQs[i]));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "enabled", 1),
            "Band " + juce::String(i + 1) + " On",
            false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(p + "type", 1),
            "Band " + juce::String(i + 1) + " Type",
            typeChoices,
            0));
    }
    return layout;
}

VisualEQProcessor::VisualEQProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "VisualEQ", createParams())
{
    slotIndex_ = SharedAnalyserState::getInstance()->registerProcessor(this);
}

VisualEQProcessor::~VisualEQProcessor()
{
    if (auto* s = SharedAnalyserState::getInstance())
        s->unregisterProcessor(slotIndex_);
}

void VisualEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 1;
    for (auto& f : filtersL_) { f.prepare(spec); f.reset(); }
    for (auto& f : filtersR_) { f.prepare(spec); f.reset(); }
    eqAnalyser_.prepare(sampleRate);
    hintsAnalyser_.prepare(sampleRate);
    updateEQFilters();
}

void VisualEQProcessor::releaseResources() {}

void VisualEQProcessor::updateEQFilters()
{
    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        float freq = *apvts_.getRawParameterValue(p + "freq");
        float gain = *apvts_.getRawParameterValue(p + "gain");
        float q    = *apvts_.getRawParameterValue(p + "q");
        bool  on   = *apvts_.getRawParameterValue(p + "enabled") > 0.5f;

        int typeIdx = juce::jlimit(0, 6, (int)*apvts_.getRawParameterValue(p + "type"));
        auto bandType = (BandType)typeIdx;

        juce::ReferenceCountedObjectPtr<FilterCoefs> c;
        if (!on)
        {
            c = FilterCoefs::makePeakFilter(sampleRate_, (double)freq, (double)q, 1.0);
        }
        else
        {
            double gl = juce::Decibels::decibelsToGain(gain);
            switch (bandType)
            {
                case BandType::LowShelf:  c = FilterCoefs::makeLowShelf   (sampleRate_, (double)freq, (double)q, gl); break;
                case BandType::HighShelf: c = FilterCoefs::makeHighShelf  (sampleRate_, (double)freq, (double)q, gl); break;
                case BandType::LowCut:    c = FilterCoefs::makeHighPass   (sampleRate_, (double)freq, (double)q); break;
                case BandType::HighCut:   c = FilterCoefs::makeLowPass    (sampleRate_, (double)freq, (double)q); break;
                case BandType::Notch:     c = FilterCoefs::makeNotch      (sampleRate_, (double)freq, (double)q); break;
                case BandType::BandPass:  c = FilterCoefs::makeBandPass   (sampleRate_, (double)freq, (double)q); break;
                default:                  c = FilterCoefs::makePeakFilter (sampleRate_, (double)freq, (double)q, gl); break;
            }
        }

        *filtersL_[i].coefficients = *c;
        *filtersR_[i].coefficients = *c;
    }
}

void VisualEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateEQFilters();

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    int    n = buffer.getNumSamples();

    for (int i = 0; i < kNumEQBands; ++i)
        for (int s = 0; s < n; ++s)
        {
            L[s] = filtersL_[i].processSample(L[s]);
            if (R) R[s] = filtersR_[i].processSample(R[s]);
        }

    eqAnalyser_.pushSamples(L, R ? R : L, n);
    hintsAnalyser_.pushSamples(L, R ? R : L, n);
}

void VisualEQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void VisualEQProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* VisualEQProcessor::createEditor()
{
    return new VisualEQEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VisualEQProcessor();
}
