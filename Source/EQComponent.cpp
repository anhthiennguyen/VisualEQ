#include "EQComponent.h"
#include "PluginProcessor.h"

static const juce::Colour kBandColours[kNumBands] = {
    juce::Colour(0xff4CAF50),
    juce::Colour(0xff2196F3),
    juce::Colour(0xffFF9800),
    juce::Colour(0xff9C27B0),
    juce::Colour(0xffF44336),
};

EQComponent::EQComponent (VisualEQProcessor& proc) : proc_(proc)
{
    startTimerHz(30);
}

EQComponent::~EQComponent() { stopTimer(); }

void EQComponent::timerCallback()
{
    bool updated = false;
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    for (auto* p : procs)
        if (p && p->getSpectrumAnalyser().pullSpectrum())
            updated = true;
    if (updated || draggedBand_ >= 0)
        repaint();
}

//==============================================================================
float EQComponent::freqToX (float freq, float w) const
{
    return w * std::log10(freq / kMinFreq) / std::log10(kMaxFreq / kMinFreq);
}

float EQComponent::xToFreq (float x, float w) const
{
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, x / w);
}

float EQComponent::specDbToY (float db, float h) const
{
    float norm = (db - kSpecMinDb) / (kSpecMaxDb - kSpecMinDb);
    return h * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
}

float EQComponent::eqDbToY (float db, float h) const
{
    float norm = (db + kEqRange) / (2.0f * kEqRange);
    return h * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
}

float EQComponent::yToEqDb (float y, float h) const
{
    return -kEqRange + (1.0f - y / h) * (2.0f * kEqRange);
}

float EQComponent::getDbAtFreq (const SpectrumAnalyser& sa, float freq)
{
    double sr = sa.getSampleRate();
    float  binWidth = (float)sr / (float)SpectrumAnalyser::fftSize;
    float  idx = freq / binWidth;
    int    lo  = juce::jlimit(0, SpectrumAnalyser::numBins - 1, (int)idx);
    int    hi  = juce::jlimit(0, SpectrumAnalyser::numBins - 1, lo + 1);
    float  t   = idx - (float)lo;
    const auto& sp = sa.getSpectrum();
    return sp[lo] * (1.0f - t) + sp[hi] * t;
}

float EQComponent::computeMagnitudeAt (float freq) const
{
    double sr  = proc_.getPluginSampleRate();
    double mag = 1.0;
    auto& apvts = proc_.getAPVTS();

    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        bool  on   = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
        if (!on) continue;
        float f    = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float q    = *apvts.getRawParameterValue(p + "q");
        float gl   = juce::Decibels::decibelsToGain(gain);

        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<float>> c;
        if      (kBandTypes[i] == BandType::LowShelf)
            c = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, (double)f, (double)q, (double)gl);
        else if (kBandTypes[i] == BandType::HighShelf)
            c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, (double)f, (double)q, (double)gl);
        else
            c = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, (double)f, (double)q, (double)gl);

        mag *= c->getMagnitudeForFrequency((double)freq, sr);
    }

    return (float)juce::Decibels::gainToDecibels(mag);
}

//==============================================================================
void EQComponent::drawGrid (juce::Graphics& g, float w, float h) const
{
    static constexpr float       kFreqs[]  = { 50,100,200,500,1000,2000,5000,10000,20000 };
    static constexpr const char* kLabels[] = { "50","100","200","500","1k","2k","5k","10k","20k" };
    static constexpr int kN = 9;

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i < kN; ++i)
        g.drawLine(freqToX(kFreqs[i], w), 0.0f, freqToX(kFreqs[i], w), h, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (float db : { -24.0f, -12.0f, -6.0f, 6.0f, 12.0f, 24.0f })
        g.drawLine(0.0f, eqDbToY(db, h), w, eqDbToY(db, h), 0.5f);

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(0.0f, eqDbToY(0.0f, h), w, eqDbToY(0.0f, h), 1.0f);

    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    for (int i = 0; i < kN; ++i)
        g.drawText(kLabels[i], (int)freqToX(kFreqs[i], w) - 12, (int)h - 14, 24, 12,
                   juce::Justification::centred);

    g.setColour(juce::Colours::white.withAlpha(0.22f));
    for (float db : { 24.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f })
    {
        juce::String lbl = (db > 0 ? "+" : "") + juce::String((int)db);
        g.drawText(lbl, (int)w - 28, (int)eqDbToY(db, h) - 5, 26, 10,
                   juce::Justification::centredRight);
    }
}

