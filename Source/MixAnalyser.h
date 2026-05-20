#pragma once
#include <JuceHeader.h>
#include "SpectrumAnalyser.h"
#include "SharedAnalyserState.h"

class VisualEQProcessor;

struct MixAdvice
{
    enum Severity { Good, Info, Warning, Critical };
    Severity     severity = Info;
    juce::String text;
    float        freqLo   = -1.0f;
    float        freqHi   = -1.0f;
    int          track    = -1;   // 1-based, -1 = all tracks
};

class MixAnalyser
{
public:
    static std::vector<MixAdvice> analyse (
        const std::array<VisualEQProcessor*, SharedAnalyserState::kMaxTracks>& procs);

private:
    // Average dB across a frequency range
    static float avgDb (const SpectrumAnalyser& sa, float loHz, float hiHz);

    // Frequency of the loudest bin in range
    static float peakFreq (const SpectrumAnalyser& sa, float loHz, float hiHz);

    // Frequency where two spectra are closest in level (highest competition)
    static float clashFreq (const SpectrumAnalyser& a, const SpectrumAnalyser& b,
                             float loHz, float hiHz);

    static juce::String t (int slot) { return "Track " + juce::String(slot + 1); }
    static juce::String hz (float f)
    {
        if (f >= 1000.0f) return juce::String(f / 1000.0f, 1) + " kHz";
        return juce::String((int)f) + " Hz";
    }
};
