#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"
#include "DSP/ChoirEngine.h"
#include "License/LicenseGuard.h"

// Voix chargée dans le plugin : glissée depuis un fichier, ou enregistrée en jouant le DAW.
struct ChoirClip
{
    juce::AudioBuffer<float> voice;   // mono
    double sampleRate = 48000.0;
    double startSeconds = 0.0;        // position sur la ligne de temps du DAW
    bool fromFile = false;            // fichier glissé : la voix d'origine est rejouée par le plugin
    juce::String name;
};

// Harmonies calculées pour un clip (même durée, alignées sur lui) + courbes pour l'afficheur.
struct ChoirRender
{
    juce::AudioBuffer<float> harmonies;   // stéréo
    double sampleRate = 48000.0;
    double startSeconds = 0.0;
    int hop = 256;                         // pas des courbes
    std::vector<float> lead;               // hauteur chantée (MIDI), 0 = rien
    std::array<std::vector<float>, onirique::ChoirEngine::kVoices> voices;
    std::vector<float> envelope;           // enveloppe de la voix (0..1), par pas
};

class OniriqueTheChoirAudioProcessor : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater,
                                       private juce::Timer
{
public:
    OniriqueTheChoirAudioProcessor();
    ~OniriqueTheChoirAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ONIRIQUE THE CHOIR"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-the-choir" };

    //==============================================================================
    // Pour l'interface (thread de l'interface)
    bool loadFile (const juce::File&, juce::String& error);
    void clearClip();
    void setRecordArmed (bool armed);
    bool isRecordArmed() const noexcept { return recArmed.load(); }
    bool isRecording() const noexcept { return recActive.load(); }
    double recordedSeconds() const noexcept { return (double) recLength.load() / juce::jmax (1.0, recRate.load()); }
    bool isRendering() const noexcept { return renderBusy.load() || renderDirty.load(); }
    bool hasClip() const;
    juce::String clipName() const;
    std::shared_ptr<const ChoirRender> currentRender() const;   // nullptr tant qu'aucun rendu
    bool detectKey();                                             // tonalité du clip -> KEY / SCALE
    juce::File exportHarmonies (juce::String& error);             // fichier WAV des harmonies (glisser dans le DAW)

    double playheadSeconds() const noexcept { return playSeconds.load(); }
    bool hostPlaying() const noexcept { return playing.load(); }
    onirique::ChoirEngine::Frame liveFrame() const noexcept;
    float consumePeak (int channel) noexcept { return peak[(size_t) juce::jlimit (0, 1, channel)].exchange (0.0f); }

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    onirique::ChoirEngine::Settings readSettings() const;
    void setClip (std::shared_ptr<ChoirClip>);
    void requestRender() noexcept { renderDirty = true; renderDirtyAt = juce::Time::getMillisecondCounter(); }
    void updateLatency();
    void finishRecording();

    class RenderThread;
    std::unique_ptr<RenderThread> renderThread;

    onirique::ChoirEngine engine;
    double sampleRate = 48000.0;
    int blockMax = 512;
    std::vector<float> mono, harmL, harmR;
    juce::AudioBuffer<float> dryDelay;    // voix retardée de la latence du moteur (mode LIVE)
    int dryDelayPos = 0, liveLatency = 0;
    std::atomic<bool> liveMode { true };
    juce::SmoothedValue<float> dryGain, wetGain, bypassMix;

    // clip et rendu : échangés sous verrou court ; le thread audio n'attend jamais (try-lock)
    mutable juce::SpinLock clipLock;
    std::shared_ptr<ChoirClip> clip;
    std::shared_ptr<const ChoirRender> render;

    // enregistrement en jouant le DAW
    std::atomic<bool> recArmed { false }, recActive { false }, recFinished { false };
    std::vector<float> recBuffer;
    std::atomic<int> recLength { 0 };
    std::atomic<double> recRate { 48000.0 }, recStart { 0.0 };

    std::atomic<bool> renderDirty { false }, renderBusy { false };
    std::atomic<juce::uint32> renderDirtyAt { 0 };

    std::atomic<double> playSeconds { 0.0 };
    std::atomic<bool> playing { false };
    std::array<std::atomic<float>, 1 + onirique::ChoirEngine::kVoices> frame {};
    std::array<std::atomic<float>, 2> peak {};

    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueTheChoirAudioProcessor)
};