//==============================================================================
// Heat map: sweeps orange → red where 2+ tracks have energy in the same band.
void EQComponent::drawMudOverlay (juce::Graphics& g, float w, float h) const
{
    auto procs = SharedAnalyserState::getInstance()->getProcessors();

    // Count active tracks
    int totalActive = 0;
    for (auto* p : procs) if (p) ++totalActive;
    if (totalActive < 2) return;

    constexpr float kThreshDb = -48.0f;
    constexpr int   kStep     = 2;

    for (int px = 0; px < (int)w; px += kStep)
    {
        float freq = xToFreq((float)px, w);

        int   tracksWithEnergy = 0;
        for (auto* p : procs)
        {
            if (!p) continue;
            if (getDbAtFreq(p->getSpectrumAnalyser(), freq) > kThreshDb)
                ++tracksWithEnergy;
        }

        if (tracksWithEnergy < 2) continue;

        // muddiness: 0 = 2 tracks just touching, 1 = 5+ tracks piling up
        float mud = juce::jlimit(0.0f, 1.0f, (float)(tracksWithEnergy - 1) / 4.0f);

        // Hue: 0.10 (amber) → 0.0 (red) as muddiness rises
        float hue   = 0.10f * (1.0f - mud);
        float alpha = 0.12f + mud * 0.35f;

        g.setColour(juce::Colour::fromHSV(hue, 0.90f, 0.95f, alpha));
        g.fillRect((float)px, 0.0f, (float)kStep, h);
    }
}

//==============================================================================
void EQComponent::drawAllSpectra (juce::Graphics& g, float w, float h) const
{
    int ownSlot = proc_.getSlotIndex();
    auto procs  = SharedAnalyserState::getInstance()->getProcessors();

    for (int slot = 0; slot < SharedAnalyserState::kMaxTracks; ++slot)
    {
        auto* p = procs[slot];
        if (!p) continue;

        bool isOwn = (slot == ownSlot);
        auto& sa   = p->getSpectrumAnalyser();
        double sr  = sa.getSampleRate();
        juce::Colour col = SharedAnalyserState::trackColour(slot);

        juce::Path fill, stroke;
        bool started = false;

        for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
        {
            float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
            if (freq < kMinFreq || freq > kMaxFreq) continue;
            float x = freqToX(freq, w);
            float y = juce::jlimit(0.0f, h, specDbToY(sa.getSpectrum()[i], h));
            if (!started) { fill.startNewSubPath(x, y); stroke.startNewSubPath(x, y); started = true; }
            else          { fill.lineTo(x, y);          stroke.lineTo(x, y); }
        }
        if (!started) continue;

        fill.lineTo(freqToX(kMaxFreq, w), h);
        fill.lineTo(freqToX(kMinFreq, w), h);
        fill.closeSubPath();

        float fillAlpha   = isOwn ? 0.20f : 0.08f;
        float strokeAlpha = isOwn ? 0.75f : 0.40f;

        g.setColour(col.withAlpha(fillAlpha));
        g.fillPath(fill);
        g.setColour(col.withAlpha(strokeAlpha));
        g.strokePath(stroke, juce::PathStrokeType(isOwn ? 1.4f : 0.9f));

        // Track label near the peak of the spectrum
        if (!isOwn)
        {
            // Find loudest bin for label placement
            float peakX = w * 0.5f, peakDb = -80.0f;
            for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
            {
                float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
                if (freq < kMinFreq || freq > kMaxFreq) continue;
                float db = sa.getSpectrum()[i];
                if (db > peakDb) { peakDb = db; peakX = freqToX(freq, w); }
            }
            if (peakDb > -60.0f)
            {
                float peakY = specDbToY(peakDb, h) - 12.0f;
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f).withStyle("Bold")));
                g.setColour(col.withAlpha(0.65f));
                g.drawText("T" + juce::String(slot + 1),
                           (int)peakX - 14, (int)peakY, 28, 10,
                           juce::Justification::centred);
            }
        }
    }
}

//==============================================================================
void EQComponent::drawEQCurve (juce::Graphics& g, float w, float h) const
{
    constexpr int kSteps = 600;
    juce::Path fill, stroke;
    bool started = false;
    float y0 = eqDbToY(0.0f, h);

    for (int i = 0; i <= kSteps; ++i)
    {
        float t    = (float)i / kSteps;
        float freq = kMinFreq * std::pow(kMaxFreq / kMinFreq, t);
        float db   = computeMagnitudeAt(freq);
        float x    = freqToX(freq, w);
        float y    = juce::jlimit(0.0f, h, eqDbToY(db, h));
        if (!started) { fill.startNewSubPath(x, y); stroke.startNewSubPath(x, y); started = true; }
        else          { fill.lineTo(x, y);          stroke.lineTo(x, y); }
    }

    fill.lineTo(freqToX(kMaxFreq, w), y0);
    fill.lineTo(freqToX(kMinFreq, w), y0);
    fill.closeSubPath();

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillPath(fill);
    g.setColour(juce::Colours::white.withAlpha(0.90f));
    g.strokePath(stroke, juce::PathStrokeType(1.8f));
}

