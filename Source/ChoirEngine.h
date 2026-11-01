// KOOR – ChoirEngine
// Turns one (mono) voice into a stereo choir.
//
// Each choir voice is a small pitch shifter (two crossfaded read heads on a
// delay line) with its own detune, timing offset, vibrato, slow pitch drift,
// tone colour and stereo position. Extra groups add an octave below and an
// octave above. No JUCE dependency, so it can be tested on its own.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace koor
{

struct ChoirParams
{
    int   voices      = 8;     // unison voices, 1..16
    float detuneCents = 15.f;  // max detune per voice
    float timingMs    = 20.f;  // max timing spread between voices
    float vibrato     = 0.3f;  // 0..1
    float width       = 0.8f;  // 0..1 stereo spread
    float brightness  = 0.5f;  // 0..1 tone of the voices
    float octDown     = 0.f;   // 0..1 level of the octave-below group
    float octUp       = 0.f;   // 0..1 level of the octave-above group
};

class ChoirEngine
{
public:
    static constexpr int kMaxUnison  = 16;
    static constexpr int kOctVoices  = 4;
    static constexpr int kMaxVoices  = kMaxUnison + 2 * kOctVoices;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;

        // 1 second of history is plenty (max timing 60 ms + window 60 ms).
        int size = 1;
        while (size < (int) (sr * 1.0f)) size <<= 1;
        buffer.assign ((size_t) size, 0.f);
        mask = size - 1;

        initVoices();
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.f);
        writePos = 0;
        decBuf.assign (kDetLen, 0.f);
        frame.assign (kDetLen, 0.f);
        cmnd.assign (kDetLen, 1.f);
        decLp = 0.f; decCount = decPos = detHop = voicedHold = 0;
        periodSamples = 0.f;
        for (auto& v : voices)
        {
            v.lp = 0.f;
            v.gain = v.targetGain;
            v.delay = v.targetDelay;
            v.window = v.nominalWindow;
        }
    }

    void setParams (const ChoirParams& p)
    {
        params = p;
        params.voices = std::clamp (params.voices, 1, kMaxUnison);

        const float unisonNorm = 1.f / std::sqrt ((float) params.voices);
        const float octNorm    = 1.f / std::sqrt ((float) kOctVoices);
        const float msToSamp   = sr / 1000.f;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (v.group == 0)
                v.targetGain = (i < params.voices) ? unisonNorm : 0.f;
            else if (v.group == 1)
                v.targetGain = params.octDown * octNorm * 0.9f;
            else
                v.targetGain = params.octUp * octNorm * 0.7f;

            // Timing: every voice sits a little behind the lead.
            v.targetDelay = 3.f + v.timingSeed * params.timingMs * msToSamp;

            // Stereo position (constant power).
            const float pan = std::clamp (v.panSeed * params.width, -1.f, 1.f);
            const float a = (pan + 1.f) * 0.25f * kPi;
            v.panL = std::cos (a);
            v.panR = std::sin (a);

            // Tone: each voice gets a slightly different low-pass.
            const float cutoff = 1500.f + params.brightness * 11000.f * (0.6f + 0.4f * v.toneSeed);
            v.lpCoef = 1.f - std::exp (-2.f * kPi * std::min (cutoff, sr * 0.45f) / sr);
        }
    }

    // in: mono input, outL/outR: stereo choir (wet only).
    void process (const float* in, float* outL, float* outR, int numSamples)
    {
        const float vibDepth   = params.vibrato * 22.f;          // cents
        const float driftDepth = 2.f + params.detuneCents * 0.35f;
        const float gainSmooth  = 1.f - std::exp (-1.f / (0.02f * sr));
        const float delaySmooth = 1.f - std::exp (-1.f / (0.25f * sr));
        const float winSmooth   = 1.f - std::exp (-1.f / (0.03f * sr));

        for (int n = 0; n < numSamples; ++n)
        {
            buffer[(size_t) writePos] = in[n];
            pushDetector (in[n]);

            float l = 0.f, r = 0.f;

            for (auto& v : voices)
            {
                v.gain += (v.targetGain - v.gain) * gainSmooth;
                if (v.gain < 1.0e-5f && v.targetGain == 0.f)
                {
                    // keep the shifter phase moving so it's ready when reactivated
                    v.phase = wrap (v.phase + 0.0001f);
                    continue;
                }

                v.delay  += (v.targetDelay - v.delay) * delaySmooth;
                v.window += (targetWindow (v.nominalWindow) - v.window) * winSmooth;

                // pitch modulation in cents
                v.lfoPhase   = wrap (v.lfoPhase + v.lfoRate / sr);
                v.driftPhase = wrap (v.driftPhase + v.driftRate / sr);

                const float cents = v.detuneSeed * params.detuneCents * (v.group == 0 ? 1.f : 0.5f)
                                  + vibDepth   * fastSin (v.lfoPhase)
                                  + driftDepth * fastSin (v.driftPhase);

                const float ratio = v.groupRatio * std::exp2 (cents / 1200.f);

                // two-head pitch shifter
                v.phase = wrap (v.phase + (1.f - ratio) / v.window);
                const float p2 = wrap (v.phase + 0.5f);

                const float s1 = read (v.delay + v.phase * v.window);
                const float s2 = read (v.delay + p2 * v.window);

                const float w = fastSin (v.phase * 0.5f); // sin(pi*phase)
                const float w1 = w * w;
                const float y = s1 * w1 + s2 * (1.f - w1);

                v.lp += v.lpCoef * (y - v.lp);

                const float out = v.lp * v.gain;
                l += out * v.panL;
                r += out * v.panR;
            }

            outL[n] = l;
            outR[n] = r;
            writePos = (writePos + 1) & mask;
        }
    }

