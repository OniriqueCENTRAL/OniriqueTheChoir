#include "PluginEditor.h"
#include <BinaryData.h>
#include "Presets.h"

using namespace onirique;

namespace
{
constexpr float kBaseW = 1480.0f, kBaseH = 900.0f;
constexpr int kMinW = 1000, kMaxW = 2400;
constexpr float kPi = juce::MathConstants<float>::pi;

// couleur de chaque voix dans l'afficheur (braise -> rose -> os), octaves en plus clair
const juce::Colour voiceColours[ChoirEngine::kVoices] {
    juce::Colour (0xffE23444), juce::Colour (0xffFF7A86), juce::Colour (0xffB8243A),
    juce::Colour (0xffFFB3BA), juce::Colour (0xff8A1C2C), juce::Colour (0xffFFD7DB)
};

juce::String intervalTooltip()
{
    return "Interval of this voice, in steps of the chosen scale (a 3RD can be major or minor depending on the note, "
           "like a real backing singer). Drag up/down or use the mouse wheel.";
}
} // namespace

float ui::baseWidth() noexcept { return kBaseW; }

//==============================================================================
void ActionButton::tick()
{
    const bool lit = isEnabled() && (hover || isMouseButtonDown());
    bool changed = ui::approach (glow, lit ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (on, active ? 1.0f : 0.0f, 0.2f);
    if (changed || active) repaint();
}

void ActionButton::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    const float beat = active ? 0.55f + 0.45f * std::sin (pulse) : 0.0f;

    g.setColour (juce::Colours::black.withAlpha (0.32f * alpha));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha ((0.12f * glow + 0.30f * on) * alpha));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (glow * 0.7f, on)).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    auto tr = r;
    if (text == "RECORD" || text.startsWith ("REC"))
    {
        // point rouge, qui bat quand l'enregistrement est armé
        const float d = r.getHeight() * 0.32f;
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + rad, r.getCentreY() });
        g.setColour (ui::ember.withAlpha ((0.35f + 0.5f * beat) * alpha));
        g.fillEllipse (dot.expanded (d * 0.6f * beat));
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.3f * beat).withMultipliedAlpha (alpha));
        g.fillEllipse (dot);
        tr = tr.withTrimmedLeft (rad * 1.2f);
    }
    g.setColour (ui::mist.withAlpha (alpha).interpolatedWith (ui::bone, glow));
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.44f, tr.getWidth() - 16.0f * u, text, true, 0.08f));
    g.drawText (text, tr, juce::Justification::centred, false);
}

//==============================================================================
void DragOutZone::tick()
{
    ready = processor.currentRender() != nullptr && ! processor.isRendering();
    bool changed = ui::approach (glow, (hover || dragging) && ready ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (avail, ready ? 1.0f : 0.0f, 0.15f);
    if (changed) repaint();
}

void DragOutZone::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = 12.0f * u;
    const float a = 0.35f + 0.65f * avail;

    g.setColour (ui::ember.withAlpha ((0.08f + 0.18f * glow) * avail));
    g.fillRoundedRectangle (r, rad);
    const float dash[] { 6.0f * u, 4.0f * u };
    juce::Path border;
    border.addRoundedRectangle (r.reduced (0.5f), rad);
    juce::Path dashed;
    juce::PathStrokeType (std::max (1.0f, 1.3f * u)).createDashedStroke (dashed, border, dash, 2);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.4f * avail + 0.6f * glow).withMultipliedAlpha (a));
    g.fillPath (dashed);

    // flèche vers le bas (vers le DAW)
    const float s = r.getHeight() * 0.32f;
    const auto c = juce::Point<float> (r.getX() + r.getHeight() * 0.55f, r.getCentreY());
    juce::Path arrow;
    arrow.startNewSubPath (c.x, c.y - s * 0.6f);
    arrow.lineTo (c.x, c.y + s * 0.5f);
    arrow.startNewSubPath (c.x - s * 0.4f, c.y + s * 0.1f);
    arrow.lineTo (c.x, c.y + s * 0.5f);
    arrow.lineTo (c.x + s * 0.4f, c.y + s * 0.1f);
    g.setColour (ui::ember.interpolatedWith (ui::bone, glow).withMultipliedAlpha (a));
    g.strokePath (arrow, juce::PathStrokeType (std::max (1.5f, 2.2f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tr = r.withTrimmedLeft (r.getHeight() * 1.05f).reduced (6.0f * u, 0.0f);
    const juce::String line1 = ready ? "DRAG HARMONIES TO YOUR DAW" : (processor.isRendering() ? "RENDERING..." : "HARMONIES TO DRAG");
    const juce::String line2 = ready ? "stereo WAV, drop it at the start of the project" : "load or record a voice first";
    g.setColour (ui::mist.withAlpha (a));
    g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, tr.getWidth(), line1, true, 0.1f));
    g.drawText (line1, tr.withTrimmedBottom (tr.getHeight() * 0.45f), juce::Justification::bottomLeft, false);
    g.setColour (ui::haze.withAlpha (a));
    g.setFont (ui::fitFont (ui::Face::mono, 10.5f * u, tr.getWidth(), line2));
    g.drawText (line2, tr.withTrimmedTop (tr.getHeight() * 0.58f), juce::Justification::topLeft, false);
}

