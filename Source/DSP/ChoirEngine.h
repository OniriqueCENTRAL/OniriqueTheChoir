#pragma once

#include "Harmony.h"
#include "PitchDetector.h"
#include <array>
#include <atomic>

namespace onirique
{
// Moteur de chœur : une voix chantée (mono) -> un chœur (stéréo).
//
// Décalage de hauteur TD-PSOLA, synchrone à la période :
//  1. la hauteur est mesurée toutes les ~5 ms (YIN) ;
//  2. des marques sont posées sur chaque période de la voix, calées sur le pic de chaque cycle glottal ;
//  3. pour chaque choriste, des grains de deux périodes (fenêtre de Hann) sont prélevés autour de ces marques
//     et recollés à l'espacement de la nouvelle période. Le timbre (formants) reste celui de la voix ; il peut
//     être déplacé en rééchantillonnant chaque grain (voix d'enfant plus petite, voix d'homme plus grande).
// Les consonnes (parties non voisées) sont recopiées sans transposition, plus bas dans le chœur.
//
// Chaque voix d'harmonie est chantée par plusieurs choristes (selon le type de chœur), chacun avec son retard,
// son léger désaccord, son timbre et sa place dans l'image stéréo. Le chœur passe ensuite par la couleur du
// micro, une saturation (cris) et une salle.
//
// Tout se fait sur une ligne de temps retardée de getLatency() échantillons, le temps de connaître la
// période suivante. Le même code sert au temps réel et au rendu d'un clip (sortie avancée de la latence).
class ChoirEngine
{
public:
    static constexpr int kVoices = 6;            // 4 voix d'harmonie + octave en dessous + octave au-dessus
    static constexpr int kSections = kVoices + 1;   // + les doublures de la voix chantée (unisson)
    static constexpr int kMaxSingers = 6;
    static constexpr int kLines = kSections * kMaxSingers;

    struct Voice
    {
        bool on = false;
        int steps = 0;        // degrés dans la gamme
        int octave = 0;
        float gain = 1.0f;    // linéaire
        float pan = 0.0f;     // -1 .. 1
    };

    struct Settings
    {
        int key = 0, scale = 0;
        float tight = 0.35f, humanize = 0.3f, width = 0.8f;
        std::array<Voice, kVoices> voices {};
        ChoirModelDef model = choirModels[0];
        int mic = 0;
        float room = 0.0f;    // 0 .. 1
        bool mono = false;
    };

    // dernière analyse, pour l'afficheur (lue depuis l'interface)
    struct Frame
    {
        float lead = 0.0f;                    // hauteur chantée (MIDI), 0 = rien
        std::array<float, kVoices> voice {};  // hauteur de chaque voix (MIDI), 0 = voix coupée
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        detector.prepare (fs);
        hop = juce::jmax (64, (int) std::lround (fs * 0.0053));
        maxT = detector.maxPeriod();
        unvoicedT = juce::jmax (32, (int) std::lround (fs * 0.005));
        const int look = detector.span() / 2;
        latency = look + hop + maxT / 2 + 2 * maxT + 2 * hop;   // grains allongés (formants) : jusqu'à 2 périodes de chaque côté
        maxHumanDelay = (int) std::ceil (fs * 0.095) + 2;

        int cap = 1;
        while (cap < 4 * (latency + detector.span() + 4 * maxT + maxHumanDelay)) cap <<= 1;
        ringSize = cap;
        mask = cap - 1;
        x.assign ((size_t) cap, 0.0f);
        lp.assign ((size_t) cap, 0.0f);
        for (auto& y : out) y.assign ((size_t) cap, 0.0f);
        analysisBuf.assign ((size_t) detector.span(), 0.0f);

        frameCap = cap / hop + 4;
        framePeriod.assign ((size_t) frameCap, 0.0f);
        frameVoiced.assign ((size_t) frameCap, 0);

        // filtre passe-bas (~900 Hz) pour caler les marques sur le pic de chaque cycle
        lpCoeffs = juce::IIRCoefficients::makeLowPass (fs, 900.0, 0.707);

        // caractère fixe de chaque choriste (même chœur à chaque lecture)
        juce::Random r (1234);
        for (auto& c : character)
        {
            c.scatter = r.nextFloat();
            c.detune = r.nextFloat() * 2.0f - 1.0f;
            c.pan = r.nextFloat() * 2.0f - 1.0f;
            c.formant = r.nextFloat() * 2.0f - 1.0f;
        }

