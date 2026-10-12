#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace onirique;

namespace
{
constexpr double kMaxClipSeconds = 600.0;     // 10 minutes
constexpr double kMaxRecordSeconds = 480.0;   // 8 minutes réservées en armant l'enregistrement

float dbToGain (float db) noexcept { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); }

// harmonies d'un clip : le même moteur que le temps réel, sortie avancée de sa latence (alignement exact)
std::shared_ptr<ChoirRender> renderClip (const ChoirClip& clip, const ChoirEngine::Settings& settings, const std::function<bool()>& shouldStop)
{
    auto out = std::make_shared<ChoirRender>();
    out->sampleRate = clip.sampleRate;
    out->startSeconds = clip.startSeconds;
    const int len = clip.voice.getNumSamples();
    out->harmonies.setSize (2, len);
    out->harmonies.clear();

    ChoirEngine eng;
    eng.prepare (clip.sampleRate);
    eng.setSettings (settings);
    const int lat = eng.getLatency();
    const int total = len + lat;
    constexpr int block = 2048;
    std::vector<float> in ((size_t) block), l ((size_t) block), r ((size_t) block);
    const float* src = clip.voice.getReadPointer (0);
    for (int pos = 0; pos < total; pos += block)
    {
        if (shouldStop()) return nullptr;
        const int n = juce::jmin (block, total - pos);
        for (int i = 0; i < n; ++i) in[(size_t) i] = pos + i < len ? src[pos + i] : 0.0f;
        eng.process (in.data(), l.data(), r.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const int dst = pos + i - lat;
            if (dst >= 0 && dst < len)
            {
                out->harmonies.setSample (0, dst, l[(size_t) i]);
                out->harmonies.setSample (1, dst, r[(size_t) i]);
            }
        }
    }

    // courbes de l'afficheur : hauteur chantée, hauteur de chaque voix, enveloppe
    PitchDetector det;
    det.prepare (clip.sampleRate);
    HarmonyMapper mapper;
    mapper.setScale (settings.key, settings.scale);
    const int hop = juce::jmax (64, (int) std::lround (clip.sampleRate * 0.01));
    out->hop = hop;
    const int frames = len / hop + 1;
    out->lead.assign ((size_t) frames, 0.0f);
    out->envelope.assign ((size_t) frames, 0.0f);
    for (auto& v : out->voices) v.assign ((size_t) frames, 0.0f);
    std::vector<float> window ((size_t) det.span(), 0.0f);
    float envMax = 1.0e-6f;
    for (int f = 0; f < frames; ++f)
    {
        if ((f & 255) == 0 && shouldStop()) return nullptr;
        const int centre = f * hop;
        const int start = centre - det.span() / 2;
        double e = 0.0;
        for (int j = 0; j < det.span(); ++j)
        {
            const int k = start + j;
            window[(size_t) j] = (k >= 0 && k < len) ? src[k] : 0.0f;
        }
        for (int j = -hop / 2; j < hop / 2; ++j)
        {
            const int k = centre + j;
            if (k >= 0 && k < len) e += (double) src[k] * src[k];
        }
        out->envelope[(size_t) f] = (float) std::sqrt (e / hop);
        envMax = juce::jmax (envMax, out->envelope[(size_t) f]);

        const auto res = det.analyse (window.data());
        if (! res.voiced) { mapper.reset(); continue; }
        const float midi = midiFromHz ((float) clip.sampleRate / res.period);
        const int note = mapper.track (midi);
        out->lead[(size_t) f] = midi;
        for (int v = 0; v < ChoirEngine::kVoices; ++v)
        {
            const auto& vs = settings.voices[(size_t) v];
            if (! vs.on) continue;
            const int shifted = mapper.shiftDegrees (note, vs.steps) + 12 * vs.octave;
            out->voices[(size_t) v][(size_t) f] = HarmonyMapper::targetPitch (midi, note, shifted, settings.tight);
        }
    }
    for (auto& e : out->envelope) e /= envMax;
    return out;
}
} // namespace

//==============================================================================
static std::pair<int, int> analyseKey (const ChoirRender&);

