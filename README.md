# ONIRIQUE THE CHOIR (JUCE 8 / VST3, AU sur Mac)

Harmonies vocales dans la tonalité, à partir d'une voix. Même DA que ONIRIQUE VOCALBOX.

## Utilisation
- **LIVE** : le plugin harmonise ce qui arrive (la piste de voix pendant que le DAW joue, ou un micro). Latence
  d'environ 58 ms, annoncée au DAW qui la compense.
- **CLIP** : la voix est dans le plugin, les harmonies sont calculées une fois, en tâche de fond, puis rejouées
  en synchro avec la ligne de temps du DAW (latence nulle).
  - glisser un fichier audio (WAV, AIFF, FLAC, MP3...) sur l'afficheur, ou **LOAD** : le fichier démarre au
    début du projet et sa voix est rejouée par le plugin ;
  - **RECORD** puis lecture du DAW : la piste est capturée à sa position sur la ligne de temps ; arrêter le DAW
    termine l'enregistrement.
  - **DRAG HARMONIES TO YOUR DAW** : glisser la zone vers le DAW dépose les harmonies en WAV stéréo 24 bits
    (avec le silence nécessaire avant : déposé au début du projet, le fichier tombe pile sur la voix).
  - **DETECT KEY** : tonalité de la voix (profils de Krumhansl) -> KEY et SCALE.
  - la voix est gardée dans le projet (FLAC dans l'état du plugin).
- **KEY / SCALE** : 11 gammes (majeure, mineures, modes, pentatoniques, chromatique).
- **VOICE 1..4** : intervalle en degrés de la gamme (une tierce est majeure ou mineure selon la note), octave,
  niveau, panoramique. **OCTAVES** : doublures à l'octave en dessous / au-dessus.
- **TIGHT** : 0 % les harmonies gardent le vibrato et les glissés de la voix, 100 % notes parfaitement justes.
- **HUMANIZE** : petits décalages et désaccords entre les voix. **WIDTH** : largeur stéréo.

## Le moteur
- `Source/DSP/PitchDetector.h` : YIN (fonction de différence par FFT), période fractionnaire, voisement.
- `Source/DSP/Harmony.h` : note de gamme la plus proche (avec hystérésis), degrés dans la gamme, TIGHT.
- `Source/DSP/ChoirEngine.h` : TD-PSOLA synchrone à la période. Marques calées sur le pic de chaque cycle
  (signal filtré à 900 Hz), grains de Hann de deux périodes recollés à la nouvelle période ; le timbre reste
  celui de la voix. Consonnes recopiées sans transposition. Même code pour le temps réel et le rendu des clips.

## Licence
`License/` est le kit commun (copie de `plugin-license/` du dépôt ONIRIQUE CENTRAL, ne pas modifier ici).
Identifiant du plugin : `onirique-the-choir`.

## Compiler
    cmake -B build
    cmake --build build --config Release

Mac : `cmake -B build -G Xcode` (binaire universel Intel + Apple Silicon, VST3 + AU + Standalone).

## Essais
- `OniriqueChoirTests` : hauteur de chaque voix d'harmonie sur une voix de synthèse (44,1 / 48 / 96 kHz),
  niveau ; lancé par la CI sur Mac et sur Windows.
- `OniriqueChoirPluginTest` (`-DONIRIQUE_PLUGIN_TEST=ON`) : de bout en bout avec le vrai processeur et la vraie
  interface (fichier, rendu, tonalité, export, lecture CLIP, LIVE, projet rouvert, capture de l'interface).

## Publication
Chaque envoi sur `main` compile Mac + Windows et publie la version dans ONIRIQUE CENTRAL (voir
`.github/workflows/build.yml`). Le message du commit devient la note de version : l'écrire pour les clients,
et ajouter `[skip ci]` pour un changement interne.
