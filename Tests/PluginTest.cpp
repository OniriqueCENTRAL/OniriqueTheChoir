// Essai de bout en bout du plugin (processeur + interface réels) :
//  fichier de voix -> rendu des harmonies -> tonalité -> fichier à glisser -> lecture CLIP synchronisée au DAW
//  -> mode LIVE -> projet sauvegardé et rouvert -> capture de l'interface.
#include "../Source/PluginProcessor.h"
#include "SynthVoice.h"
#include <iostream>

using namespace onirique;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok     " : "  ECHEC  ") << what << std::endl;
    if (! ok) ++failures;
}

void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

bool waitForRender (OniriqueTheChoirAudioProcessor& p, int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    pump (400);
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (p.currentRender() != nullptr && ! p.isRendering()) return true;
        pump (50);
    }
    return false;
}

// tête de lecture d'un DAW qui joue depuis `start` secondes
struct FakePlayHead : juce::AudioPlayHead
{
    double seconds = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setTimeInSeconds (seconds);
        p.setTimeInSamples ((juce::int64) (seconds * 48000.0));
        return p;
    }
};

double rms (const juce::AudioBuffer<float>& b)
{
    double s = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return std::sqrt (s / juce::jmax (1, b.getNumChannels() * b.getNumSamples()));
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File shotFile = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();
    const double fs = 48000.0;

    // voix de test : C4 E4 G4 C5, 1 s par note, dans un WAV
    const auto voice = synthVoice (fs, { 60.0f, 64.0f, 67.0f, 72.0f }, 1.0, 25.0);
    auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("choir-test-voice.wav");
    {
        wavFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (wavFile.createOutputStream().release(), fs, 1, 24, {}, 0));
        juce::AudioBuffer<float> b (1, (int) voice.size());
        b.copyFrom (0, 0, voice.data(), (int) voice.size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    std::cout << "Clip glissé" << std::endl;
    auto proc = std::make_unique<OniriqueTheChoirAudioProcessor>();
    proc->setPlayConfigDetails (2, 2, fs, 512);
    proc->prepareToPlay (fs, 512);
    juce::String error;
    check (proc->loadFile (wavFile, error), "fichier chargé " + error);
    check (proc->apvts.getRawParameterValue (ids::source)->load() > 0.5f, "passe en mode CLIP");
    check (waitForRender (*proc, 20000), "harmonies rendues");
    auto render = proc->currentRender();
    check (render != nullptr && render->harmonies.getNumSamples() == (int) voice.size(), "rendu de la même durée que la voix");
    if (render != nullptr)
    {
        check (rms (render->harmonies) > 0.01, "harmonies audibles (RMS " + juce::String (rms (render->harmonies), 4) + ")");
        int voiced = 0;
        for (auto m : render->lead) if (m > 0.0f) ++voiced;
        check (voiced > (int) render->lead.size() / 2, "hauteur suivie sur la voix");
    }

    // tonalité proposée dès le chargement, sans toucher aux réglages
    check (proc->suggestedKey() == 0 && proc->suggestedScale() == 0,
           "tonalité proposée après le chargement : " + (proc->suggestedKey() >= 0 ? keyNames[proc->suggestedKey()] : juce::String ("aucune")));

    // presets : tout le chœur change, la tonalité du morceau reste
    {
        auto setRaw = [&] (const char* id, float v) { auto* p = proc->apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        setRaw (ids::key, 2.0f);   // D
        proc->applyPreset (5);     // Gang Chant
        check ((int) proc->apvts.getRawParameterValue (ids::model)->load() == 3 && proc->apvts.getRawParameterValue (ids::octDownOn)->load() > 0.5f
                   && proc->apvts.getRawParameterValue (ids::voiceOn[0])->load() < 0.5f,
               "preset GANG CHANT appliqué");
        check ((int) proc->apvts.getRawParameterValue (ids::key)->load() == 2, "preset : tonalité du morceau gardée");
        proc->applySuggestedKey();
        check ((int) proc->apvts.getRawParameterValue (ids::key)->load() == 0, "APPLY : tonalité proposée appliquée");
        proc->applyPreset (0);     // Sunday Gospel
        check (waitForRender (*proc, 20000), "harmonies recalculées après le preset");
    }

    // ▶ dans le plugin, DAW à l'arrêt : voix + chœur
    {
        FakePlayHead stopped;
        stopped.playing = false;
        proc->setPlayHead (&stopped);
        proc->setPreview (true);
        juce::AudioBuffer<float> pv (2, 512);
        juce::MidiBuffer m;
        double e = 0.0;
        for (int b = 0; b < 100; ++b) { pv.clear(); proc->processBlock (pv, m); e += rms (pv); }
        check (e / 100.0 > 0.01, "PLAY : voix et chœur sans lancer le DAW");
        check (proc->previewSeconds() > 0.9, "PLAY : la lecture avance (" + juce::String (proc->previewSeconds(), 2) + " s)");
        proc->setPreview (false);
        proc->setPlayHead (nullptr);
    }

    check (proc->detectKey(), "tonalité détectée");
    const int key = (int) proc->apvts.getRawParameterValue (ids::key)->load();
    const int scaleIdx = (int) proc->apvts.getRawParameterValue (ids::scale)->load();
    check (key == 0 && scaleIdx == 0, "tonalité = C MAJOR (trouvé " + keyNames[key] + " " + scales[scaleIdx].name + ")");

    const auto exported = proc->exportHarmonies (error);
    check (exported.existsAsFile(), "fichier à glisser : " + exported.getFileName());
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (exported.createInputStream().release(), true));
        check (r != nullptr && r->numChannels == 2 && std::abs ((double) r->lengthInSamples - (double) voice.size()) < 10,
               "WAV stéréo, même durée que la voix");
    }

    // lecture synchronisée au DAW : à 1,5 s, la voix du fichier et ses harmonies sortent
    FakePlayHead head;
    head.seconds = 1.5;
    proc->setPlayHead (&head);
    pump (300);   // rendu relancé par la détection de tonalité
    waitForRender (*proc, 20000);
    juce::AudioBuffer<float> io (2, 512);
    juce::MidiBuffer midi;
    io.clear();
    proc->processBlock (io, midi);
    check (rms (io) > 0.01, "CLIP : voix + harmonies à la position du DAW (RMS " + juce::String (rms (io), 4) + ")");
    head.playing = false;
    io.clear();
    proc->processBlock (io, midi);
    check (rms (io) < 1.0e-6, "CLIP : silence quand le DAW est à l'arrêt");

    // projet sauvegardé puis rouvert : la voix revient et les harmonies sont recalculées
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    check (state.getSize() > 10000, "projet sauvegardé avec la voix (" + juce::String ((int) state.getSize() / 1024) + " Ko)");
    {
        auto reopened = std::make_unique<OniriqueTheChoirAudioProcessor>();
        reopened->setPlayConfigDetails (2, 2, fs, 512);
        reopened->prepareToPlay (fs, 512);
        reopened->setStateInformation (state.getData(), (int) state.getSize());
        check (reopened->hasClip(), "projet rouvert : voix retrouvée");
        check (waitForRender (*reopened, 20000), "projet rouvert : harmonies recalculées");
    }

    // capture de l'interface avec la voix chargée (page de licence masquée)
    if (shotFile != juce::File())
    {
        proc->setPlayHead (nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditorIfNeeded());
        editor->setSize (1480, 900);
        editor->setVisible (true);
        for (int i = editor->getNumChildComponents(); --i >= 0;)   // la licence : la dernière couche qui couvre toute la fenêtre
            if (auto* child = editor->getChildComponent (i); child->getBounds() == editor->getLocalBounds()) { child->setVisible (false); break; }
        pump (1500);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        shotFile.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = shotFile.createOutputStream()) png.writeImageToStream (img, *out);
        check (shotFile.existsAsFile(), "capture de l'interface : " + shotFile.getFullPathName());
        proc->editorBeingDeleted (editor.get());
        editor.reset();
    }

    // LIVE : la voix qui arrive est harmonisée en temps réel
    std::cout << "Mode LIVE" << std::endl;
    proc->clearClip();
    check (proc->apvts.getRawParameterValue (ids::source)->load() < 0.5f, "CLEAR : retour en LIVE");
    pump (200);
    proc->prepareToPlay (fs, 512);
    check (proc->getLatencySamples() > 0, "latence annoncée au DAW en LIVE : " + juce::String (proc->getLatencySamples()) + " échantillons");
    double outEnergy = 0.0;
    for (int pos = 0; pos + 512 <= (int) voice.size(); pos += 512)
    {
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice.data() + pos, 512);
        proc->processBlock (io, midi);
        if (pos > (int) fs) outEnergy += rms (io);
    }
    check (outEnergy > 1.0, "LIVE : sortie voix + harmonies");

    proc.reset();
    wavFile.deleteFile();
    std::cout << (failures == 0 ? "TOUT EST BON" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
