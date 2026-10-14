#pragma once
// Axell Penta EQ - fit parametric bands to a target curve (framework independent; GUI thread).
//
// Used by "Draw" (a curve sketched with the mouse) and "Match" (the difference between a reference
// spectrum and this track's). The target is a dB curve on a log-spaced frequency grid. The fit places
// bands greedily on the largest remaining deviation - a bell at a bump or dip (Q from its width), a
// shelf where the deviation runs off the end of the band - and then refines frequency, gain and Q of
// all bands together by damped Gauss-Newton on the analog band curves (the shapes the plugin realises).

#include "Bands.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace penta::dsp
{
struct FitBand
{
    int type = kBell;
    double freqHz = 1000.0, gainDb = 0.0, q = 1.0;
};

class CurveFitter
{
public:
    // hz: grid (ascending, log-spaced); targetDb: the curve to reach; weight: optional (0 = ignore a point)
    void setTarget (std::vector<double> hz, std::vector<double> targetDb, std::vector<double> weight = {})
    {
        f = std::move (hz);
        t = std::move (targetDb);
        w = weight.empty() ? std::vector<double> (f.size(), 1.0) : std::move (weight);
    }

    // fit up to maxBands bands; stops early when the curve is within tolDb
    std::vector<FitBand> fit (int maxBands, double tolDb = 0.25) const
    {
        std::vector<FitBand> bands;
        std::vector<double> cur (f.size(), 0.0);
        for (int b = 0; b < maxBands; ++b)
        {
            std::vector<double> r (f.size());
            for (size_t i = 0; i < f.size(); ++i)
                r[i] = (t[i] - cur[i]) * (w[i] > 0.0 ? 1.0 : 0.0);
            const auto nb = place (r);
            if (std::abs (nb.gainDb) < tolDb)
                break;
            bands.push_back (nb);
            refine (bands, 8);
            curve (bands, cur);
            double worst = 0.0;
            for (size_t i = 0; i < f.size(); ++i)
                if (w[i] > 0.0)
                    worst = std::max (worst, std::abs (t[i] - cur[i]));
            if (worst < tolDb)
                break;
        }
        refine (bands, 30);
        // drop bands that ended up doing nothing
        bands.erase (std::remove_if (bands.begin(), bands.end(), [] (const FitBand& b) { return std::abs (b.gainDb) < 0.1; }), bands.end());
        return bands;
    }

    // the summed analog curve of a set of bands on the grid (dB)
    void curve (const std::vector<FitBand>& bands, std::vector<double>& out) const
    {
        out.assign (f.size(), 0.0);
        ChainDesign d;
        for (const auto& b : bands)
        {
            build (b, d);
            for (size_t i = 0; i < f.size(); ++i)
                out[i] += 10.0 * std::log10 (std::max (designMagSq (d, 2.0 * kPi * f[i] / kFs), 1.0e-30));
        }
    }

    double rmsError (const std::vector<FitBand>& bands) const
    {
        std::vector<double> c;
        curve (bands, c);
        double s = 0.0, n = 0.0;
        for (size_t i = 0; i < f.size(); ++i)
        {
            s += w[i] * (t[i] - c[i]) * (t[i] - c[i]);
            n += w[i];
        }
        return std::sqrt (s / std::max (n, 1.0e-9));
    }

private:
    static constexpr double kFs = 192000.0; // analog curves: evaluate far below Nyquist
    static constexpr double kMaxGain = 24.0;

    static void build (const FitBand& b, ChainDesign& d)
    {
        BandShape s;
        s.type = b.type;
        s.freqHz = b.freqHz;
        s.gainDb = b.gainDb;
        s.q = b.q;
        s.slopeIdx = 1; // shelves: 2nd order
        buildBand (s, kFs, d, false);
    }

    // a new band on the largest (smoothed) deviation of the residual r
    FitBand place (const std::vector<double>& r) const
    {
        const size_t n = f.size();
        size_t pk = 0;
        double best = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double v = (i > 0 && i + 1 < n) ? 0.25 * r[i - 1] + 0.5 * r[i] + 0.25 * r[i + 1] : r[i];
            if (w[i] > 0.0 && std::abs (v) > best)
            {
                best = std::abs (v);
                pk = i;
            }
        }
        FitBand b;
        b.gainDb = std::clamp (r[pk], -kMaxGain, kMaxGain);
        b.freqHz = f[pk];
        // width: where the deviation falls to half on either side (octaves)
        size_t lo = pk, hi = pk;
        while (lo > 0 && std::abs (r[lo - 1]) > 0.5 * std::abs (r[pk]) && r[lo - 1] * r[pk] > 0.0)
            --lo;
        while (hi + 1 < n && std::abs (r[hi + 1]) > 0.5 * std::abs (r[pk]) && r[hi + 1] * r[pk] > 0.0)
            ++hi;
        const double bw = std::max (0.1, std::log2 (f[hi] / f[lo]));
        b.q = std::clamp (std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0), 0.3, 12.0);
        // runs off an end of the band: a shelf, half-way point at the edge of the run
        if (lo == 0 && hi + 1 < n && f[pk] < 120.0)
        {
            b.type = kLowShelf;
            b.freqHz = f[hi];
            b.q = 0.7;
        }
        else if (hi + 1 == n && lo > 0 && f[pk] > 6000.0)
        {
            b.type = kHighShelf;
            b.freqHz = f[lo];
            b.q = 0.7;
        }
        return b;
    }

    // damped Gauss-Newton over (log2 f, gain, log2 q) of all bands
    void refine (std::vector<FitBand>& bands, int iters) const
    {
        const size_t n = f.size(), m = bands.size() * 3;
        if (m == 0)
            return;
        std::vector<double> cur, J (n * m), r (n), A (m * m), g (m), x (m);
        auto pack = [&] (const std::vector<FitBand>& b)
        {
            for (size_t k = 0; k < b.size(); ++k)
            {
                x[3 * k] = std::log2 (b[k].freqHz);
                x[3 * k + 1] = b[k].gainDb;
                x[3 * k + 2] = std::log2 (b[k].q);
            }
        };
        auto unpack = [&] (const std::vector<double>& v, std::vector<FitBand>& b)
        {
            for (size_t k = 0; k < b.size(); ++k)
            {
                b[k].freqHz = std::clamp (std::exp2 (v[3 * k]), 20.0, 22000.0);
                b[k].gainDb = std::clamp (v[3 * k + 1], -kMaxGain, kMaxGain);
                b[k].q = std::clamp (std::exp2 (v[3 * k + 2]), 0.2, 16.0);
            }
        };
        auto cost = [&] (const std::vector<FitBand>& b)
        {
            curve (b, cur);
            double s = 0.0;
            for (size_t i = 0; i < n; ++i)
                s += w[i] * (t[i] - cur[i]) * (t[i] - cur[i]);
            return s;
        };
        double lambda = 1.0e-2;
        double c0 = cost (bands);
        for (int it = 0; it < iters; ++it)
        {
            pack (bands);
            for (size_t i = 0; i < n; ++i)
                r[i] = std::sqrt (w[i]) * (t[i] - cur[i]);
            // numeric Jacobian, band by band (a band's parameters move only its own curve)
            ChainDesign d;
            for (size_t k = 0; k < bands.size(); ++k)
                for (int p = 0; p < 3; ++p)
                {
                    const double h = p == 1 ? 0.05 : 0.01;
                    FitBand bp = bands[k], bm = bands[k];
                    auto shift = [&] (FitBand& b, double s)
                    {
                        if (p == 0) b.freqHz *= std::exp2 (s);
                        else if (p == 1) b.gainDb += s;
                        else b.q *= std::exp2 (s);
                    };
                    shift (bp, h);
                    shift (bm, -h);
                    std::vector<double> cp (n), cm (n);
                    build (bp, d);
                    for (size_t i = 0; i < n; ++i)
                        cp[i] = 10.0 * std::log10 (std::max (designMagSq (d, 2.0 * kPi * f[i] / kFs), 1.0e-30));
                    build (bm, d);
                    for (size_t i = 0; i < n; ++i)
                        cm[i] = 10.0 * std::log10 (std::max (designMagSq (d, 2.0 * kPi * f[i] / kFs), 1.0e-30));
                    for (size_t i = 0; i < n; ++i)
                        J[i * m + 3 * k + (size_t) p] = std::sqrt (w[i]) * (cp[i] - cm[i]) / (2.0 * h);
                }
            std::fill (A.begin(), A.end(), 0.0);
            std::fill (g.begin(), g.end(), 0.0);
            for (size_t i = 0; i < n; ++i)
                for (size_t a = 0; a < m; ++a)
                {
                    const double ja = J[i * m + a];
                    if (same (ja, 0.0))
                        continue;
                    g[a] += ja * r[i];
                    for (size_t c = 0; c < m; ++c)
                        A[a * m + c] += ja * J[i * m + c];
                }
            bool improved = false;
            for (int tries = 0; tries < 6 && ! improved; ++tries)
            {
                std::vector<double> M = A, y = g;
                for (size_t a = 0; a < m; ++a)
                    M[a * m + a] += lambda * (A[a * m + a] + 1.0e-6);
                if (! solve (M, y, m))
                {
                    lambda *= 10.0;
                    continue;
                }
                auto trial = bands;
                std::vector<double> xt = x;
                for (size_t a = 0; a < m; ++a)
                    xt[a] += y[a];
                unpack (xt, trial);
                const double c1 = cost (trial);
                if (c1 < c0)
                {
                    bands = trial;
                    c0 = c1;
                    lambda = std::max (1.0e-6, lambda * 0.3);
                    improved = true;
                }
                else
                    lambda *= 10.0;
            }
            if (! improved)
                break;
            cost (bands); // cur for the next residual
        }
    }

    static bool solve (std::vector<double>& A, std::vector<double>& b, size_t n)
    {
        for (size_t c = 0; c < n; ++c)
        {
            size_t piv = c;
            for (size_t r = c + 1; r < n; ++r)
                if (std::abs (A[r * n + c]) > std::abs (A[piv * n + c]))
                    piv = r;
            if (! (std::abs (A[piv * n + c]) > 1.0e-300))
                return false;
            if (piv != c)
            {
                for (size_t k = 0; k < n; ++k)
                    std::swap (A[c * n + k], A[piv * n + k]);
                std::swap (b[c], b[piv]);
            }
            for (size_t r = c + 1; r < n; ++r)
            {
                const double q = A[r * n + c] / A[c * n + c];
                if (same (q, 0.0))
                    continue;
                for (size_t k = c; k < n; ++k)
                    A[r * n + k] -= q * A[c * n + k];
                b[r] -= q * b[c];
            }
        }
        for (size_t c = n; c-- > 0;)
        {
            double v = b[c];
            for (size_t k = c + 1; k < n; ++k)
                v -= A[c * n + k] * b[k];
            b[c] = v / A[c * n + c];
        }
        return true;
    }

    std::vector<double> f, t, w;
};
} // namespace penta::dsp
