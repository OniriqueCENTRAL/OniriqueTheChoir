#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

// Voix de synthèse pour les essais : dents de scie à bande limitée -> 3 formants (voyelle « a »), avec vibrato.
inline std::vector<float> synthVoice (double fs, const std::vector<float>& notes, double noteSeconds, double vibratoCents)
{
    const int n = (int) (fs * noteSeconds * (double) notes.size());
    std::vector<float> out ((size_t) n, 0.0f);
    juce::IIRFilter f1, f2, f3;
    f1.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 750.0, 6.0));
    f2.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 1150.0, 8.0));
    f3.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 2600.0, 10.0));
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const int idx = juce::jmin ((int) notes.size() - 1, (int) (t / noteSeconds));
        const double midi = notes[(size_t) idx] + vibratoCents / 100.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
        const double hz = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
        phase += hz / fs;
        if (phase >= 1.0) phase -= 1.0;
        double saw = 0.0;
        for (int h = 1; h * hz < fs * 0.45; ++h) saw += std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / h;
        const float s = (float) (saw * 0.2);
        // attaque / relâchement doux sur chaque note
        const double inNote = std::fmod (t, noteSeconds);
        const float env = (float) juce::jmin (1.0, inNote / 0.03, (noteSeconds - inNote) / 0.05);
        out[(size_t) i] = env * 0.5f * (f1.processSingleSampleRaw (s) + 0.6f * f2.processSingleSampleRaw (s) + 0.3f * f3.processSingleSampleRaw (s));
    }
    return out;
}
