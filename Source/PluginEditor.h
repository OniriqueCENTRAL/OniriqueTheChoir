#pragma once

#include "PluginProcessor.h"
#include "UI/OniriqueUI.h"
#include "License/LicenseOverlay.h"

// Bouton d'action (LOAD, CLEAR, DETECT KEY...) : pastille au liseré fin, braise au survol.
class ActionButton : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    ActionButton (const juce::String& label, const juce::String& tip) : text (label) { setTooltip (tip); }
    std::function<void()> onClick;
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }   // état allumé (ex. RECORD armé)
    void setPulse (float p) { pulse = p; }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }

private:
    juce::String text;
    bool hover = false, active = false;
    float glow = 0.0f, on = 0.0f, pulse = 0.0f;
};

// Zone « glisser » : un glisser-déposer vers le DAW emporte le fichier WAV des harmonies.
class DragOutZone : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit DragOutZone (OniriqueTheChoirAudioProcessor& p) : processor (p)
    {
        setTooltip ("Drag the generated harmonies into your DAW (stereo WAV, 24-bit). Drop the file at the very start "
                    "of the project: it lines up exactly with the voice.");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }
    void mouseDrag (const juce::MouseEvent&) override;

private:
    OniriqueTheChoirAudioProcessor& processor;
    bool hover = false, ready = false, dragging = false;
    float glow = 0.0f, avail = 0.0f;
};

// Afficheur : hauteur de la voix (blanc) et de chaque harmonie (braise), sur une grille des notes de la gamme.
// LIVE : les 4 dernières secondes défilent. CLIP : tout le clip, avec la tête de lecture du DAW.
class ChoirDisplay : public juce::Component, public Animated
{
public:
    explicit ChoirDisplay (OniriqueTheChoirAudioProcessor& p) : processor (p) {}
    void setDropHover (bool h) { dropHover = h; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    float yFor (float midi, juce::Rectangle<float> r) const noexcept { return r.getBottom() - (midi - lo) / (hi - lo) * r.getHeight(); }
    void fitRange (float centre);

    OniriqueTheChoirAudioProcessor& processor;
    static constexpr int kHistory = 240;   // 4 s à 60 images/s
    std::array<onirique::ChoirEngine::Frame, kHistory> history {};
    int writeAt = 0;
    float lo = 48.0f, hi = 76.0f, centreShown = 62.0f;
    bool dropHover = false;
    float dropGlow = 0.0f;
    std::shared_ptr<const ChoirRender> render;
};

class OniriqueTheChoirAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             public juce::FileDragAndDropTarget,
                                             public juce::DragAndDropContainer,
                                             private juce::Timer
{
public:
    explicit OniriqueTheChoirAudioProcessorEditor (OniriqueTheChoirAudioProcessor&);
    ~OniriqueTheChoirAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintVeils (juce::Graphics&);   // voiles des voix coupées, dessinés par la couche « veils »
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { display.setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override { display.setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using Layout = KnobComponent::Layout;

    struct Section
    {
        juce::String title;
        juce::Rectangle<float> bounds;
        PowerButton* power = nullptr;
    };

    void timerCallback() override;
    float scale() const noexcept;
    void paintBackdrop (juce::Graphics&);
    void paintCards (juce::Graphics&);
    void rebuildLayers (float pixelScale);
    void chooseFile();
    void showMessage (const juce::String& text);

    OniriqueTheChoirAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };
    std::vector<Animated*> animated;
    std::vector<Section> sections;

    // en-tête
    SegmentSelector sourceSel;
    PillToggle bypassButton { "BYPASS", PillToggle::Style::small };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // VOICE
    ChoirDisplay display { processor };
    ActionButton recordButton { "RECORD", "Arm, then play your DAW: the track is captured into THE CHOIR, in sync with the timeline. "
                                          "Stop the DAW to finish: the harmonies are then rendered in high quality." };
    ActionButton loadButton { "LOAD", "Load a vocal file (WAV, AIFF, FLAC, MP3...). You can also drag a file onto the display." };
    ActionButton clearButton { "CLEAR", "Remove the voice from the plugin and go back to LIVE." };
    ActionButton detectButton { "DETECT KEY", "Detect the key of the voice and set KEY and SCALE." };
    DragOutZone dragOut { processor };

    // KEY
    SegmentSelector keySel;
    DragValue scaleValue;
    KnobComponent tightKnob, humanizeKnob, widthKnob;

    // voix
    std::array<PowerButton, onirique::ids::kHarmonyVoices> voicePower;
    std::array<std::unique_ptr<ButtonAttachment>, onirique::ids::kHarmonyVoices> voicePowerAtt;
    std::array<std::unique_ptr<DragValue>, onirique::ids::kHarmonyVoices> intervalValue;
    std::array<std::unique_ptr<SegmentSelector>, onirique::ids::kHarmonyVoices> octaveSel;
    std::array<std::unique_ptr<KnobComponent>, onirique::ids::kHarmonyVoices> levelKnob, panKnob;

    // OCTAVES, MIX, OUTPUT
    PowerButton octDownPower, octUpPower;
    std::unique_ptr<ButtonAttachment> octDownAtt, octUpAtt;
    KnobComponent octDownKnob, octUpKnob, dryKnob, wetKnob;
    LevelMeter meter;

    // couche transparente au-dessus des réglages (sous la licence) : voiles des voix coupées
    struct Layer : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Layer() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    } veils;

    juce::Image logo, logoScaled, logoGlow;
    juce::Rectangle<float> logoRect, statusRect;
    juce::Image backdropLayer, cardsLayer;
    float layersScale = 0.0f;
    double clock = 0.0;
    int lastPowers = -1;
    juce::String message;
    int messageFrames = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueTheChoirAudioProcessorEditor)
};
