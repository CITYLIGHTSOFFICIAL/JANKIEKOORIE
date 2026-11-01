#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float voices, detune, timing, vibrato, width, bright, octDown, octUp, reverb, mix;
    };

    // voices, detune(ct), timing(ms), vibrato%, width%, bright%, oct-1%, oct+1%, reverb%, mix%
    const Preset presets[] =
    {
        { "Klein ensemble",  4,  9, 14, 25, 60, 55,  0,  0, 20, 60 },
        { "Koor",            8, 15, 22, 30, 80, 50,  0,  0, 30, 70 },
        { "Groot koor",     16, 22, 35, 35, 100, 45, 25,  0, 40, 80 },
        { "Gospel",         10, 12, 18, 45, 85, 65,  0, 15, 25, 70 },
        { "Engelen",        12, 18, 30, 30, 100, 75,  0, 55, 55, 75 },
        { "Monniken",        8, 10, 25, 15, 70, 30, 60,  0, 45, 75 },
        { "Muur van stemmen",16, 35, 55, 40, 100, 50, 40, 35, 50, 90 },
    };
    constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
}

KoorProcessor::KoorProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KOOR", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout KoorProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };

    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "voices", 1 }, "Stemmen", 1, 16, 8));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 }, "Ontstemming",
                    NormalisableRange<float> (0.f, 50.f, 0.1f), 15.f,
                    AudioParameterFloatAttributes().withLabel ("ct")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "timing", 1 }, "Timing",
                    NormalisableRange<float> (0.f, 60.f, 0.1f), 22.f,
                    AudioParameterFloatAttributes().withLabel ("ms")));

    auto percent = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                        NormalisableRange<float> (0.f, 100.f, 0.1f), def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    };

    percent ("vibrato", "Vibrato",     30.f);
    percent ("width",   "Breedte",     80.f);
    percent ("bright",  "Helderheid",  50.f);
    percent ("octdown", "Octaaf -1",    0.f);
    percent ("octup",   "Octaaf +1",    0.f);
    percent ("reverb",  "Galm",        30.f);
    percent ("mix",     "Mix",         70.f);

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Uitvoer",
                    NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f,
                    AudioParameterFloatAttributes().withLabel ("dB")));

    return { p.begin(), p.end() };
}

bool KoorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void KoorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();

    wetBuffer.setSize (2, samplesPerBlock, false, true, true);
    reverbBuffer.setSize (2, samplesPerBlock, false, true, true);
    monoIn.assign ((size_t) samplesPerBlock, 0.f);

    mixSmooth.reset (sampleRate, 0.05);
    outSmooth.reset (sampleRate, 0.05);
    reverbSmooth.reset (sampleRate, 0.05);
    mixSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load() * 0.01f);
    outSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    reverbSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue ("reverb")->load() * 0.01f);
}

void KoorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    if ((int) monoIn.size() < numSamples)            // host gave a bigger block than promised
    {
        monoIn.resize ((size_t) numSamples);
        wetBuffer.setSize (2, numSamples, false, true, true);
        reverbBuffer.setSize (2, numSamples, false, true, true);
    }

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    koor::ChoirParams cp;
    cp.voices      = (int) get ("voices");
    cp.detuneCents = get ("detune");
    cp.timingMs    = get ("timing");
    cp.vibrato     = get ("vibrato") * 0.01f;
    cp.width       = get ("width")   * 0.01f;
    cp.brightness  = get ("bright")  * 0.01f;
    cp.octDown     = get ("octdown") * 0.01f;
    cp.octUp       = get ("octup")   * 0.01f;
    engine.setParams (cp);

    mixSmooth.setTargetValue (get ("mix") * 0.01f);
    outSmooth.setTargetValue (juce::Decibels::decibelsToGain (get ("output")));
    const float rv = get ("reverb") * 0.01f;
    reverbSmooth.setTargetValue (rv);

    // mono sum of the input
    {
        auto* m = monoIn.data();
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), numSamples);
        if (numIn > 1)
        {
            juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), numSamples);
            juce::FloatVectorOperations::multiply (m, 0.5f, numSamples);
        }
    }

    // if the input is mono, make the dry signal stereo
    if (numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    auto* wl = wetBuffer.getWritePointer (0);
    auto* wr = wetBuffer.getWritePointer (1);
    engine.process (monoIn.data(), wl, wr, numSamples);

    // hall on the choir
    juce::Reverb::Parameters rp;
    rp.roomSize   = 0.55f + 0.35f * rv;
    rp.damping    = 0.45f;
    rp.width      = 1.0f;
    rp.wetLevel   = 1.0f;
    rp.dryLevel   = 0.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    {
        // reverb runs on a copy of the choir, then gets blended back in
        auto& scratch = reverbBuffer;
        scratch.copyFrom (0, 0, wetBuffer, 0, 0, numSamples);
        scratch.copyFrom (1, 0, wetBuffer, 1, 0, numSamples);
        reverb.processStereo (scratch.getWritePointer (0), scratch.getWritePointer (1), numSamples);

        auto* sl = scratch.getReadPointer (0);
        auto* sr = scratch.getReadPointer (1);
        auto* dl = buffer.getWritePointer (0);
        auto* dr = buffer.getWritePointer (1);

        float peak = 0.f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float r   = reverbSmooth.getNextValue();
            const float mix = mixSmooth.getNextValue();
            const float g   = outSmooth.getNextValue();

            const float choirL = wl[i] * (1.f - 0.5f * r) + sl[i] * r * 0.6f;
            const float choirR = wr[i] * (1.f - 0.5f * r) + sr[i] * r * 0.6f;

            // equal-ish power blend: lead stays present until high mix values
            const float dryG = std::cos (mix * juce::MathConstants<float>::halfPi);
            const float wetG = std::sin (mix * juce::MathConstants<float>::halfPi);

            float outL = (dl[i] * dryG + choirL * wetG) * g;
            float outR = (dr[i] * dryG + choirR * wetG) * g;

            // gentle safety saturation above 0 dBFS
            auto soft = [] (float x) { return std::abs (x) < 0.9f ? x : std::tanh (x); };
            dl[i] = soft (outL);
            dr[i] = soft (outR);

            peak = std::max (peak, std::abs (choirL * wetG));
        }
        meterLevel.store (std::max (peak, meterLevel.load() * 0.92f));
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
int KoorProcessor::getNumPrograms() { return numPresets; }

const juce::String KoorProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void KoorProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets)) return;
    currentProgram = index;
    const auto& pr = presets[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set ("voices",  pr.voices);
    set ("detune",  pr.detune);
    set ("timing",  pr.timing);
    set ("vibrato", pr.vibrato);
    set ("width",   pr.width);
    set ("bright",  pr.bright);
    set ("octdown", pr.octDown);
    set ("octup",   pr.octUp);
    set ("reverb",  pr.reverb);
    set ("mix",     pr.mix);
}

void KoorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KoorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* KoorProcessor::createEditor()
{
    return new KoorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KoorProcessor();
}
