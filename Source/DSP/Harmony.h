#pragma once

#include "../Parameters.h"
#include <cmath>

namespace onirique
{
// Où placer chaque voix d'harmonie, à partir de la hauteur chantée.
//  - la note chantée est rattachée à la note de la gamme la plus proche, avec une hystérésis : un vibrato
//    ou une petite approche ne font pas sauter l'harmonie d'une note à l'autre ;
//  - chaque voix monte ou descend d'un nombre de degrés DANS la gamme (une tierce peut être majeure ou mineure
//    selon le degré, comme le ferait un choriste), plus des octaves ;
//  - TIGHT : 0 = la voix garde l'écart de la voix chantée à la note (vibrato, glissés), 1 = note parfaitement juste.
class HarmonyMapper
{
public:
    void setScale (int keyIndex, int scaleIndex) noexcept
    {
        key = keyIndex;
        scale = juce::jlimit (0, kNumScales - 1, scaleIndex);
        mask = scales[scale].mask;
    }

    void reset() noexcept { note = -1; }

    bool inScale (int midi) const noexcept
    {
        const int pc = ((midi - key) % 12 + 12) % 12;
        return (mask >> pc) & 1;
    }

    // note de la gamme la plus proche (à égalité : la plus grave)
    int nearestInScale (float midi) const noexcept
    {
        const int base = (int) std::floor (midi);
        int best = base, bestDist = 1000;
        for (int n = base - 6; n <= base + 7; ++n)
        {
            if (! inScale (n)) continue;
            const float d = std::abs ((float) n - midi);
            const int di = (int) std::lround (d * 1000.0f);
            if (di < bestDist) { bestDist = di; best = n; }
        }
        return best;
    }

    // suit la voix chantée : renvoie la note de gamme courante
    int track (float midi) noexcept
    {
        if (note < 0 || std::abs (midi - (float) note) > 0.62f || ! inScale (note))
            note = nearestInScale (midi);
        return note;
    }

    // monte / descend de `steps` degrés dans la gamme ; en chromatique, intervalles de la gamme majeure
    int shiftDegrees (int fromNote, int steps) const noexcept
    {
        if (steps == 0) return fromNote;
        if (scale == kChromatic)
        {
            static constexpr int major[] { 0, 2, 4, 5, 7, 9, 11 };
            const int s = std::abs (steps);
            const int semis = major[s % 7] + 12 * (s / 7);
            return fromNote + (steps > 0 ? semis : -semis);
        }
        const int dir = steps > 0 ? 1 : -1;
        int n = fromNote;
        for (int i = 0; i < std::abs (steps); ++i)
        {
            do n += dir; while (! inScale (n));
        }
        return n;
    }

    // hauteur visée (MIDI, fractionnaire) d'une voix
    static float targetPitch (float sung, int scaleNote, int shiftedNote, float tightness) noexcept
    {
        const float deviation = sung - (float) scaleNote;
        return (float) shiftedNote + deviation * (1.0f - tightness);
    }

private:
    int key = 0, scale = 0, note = -1;
    juce::uint16 mask = scales[0].mask;
};

inline float midiFromHz (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

inline juce::String noteName (int midi)
{
    return keyNames[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1);
}
} // namespace onirique