// Rendu des harmonies d'un clip en tâche de fond, relancé à chaque réglage (après 250 ms de calme).
class OniriqueTheChoirAudioProcessor::RenderThread : public juce::Thread
{
public:
    explicit RenderThread (OniriqueTheChoirAudioProcessor& p) : juce::Thread ("ONIRIQUE THE CHOIR render"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (60);
            if (! owner.renderDirty.load()) continue;
            if (juce::Time::getMillisecondCounter() - owner.renderDirtyAt.load() < 250) continue;

            owner.renderDirty = false;
            owner.renderBusy = true;
            std::shared_ptr<ChoirClip> c;
            {
                const juce::SpinLock::ScopedLockType sl (owner.clipLock);
                c = owner.clip;
            }
            if (c != nullptr)
            {
                auto r = renderClip (*c, owner.readSettings(), [this] { return threadShouldExit() || owner.renderDirty.load(); });
                if (r != nullptr)
                {
                    // voix tout juste chargée ou enregistrée : la tonalité détectée est proposée
                    if (owner.keyPending.exchange (false))
                    {
                        const auto [k, s] = analyseKey (*r);
                        owner.suggestScale = s;
                        owner.suggestKey = k;
                    }
                    std::shared_ptr<const ChoirRender> old;   // libéré ici, jamais dans le thread audio
                    const juce::SpinLock::ScopedLockType sl (owner.clipLock);
                    if (owner.clip == c) { old = std::move (owner.render); owner.render = std::move (r); }
                }
            }
            owner.renderBusy = false;
        }
    }

private:
    OniriqueTheChoirAudioProcessor& owner;
};

//==============================================================================
OniriqueTheChoirAudioProcessor::OniriqueTheChoirAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    formats.registerBasicFormats();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);
    liveMode = apvts.getRawParameterValue (ids::source)->load() < 0.5f;

    renderThread = std::make_unique<RenderThread> (*this);
    renderThread->startThread (juce::Thread::Priority::low);
    startTimerHz (20);
}

OniriqueTheChoirAudioProcessor::~OniriqueTheChoirAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    renderThread->stopThread (5000);   // le rendu s'interrompt à chaque bloc : arrêt immédiat
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool OniriqueTheChoirAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void OniriqueTheChoirAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    blockMax = juce::jmax (1, samplesPerBlock);
    engine.prepare (sr);
    liveLatency = engine.getLatency();
    mono.assign ((size_t) blockMax, 0.0f);
    harmL.assign ((size_t) blockMax, 0.0f);
    harmR.assign ((size_t) blockMax, 0.0f);
    dryDelay.setSize (2, liveLatency + blockMax + 1);
    dryDelay.clear();
    dryDelayPos = 0;
    for (auto* s : { &dryGain, &wetGain, &bypassMix }) s->reset (sr, 0.03);
    dryGain.setCurrentAndTargetValue (dbToGain (apvts.getRawParameterValue (ids::dry)->load()));
    wetGain.setCurrentAndTargetValue (dbToGain (apvts.getRawParameterValue (ids::wet)->load()));
    bypassMix.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    setLatencySamples (liveMode.load() ? liveLatency : 0);
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export
}

ChoirEngine::Settings OniriqueTheChoirAudioProcessor::readSettings() const
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ChoirEngine::Settings s;
    s.key = juce::jlimit (0, 11, (int) raw (ids::key));
    s.scale = juce::jlimit (0, kNumScales - 1, (int) raw (ids::scale));
    s.tight = raw (ids::tight) / 100.0f;
    s.humanize = raw (ids::humanize) / 100.0f;
    s.width = raw (ids::width) / 100.0f;
    for (int v = 0; v < ids::kHarmonyVoices; ++v)
    {
        auto& vs = s.voices[(size_t) v];
        vs.on = raw (ids::voiceOn[v]) > 0.5f;
        vs.steps = intervalSteps[juce::jlimit (0, (int) std::size (intervalSteps) - 1, (int) raw (ids::voiceInterval[v]))];
        vs.octave = juce::roundToInt (raw (ids::voiceOctave[v])) - 2;   // choix -2 .. +2
        vs.gain = dbToGain (raw (ids::voiceLevel[v]));
        vs.pan = raw (ids::voicePan[v]) / 100.0f;
    }
    s.voices[4] = { raw (ids::octDownOn) > 0.5f, 0, -1, dbToGain (raw (ids::octDownLvl)), 0.0f };
    s.voices[5] = { raw (ids::octUpOn) > 0.5f, 0, 1, dbToGain (raw (ids::octUpLvl)), 0.0f };
    s.model = choirModels[juce::jlimit (0, kNumModels - 1, (int) raw (ids::model))];
    s.mic = juce::jlimit (0, kNumMics - 1, (int) raw (ids::mic));
    s.room = raw (ids::room) / 100.0f;
    s.mono = raw (ids::stereo) > 0.5f;
    return s;
}

void OniriqueTheChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, n);
    if (n > blockMax) { mono.resize ((size_t) n); harmL.resize ((size_t) n); harmR.resize ((size_t) n); blockMax = n; }

    // position du DAW
    bool isPlaying = false;
    double timeSec = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying() || pos->getIsRecording();
            if (auto t = pos->getTimeInSeconds()) timeSec = *t;
            else if (auto ts = pos->getTimeInSamples()) timeSec = (double) *ts / sampleRate;
        }
    playing = isPlaying;
    playSeconds = timeSec;

    // voix mono (moyenne des entrées)
    const int chans = juce::jmax (1, juce::jmin (inCh, buffer.getNumChannels()));
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < chans; ++c) s += buffer.getSample (c, i);
        mono[(size_t) i] = s / (float) chans;
    }

    // enregistrement en jouant le DAW
    if (recArmed.load() && isPlaying)
    {
        if (! recActive.load())
        {
            recLength = 0;
            recRate = sampleRate;
            recStart = timeSec;
            recActive = true;
        }
        const int at = recLength.load();
        const int room = (int) recBuffer.size() - at;
        const int take = juce::jmin (n, room);
        if (take > 0) std::copy (mono.begin(), mono.begin() + take, recBuffer.begin() + at);
        recLength = at + juce::jmax (0, take);
    }
    else if (recActive.load())
    {
        recActive = false;
        recFinished = true;
    }

    dryGain.setTargetValue (dbToGain (apvts.getRawParameterValue (ids::dry)->load()));
    wetGain.setTargetValue (dbToGain (apvts.getRawParameterValue (ids::wet)->load()));
    bypassMix.setTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);

    const bool live = liveMode.load();
    std::fill (harmL.begin(), harmL.begin() + n, 0.0f);
    std::fill (harmR.begin(), harmR.begin() + n, 0.0f);

    if (live)
    {
        engine.setSettings (readSettings());
        engine.process (mono.data(), harmL.data(), harmR.data(), n);
        const auto f = engine.getFrame();
        frame[0] = f.lead;
        for (int v = 0; v < ChoirEngine::kVoices; ++v) frame[(size_t) v + 1] = f.voice[(size_t) v];

        // voix d'origine retardée de la latence du moteur (le DAW compense la latence annoncée)
        const int dl = dryDelay.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const int readPos = (dryDelayPos - liveLatency + dl) % dl;
            for (int c = 0; c < juce::jmin (2, outCh); ++c)
            {
                const int srcCh = juce::jmin (c, juce::jmax (0, inCh - 1));
                const float in = inCh > 0 ? buffer.getSample (srcCh, i) : 0.0f;
                dryDelay.setSample (c, dryDelayPos, in);
                buffer.setSample (c, i, dryDelay.getSample (c, readPos));
            }
            dryDelayPos = (dryDelayPos + 1) % dl;
        }
    }
    else
    {
        // mode CLIP : harmonies (et voix du fichier glissé) rejouées à la position du DAW
        // ▶ dans le plugin (DAW à l'arrêt) : le clip est rejoué depuis son début, voix comprise
        if (previewRestart.exchange (false)) previewIdx = 0.0;
        const bool preview = previewOn.load() && ! isPlaying;
        if (isPlaying && previewOn.load()) previewOn = false;   // le DAW reprend la main
        const juce::SpinLock::ScopedTryLockType sl (clipLock);
        if (sl.isLocked() && (isPlaying || preview) && render != nullptr)
        {
            const auto& rd = *render;
            const int len = rd.harmonies.getNumSamples();
            const float* hl = rd.harmonies.getReadPointer (0);
            const float* hr = rd.harmonies.getReadPointer (1);
            const bool playVoice = clip != nullptr && (clip->fromFile || preview) && clip->voice.getNumSamples() > 0;
            const float* voice = playVoice ? clip->voice.getReadPointer (0) : nullptr;
            const int voiceLen = voice != nullptr ? clip->voice.getNumSamples() : 0;
            const double step = rd.sampleRate / sampleRate;
            double idx = preview ? previewIdx : (timeSec - rd.startSeconds) * rd.sampleRate;
            if (preview)
            {
                previewIdx += step * n;
                previewSec = previewIdx / rd.sampleRate;
                if (previewIdx >= (double) len) { previewOn = false; previewIdx = 0.0; }
            }
            for (int i = 0; i < n; ++i, idx += step)
            {
                if (idx < 0.0 || idx >= (double) (len - 1)) continue;
                const int k = (int) idx;
                const float fr = (float) (idx - k);
                harmL[(size_t) i] = hl[k] + fr * (hl[k + 1] - hl[k]);
                harmR[(size_t) i] = hr[k] + fr * (hr[k + 1] - hr[k]);
                if (voice != nullptr && k + 1 < voiceLen)
                {
                    const float v = voice[k] + fr * (voice[k + 1] - voice[k]);
                    for (int c = 0; c < juce::jmin (2, outCh); ++c) buffer.addSample (c, i, v);
                }
            }
        }
        for (auto& f : frame) f = 0.0f;
    }

    // mélange : voix d'origine * DRY + harmonies * HARMONIES ; BYPASS = voix d'origine seule
    for (int i = 0; i < n; ++i)
    {
        const float d = dryGain.getNextValue(), w = wetGain.getNextValue(), b = bypassMix.getNextValue();
        const float dg = d + (1.0f - d) * b, wg = w * (1.0f - b);
        if (outCh >= 2)
        {
            buffer.setSample (0, i, buffer.getSample (0, i) * dg + harmL[(size_t) i] * wg);
            buffer.setSample (1, i, buffer.getSample (1, i) * dg + harmR[(size_t) i] * wg);
        }
        else if (outCh == 1)
        {
            buffer.setSample (0, i, buffer.getSample (0, i) * dg + 0.5f * (harmL[(size_t) i] + harmR[(size_t) i]) * wg);
        }
    }

    // sécurité : jamais de NaN / Inf vers le DAW
    for (int c = 0; c < outCh; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    license.process (buffer, getSampleRate(), isNonRealtime());

    for (int c = 0; c < juce::jmin (2, outCh); ++c)
    {
        const float m = buffer.getMagnitude (c, 0, n);
        float prev = peak[(size_t) c].load();
        while (m > prev && ! peak[(size_t) c].compare_exchange_weak (prev, m)) {}
    }
}

