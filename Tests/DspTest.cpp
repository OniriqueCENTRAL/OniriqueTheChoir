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
} // namespace

int main()
{
    int failures = 0;
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