void DragOutZone::mouseDrag (const juce::MouseEvent& e)
{
    if (! ready || dragging || e.getDistanceFromDragStart() < 6) return;
    juce::String error;
    const auto file = processor.exportHarmonies (error);
    if (! file.existsAsFile()) return;
    dragging = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [this] { dragging = false; });
    else
        dragging = false;
}

//==============================================================================
void ChoirDisplay::fitRange (float centre)
{
    ui::approach (centreShown, centre, 0.04f);
    lo = std::round (centreShown) - 15.0f;
    hi = std::round (centreShown) + 15.0f;
}

void ChoirDisplay::tick()
{
    render = processor.currentRender();
    const bool clipMode = processor.apvts.getRawParameterValue (ids::source)->load() > 0.5f;
    if (! clipMode)
    {
        history[(size_t) writeAt] = processor.liveFrame();
        writeAt = (writeAt + 1) % kHistory;
        // centre de la vue : moyenne de la voix chantée récente
        float sum = 0.0f;
        int count = 0;
        for (const auto& f : history) if (f.lead > 0.0f) { sum += f.lead; ++count; }
        if (count > 0) fitRange (sum / (float) count);
    }
    else if (render != nullptr)
    {
        std::vector<float> v;
        for (auto m : render->lead) if (m > 0.0f) v.push_back (m);
        if (! v.empty())
        {
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            fitRange (v[v.size() / 2]);
        }
    }
    ui::approach (dropGlow, dropHover ? 1.0f : 0.0f, 0.2f);
    repaint();
}