ChoirEngine::Frame OniriqueTheChoirAudioProcessor::liveFrame() const noexcept
{
    ChoirEngine::Frame f;
    f.lead = frame[0].load();
    for (int v = 0; v < ChoirEngine::kVoices; ++v) f.voice[(size_t) v] = frame[(size_t) v + 1].load();
    return f;
}

//==============================================================================
void OniriqueTheChoirAudioProcessor::parameterChanged (const juce::String& id, float value)
{
    if (id == ids::source)
    {
        liveMode = value < 0.5f;
        triggerAsyncUpdate();   // latence annoncée au DAW : depuis le thread de l'interface
        return;
    }
    if (id == ids::dry || id == ids::wet || id == ids::bypass) return;
    requestRender();
}

void OniriqueTheChoirAudioProcessor::handleAsyncUpdate() { updateLatency(); }

void OniriqueTheChoirAudioProcessor::updateLatency()
{
    setLatencySamples (liveMode.load() ? liveLatency : 0);
}

void OniriqueTheChoirAudioProcessor::timerCallback()
{
    if (recFinished.exchange (false))
        finishRecording();
}

void OniriqueTheChoirAudioProcessor::setRecordArmed (bool armed)
{
    if (armed)
    {
        const auto needed = (size_t) (kMaxRecordSeconds * juce::jmax (8000.0, sampleRate));
        if (recBuffer.size() < needed && ! recActive.load()) recBuffer.assign (needed, 0.0f);
    }
    recArmed = armed;
}

void OniriqueTheChoirAudioProcessor::finishRecording()
{
    const int len = recLength.load();
    if (len < (int) (0.2 * recRate.load())) return;   // moins de 0,2 s : ignoré

    auto c = std::make_shared<ChoirClip>();
    c->voice.setSize (1, len);
    c->voice.copyFrom (0, 0, recBuffer.data(), len);
    c->sampleRate = recRate.load();
    c->startSeconds = recStart.load();
    c->fromFile = false;
    c->name = "Recorded voice";
    setClip (c);
    recArmed = false;
}

bool OniriqueTheChoirAudioProcessor::loadFile (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported audio file: " + file.getFileName(); return false; }
    const auto total = (juce::int64) juce::jmin ((double) reader->lengthInSamples, kMaxClipSeconds * reader->sampleRate);
    if (total < 1) { error = "The file is empty."; return false; }

    juce::AudioBuffer<float> tmp ((int) reader->numChannels, (int) total);
    reader->read (&tmp, 0, (int) total, 0, true, true);
    auto c = std::make_shared<ChoirClip>();
    c->voice.setSize (1, (int) total);
    c->voice.clear();
    for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
        c->voice.addFrom (0, 0, tmp, ch, 0, (int) total, 1.0f / (float) tmp.getNumChannels());
    c->sampleRate = reader->sampleRate;
    c->startSeconds = 0.0;
    c->fromFile = true;
    c->name = file.getFileNameWithoutExtension();
    setClip (c);
    return true;
}

