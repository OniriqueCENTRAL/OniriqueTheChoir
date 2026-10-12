#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace onirique
{
// Détection de hauteur YIN (de Cheveigné & Kawahara), la fonction de différence étant calculée par FFT :
//   d(tau) = E(0) + E(tau) - 2 r(tau), r = intercorrélation, E = énergies glissantes (sommes cumulées).
// Un appel analyse W + tauMax échantillons et rend la période en échantillons (fractionnaire) et un indice
// d'apériodicité (0 = parfaitement périodique). Aucune allocation après prepare().
class PitchDetector
{
public:
    struct Result
    {
        float period = 0.0f;        // en échantillons ; 0 = pas de hauteur
        float aperiodicity = 1.0f;
        bool voiced = false;
    };

    // minHz / maxHz : bornes de la voix chantée ; window = W = tauMax
    void prepare (double sampleRate, float minHz = 60.0f, float maxHz = 1100.0f)
    {
        fs = sampleRate;
        tauMax = (int) std::ceil (fs / minHz);
        tauMin = juce::jmax (2, (int) std::floor (fs / maxHz));
        W = tauMax;
        const int needed = W + tauMax;
        order = 1;
        while ((1 << order) < needed) ++order;
        N = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        bufA.assign ((size_t) N * 2, 0.0f);
        bufB.assign ((size_t) N * 2, 0.0f);
        d.assign ((size_t) tauMax + 1, 0.0f);
        energy.assign ((size_t) needed + 1, 0.0f);
    }

    int span() const noexcept { return W + tauMax; }   // échantillons lus par analyse
    int maxPeriod() const noexcept { return tauMax; }

    // x : span() échantillons ; centre de l'analyse = x + span() / 2
    Result analyse (const float* x) noexcept
    {
        Result res;
        const int n = span();

        // énergie : le silence (< -60 dBFS) n'a pas de hauteur
        double e = 0.0;
        energy[0] = 0.0f;
        for (int i = 0; i < n; ++i) { e += (double) x[i] * x[i]; energy[(size_t) i + 1] = (float) e; }
        if (e / n < 1.0e-6) return res;

        std::fill (bufA.begin(), bufA.end(), 0.0f);
        std::fill (bufB.begin(), bufB.end(), 0.0f);
        std::copy (x, x + W, bufA.begin());
        std::copy (x, x + n, bufB.begin());
        fft->performRealOnlyForwardTransform (bufA.data(), true);
        fft->performRealOnlyForwardTransform (bufB.data(), true);
        // conj(A) * B : intercorrélation r(tau) = sum_j a[j] b[j + tau]
        for (int k = 0; k <= N / 2; ++k)
        {
            const float ar = bufA[(size_t) 2 * k], ai = bufA[(size_t) 2 * k + 1];
            const float br = bufB[(size_t) 2 * k], bi = bufB[(size_t) 2 * k + 1];
            bufA[(size_t) 2 * k]     = ar * br + ai * bi;
            bufA[(size_t) 2 * k + 1] = ar * bi - ai * br;
        }
        fft->performRealOnlyInverseTransform (bufA.data());

        const float e0 = energy[(size_t) W];
        d[0] = 0.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax && tau + W <= n; ++tau)
        {
            const float et = energy[(size_t) (tau + W)] - energy[(size_t) tau];
            const float diff = juce::jmax (0.0f, e0 + et - 2.0f * bufA[(size_t) tau]);
            running += diff;
            d[(size_t) tau] = running > 0.0f ? diff * (float) tau / running : 1.0f;   // différence normalisée cumulée
        }

        // premier creux sous le seuil, sinon le minimum global
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin + 1; tau < tauMax; ++tau)
                if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        }

        // interpolation parabolique autour du creux
        float period = (float) best;
        if (best > 1 && best < tauMax - 1)
        {
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f) period += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
        }

        res.period = period;
        res.aperiodicity = d[(size_t) best];
        res.voiced = res.aperiodicity < voicedLimit;
        return res;
    }

private:
    static constexpr float threshold = 0.12f, voicedLimit = 0.25f;
    double fs = 48000.0;
    int tauMin = 40, tauMax = 800, W = 800, N = 2048, order = 11;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> bufA, bufB, d, energy;
};
} // namespace onirique