void ChoirDisplay::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    const auto plot = area.reduced (14.0f * u, 12.0f * u).withTrimmedLeft (34.0f * u);
    const bool clipMode = processor.apvts.getRawParameterValue (ids::source)->load() > 0.5f;

    // grille : notes de la gamme (tonique plus marquée), nom à gauche
    HarmonyMapper scaleMap;
    scaleMap.setScale ((int) processor.apvts.getRawParameterValue (ids::key)->load(),
                       (int) processor.apvts.getRawParameterValue (ids::scale)->load());
    const int keyIdx = (int) processor.apvts.getRawParameterValue (ids::key)->load();
    const float labelH = 12.0f * u;
    float lastLabelY = 1.0e9f;
    for (int m = (int) std::floor (hi); m >= (int) std::ceil (lo); --m)
    {
        const float y = yFor ((float) m, plot);
        const bool tonic = ((m - keyIdx) % 12 + 12) % 12 == 0;
        if (! scaleMap.inScale (m)) continue;
        g.setColour (ui::line.withMultipliedAlpha (tonic ? 2.2f : 1.0f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        // nom de la note là où il a la place ; la tonique toujours
        if (! tonic && std::abs (y - lastLabelY) < labelH * 1.1f) continue;
        g.setColour (tonic ? ui::mist.withAlpha (0.85f) : ui::haze.withAlpha (0.55f));
        g.setFont (ui::font (ui::Face::mono, 9.5f * u, tonic));
        g.drawText (noteName (m), juce::Rectangle<float> (area.getX() + 6.0f * u, y - labelH * 0.5f, 34.0f * u, labelH),
                    juce::Justification::centredLeft, false);
        lastLabelY = y;
    }

    auto drawCurve = [&] (auto valueAt, int count, juce::Colour c, float thick, float glowAmt)
    {
        juce::Path p;
        bool open = false;
        for (int i = 0; i < count; ++i)
        {
            const float m = valueAt (i);
            if (m <= 0.0f) { open = false; continue; }
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) juce::jmax (1, count - 1);
            const float y = juce::jlimit (plot.getY(), plot.getBottom(), yFor (m, plot));
            if (! open) { p.startNewSubPath (x, y); open = true; }
            else p.lineTo (x, y);
        }
        if (glowAmt > 0.0f) ui::strokeGlow (g, p, c, thick, glowAmt);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.expanded (2.0f).toNearestInt());

        if (! clipMode)
        {
            const auto at = [this] (int i) -> const ChoirEngine::Frame& { return history[(size_t) ((writeAt + i) % kHistory)]; };
            for (int v = ChoirEngine::kVoices - 1; v >= 0; --v)
                drawCurve ([&] (int i) { return at (i).voice[(size_t) v]; }, kHistory, voiceColours[v].withAlpha (0.9f), 2.2f * u, 0.5f);
            drawCurve ([&] (int i) { return at (i).lead; }, kHistory, ui::bone, 2.6f * u, 0.0f);
        }
        else if (render != nullptr)
        {
            const int frames = (int) render->lead.size();
            const int step = juce::jmax (1, frames / juce::jmax (1, (int) plot.getWidth()));   // un point par pixel
            const int count = frames / step;
            // enveloppe de la voix, en fond
            juce::Path env;
            env.startNewSubPath (plot.getX(), plot.getBottom());
            for (int i = 0; i < count; ++i)
                env.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) juce::jmax (1, count - 1),
                            plot.getBottom() - render->envelope[(size_t) (i * step)] * plot.getHeight() * 0.22f);
            env.lineTo (plot.getRight(), plot.getBottom());
            env.closeSubPath();
            g.setColour (ui::ember.withAlpha (0.10f));
            g.fillPath (env);

            for (int v = ChoirEngine::kVoices - 1; v >= 0; --v)
                drawCurve ([&] (int i) { return render->voices[(size_t) v][(size_t) (i * step)]; }, count, voiceColours[v].withAlpha (0.85f), 1.8f * u, 0.0f);
            drawCurve ([&] (int i) { return render->lead[(size_t) (i * step)]; }, count, ui::bone, 2.0f * u, 0.0f);

            // tête de lecture
            const double dur = (double) render->harmonies.getNumSamples() / render->sampleRate;
            const bool preview = processor.isPreviewing();
            const double t = preview ? processor.previewSeconds() : processor.playheadSeconds() - render->startSeconds;
            if ((preview || processor.hostPlaying()) && t >= 0.0 && t <= dur)
            {
                const float x = plot.getX() + plot.getWidth() * (float) (t / dur);
                g.setColour (ui::ember.withAlpha (0.25f));
                g.fillRect (juce::Rectangle<float> (x - 3.0f * u, plot.getY(), 6.0f * u, plot.getHeight()));
                g.setColour (ui::bone);
                g.fillRect (juce::Rectangle<float> (x - 0.75f * u, plot.getY(), 1.5f * u, plot.getHeight()));
            }
        }
    }

    // états vides et messages
    juce::String title, sub;
    if (processor.isRecording())
    {
        title = "RECORDING  " + juce::String (processor.recordedSeconds(), 1) + " s";
        sub = "stop your DAW to finish";
    }
    else if (processor.isRecordArmed())
    {
        title = "RECORD ARMED";
        sub = "press play in your DAW: the track is captured in sync";
    }
    else if (clipMode && render == nullptr)
    {
        title = processor.hasClip() ? "RENDERING THE HARMONIES..." : "DROP A VOCAL HERE";
        sub = processor.hasClip() ? "high-quality PSOLA, one voice at a time" : "or press RECORD and play your DAW";
    }
    else if (! clipMode && processor.liveFrame().lead <= 0.0f)
    {
        title = "LIVE";
        sub = "play your vocal track: the harmonies follow in real time  -  or drop a vocal file here";
    }
    if (title.isNotEmpty())
    {
        const auto box = plot.withSizeKeepingCentre (juce::jmin (plot.getWidth(), 620.0f * u), 70.0f * u);
        g.setColour (ui::mist.withAlpha (0.85f));
        g.setFont (ui::font (ui::Face::display, 17.0f * u, true, 0.12f));
        g.drawText (title, box.withTrimmedBottom (box.getHeight() * 0.5f), juce::Justification::centredBottom, false);
        g.setColour (ui::haze);
        g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, box.getWidth(), sub));
        g.drawText (sub, box.withTrimmedTop (box.getHeight() * 0.55f), juce::Justification::centredTop, false);
    }

    // fichier survolé
    if (dropGlow > 0.01f)
    {
        g.setColour (ui::ember.withAlpha (0.16f * dropGlow));
        g.fillRoundedRectangle (area, 12.0f * u);
        g.setColour (ui::ember.withAlpha (dropGlow));
        g.drawRoundedRectangle (area.reduced (2.0f * u), 12.0f * u, 2.0f * u);
        g.setColour (ui::bone.withAlpha (dropGlow));
        g.setFont (ui::font (ui::Face::display, 20.0f * u, true, 0.12f));
        g.drawText ("DROP TO LOAD THE VOICE", area, juce::Justification::centred, false);
    }
}