        reverb.setSampleRate (fs);
        micReady = -1;
        reset();
    }

    void reset()
    {
        std::fill (x.begin(), x.end(), 0.0f);
        std::fill (lp.begin(), lp.end(), 0.0f);
        for (auto& y : out) std::fill (y.begin(), y.end(), 0.0f);
        lpFilter.setCoefficients (lpCoeffs);
        lpFilter.reset();
        written = 0;
        framesDone = 0;
        marks.clear();
        mapper.reset();
        for (int l = 0; l < kLines; ++l)
        {
            synth[(size_t) l] = SynthState {};
            synth[(size_t) l].next = (double) latency * 0.25;
        }
        rng.setSeed (7);
        for (auto& h : human) h = Human {};
        for (auto& f : micFilters) for (auto& b : f) b.reset();
        reverb.reset();
        lastFrame = Frame {};
    }

    int getLatency() const noexcept { return latency; }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        mapper.setScale (s.key, s.scale);
        if (s.mic != micReady)
        {
            const auto& m = mics[juce::jlimit (0, kNumMics - 1, s.mic)];
            const auto low = juce::IIRCoefficients::makeLowShelf (fs, m.lowHz, 0.7, juce::Decibels::decibelsToGain (m.lowDb));
            const auto mid = juce::IIRCoefficients::makePeakFilter (fs, m.midHz, m.midQ, juce::Decibels::decibelsToGain (m.midDb));
            const auto high = juce::IIRCoefficients::makeHighShelf (fs, juce::jmin ((double) m.highHz, fs * 0.45), 0.7, juce::Decibels::decibelsToGain (m.highDb));
            for (auto& ch : micFilters)
            {
                ch[0].setCoefficients (low);
                ch[1].setCoefficients (mid);
                ch[2].setCoefficients (high);
            }
            micActive = std::abs (m.lowDb) + std::abs (m.midDb) + std::abs (m.highDb) > 0.01f;
            micReady = s.mic;
        }
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.45f + 0.45f * s.room;
        rp.damping = 0.45f;
        rp.wetLevel = 0.42f * s.room;
        rp.dryLevel = 1.0f;
        rp.width = s.mono ? 0.0f : 1.0f;
        reverb.setParameters (rp);
    }

    // in : mono ; outL / outR : le chœur seul (remplacé, pas additionné)
    void process (const float* in, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto w = (size_t) (written & mask);
            x[w] = in[i];
            lp[w] = lpFilter.processSingleSampleRaw (in[i]);
            ++written;

            if ((written % hop) == 0)
            {
                analyseNextFrames();
                placeMarks();
                placeGrains();
            }

            // sortie : temps retardé
            const long long e = written - 1 - latency;
            float l = 0.0f, r = 0.0f;
            if (e >= 0)
            {
                for (int line = 0; line < kLines; ++line)
                {
                    auto& y = out[(size_t) line];
                    const auto& h = human[(size_t) line];
                    const long long readAt = e - h.delay;
                    const float s = readAt >= 0 ? y[(size_t) (readAt & mask)] : 0.0f;
                    // vidé derrière la lecture la plus en retard possible : un changement de réglage ne laisse rien traîner
                    if (e - maxHumanDelay - 1 >= 0) y[(size_t) ((e - maxHumanDelay - 1) & mask)] = 0.0f;
                    if (s == 0.0f || ! h.active) continue;
                    l += s * h.gainL;
                    r += s * h.gainR;
                }
            }
            outL[i] = l;
            outR[i] = r;
        }
        busProcess (outL, outR, n);
    }

    Frame getFrame() const noexcept { return lastFrame; }

