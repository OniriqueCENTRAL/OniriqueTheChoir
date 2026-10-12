#pragma once

#include "Harmony.h"
#include "PitchDetector.h"
#include <array>
#include <atomic>

namespace onirique
{
// Moteur d'harmonies : une voix chantée (mono) -> jusqu'à 6 voix d'harmonie (stéréo).
//
// Décalage de hauteur TD-PSOLA, synchrone à la période :
//  1. la hauteur est mesurée toutes les ~5 ms (YIN) ;
//  2. des marques sont posées sur chaque période de la voix, calées sur le pic de chaque cycle glottal ;
//  3. pour chaque voix, des grains de deux périodes (fenêtre de Hann) sont prélevés autour de ces marques et
//     recollés à l'espacement de la nouvelle période. Le timbre (formants) est celui de la voix d'origine :
//     pas d'effet « chipmunk », pas de phasing, pas de flou de transitoires comme avec un vocodeur de phase.
// Les consonnes (parties non voisées) sont recopiées sans transposition, plus bas dans les harmonies.
//
// Tout se fait sur une ligne de temps retardée de getLatency() échantillons, le temps de connaître la
// période suivante. Le même code sert au temps réel et au rendu d'un clip (sortie avancée de la latence).
class ChoirEngine
{
public:
    static constexpr int kVoices = 6;   // 4 voix d'harmonie + octave en dessous + octave au-dessus

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
    };

    // dernière analyse, pour l'afficheur (lue depuis l'interface)
    struct Frame
    {
        float lead = 0.0f;                    // hauteur chantée (MIDI), 0 = pas de voix
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
        latency = look + hop + maxT / 2 + maxT + 2 * hop;
        maxHumanDelay = (int) std::ceil (fs * 0.03) + 2;

        int cap = 1;
        while (cap < 8 * (latency + detector.span() + 4 * maxT)) cap <<= 1;
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
        for (int v = 0; v < kVoices; ++v)
        {
            synth[(size_t) v] = SynthState {};
            synth[(size_t) v].next = (double) latency * 0.25;
        }
        rng.setSeed (7);
        for (auto& h : human) h = Human {};
        lastFrame = Frame {};
    }