//==============================================================================
OniriqueTheChoirAudioProcessorEditor::OniriqueTheChoirAudioProcessorEditor (OniriqueTheChoirAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      sourceSel (p.apvts, ids::source, { "LIVE", "CLIP" },
                 { "LIVE: harmonizes what comes in, in real time (latency compensated by your DAW).",
                   "CLIP: plays harmonies rendered in high quality from the loaded or recorded voice, in sync with your DAW." }),
      modelSel (p.apvts, ids::model, { "STUDIO", "GOSPEL", "CHILDREN", "GANG" },
                { "STUDIO: one clean voice per harmony.",
                  "GOSPEL: three singers per harmony plus a unison section, loose and warm, in a hall.",
                  "CHILDREN: a children's choir: one octave up, with the smaller timbre of young voices.",
                  "GANG: a crowd of men shouting in unison, scattered attacks, gritty and wide." }),
      stereoSel (p.apvts, ids::stereo, { "ST", "MONO" }, { "Stereo choir.", "Mono choir, centred." }),
      micValue (p.apvts, ids::mic, "Microphone colour applied to the choir: drag up/down or use the mouse wheel."),
      roomKnob (p.apvts, ids::room, "ROOM", "0", "100", Layout::textBelow, "Room around the choir (from dry to a large hall).", lnf),
      keySel (p.apvts, ids::key, keyNames, {}),
      scaleValue (p.apvts, ids::scale, "Scale used for the harmonies: drag up/down or use the mouse wheel."),
      tightKnob (p.apvts, ids::tight, "TIGHT", "0", "100", Layout::textBelow,
                 "0 %: the harmonies keep the singer's vibrato and slides. 100 %: perfectly in tune notes.", lnf),
      humanizeKnob (p.apvts, ids::humanize, "HUMANIZE", "0", "100", Layout::textBelow,
                    "Small timing and tuning differences between the voices: a real choir instead of clones.", lnf),
      widthKnob (p.apvts, ids::width, "WIDTH", "0", "100", Layout::textBelow, "Stereo spread of the harmony voices.", lnf),
      octDownKnob (p.apvts, ids::octDownLvl, "OCTAVE DOWN", "-inf", "+6", Layout::horizontal, "Level of the voice one octave below.", lnf),
      octUpKnob (p.apvts, ids::octUpLvl, "OCTAVE UP", "-inf", "+6", Layout::horizontal, "Level of the voice one octave above.", lnf),
      dryKnob (p.apvts, ids::dry, "DRY", "-inf", "+6", Layout::textBelow, "Level of the original voice.", lnf),
      wetKnob (p.apvts, ids::wet, "HARMONIES", "-inf", "+6", Layout::textBelow, "Level of all the harmonies.", lnf),
      meter ([&p] (int ch) { return p.consumePeak (ch); })
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    setOpaque (true);

    sourceSel.setTitle ("SOURCE");
    for (auto* c : std::initializer_list<juce::Component*> { &sourceSel, &bypassButton, &display, &recordButton, &loadButton, &clearButton,
                                                             &detectButton, &dragOut, &keySel, &scaleValue, &tightKnob, &humanizeKnob,
                                                             &widthKnob, &octDownPower, &octUpPower, &octDownKnob, &octUpKnob,
                                                             &dryKnob, &wetKnob, &meter })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &sourceSel, &bypassButton, &display, &recordButton, &loadButton, &clearButton,
                                                      &detectButton, &dragOut, &keySel, &scaleValue, &tightKnob, &humanizeKnob,
                                                      &widthKnob, &octDownPower, &octUpPower, &octDownKnob, &octUpKnob,
                                                      &dryKnob, &wetKnob, &meter })
        animated.push_back (a);
    keySel.setTitle ("KEY");
    scaleValue.setPrefix ("SCALE  ");
    modelSel.setTitle ("MODEL", "Type of choir.");
    stereoSel.setTitle ("OUT");
    micValue.setPrefix ("MIC  ");
    for (auto* c : std::initializer_list<juce::Component*> { &previewButton, &presetPrev, &presetName, &presetNext, &suggestButton,
                                                             &modelSel, &stereoSel, &micValue, &roomKnob })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &previewButton, &presetPrev, &presetName, &presetNext, &suggestButton,
                                                      &modelSel, &stereoSel, &micValue, &roomKnob })
        animated.push_back (a);

    previewButton.onClick = [this] { processor.setPreview (! processor.isPreviewing()); };
    suggestButton.setActive (true);
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { choosePreset(); };
    suggestButton.onClick = [this]
    {
        processor.applySuggestedKey();
        showMessage ("Key applied: " + keyNames[juce::jlimit (0, 11, processor.suggestedKey())] + " "
                     + juce::String (scales[processor.suggestedScale()].name));
    };

    bypassButton.setTooltip ("Bypass: original voice only (latency kept).");
    bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, ids::bypass, bypassButton);
    octDownPower.setTooltip ("Double the voice one octave below.");
    octUpPower.setTooltip ("Double the voice one octave above.");
    octDownAtt = std::make_unique<ButtonAttachment> (p.apvts, ids::octDownOn, octDownPower);
    octUpAtt = std::make_unique<ButtonAttachment> (p.apvts, ids::octUpOn, octUpPower);

    for (int v = 0; v < ids::kHarmonyVoices; ++v)
    {
        voicePower[(size_t) v].setTooltip ("Turn voice " + juce::String (v + 1) + " on/off.");
        voicePowerAtt[(size_t) v] = std::make_unique<ButtonAttachment> (p.apvts, ids::voiceOn[v], voicePower[(size_t) v]);
        intervalValue[(size_t) v] = std::make_unique<DragValue> (p.apvts, ids::voiceInterval[v], intervalTooltip());
        octaveSel[(size_t) v] = std::make_unique<SegmentSelector> (p.apvts, ids::voiceOctave[v], juce::StringArray { "-2", "-1", "0", "+1", "+2" },
                                                                   juce::StringArray {});
        octaveSel[(size_t) v]->setTitle ("OCT", "Octave of this voice.");
        levelKnob[(size_t) v] = std::make_unique<KnobComponent> (p.apvts, ids::voiceLevel[v], "LEVEL", "-inf", "+6", Layout::textBelow,
                                                                 "Level of this voice.", lnf);
        panKnob[(size_t) v] = std::make_unique<KnobComponent> (p.apvts, ids::voicePan[v], "PAN", "L", "R", Layout::textBelow,
                                                               "Position of this voice in the stereo field.", lnf, true);
        for (auto* c : std::initializer_list<juce::Component*> { &voicePower[(size_t) v], intervalValue[(size_t) v].get(), octaveSel[(size_t) v].get(),
                                                                 levelKnob[(size_t) v].get(), panKnob[(size_t) v].get() })
            addAndMakeVisible (c);
        for (auto* a : std::initializer_list<Animated*> { &voicePower[(size_t) v], intervalValue[(size_t) v].get(), octaveSel[(size_t) v].get(),
                                                          levelKnob[(size_t) v].get(), panKnob[(size_t) v].get() })
            animated.push_back (a);
    }

    recordButton.onClick = [this]
    {
        if (processor.isRecording()) { processor.setRecordArmed (false); return; }
        processor.setRecordArmed (! processor.isRecordArmed());
    };
    loadButton.onClick = [this] { chooseFile(); };
    clearButton.onClick = [this] { processor.clearClip(); };
    detectButton.onClick = [this]
    {
        if (! processor.detectKey()) showMessage ("Load or record a voice first.");
        else showMessage ("Key detected: " + keyNames[(int) processor.apvts.getRawParameterValue (ids::key)->load()] + " "
                          + juce::String (scales[(int) processor.apvts.getRawParameterValue (ids::scale)->load()].name));
    };

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (kBaseW / kBaseH);
    const int w = juce::jlimit (kMinW, kMaxW, (int) p.apvts.state.getProperty ("uiWidth", 1260));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));

    veils.draw = [this] (juce::Graphics& g) { paintVeils (g); };
    addAndMakeVisible (veils);

    // Licence : pastille et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE THE CHOIR", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

    startTimerHz (60);
}