private:
    struct Mark
    {
        long long pos = 0;
        float period = 0.0f;   // longueur d'un cycle (échantillons)
        float midi = 0.0f;     // hauteur chantée
        int scaleNote = 0;
        bool voiced = false;
    };

    struct SynthState
    {
        double next = 0.0;         // prochaine marque de synthèse (temps retardé)
        float logRatio = 0.0f;     // transposition lissée (log2)
        bool hasRatio = false;
    };

    struct Human
    {
        bool active = false;
        int delay = 0;             // retard du choriste (échantillons)
        float gainL = 0.0f, gainR = 0.0f;
        float drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;
    };

    struct Character { float scatter = 0, detune = 0, pan = 0, formant = 0; };

    // petite file circulaire de marques (positions croissantes)
    struct MarkQueue
    {
        static constexpr int cap = 4096;
        std::array<Mark, cap> m {};
        int head = 0, count = 0;
        void clear() noexcept { head = 0; count = 0; }
        void push (const Mark& k) noexcept
        {
            if (count == cap) { head = (head + 1) % cap; --count; }
            m[(size_t) ((head + count) % cap)] = k;
            ++count;
        }
        void popFront() noexcept { if (count > 0) { head = (head + 1) % cap; --count; } }
        const Mark& at (int i) const noexcept { return m[(size_t) ((head + i) % cap)]; }
        const Mark& back() const noexcept { return at (count - 1); }
        bool empty() const noexcept { return count == 0; }
        int size() const noexcept { return count; }
    };

    float sampleAt (const std::vector<float>& buf, long long t) const noexcept
    {
        return (t < 0 || t >= written || written - t > ringSize) ? 0.0f : buf[(size_t) (t & mask)];
    }

    float sampleAtFrac (double t) const noexcept
    {
        const auto i = (long long) std::floor (t);
        const float f = (float) (t - (double) i);
        return sampleAt (x, i) + f * (sampleAt (x, i + 1) - sampleAt (x, i));
    }

    // analyse toutes les trames dont la fenêtre est disponible ; trame k centrée sur k * hop
    void analyseNextFrames() noexcept
    {
        const int half = detector.span() / 2;
        while (true)
        {
            const long long centre = framesDone * (long long) hop;
            if (centre + half > written) break;
            const long long start = centre - half;
            for (int j = 0; j < detector.span(); ++j)
                analysisBuf[(size_t) j] = sampleAt (x, start + j);
            const auto res = detector.analyse (analysisBuf.data());
            const auto slot = (size_t) (framesDone % frameCap);
            framePeriod[slot] = res.voiced ? res.period : 0.0f;
            frameVoiced[slot] = res.voiced ? 1 : 0;
            ++framesDone;
        }
    }

    // période au temps t (trame la plus proche) ; 0 si non voisé
    float periodAt (long long t) const noexcept
    {
        long long k = (t + hop / 2) / hop;
        k = juce::jlimit (juce::jmax (0LL, framesDone - frameCap + 1), juce::jmax (0LL, framesDone - 1), k);
        return frameVoiced[(size_t) (k % frameCap)] ? framePeriod[(size_t) (k % frameCap)] : 0.0f;
    }

    long long analysedUntil() const noexcept { return (framesDone - 1) * (long long) hop; }

    void placeMarks() noexcept
    {
        const long long limit = analysedUntil() - hop;
        if (limit <= 0) return;

        if (marks.empty())
            marks.push (makeMark (juce::jmax (0LL, written - (long long) ringSize / 2), false));

        while (true)
        {
            const Mark& last = marks.back();
            const float T = periodAt (last.pos);
            long long next;
            bool voiced = T > 0.0f;
            if (voiced)
            {
                // prochaine période : recherche du pic (signal filtré) autour de la position prévue
                const long long predicted = last.pos + (long long) std::lround (T);
                const float span = last.voiced ? 0.2f * T : 0.5f * T;
                if (predicted + (long long) span > limit) break;
                long long best = predicted;
                float bestV = -1.0e9f;
                for (long long t = predicted - (long long) span; t <= predicted + (long long) span; ++t)
                {
                    const float v = sampleAt (lp, t);
                    if (v > bestV) { bestV = v; best = t; }
                }
                next = juce::jmax (last.pos + (long long) (0.6f * T), best);
            }
            else
            {
                next = last.pos + unvoicedT;
                if (next > limit) break;
            }
            marks.push (makeMark (next, voiced));
        }

        // garde les marques encore utiles aux grains à venir
        auto oldest = (long long) synth[0].next;
        for (const auto& s : synth) oldest = juce::jmin (oldest, (long long) s.next);
        while (marks.size() > 2 && marks.at (1).pos < oldest - 3 * maxT) marks.popFront();
    }

    Mark makeMark (long long pos, bool voicedHint) noexcept
    {
        Mark k;
        k.pos = pos;
        const float T = periodAt (pos);
        k.voiced = voicedHint && T > 0.0f;
        k.period = k.voiced ? juce::jlimit (8.0f, (float) maxT, T) : (float) unvoicedT;
        if (k.voiced)
        {
            k.midi = midiFromHz ((float) fs / k.period);
            k.scaleNote = mapper.track (k.midi);
        }
        else
        {
            mapper.reset();
        }
        return k;
    }

    // section (voix d'harmonie, ou doublures de la voix chantée) : réglages effectifs
    Voice sectionVoice (int section) const noexcept
    {
        if (section < kVoices) return settings.voices[(size_t) section];
        const auto& m = settings.model;
        return { m.leadSingers > 0 && m.leadGain > 0.0f, 0, 0, m.leadGain, 0.0f };
    }

    int singersIn (int section) const noexcept
    {
        const auto& m = settings.model;
        return juce::jlimit (0, kMaxSingers, section < kVoices ? m.singers : m.leadSingers);
    }

    // hauteur visée par une section pour une marque
    float sectionTarget (int section, const Mark& k) const noexcept
    {
        const auto vs = sectionVoice (section);
        const int shifted = mapper.shiftDegrees (k.scaleNote, vs.steps) + 12 * (vs.octave + settings.model.octaveShift);
        return HarmonyMapper::targetPitch (k.midi, k.scaleNote, shifted, settings.tight);
    }

    // retard, gains et activité de chaque choriste, d'après les réglages
    void updateSingers() noexcept
    {
        const auto& m = settings.model;
        for (int section = 0; section < kSections; ++section)
        {
            const auto vs = sectionVoice (section);
            const int count = singersIn (section);
            for (int s = 0; s < kMaxSingers; ++s)
            {
                const int line = section * kMaxSingers + s;
                auto& h = human[(size_t) line];
                const auto& c = character[(size_t) line];
                h.active = vs.on && s < count;
                if (! h.active) continue;

                // retard : celui de la voix (HUMANIZE) + l'éparpillement du chœur (premier choriste à l'heure)
                const double voiceMs = settings.humanize * (4.0 + 5.0 * (section % kVoices));
                const double scatterMs = s == 0 ? 0.0 : m.scatterMs * (0.35 + 0.65 * c.scatter) * (0.5 + settings.humanize);
                h.delay = juce::jmin (maxHumanDelay, (int) std::lround (fs * 0.001 * (voiceMs + scatterMs)));

                // place stéréo : la voix, ouverte autour d'elle quand plusieurs choristes la chantent
                float pan = vs.pan + (count > 1 ? c.pan * (section < kVoices ? 0.35f : 0.6f) : 0.0f);
                pan = settings.mono ? 0.0f : juce::jlimit (-1.0f, 1.0f, pan * settings.width);
                const float ang = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const float g = vs.gain / std::sqrt ((float) juce::jmax (1, count));
                h.gainL = g * std::cos (ang) * juce::MathConstants<float>::sqrt2;
                h.gainR = g * std::sin (ang) * juce::MathConstants<float>::sqrt2;
            }
        }
    }

    void placeGrains() noexcept
    {
        if (marks.size() < 2) return;
        const long long horizon = marks.back().pos - maxT;
        updateSingers();
        const auto& m = settings.model;

        for (int line = 0; line < kLines; ++line)
        {
            const int section = line / kMaxSingers, singer = line % kMaxSingers;
            auto& st = synth[(size_t) line];
            auto& y = out[(size_t) line];
            auto& h = human[(size_t) line];
            const auto& c = character[(size_t) line];

            // désaccord : fixe pour chaque choriste + dérive lente (HUMANIZE)
            if (--h.driftCountdown <= 0)
            {
                h.driftTarget = (rng.nextFloat() * 2.0f - 1.0f) * 14.0f * settings.humanize;
                h.driftCountdown = 60 + rng.nextInt (80);
            }
            h.drift += 0.02f * (h.driftTarget - h.drift);
            const float detuneCents = (singer == 0 ? 0.0f : c.detune * m.detuneCents) + h.drift;
            const float formant = juce::jlimit (0.6f, 1.6f, m.formant * (singer == 0 ? 1.0f : 1.0f + 0.035f * c.formant));

            while (st.next < (double) horizon)
            {
                // marque d'analyse la plus proche de la marque de synthèse
                int i = 0;
                while (i + 1 < marks.size() && marks.at (i + 1).pos <= (long long) st.next) ++i;
                const Mark& a = (i + 1 < marks.size()
                                 && std::abs ((double) marks.at (i + 1).pos - st.next) < std::abs ((double) marks.at (i).pos - st.next))
                                    ? marks.at (i + 1) : marks.at (i);

                float ratio = 1.0f, gain = 0.4f, fr = 1.0f;   // consonnes : non transposées, plus discrètes
                if (a.voiced)
                {
                    const float target = sectionTarget (section, a) + detuneCents * 0.01f;
                    const float wanted = (target - a.midi) / 12.0f;   // log2 de la transposition
                    const float glide = 1.0f - std::exp (-a.period / (float) (fs * 0.02));
                    st.logRatio = st.hasRatio ? st.logRatio + glide * (wanted - st.logRatio) : wanted;
                    st.hasRatio = true;
                    ratio = std::exp2 (st.logRatio);
                    gain = 1.0f;
                    fr = formant;
                }
                else
                {
                    st.hasRatio = false;
                }

                if (h.active)
                {
                    // grain de deux périodes, fenêtre de Hann, rééchantillonné (formants) puis recollé sur la marque de synthèse
                    const long long centre = (long long) std::lround (st.next);
                    const int half = juce::jmax (4, (int) std::lround (a.period / fr));
                    const float norm = gain * fr / juce::jmax (0.25f, ratio);   // recouvrement des fenêtres : somme = ratio / fr
                    const float inv = juce::MathConstants<float>::pi / (float) half;
                    for (int k = -half + 1; k < half; ++k)
                    {
                        const float w = 0.5f + 0.5f * std::cos ((float) k * inv);
                        const long long dst = centre + k;
                        const float v = fr == 1.0f ? sampleAt (x, a.pos + k) : sampleAtFrac ((double) a.pos + (double) k * fr);
                        y[(size_t) (dst & mask)] += w * norm * v;
                    }
                }

                st.next += a.voiced ? (double) a.period / (double) ratio : (double) a.period;
            }
        }

        // pour l'afficheur : dernière marque voisée
        for (int i = marks.size() - 1; i >= 0; --i)
        {
            const Mark& a = marks.at (i);
            if (a.pos > marks.back().pos - 2 * hop) continue;
            Frame f;
            if (a.voiced)
            {
                f.lead = a.midi;
                for (int v = 0; v < kVoices; ++v)
                    f.voice[(size_t) v] = settings.voices[(size_t) v].on ? sectionTarget (v, a) : 0.0f;
            }
            lastFrame = f;
            break;
        }
    }

    // bus du chœur : mono, couleur du micro, saturation (cris), salle
    void busProcess (float* l, float* r, int n) noexcept
    {
        const auto& m = settings.model;
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            if (settings.mono) a = b = 0.5f * (a + b);
            if (micActive)
            {
                for (auto& f : micFilters[0]) a = f.processSingleSampleRaw (a);
                for (auto& f : micFilters[1]) b = f.processSingleSampleRaw (b);
            }
            if (m.drive > 0.0f)
            {
                const float k = 1.0f + 6.0f * m.drive;
                const float makeup = 1.0f / std::tanh (k * 0.35f) * 0.35f;
                a = std::tanh (a * k) * makeup;
                b = std::tanh (b * k) * makeup;
            }
            l[i] = a;
            r[i] = b;
        }
        if (settings.room > 0.001f) reverb.processStereo (l, r, n);
    }

    double fs = 48000.0;
    PitchDetector detector;
    HarmonyMapper mapper;
    Settings settings;
    int hop = 256, maxT = 800, unvoicedT = 240, latency = 3000, maxHumanDelay = 4600;
    int ringSize = 1 << 16, mask = (1 << 16) - 1;
    std::vector<float> x, lp, analysisBuf;
    std::array<std::vector<float>, kLines> out;
    long long written = 0, framesDone = 0;
    int frameCap = 1024;
    std::vector<float> framePeriod;
    std::vector<char> frameVoiced;
    MarkQueue marks;
    std::array<SynthState, kLines> synth {};
    std::array<Human, kLines> human {};
    std::array<Character, kLines> character {};
    juce::IIRCoefficients lpCoeffs;
    juce::IIRFilter lpFilter;
    std::array<std::array<juce::IIRFilter, 3>, 2> micFilters;
    int micReady = -1;
    bool micActive = false;
    juce::Reverb reverb;
    juce::Random rng;
    Frame lastFrame;
};
} // namespace onirique