void OniriqueTheChoirAudioProcessor::setClip (std::shared_ptr<ChoirClip> c)
{
    std::shared_ptr<ChoirClip> oldClip;
    std::shared_ptr<const ChoirRender> oldRender;
    {
        const juce::SpinLock::ScopedLockType sl (clipLock);
        oldClip = std::move (clip);
        oldRender = std::move (render);
        clip = std::move (c);
    }
    const bool any = hasClip();
    previewOn = false;
    suggestKey = -1;            // nouvelle voix : sa tonalité sera proposée après le premier rendu
    keyPending = any;
    auto* src = apvts.getParameter (ids::source);
    src->setValueNotifyingHost (src->convertTo0to1 (any ? 1.0f : 0.0f));
    if (any) { renderDirty = true; renderDirtyAt = 0; }   // rendu tout de suite
}

void OniriqueTheChoirAudioProcessor::clearClip() { setClip (nullptr); }

bool OniriqueTheChoirAudioProcessor::hasClip() const
{
    const juce::SpinLock::ScopedLockType sl (clipLock);
    return clip != nullptr;
}

juce::String OniriqueTheChoirAudioProcessor::clipName() const
{
    const juce::SpinLock::ScopedLockType sl (clipLock);
    return clip != nullptr ? clip->name : juce::String();
}

std::shared_ptr<const ChoirRender> OniriqueTheChoirAudioProcessor::currentRender() const
{
    const juce::SpinLock::ScopedLockType sl (clipLock);
    return render;
}

// tonalité : profils de Krumhansl-Schmuckler sur la durée de chaque classe de hauteur chantée.
// Rend { tonique, gamme (0 majeure, 1 mineure) }, ou { -1, 0 } si rien n'est chanté.
static std::pair<int, int> analyseKey (const ChoirRender& rd)
{
    const auto* r = &rd;
    std::array<double, 12> hist {};
    double total = 0.0;
    for (size_t f = 0; f < r->lead.size(); ++f)
    {
        if (r->lead[f] <= 0.0f) continue;
        const int pc = ((int) std::lround (r->lead[f]) % 12 + 12) % 12;
        const double w = 0.2 + r->envelope[f];
        hist[(size_t) pc] += w;
        total += w;
    }
    if (total <= 0.0) return { -1, 0 };

    static constexpr double major[] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double minor[] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&hist] (const double* prof, int key)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += hist[(size_t) ((i + key) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(size_t) ((i + key) % 12)] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxy / std::sqrt (sxx * syy + 1e-12);
    };
    int bestKey = 0, bestScale = 0;
    double best = -2.0;
    for (int k = 0; k < 12; ++k)
    {
        if (const double c = corr (major, k); c > best) { best = c; bestKey = k; bestScale = 0; }
        if (const double c = corr (minor, k); c > best) { best = c; bestKey = k; bestScale = 1; }
    }
    return { bestKey, bestScale };
}

static void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
}

bool OniriqueTheChoirAudioProcessor::detectKey()
{
    const auto r = currentRender();
    if (r == nullptr) return false;
    const auto [k, s] = analyseKey (*r);
    if (k < 0) return false;
    setParam (apvts, ids::key, (float) k);
    setParam (apvts, ids::scale, (float) s);
    return true;
}

void OniriqueTheChoirAudioProcessor::applySuggestedKey()
{
    const int k = suggestKey.load();
    if (k < 0) return;
    setParam (apvts, ids::key, (float) k);
    setParam (apvts, ids::scale, (float) suggestScale.load());
}

void OniriqueTheChoirAudioProcessor::applyPreset (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    // tout le chœur repart des valeurs par défaut ; la source, la tonalité et le bypass ne bougent pas
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (id == ids::source || id == ids::key || id == ids::scale || id == ids::bypass) continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    for (const auto& [id, value] : list[(size_t) index].values)   // PresetValue { id, value }
        setParam (apvts, id, value);
    apvts.state.setProperty ("preset", index, nullptr);
}

int OniriqueTheChoirAudioProcessor::currentPreset() const
{
    return (int) apvts.state.getProperty ("preset", -1);
}