OniriqueTheChoirAudioProcessorEditor::~OniriqueTheChoirAudioProcessorEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

float OniriqueTheChoirAudioProcessorEditor::scale() const noexcept { return (float) getWidth() / kBaseW; }

void OniriqueTheChoirAudioProcessorEditor::showMessage (const juce::String& text)
{
    message = text;
    messageFrames = 240;
    repaint (statusRect.getSmallestIntegerContainer().expanded (4));
}

void OniriqueTheChoirAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    recordButton.setActive (processor.isRecordArmed() || processor.isRecording());
    recordButton.setPulse ((float) clock * (processor.isRecording() ? 9.0f : 4.0f));
    recordButton.setText (processor.isRecording() ? "STOP" : "RECORD");
    const bool clip = processor.hasClip();
    clearButton.setEnabled (clip);
    const bool rendered = processor.currentRender() != nullptr;
    detectButton.setEnabled (rendered);
    previewButton.setEnabled (rendered);
    previewButton.setActive (processor.isPreviewing());
    previewButton.setText (processor.isPreviewing() ? "STOP" : "PLAY");

    const int preset = processor.currentPreset();
    const auto& presets = factoryPresets();
    presetName.setText (preset >= 0 && preset < (int) presets.size()
                            ? juce::String (presets[(size_t) preset].category) + "  -  " + presets[(size_t) preset].name
                            : juce::String ("PRESETS"));

    // tonalité proposée : visible tant qu'elle diffère des réglages
    const int sk = processor.suggestedKey(), ss = processor.suggestedScale();
    const bool differs = sk >= 0 && (sk != (int) processor.apvts.getRawParameterValue (ids::key)->load()
                                     || ss != (int) processor.apvts.getRawParameterValue (ids::scale)->load());
    suggestButton.setVisible (differs);
    if (differs) suggestButton.setText ("SUGGESTED KEY  " + keyNames[sk] + " " + scales[juce::jlimit (0, kNumScales - 1, ss)].name + "  -  APPLY");

    for (auto* a : animated) a->tick();

    // voiles des voix coupées : redessinés quand un bouton power change
    int powers = 0;
    for (int v = 0; v < ids::kHarmonyVoices; ++v) powers |= voicePower[(size_t) v].getToggleState() ? (1 << v) : 0;
    if (powers != lastPowers) { lastPowers = powers; veils.repaint(); }

    if (messageFrames > 0 && --messageFrames == 0) repaint (statusRect.getSmallestIntegerContainer().expanded (4));
    repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
}

