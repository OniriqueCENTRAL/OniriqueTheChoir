// Essais du moteur d'harmonies : une voix de synthèse (impulsions glottiques filtrées par des formants, avec
// vibrato) chante C4, E4, G4 ; on mesure la hauteur produite par chaque voix d'harmonie au milieu de chaque note.
#include "../Source/DSP/ChoirEngine.h"
#include "SynthVoice.h"
#include <iostream>

using namespace onirique;

namespace
{
// hauteur médiane (MIDI) mesurée sur [from, to)
float measure (const std::vector<float>& sig, double fs, int from, int to)
{
    PitchDetector det;
    det.prepare (fs);
    std::vector<float> found;
    for (int c = from; c + det.span() < to; c += 512)
    {
        const auto r = det.analyse (sig.data() + c);
        if (r.voiced) found.push_back (midiFromHz ((float) fs / r.period));
    }
    if (found.empty()) return 0.0f;
    std::sort (found.begin(), found.end());
    return found[found.size() / 2];
}

// centre de gravité du spectre (Hz) : plus haut = timbre plus clair / voix plus petite
float spectralCentroid (const std::vector<float>& sig, double fs, int from, int to)
{
    constexpr int order = 12, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    double num = 0.0, den = 0.0;
    for (int start = from; start + n < to; start += n / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = sig[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < n / 2; ++k)
        {
            const double hz = k * fs / n, m = buf[(size_t) k];
            num += hz * m;
            den += m;
        }
    }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

struct RunResult { std::vector<float> out; int latency = 0; };

RunResult runEngine (const std::vector<float>& voice, double fs, const ChoirEngine::Settings& s)
{
    ChoirEngine eng;
    eng.prepare (fs);
    eng.setSettings (s);
    const int n = (int) voice.size();
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; i += 512)
        eng.process (voice.data() + i, l.data() + i, r.data() + i, juce::jmin (512, n - i));
    RunResult res;
    res.latency = eng.getLatency();
    res.out.assign ((size_t) n, 0.0f);
    for (int i = 0; i + res.latency < n; ++i) res.out[(size_t) i] = 0.5f * (l[(size_t) (i + res.latency)] + r[(size_t) (i + res.latency)]);
    return res;
}
} // namespace