void OniriqueTheChoirAudioProcessor::setPreview (bool shouldPlay)
{
    if (shouldPlay && currentRender() == nullptr) return;
    if (shouldPlay)
    {
        setParam (apvts, ids::source, 1.0f);   // le clip passe en mode CLIP
        previewSec = 0.0;
        previewRestart = true;
    }
    previewOn = shouldPlay;
}

juce::File OniriqueTheChoirAudioProcessor::exportHarmonies (juce::String& error)
{
    const auto r = currentRender();
    if (r == nullptr) { error = "No harmonies yet: load or record a voice first."; return {}; }

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ONIRIQUE THE CHOIR");
    dir.createDirectory();
    auto name = clipName().trim();
    if (name.isEmpty()) name = "Voice";
    auto file = dir.getChildFile (juce::File::createLegalFileName (name + " - ONIRIQUE harmonies.wav"));
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) { error = "Cannot write " + file.getFullPathName(); return {}; }
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), r->sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) { error = "Cannot create the WAV file."; return {}; }
    stream.release();

    // silence jusqu'au début du clip : déposé au début du projet, le fichier tombe pile sur la voix
    const int lead = (int) std::lround (juce::jmax (0.0, r->startSeconds) * r->sampleRate);
    juce::AudioBuffer<float> silence (2, 8192);
    silence.clear();
    for (int left = lead; left > 0; left -= silence.getNumSamples())
        writer->writeFromAudioSampleBuffer (silence, 0, juce::jmin (left, silence.getNumSamples()));
    writer->writeFromAudioSampleBuffer (r->harmonies, 0, r->harmonies.getNumSamples());
    writer.reset();
    return file;
}

//==============================================================================
void OniriqueTheChoirAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    std::shared_ptr<ChoirClip> c;
    {
        const juce::SpinLock::ScopedLockType sl (clipLock);
        c = clip;
    }
    if (c != nullptr && c->voice.getNumSamples() > 0)
    {
        // la voix est gardée avec le projet (FLAC 24 bits, sans perte)
        juce::MemoryBlock audio;
        {
            juce::FlacAudioFormat flac;
            auto mos = std::make_unique<juce::MemoryOutputStream> (audio, false);
            std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (mos.get(), c->sampleRate, 1, 24, {}, 0));
            if (writer != nullptr)
            {
                mos.release();   // le writer possède désormais le flux
                writer->writeFromAudioSampleBuffer (c->voice, 0, c->voice.getNumSamples());
            }
        }
        auto* e = xml->createNewChildElement ("CLIP");
        e->setAttribute ("start", c->startSeconds);
        e->setAttribute ("fromFile", c->fromFile);
        e->setAttribute ("name", c->name);
        e->setAttribute ("rate", c->sampleRate);
        e->addTextElement (audio.toBase64Encoding());
    }
    copyXmlToBinary (*xml, dest);
}

void OniriqueTheChoirAudioProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr) return;

    std::shared_ptr<ChoirClip> c;
    if (auto* e = xml->getChildByName ("CLIP"))
    {
        juce::MemoryBlock audio;
        if (audio.fromBase64Encoding (e->getAllSubText().trim()))
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (audio, true), true));
            if (reader != nullptr && reader->lengthInSamples > 0)
            {
                c = std::make_shared<ChoirClip>();
                c->voice.setSize (1, (int) reader->lengthInSamples);
                reader->read (&c->voice, 0, (int) reader->lengthInSamples, 0, true, false);
                c->sampleRate = reader->sampleRate;
                c->startSeconds = e->getDoubleAttribute ("start");
                c->fromFile = e->getBoolAttribute ("fromFile");
                c->name = e->getStringAttribute ("name");
            }
        }
        xml->removeChildElement (e, true);
    }

    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    liveMode = apvts.getRawParameterValue (ids::source)->load() < 0.5f;

    {
        std::shared_ptr<ChoirClip> oldClip;
        std::shared_ptr<const ChoirRender> oldRender;
        const juce::SpinLock::ScopedLockType sl (clipLock);
        oldClip = std::move (clip);
        oldRender = std::move (render);
        clip = c;
    }
    if (c != nullptr) { renderDirty = true; renderDirtyAt = 0; }
    triggerAsyncUpdate();
}

juce::AudioProcessorEditor* OniriqueTheChoirAudioProcessor::createEditor()
{
    return new OniriqueTheChoirAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueTheChoirAudioProcessor();
}