bool OniriqueTheChoirAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf;wma")) return true;
    return false;
}

void OniriqueTheChoirAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    display.setDropHover (false);
    for (const auto& f : files)
    {
        juce::String error;
        if (processor.loadFile (juce::File (f), error)) { showMessage ("Loaded: " + juce::File (f).getFileName()); return; }
        showMessage (error);
    }
}

void OniriqueTheChoirAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a vocal", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String error;
        showMessage (processor.loadFile (f, error) ? "Loaded: " + f.getFileName() : error);
    });
}

void OniriqueTheChoirAudioProcessorEditor::choosePreset()
{
    const auto& presets = factoryPresets();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lnf);
    juce::String category;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (category != presets[(size_t) i].category)
        {
            category = presets[(size_t) i].category;
            menu.addSectionHeader (category);
        }
        menu.addItem (i + 1, presets[(size_t) i].name, true, i == processor.currentPreset());
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName).withMinimumWidth (presetName.getWidth()),
                        [safe = juce::Component::SafePointer<OniriqueTheChoirAudioProcessorEditor> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0) safe->processor.applyPreset (result - 1);
                        });
}

void OniriqueTheChoirAudioProcessorEditor::stepPreset (int delta)
{
    const int count = (int) factoryPresets().size();
    const int now = processor.currentPreset();
    processor.applyPreset (now < 0 ? (delta > 0 ? 0 : count - 1) : (now + delta + count) % count);
}