private:
    static constexpr float kPi = 3.14159265358979f;

    struct Voice
    {
        int   group = 0;          // 0 unison, 1 octave down, 2 octave up
        float groupRatio = 1.f;
        float window = 1000.f;    // shifter window in samples (follows the voice's pitch)
        float nominalWindow = 1000.f;

        float detuneSeed = 0.f;   // -1..1
        float timingSeed = 0.f;   //  0..1
        float panSeed    = 0.f;   // -1..1
        float toneSeed   = 0.f;   //  0..1

        float phase = 0.f;
        float lfoPhase = 0.f, lfoRate = 5.f;
        float driftPhase = 0.f, driftRate = 0.2f;

        float gain = 0.f, targetGain = 0.f;
        float delay = 3.f, targetDelay = 3.f;
        float lp = 0.f, lpCoef = 1.f;
        float panL = 0.707f, panR = 0.707f;
    };

    struct Rng
    {
        uint32_t s;
        float next()    { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / (float) 0x1000000; }
        float bipolar() { return next() * 2.f - 1.f; }
    };

    void initVoices()
    {
        Rng rng { 0x4B4F4F52u }; // fixed seed -> the same choir every session

        voices.resize (kMaxVoices);
        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (i < kMaxUnison)                     { v.group = 0; v.groupRatio = 1.f;  v.nominalWindow = v.window = 0.030f * sr; }
            else if (i < kMaxUnison + kOctVoices)   { v.group = 1; v.groupRatio = 0.5f; v.nominalWindow = v.window = 0.060f * sr; }
            else                                    { v.group = 2; v.groupRatio = 2.f;  v.nominalWindow = v.window = 0.040f * sr; }

            // alternate sides so any voice count stays balanced
            const float side = (i % 2 == 0) ? -1.f : 1.f;
            v.panSeed = side * (0.25f + 0.75f * rng.next());
            if (i == 0) v.panSeed *= 0.3f;

            // spread detune evenly-ish, with some randomness
            v.detuneSeed = std::clamp (side * (0.2f + 0.8f * rng.next()), -1.f, 1.f);
            v.timingSeed = 0.15f + 0.85f * rng.next();
            v.toneSeed   = rng.next();

            v.phase      = rng.next();
            v.lfoPhase   = rng.next();
            v.lfoRate    = 4.6f + 1.6f * rng.next();
            v.driftPhase = rng.next();
            v.driftRate  = 0.08f + 0.3f * rng.next();
        }
    }

    //==========================================================================
    // Light-weight pitch detector (YIN on a 4x decimated signal).
    // Knowing the period of the voice lets every shifter use a window that is
    // a whole number of periods long, so its two read heads stay in phase.
    // That removes most of the "phasey/flangy" sound of simple shifters.
    void pushDetector (float x)
    {
        decLp += 0.35f * (x - decLp);
        if (++decCount < kDecim) return;
        decCount = 0;

        decBuf[(size_t) decPos] = decLp;
        decPos = (decPos + 1) % kDetLen;

        if (++detHop >= kDetHop)
        {
            detHop = 0;
            runDetector();
        }
    }

    void runDetector()
    {
        const float dsr = sr / (float) kDecim;
        const int minLag = std::max (2, (int) (dsr / 1000.f));
        const int maxLag = std::min (kDetLen / 2 - 1, (int) (dsr / 60.f));
        const int W = kDetLen - maxLag - 1;

        // unroll the ring buffer, oldest first
        for (int i = 0; i < kDetLen; ++i)
            frame[(size_t) i] = decBuf[(size_t) ((decPos + i) % kDetLen)];

        double energy = 0.0;
        for (int i = 0; i < W; ++i) energy += frame[(size_t) i] * frame[(size_t) i];
        if (energy / W < 1.0e-6) { voicedHold = std::max (0, voicedHold - 1); if (voicedHold == 0) periodSamples = 0.f; return; }

        float running = 0.f;
        int best = -1;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            float d = 0.f;
            for (int i = 0; i < W; ++i)
            {
                const float diff = frame[(size_t) i] - frame[(size_t) (i + lag)];
                d += diff * diff;
            }
            running += d;
            cmnd[(size_t) lag] = d * (float) lag / std::max (running, 1.0e-12f);

            if (lag > minLag + 1 && best < 0 && cmnd[(size_t) (lag - 1)] < 0.18f
                && cmnd[(size_t) (lag - 1)] <= cmnd[(size_t) lag])
            {
                best = lag - 1;
                break;
            }
        }

        if (best < 0) { voicedHold = std::max (0, voicedHold - 1); if (voicedHold == 0) periodSamples = 0.f; return; }

        // parabolic refinement
        float lag = (float) best;
        const float a = cmnd[(size_t) (best - 1)], b = cmnd[(size_t) best], c = cmnd[(size_t) (best + 1)];
        const float den = a - 2.f * b + c;
        if (std::abs (den) > 1.0e-9f) lag += 0.5f * (a - c) / den;

        periodSamples = lag * (float) kDecim;
        voicedHold = 6;
    }

    float targetWindow (float nominal) const
    {
        if (periodSamples <= 0.f) return nominal;
        // even number of periods -> both heads (half a window apart) are in phase
        const float twoP = 2.f * periodSamples;
        const float k = std::max (1.f, std::round (nominal / twoP));
        return k * twoP;
    }

    // cubic (Hermite) read, delay in samples
    float read (float delay) const
    {
        const float pos = (float) writePos - delay;
        const float fl  = std::floor (pos);
        const float t   = pos - fl;
        const int   i   = (int) fl;

        const float y0 = buffer[(size_t) ((i - 1) & mask)];
        const float y1 = buffer[(size_t) ( i      & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)];
        const float y3 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    static float wrap (float x) { return x - std::floor (x); }

    // sin(2*pi*phase), phase in 0..1
    static float fastSin (float phase) { return std::sin (2.f * kPi * phase); }

    static constexpr int kDecim = 4, kDetLen = 512, kDetHop = 64;
    std::vector<float> decBuf, frame, cmnd;
    float decLp = 0.f, periodSamples = 0.f;
    int decCount = 0, decPos = 0, detHop = 0, voicedHold = 0;

    float sr = 44100.f;
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
    std::vector<Voice> voices;
    ChoirParams params;
};

} // namespace koor