int main()
{
    int failures = 0;

    // types de chœur : plusieurs choristes décalés et désaccordés, la hauteur reste juste ;
    // CHILDREN monte d'une octave avec un timbre plus clair ; GANG (saturation) et la salle restent propres
    {
        const double fs = 48000.0;
        const std::vector<float> notes { 60.0f, 64.0f, 67.0f };
        const auto voice = synthVoice (fs, notes, 1.0, 25.0);
        float studioCentroid = 0.0f;
        for (int m = 0; m < kNumModels; ++m)
        {
            ChoirEngine::Settings s;
            s.key = 0; s.scale = 0; s.tight = 1.0f; s.humanize = 0.3f; s.width = 1.0f;
            s.model = choirModels[m];
            s.model.leadSingers = 0;   // on mesure la voix d'harmonie seule
            s.mic = 1;                 // U67
            s.room = m == 3 ? 0.4f : 0.0f;
            s.voices[0] = { true, 2, 0, 1.0f, 0.0f };   // tierce au-dessus
            const auto res = runEngine (voice, fs, s);
            const int up = 12 * choirModels[m].octaveShift;
            const int expect[] { 64 + up, 67 + up, 71 + up };
            std::cout << "CHOEUR " << choirModels[m].name << " :";
            bool finite = true;
            float peak = 0.0f;
            for (auto v : res.out) { finite = finite && std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }
            for (int k = 0; k < 3; ++k)
            {
                const float got = measure (res.out, fs, (int) ((k + 0.25) * fs), (int) ((k + 0.8) * fs));
                const bool ok = std::abs (got - (float) expect[k]) < 0.3f;
                if (! ok) ++failures;
                std::cout << "  " << noteName (expect[k]) << " " << juce::String (got, 2) << (ok ? " ok" : " ECHEC");
            }
            const bool levelOk = finite && peak > 0.02f && peak < 2.0f;
            if (! levelOk) ++failures;
            const float centroid = spectralCentroid (res.out, fs, (int) (0.2 * fs), (int) (2.8 * fs));
            if (m == 0) studioCentroid = centroid;
            std::cout << "   crête " << juce::String (peak, 2) << (levelOk ? "" : " ECHEC") << "   centre spectral " << (int) centroid << " Hz" << std::endl;
            if (m == 2)
            {
                const bool brighter = centroid > studioCentroid * 1.15f;
                if (! brighter) ++failures;
                std::cout << "  CHILDREN plus clair que STUDIO : " << (brighter ? "ok" : "ECHEC") << std::endl;
            }
        }
    }

    for (const double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const std::vector<float> notes { 60.0f, 64.0f, 67.0f };   // C4 E4 G4
        const double noteSec = 1.0;
        const auto voice = synthVoice (fs, notes, noteSec, 25.0);

        struct Case { const char* name; int steps, octave; std::vector<int> expect; };
        const Case cases[] {
            { "3RD UP",          2,  0, { 64, 67, 71 } },
            { "5TH UP",          4,  0, { 67, 71, 74 } },
            { "3RD DOWN",       -2,  0, { 57, 60, 64 } },
            { "UNISON OCT DOWN", 0, -1, { 48, 52, 55 } },
            { "UNISON OCT UP",   0,  1, { 72, 76, 79 } },
        };

        for (const auto& c : cases)
        {
            ChoirEngine eng;
            eng.prepare (fs);
            ChoirEngine::Settings s;
            s.key = 0; s.scale = 0; s.tight = 1.0f; s.humanize = 0.0f; s.width = 0.0f;
            s.voices[0] = { true, c.steps, c.octave, 1.0f, 0.0f };
            eng.setSettings (s);

            const int n = (int) voice.size();
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; i += 512)
            {
                const int len = juce::jmin (512, n - i);
                eng.process (voice.data() + i, l.data() + i, r.data() + i, len);
            }
            // sortie avancée de la latence, comme pour le rendu d'un clip
            const int lat = eng.getLatency();
            std::vector<float> outSig ((size_t) n, 0.0f);
            for (int i = 0; i + lat < n; ++i) outSig[(size_t) i] = l[(size_t) (i + lat)];

            std::cout << (int) fs << " Hz  " << c.name << " :";
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const int from = (int) ((k + 0.25) * noteSec * fs), to = (int) ((k + 0.8) * noteSec * fs);
                const float got = measure (outSig, fs, from, to);
                const bool ok = std::abs (got - (float) c.expect[k]) < 0.2f;
                if (! ok) ++failures;
                std::cout << "  " << noteName (c.expect[k]) << " attendu " << juce::String (got, 2) << (ok ? " ok" : " ECHEC");
            }
            // niveau : comparable à la voix d'origine, sans NaN
            double rmsIn = 0.0, rmsOut = 0.0;
            bool finite = true;
            for (int i = 0; i < n - lat; ++i)
            {
                rmsIn += voice[(size_t) i] * voice[(size_t) i];
                rmsOut += outSig[(size_t) i] * outSig[(size_t) i];
                finite = finite && std::isfinite (outSig[(size_t) i]);
            }
            const double ratioDb = 10.0 * std::log10 ((rmsOut + 1e-20) / (rmsIn + 1e-20));
            const bool levelOk = finite && std::abs (ratioDb) < 4.0;
            if (! levelOk) ++failures;
            std::cout << "   niveau " << juce::String (ratioDb, 1) << " dB" << (levelOk ? "" : " ECHEC") << std::endl;
        }
    }
    std::cout << (failures == 0 ? "TOUT EST BON" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