//==============================================================================
void OniriqueTheChoirAudioProcessorEditor::resized()
{
    backdropLayer = {};   // fond et cartes redessinés à la nouvelle taille au prochain paint()
    const float u = scale();
    const float W = (float) getWidth();
    veils.setBounds (getLocalBounds());
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, { 466, 30, 248, 26 });
    processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);

    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u); };
    auto place = [] (juce::Component& c, juce::Rectangle<float> r) { c.setBounds (r.toNearestInt()); };

    // en-tête
    logoRect = R (22, 14, 64, 58);
    place (bypassButton, R (kBaseW - 22 - 120, 26, 120, 36));
    place (sourceSel, R (kBaseW - 22 - 120 - 14 - 250, 26, 250, 36));
    place (presetPrev, R (732, 26, 36, 36));
    place (presetName, R (774, 26, 250, 36));
    place (presetNext, R (1030, 26, 36, 36));
    statusRect = R (22, 76, 900, 16);

    const float m = 22.0f, gap = 16.0f;
    sections.clear();

    // VOICE
    const float topY = 96.0f, topH = 380.0f, voiceW = 1000.0f;
    sections.push_back ({ "VOICE", R (m, topY, voiceW, topH) });
    const float bx = m + voiceW - 16.0f;
    place (detectButton, R (bx - 120, topY + 8, 120, 28));
    place (clearButton, R (bx - 120 - 10 - 86, topY + 8, 86, 28));
    place (loadButton, R (bx - 120 - 10 - 86 - 10 - 86, topY + 8, 86, 28));
    place (recordButton, R (bx - 120 - 10 - 86 - 10 - 86 - 10 - 116, topY + 8, 116, 28));
    place (previewButton, R (bx - 120 - 10 - 86 - 10 - 86 - 10 - 116 - 10 - 96, topY + 8, 96, 28));
    place (display, R (m + 14, topY + 44, voiceW - 28, topH - 44 - 66));
    place (dragOut, R (m + 14, topY + topH - 60, voiceW - 28, 48));

    // KEY
    const float kx = m + voiceW + gap, kw = W / u - m - kx;
    sections.push_back ({ "KEY & SCALE", R (kx, topY, kw, topH) });
    place (keySel, R (kx + 14, topY + 46, kw - 28, 34));
    place (scaleValue, R (kx + 14, topY + 92, kw - 28, 32));
    place (suggestButton, R (kx + 14, topY + 134, kw - 28, 30));
    const float knobW = (kw - 28) / 3.0f;
    place (tightKnob, R (kx + 14, topY + 180, knobW, 190));
    place (humanizeKnob, R (kx + 14 + knobW, topY + 180, knobW, 190));
    place (widthKnob, R (kx + 14 + 2 * knobW, topY + 180, knobW, 190));

    // VOICES
    const float vy = topY + topH + gap, vh = 232.0f;
    const float vw = (kBaseW - 2 * m - 3 * gap) / 4.0f;
    for (int v = 0; v < ids::kHarmonyVoices; ++v)
    {
        const float x = m + (float) v * (vw + gap);
        sections.push_back ({ "VOICE " + juce::String (v + 1), R (x, vy, vw, vh), &voicePower[(size_t) v] });
        place (voicePower[(size_t) v], R (x + vw - 40, vy + 6, 28, 28));
        place (*intervalValue[(size_t) v], R (x + 14, vy + 44, vw - 28, 30));
        place (*octaveSel[(size_t) v], R (x + 14, vy + 82, vw - 28, 30));
        place (*levelKnob[(size_t) v], R (x + 14, vy + 118, (vw - 28) * 0.5f, 106));
        place (*panKnob[(size_t) v], R (x + 14 + (vw - 28) * 0.5f, vy + 118, (vw - 28) * 0.5f, 106));
    }

    // CHOIR, OCTAVES, MIX, OUTPUT
    const float by = vy + vh + gap, bh = kBaseH - by - m;
    const float cw = 520.0f, ow = 380.0f, mw = 330.0f;
    const float ox = m + cw + gap, mx = ox + ow + gap, outX = mx + mw + gap, outW = kBaseW - m - outX;
    sections.push_back ({ "CHOIR", R (m, by, cw, bh) });
    place (modelSel, R (m + 14, by + 44, cw - 28, 34));
    place (micValue, R (m + 14, by + 88, (cw - 40) * 0.5f, 32));
    place (stereoSel, R (m + 26 + (cw - 40) * 0.5f, by + 88, (cw - 40) * 0.5f, 32));
    sections.push_back ({ "OCTAVES", R (ox, by, ow, bh) });
    place (octDownPower, R (ox + 16, by + 50, 26, 26));
    place (octDownKnob, R (ox + 50, by + 44, ow - 66, 38));
    place (octUpPower, R (ox + 16, by + 96, 26, 26));
    place (octUpKnob, R (ox + 50, by + 90, ow - 66, 38));
    sections.push_back ({ "MIX", R (mx, by, mw, bh) });
    const float mk = (mw - 24) / 3.0f;
    place (dryKnob, R (mx + 12, by + 34, mk, bh - 40));
    place (wetKnob, R (mx + 12 + mk, by + 34, mk, bh - 40));
    place (roomKnob, R (mx + 12 + 2 * mk, by + 34, mk, bh - 40));
    sections.push_back ({ "OUTPUT", R (outX, by, outW, bh) });
    place (meter, R (outX + outW * 0.5f - 40, by + 38, 80, bh - 50));

    for (auto& k : { &tightKnob, &humanizeKnob, &widthKnob }) k->setMaxDiameter (96.0f * u);
    for (int v = 0; v < ids::kHarmonyVoices; ++v) { levelKnob[(size_t) v]->setMaxDiameter (62.0f * u); panKnob[(size_t) v]->setMaxDiameter (62.0f * u); }
    for (auto& k : { &dryKnob, &wetKnob, &roomKnob }) k->setMaxDiameter (58.0f * u);

    if (logo.isValid())
    {
        // logo pré-réduit (net) + version floutée pour la lueur, recalculés seulement au redimensionnement
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * (float) logo.getWidth() / (float) logo.getHeight()), lh,
                                    juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true, juce::SoftwareImageType());
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true, juce::SoftwareImageType());
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }
}

//==============================================================================
void OniriqueTheChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float u = scale();

    // fond et cartes : images prêtes, à la résolution réelle de l'écran (Retina : x2)
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
        rebuildLayers (ps);
    const auto toLogical = juce::AffineTransform::scale (1.0f / layersScale);
    g.setOpacity (1.0f);
    g.drawImageTransformed (backdropLayer, toLogical);
    g.drawImageTransformed (cardsLayer, toLogical);

    // logo : lueur braise qui respire
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin ((float) clock * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        const auto lr = logoRect.withSizeKeepingCentre (logoRect.getHeight() * (float) logoScaled.getWidth() / (float) logoScaled.getHeight(), logoRect.getHeight());
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, lr.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, lr, juce::RectanglePlacement::stretchToFit);
    }

    // point braise des cartes de voix : suit le bouton power
    for (const auto& s : sections)
    {
        const bool on = s.power == nullptr || s.power->getToggleState();
        const float dot = 6.0f * u;
        const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ s.bounds.getX() + 20.0f * u, s.bounds.getY() + 19.0f * u });
        g.setColour (ui::ember.withAlpha (on ? 0.35f : 0.0f));
        g.fillEllipse (dotR.expanded (dot * 0.7f));
        g.setColour (on ? ui::ember : ui::haze.withAlpha (0.5f));
        g.fillEllipse (dotR);
    }

    // ligne d'état : clip chargé, message
    juce::String status = message;
    if (status.isEmpty() || messageFrames == 0)
    {
        status = processor.hasClip() ? "CLIP: " + processor.clipName() : juce::String ("NO CLIP LOADED");
        if (processor.isRendering()) status << "  -  RENDERING";
    }
    g.setColour (ui::haze);
    g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, statusRect.getWidth(), status));
    g.drawText (status, statusRect.translated (logoRect.getRight() + 14.0f * u - statusRect.getX(), 0.0f), juce::Justification::centredLeft, false);
}

