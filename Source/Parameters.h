#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Paramètres de ONIRIQUE THE CHOIR : identifiants et layout APVTS.
namespace onirique::ids
{
inline constexpr const char* source   = "source";     // LIVE (ce qui arrive) / CLIP (voix enregistrée ou glissée)
inline constexpr const char* key      = "key";        // tonique : C .. B
inline constexpr const char* scale    = "scale";      // gamme
inline constexpr const char* tight    = "tight";      // 0 = les harmonies suivent le vibrato de la voix, 100 = notes justes
inline constexpr const char* humanize = "humanize";   // décalages et désaccords légers entre les voix (effet chœur)
inline constexpr const char* width    = "width";      // largeur stéréo des voix
inline constexpr const char* dry      = "dry";        // voix d'origine
inline constexpr const char* wet      = "wet";        // harmonies
inline constexpr const char* bypass   = "bypass";
inline constexpr const char* model    = "model";      // type de chœur : STUDIO / GOSPEL / CHILDREN / GANG
inline constexpr const char* mic      = "mic";        // couleur du micro sur le chœur
inline constexpr const char* room     = "room";       // salle (réverbération) du chœur
inline constexpr const char* stereo   = "stereo";     // sortie du chœur : ST / MONO

inline constexpr int kHarmonyVoices = 4;
inline const char* const voiceOn[kHarmonyVoices]       { "v1On", "v2On", "v3On", "v4On" };
inline const char* const voiceInterval[kHarmonyVoices] { "v1Int", "v2Int", "v3Int", "v4Int" };
inline const char* const voiceOctave[kHarmonyVoices]   { "v1Oct", "v2Oct", "v3Oct", "v4Oct" };
inline const char* const voiceLevel[kHarmonyVoices]    { "v1Lvl", "v2Lvl", "v3Lvl", "v4Lvl" };
inline const char* const voicePan[kHarmonyVoices]      { "v1Pan", "v2Pan", "v3Pan", "v4Pan" };

inline constexpr const char* octDownOn  = "octDownOn";   // doublure à l'octave en dessous
inline constexpr const char* octDownLvl = "octDownLvl";
inline constexpr const char* octUpOn    = "octUpOn";     // doublure à l'octave au-dessus
inline constexpr const char* octUpLvl   = "octUpLvl";
} // namespace onirique::ids

