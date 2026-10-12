#pragma once

#include "Parameters.h"
#include <vector>

// Presets d'usine : des chœurs complets, prêts à écouter sur la voix chargée.
// Un preset règle tout le chœur ; la tonalité (KEY / SCALE) reste celle du morceau.
// Valeurs dans les unités des paramètres : index pour les choix, dB, %.
// Intervalles : 0 UNISON, 1 2ND UP, 2 3RD UP, 3 4TH UP, 4 5TH UP, 5 6TH UP, 6 7TH UP,
//               7 2ND DOWN, 8 3RD DOWN, 9 4TH DOWN, 10 5TH DOWN, 11 6TH DOWN, 12 7TH DOWN. Octave : 2 = 0.
namespace onirique
{
struct PresetValue { const char* id; float value; };

struct ChoirPreset
{
    const char* name;
    const char* category;
    std::vector<PresetValue> values;
};

inline const std::vector<ChoirPreset>& factoryPresets()
{
    using namespace ids;
    static const std::vector<ChoirPreset> list {
        { "Sunday Gospel", "GOSPEL", {
            { model, 1 }, { mic, 1 }, { room, 35 }, { tight, 30 }, { humanize, 55 }, { width, 90 }, { wet, -2 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -45 },
            { voiceOn[1], 1 }, { voiceInterval[1], 4 }, { voicePan[1], 45 },
            { voiceOn[2], 1 }, { voiceInterval[2], 8 }, { voicePan[2], -20 },
            { voiceOn[3], 0 }, { octDownOn, 1 }, { octDownLvl, -9 } } },
        { "Gospel Thirds", "GOSPEL", {
            { model, 1 }, { mic, 2 }, { room, 40 }, { tight, 25 }, { humanize, 50 }, { width, 85 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -55 },
            { voiceOn[1], 1 }, { voiceInterval[1], 8 }, { voicePan[1], 55 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Soul Fifths", "GOSPEL", {
            { model, 1 }, { mic, 1 }, { room, 30 }, { tight, 45 }, { humanize, 45 }, { width, 80 },
            { voiceOn[0], 1 }, { voiceInterval[0], 4 }, { voicePan[0], -50 },
            { voiceOn[1], 1 }, { voiceInterval[1], 10 }, { voicePan[1], 50 },
            { voiceOn[2], 1 }, { voiceInterval[2], 0 }, { voiceOctave[2], 3 }, { voiceLevel[2], -8 },
            { voiceOn[3], 0 } } },
        { "Children's Hymn", "CHILDREN", {
            { model, 2 }, { mic, 3 }, { room, 50 }, { tight, 60 }, { humanize, 40 }, { width, 85 }, { wet, 0 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -40 },
            { voiceOn[1], 1 }, { voiceInterval[1], 4 }, { voicePan[1], 40 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Little Angels", "CHILDREN", {
            { model, 2 }, { mic, 3 }, { room, 62 }, { tight, 55 }, { humanize, 35 }, { width, 100 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -70 },
            { voiceOn[1], 1 }, { voiceInterval[1], 8 }, { voicePan[1], 70 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Gang Chant", "GANG", {
            { model, 3 }, { mic, 4 }, { room, 25 }, { tight, 40 }, { humanize, 75 }, { width, 100 }, { wet, -1 },
            { voiceOn[0], 0 }, { voiceOn[1], 0 }, { voiceOn[2], 0 }, { voiceOn[3], 0 },
            { octDownOn, 1 }, { octDownLvl, -4 } } },
        { "Stadium Shout", "GANG", {
            { model, 3 }, { mic, 4 }, { room, 45 }, { tight, 35 }, { humanize, 85 }, { width, 100 },
            { voiceOn[0], 1 }, { voiceInterval[0], 10 }, { voicePan[0], 0 },
            { voiceOn[1], 0 }, { voiceOn[2], 0 }, { voiceOn[3], 0 },
            { octDownOn, 1 }, { octDownLvl, -6 } } },
        { "Terrace Fifths", "GANG", {
            { model, 3 }, { mic, 5 }, { room, 30 }, { tight, 45 }, { humanize, 70 }, { width, 90 },
            { voiceOn[0], 1 }, { voiceInterval[0], 4 }, { voicePan[0], -35 },
            { voiceOn[1], 1 }, { voiceInterval[1], 2 }, { voicePan[1], 35 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Pop Stack", "STUDIO", {
            { model, 0 }, { mic, 0 }, { room, 10 }, { tight, 70 }, { humanize, 25 }, { width, 80 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -45 },
            { voiceOn[1], 1 }, { voiceInterval[1], 4 }, { voicePan[1], 45 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Wide Thirds", "STUDIO", {
            { model, 0 }, { mic, 1 }, { room, 15 }, { tight, 55 }, { humanize, 30 }, { width, 100 },
            { voiceOn[0], 1 }, { voiceInterval[0], 2 }, { voicePan[0], -80 },
            { voiceOn[1], 1 }, { voiceInterval[1], 8 }, { voicePan[1], 80 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
        { "Octave Wall", "STUDIO", {
            { model, 0 }, { mic, 3 }, { room, 20 }, { tight, 50 }, { humanize, 30 }, { width, 70 },
            { voiceOn[0], 0 }, { voiceOn[1], 0 }, { voiceOn[2], 0 }, { voiceOn[3], 0 },
            { octDownOn, 1 }, { octDownLvl, -4 }, { octUpOn, 1 }, { octUpLvl, -7 } } },
        { "Dark Unison", "STUDIO", {
            { model, 0 }, { mic, 5 }, { room, 20 }, { tight, 20 }, { humanize, 70 }, { width, 90 },
            { voiceOn[0], 1 }, { voiceInterval[0], 0 }, { voicePan[0], -60 },
            { voiceOn[1], 1 }, { voiceInterval[1], 0 }, { voicePan[1], 60 },
            { voiceOn[2], 0 }, { voiceOn[3], 0 } } },
    };
    return list;
}
} // namespace onirique