    int getLatency() const noexcept { return latency; }
    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        mapper.setScale (s.key, s.scale);
    }

    // in : mono ; outL / outR : harmonies seules (remplacées, pas additionnées)
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
                for (int v = 0; v < kVoices; ++v)
                {
                    const auto& vs = settings.voices[(size_t) v];
                    auto& h = human[(size_t) v];
                    const long long readAt = e - h.delay;
                    auto& y = out[(size_t) v];
                    const float s = readAt >= 0 ? y[(size_t) (readAt & mask)] : 0.0f;
                    // vidé derrière la lecture la plus en retard possible : un changement de HUMANIZE ne laisse rien traîner
                    if (e - maxHumanDelay - 1 >= 0) y[(size_t) ((e - maxHumanDelay - 1) & mask)] = 0.0f;
                    if (! vs.on) continue;
                    const float p = juce::jlimit (-1.0f, 1.0f, vs.pan * settings.width);
                    const float ang = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    l += s * vs.gain * std::cos (ang) * juce::MathConstants<float>::sqrt2;
                    r += s * vs.gain * std::sin (ang) * juce::MathConstants<float>::sqrt2;
                }
            }
            outL[i] = l;
            outR[i] = r;
        }
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
        int delay = 0;             // décalage (échantillons) de la voix, effet chœur
        float drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;
    };

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
        long long oldest = (long long) synth[0].next;
        for (const auto& s : synth) oldest = juce::jmin (oldest, (long long) s.next);
        while (marks.size() > 2 && marks.at (1).pos < oldest - 2 * maxT) marks.popFront();
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

    // hauteur visée par une voix pour une marque
    float voiceTarget (int v, const Mark& k) const noexcept
    {
        const auto& vs = settings.voices[(size_t) v];
        const int shifted = mapper.shiftDegrees (k.scaleNote, vs.steps) + 12 * vs.octave;
        return HarmonyMapper::targetPitch (k.midi, k.scaleNote, shifted, settings.tight);
    }

    void placeGrains() noexcept
    {
        if (marks.size() < 2) return;
        const long long horizon = marks.back().pos - maxT / 2;

        for (int v = 0; v < kVoices; ++v)
        {
            auto& st = synth[(size_t) v];
            auto& y = out[(size_t) v];
            auto& h = human[(size_t) v];
            const auto& vs = settings.voices[(size_t) v];

            // humanize : désaccord lent et léger (jusqu'à ±14 cents) et petit retard propre à chaque voix
            h.delay = juce::jmin (maxHumanDelay, (int) std::lround (fs * 0.001 * settings.humanize * (4.0 + 5.0 * v)));
            if (--h.driftCountdown <= 0)
            {
                h.driftTarget = (rng.nextFloat() * 2.0f - 1.0f) * 14.0f * settings.humanize;
                h.driftCountdown = 60 + rng.nextInt (80);
            }
            h.drift += 0.02f * (h.driftTarget - h.drift);

            while (st.next < (double) horizon)
            {
                // marque d'analyse la plus proche de la marque de synthèse
                int i = 0;
                while (i + 1 < marks.size() && marks.at (i + 1).pos <= (long long) st.next) ++i;
                const Mark& a = (i + 1 < marks.size()
                                 && std::abs ((double) marks.at (i + 1).pos - st.next) < std::abs ((double) marks.at (i).pos - st.next))
                                    ? marks.at (i + 1) : marks.at (i);

                float ratio = 1.0f, gain = 0.4f;   // consonnes : non transposées, plus discrètes
                int T = (int) std::lround (a.period);
                if (a.voiced)
                {
                    const float target = voiceTarget (v, a) + h.drift * 0.01f;
                    const float wanted = (target - a.midi) / 12.0f;   // log2 de la transposition
                    const float glide = 1.0f - std::exp (-a.period / (float) (fs * 0.02));
                    st.logRatio = st.hasRatio ? st.logRatio + glide * (wanted - st.logRatio) : wanted;
                    st.hasRatio = true;
                    ratio = std::exp2 (st.logRatio);
                    gain = 1.0f;
                }
                else
                {
                    st.hasRatio = false;
                }

                if (vs.on)
                {
                    // grain de deux périodes, fenêtre de Hann, recollé centré sur la marque de synthèse
                    const long long centre = (long long) std::lround (st.next);
                    const float norm = gain / juce::jmax (0.25f, ratio);   // recouvrement des fenêtres : somme = ratio
                    const float inv = juce::MathConstants<float>::pi / (float) T;
                    for (int k = -T + 1; k < T; ++k)
                    {
                        const float w = 0.5f + 0.5f * std::cos ((float) k * inv);
                        const long long dst = centre + k;
                        if (dst < written - (long long) ringSize + 1) continue;
                        y[(size_t) (dst & mask)] += w * norm * sampleAt (x, a.pos + k);
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
                    f.voice[(size_t) v] = settings.voices[(size_t) v].on ? voiceTarget (v, a) : 0.0f;
            }
            lastFrame = f;
            break;
        }
    }

    double fs = 48000.0;
    PitchDetector detector;
    HarmonyMapper mapper;
    Settings settings;
    int hop = 256, maxT = 800, unvoicedT = 240, latency = 3000, maxHumanDelay = 1500;
    int ringSize = 1 << 16, mask = (1 << 16) - 1;
    std::vector<float> x, lp, analysisBuf;
    std::array<std::vector<float>, kVoices> out;
    long long written = 0, framesDone = 0;
    int frameCap = 1024;
    std::vector<float> framePeriod;
    std::vector<char> frameVoiced;
    MarkQueue marks;
    std::array<SynthState, kVoices> synth {};
    std::array<Human, kVoices> human {};
    juce::IIRCoefficients lpCoeffs;
    juce::IIRFilter lpFilter;
    juce::Random rng;
    Frame lastFrame;
};
} // namespace onirique