namespace onirique
{
inline const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// gammes : masque des 12 demi-tons à partir de la tonique (bit 0 = tonique)
struct ScaleDef { const char* name; juce::uint16 mask; };
inline constexpr ScaleDef scales[] {
    { "MAJOR",          0b101010110101 },   // 0 2 4 5 7 9 11
    { "MINOR",          0b010110101101 },   // 0 2 3 5 7 8 10
    { "HARMONIC MINOR", 0b100110101101 },   // 0 2 3 5 7 8 11
    { "MELODIC MINOR",  0b101010101101 },   // 0 2 3 5 7 9 11
    { "DORIAN",         0b011010101101 },   // 0 2 3 5 7 9 10
    { "PHRYGIAN",       0b010110101011 },   // 0 1 3 5 7 8 10
    { "LYDIAN",         0b101011010101 },   // 0 2 4 6 7 9 11
    { "MIXOLYDIAN",     0b011010110101 },   // 0 2 4 5 7 9 10
    { "MAJ PENTATONIC", 0b001010010101 },   // 0 2 4 7 9
    { "MIN PENTATONIC", 0b010010101001 },   // 0 3 5 7 10
    { "CHROMATIC",      0b111111111111 },
};
inline constexpr int kNumScales = (int) (sizeof (scales) / sizeof (scales[0]));
inline constexpr int kChromatic = kNumScales - 1;

// types de chœur : chaque voix d'harmonie est chantée par plusieurs choristes, chacun un peu décalé et désaccordé
struct ChoirModelDef
{
    const char* name;
    int singers;          // choristes par voix d'harmonie
    int leadSingers;      // choristes qui doublent la voix chantée à l'unisson (0 = aucun)
    int octaveShift;      // tout le chœur une octave plus haut (enfants)
    float formant;        // 1 = timbre d'origine, > 1 voix plus petite (enfant), < 1 plus grande (homme)
    float scatterMs;      // décalage maximal entre choristes
    float detuneCents;    // désaccord maximal entre choristes
    float leadGain;       // niveau des doublures à l'unisson
    float drive;          // saturation (cris)
};
inline constexpr ChoirModelDef choirModels[] {
    { "STUDIO",   1, 0, 0, 1.00f,  4.0f,  4.0f, 0.0f, 0.0f },
    { "GOSPEL",   3, 2, 0, 1.00f, 22.0f, 11.0f, 0.6f, 0.0f },
    { "CHILDREN", 3, 3, 1, 1.24f, 18.0f,  9.0f, 0.9f, 0.0f },
    { "GANG",     5, 6, 0, 0.90f, 45.0f, 18.0f, 1.0f, 0.45f },
};
inline constexpr int kNumModels = (int) (sizeof (choirModels) / sizeof (choirModels[0]));

// micros : couleur appliquée au chœur (grave, présence, aigu)
struct MicDef { const char* name; float lowHz, lowDb, midHz, midDb, midQ, highHz, highDb; };
inline constexpr MicDef mics[] {
    { "CLEAN",  100.0f,  0.0f, 1000.0f, 0.0f, 0.7f, 8000.0f,  0.0f },
    { "U67",    120.0f,  2.0f,  500.0f, 1.0f, 0.8f, 9000.0f,  1.5f },   // chaud, rond
    { "U47",    110.0f,  2.5f, 2500.0f, -1.0f, 0.9f, 7000.0f, -1.5f },  // gras, aigus doux
    { "C414",    90.0f, -1.0f, 3500.0f, 1.5f, 1.0f, 11000.0f, 3.0f },   // clair, détaillé
    { "SM7B",   140.0f,  1.5f, 4500.0f, 3.0f, 1.2f, 10000.0f, -3.0f },  // présence radio, aigus coupés
    { "RIBBON", 150.0f,  1.0f, 2000.0f, -1.5f, 0.7f, 6000.0f, -5.0f },  // sombre, vintage
};
inline constexpr int kNumMics = (int) (sizeof (mics) / sizeof (mics[0]));

// intervalles d'une voix, en degrés de la gamme (0 = unisson, +2 = tierce au-dessus, -2 = tierce en dessous...)
inline constexpr int intervalSteps[] { 0, 1, 2, 3, 4, 5, 6, -1, -2, -3, -4, -5, -6 };
inline const juce::StringArray intervalNames { "UNISON", "2ND UP", "3RD UP", "4TH UP", "5TH UP", "6TH UP", "7TH UP",
                                               "2ND DOWN", "3RD DOWN", "4TH DOWN", "5TH DOWN", "6TH DOWN", "7TH DOWN" };

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    auto pct = AudioParameterFloatAttributes().withLabel ("%")
                   .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
    auto db = AudioParameterFloatAttributes().withLabel ("dB")
                  .withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; });
    auto pan = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        const int p = roundToInt (v);
        return p == 0 ? String ("C") : (p < 0 ? "L" + String (-p) : "R" + String (p));
    });
    const NormalisableRange<float> dbRange (-60.0f, 6.0f, 0.1f, 2.2f);

    StringArray scaleNames;
    for (const auto& s : scales) scaleNames.add (s.name);

    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::source, 1 }, "Source", StringArray { "LIVE", "CLIP" }, 0));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, "Key", keyNames, 0));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale", scaleNames, 0));
    ps.push_back (std::make_unique<P> (ParameterID { ids::tight, 1 }, "Tight", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f, pct));
    ps.push_back (std::make_unique<P> (ParameterID { ids::humanize, 1 }, "Humanize", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pct));
    ps.push_back (std::make_unique<P> (ParameterID { ids::width, 1 }, "Width", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f, pct));
    ps.push_back (std::make_unique<P> (ParameterID { ids::dry, 1 }, "Dry", dbRange, 0.0f, db));
    ps.push_back (std::make_unique<P> (ParameterID { ids::wet, 1 }, "Harmonies", dbRange, -3.0f, db));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    StringArray modelNames, micNames;
    for (const auto& m : choirModels) modelNames.add (m.name);
    for (const auto& m : mics) micNames.add (m.name);
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::model, 1 }, "Choir Model", modelNames, 1));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mic, 1 }, "Microphone", micNames, 1));
    ps.push_back (std::make_unique<P> (ParameterID { ids::room, 1 }, "Room", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pct));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::stereo, 1 }, "Output", StringArray { "ST", "MONO" }, 0));

    // voix par défaut : tierce et quinte au-dessus, tierce en dessous (une octave plus bas), quarte au-dessus coupée
    const int  defInt[ids::kHarmonyVoices] { 2, 4, 8, 3 };
    const int  defOct[ids::kHarmonyVoices] { 0, 0, 0, 0 };
    const bool defOn[ids::kHarmonyVoices]  { true, true, true, false };
    const float defPan[ids::kHarmonyVoices] { -45.0f, 45.0f, -20.0f, 20.0f };
    for (int v = 0; v < ids::kHarmonyVoices; ++v)
    {
        const String n = "Voice " + String (v + 1) + " ";
        ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::voiceOn[v], 1 }, n + "On", defOn[v]));
        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::voiceInterval[v], 1 }, n + "Interval", intervalNames, defInt[v]));
        // octave : choix -2 .. +2 (index 2 = 0), pour le sélecteur segmenté
        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::voiceOctave[v], 1 }, n + "Octave",
                                                               StringArray { "-2", "-1", "0", "+1", "+2" }, defOct[v] + 2));
        ps.push_back (std::make_unique<P> (ParameterID { ids::voiceLevel[v], 1 }, n + "Level", dbRange, -2.0f, db));
        ps.push_back (std::make_unique<P> (ParameterID { ids::voicePan[v], 1 }, n + "Pan", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), defPan[v], pan));
    }

    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::octDownOn, 1 }, "Octave Down", false));
    ps.push_back (std::make_unique<P> (ParameterID { ids::octDownLvl, 1 }, "Octave Down Level", dbRange, -6.0f, db));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::octUpOn, 1 }, "Octave Up", false));
    ps.push_back (std::make_unique<P> (ParameterID { ids::octUpLvl, 1 }, "Octave Up Level", dbRange, -9.0f, db));

    return { ps.begin(), ps.end() };
}
} // namespace onirique