//==============================================================================
void EQComponent::drawNodes (juce::Graphics& g, float w, float h) const
{
    auto& apvts = proc_.getAPVTS();
    float y0    = eqDbToY(0.0f, h);

    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        bool  on   = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float nx   = freqToX(freq, w);
        float ny   = eqDbToY(gain, h);
        juce::Colour col = kBandColours[i];

        g.setColour(col.withAlpha(on ? 0.22f : 0.07f));
        g.drawLine(nx, y0, nx, ny, 1.0f);

        float r = (i == hoveredBand_ || i == draggedBand_) ? 7.5f : 6.0f;
        g.setColour(col.withAlpha(on ? (i == draggedBand_ ? 1.0f : 0.82f) : 0.30f));
        g.fillEllipse(nx - r, ny - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(on ? 0.65f : 0.18f));
        g.drawEllipse(nx - r, ny - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setFont(juce::Font(juce::FontOptions().withHeight(8.0f).withStyle("Bold")));
        g.setColour(juce::Colours::white.withAlpha(on ? 0.90f : 0.35f));
        g.drawText(juce::String(i + 1), (int)(nx - r), (int)(ny - r),
                   (int)(r * 2), (int)(r * 2), juce::Justification::centred);
    }
}

//==============================================================================
int EQComponent::bandAtPoint (juce::Point<float> pt, float w, float h) const
{
    auto& apvts = proc_.getAPVTS();
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float d = std::hypot(pt.x - freqToX(freq, w), pt.y - eqDbToY(gain, h));
        if (d < 12.0f) return i;
    }
    return -1;
}

void EQComponent::mouseDown (const juce::MouseEvent& e)
{
    draggedBand_ = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    repaint();
}

void EQComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedBand_ < 0) return;
    float w = (float)getWidth(), h = (float)(getHeight() - 20);
    juce::String p = "band" + juce::String(draggedBand_) + "_";
    float newFreq = juce::jlimit(kMinFreq, kMaxFreq, xToFreq(juce::jlimit(0.0f, w, e.position.x), w));
    float newGain = juce::jlimit(-kEqRange, kEqRange, yToEqDb(juce::jlimit(0.0f, h, e.position.y), h));
    if (auto* fp = proc_.getAPVTS().getParameter(p + "freq"))
        fp->setValueNotifyingHost(fp->convertTo0to1(newFreq));
    if (auto* gp = proc_.getAPVTS().getParameter(p + "gain"))
        gp->setValueNotifyingHost(gp->convertTo0to1(newGain));
    repaint();
}

void EQComponent::mouseUp (const juce::MouseEvent&)    { draggedBand_ = -1; repaint(); }

void EQComponent::mouseMove (const juce::MouseEvent& e)
{
    int band = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    if (band != hoveredBand_) { hoveredBand_ = band; repaint(); }
    setMouseCursor(band >= 0 ? juce::MouseCursor::DraggingHandCursor
                             : juce::MouseCursor::CrosshairCursor);
}

void EQComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    if (band < 0) return;
    juce::String p = "band" + juce::String(band) + "_";
    auto* qp = proc_.getAPVTS().getParameter(p + "q");
    if (!qp) return;
    float q    = *proc_.getAPVTS().getRawParameterValue(p + "q");
    float newQ = juce::jlimit(0.1f, 10.0f, q * (1.0f + wheel.deltaY * 0.15f));
    qp->setValueNotifyingHost(qp->convertTo0to1(newQ));
    repaint();
}

//==============================================================================
void EQComponent::paint (juce::Graphics& g)
{
    float w = (float)getWidth();
    float h = (float)(getHeight() - 20);

    g.fillAll(juce::Colour(0xff070b10));

    drawGrid       (g, w, h);
    drawMudOverlay (g, w, h);
    drawAllSpectra (g, w, h);
    drawEQCurve    (g, w, h);
    drawNodes      (g, w, h);

    // Status bar
    g.setColour(juce::Colours::black.withAlpha(0.70f));
    g.fillRect(0.0f, h, w, 20.0f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.40f));

    auto& apvts = proc_.getAPVTS();
    int   show  = draggedBand_ >= 0 ? draggedBand_ : hoveredBand_;
    juce::String txt;

    if (show >= 0)
    {
        juce::String p = "band" + juce::String(show) + "_";
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float q    = *apvts.getRawParameterValue(p + "q");
        static const char* kTypes[] = { "Low Shelf","Peak","Peak","Peak","High Shelf" };
        txt = "Band " + juce::String(show + 1) + " [" + kTypes[show] + "]"
            + "   Freq: " + juce::String((int)freq) + " Hz"
            + "   Gain: " + (gain >= 0 ? "+" : "") + juce::String(gain, 1) + " dB"
            + "   Q: " + juce::String(q, 2)
            + "   (scroll to adjust Q)";
    }
    else
    {
        // Count active tracks for status
        int n = 0;
        for (auto* p2 : SharedAnalyserState::getInstance()->getProcessors())
            if (p2) ++n;
        txt = "VisualEQ  |  Track " + juce::String(proc_.getSlotIndex() + 1)
            + " of " + juce::String(n)
            + "   |  drag nodes: freq + gain   |   scroll: Q"
            + (n >= 2 ? "   |  orange/red = frequency clash" : "");
    }
    g.drawText(txt, 6, (int)h + 1, (int)w - 12, 18, juce::Justification::centredLeft);
}

void EQComponent::resized() {}