// voix coupée : voile nuit par-dessus le contenu de sa carte (titre et bouton power restent nets)
void OniriqueTheChoirAudioProcessorEditor::paintVeils (juce::Graphics& g)
{
    const float u = scale();
    for (const auto& s : sections)
    {
        if (s.power == nullptr || s.power->getToggleState()) continue;
        g.setColour (ui::deep.withAlpha (0.62f));
        g.fillRoundedRectangle (s.bounds.reduced (1.0f).withTrimmedTop (38.0f * u), 14.0f * u);
    }
}

void OniriqueTheChoirAudioProcessorEditor::rebuildLayers (float pixelScale)
{
    layersScale = pixelScale;
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale));
    const int h = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    const auto toPixels = juce::AffineTransform::scale (pixelScale);

    backdropLayer = juce::Image (juce::Image::RGB, w, h, false);
    {
        juce::Graphics bg (backdropLayer);
        bg.addTransform (toPixels);
        paintBackdrop (bg);
    }
    cardsLayer = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics cg (cardsLayer);
        cg.addTransform (toPixels);
        paintCards (cg);
    }
}

// ---- fond : sang -> nuit, halo braise en haut à droite, titre
void OniriqueTheChoirAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (ui::night);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.85f), 0.0f, 0.0f, ui::night.withAlpha (0.0f), 0.0f, H * 0.62f, false));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.20f), W * 0.74f, -40.0f * u,
                                             ui::ember.withAlpha (0.0f), W * 0.74f + W * 0.42f, -40.0f * u, true));
    g.fillRect (getLocalBounds());

    const float x = logoRect.getRight() + 14.0f * u;
    auto f1 = ui::font (ui::Face::display, 30.0f * u, true, 0.10f);
    auto f2 = ui::font (ui::Face::display, 30.0f * u, false, 0.10f);
    const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
    const float w2 = ui::textWidth (f2, "THE CHOIR") + 2.0f;
    const float y = 14.0f * u, hh = 36.0f * u;
    g.setFont (f1);
    g.setColour (ui::ember.withAlpha (0.35f));
    g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y + 1.0f * u, w1, hh), juce::Justification::centredLeft, false);
    g.setColour (ui::mist);
    g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
    g.setFont (f2);
    g.setColour (ui::haze);
    g.drawText ("THE CHOIR", juce::Rectangle<float> (x + w1 + 10.0f * u, y, w2, hh), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.withAlpha (0.7f));
    g.setFont (ui::font (ui::Face::mono, 10.5f * u, false, 0.18f));
    g.drawText ("HARMONIES  -  IN KEY  -  PSOLA", juce::Rectangle<float> (x, y + 36.0f * u, 400.0f * u, 18.0f * u), juce::Justification::centredLeft, false);
}

// ---- cartes : verre sombre, filet fin, ombre portée, titre
void OniriqueTheChoirAudioProcessorEditor::paintCards (juce::Graphics& g)
{
    const float u = scale();
    for (const auto& s : sections)
    {
        const auto r = s.bounds;
        const float rad = 16.0f * u;
        juce::Path card;
        card.addRoundedRectangle (r, rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (28.0f * u), { 0, juce::roundToInt (12.0f * u) })
            .drawForPath (g, card);
        g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.94f), 0.0f, r.getY(), ui::deep.withAlpha (0.96f), 0.0f, r.getBottom(), false));
        g.fillPath (card);
        g.setColour (ui::line);
        g.strokePath (card, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.5f * u), r.getX() + rad, r.getRight() - rad);

        const float maxW = std::min (r.getWidth() - (s.power != nullptr ? 90.0f : 60.0f) * u, 300.0f * u);
        g.setColour (ui::mist.withAlpha (0.92f));
        g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, maxW, s.title, true, 0.18f));
        g.drawText (s.title, juce::Rectangle<float> (r.getX() + 32.0f * u, r.getY() + 8.0f * u, maxW, 22.0f * u),
                    juce::Justification::centredLeft, false);
    }
}
