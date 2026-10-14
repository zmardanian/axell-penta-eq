#pragma once
// Axell Penta EQ - linear-phase EQ stage (framework independent).
//
// FIR from the analog magnitude of the static EQ (frequency sampling, zero phase, centred at N/2,
// Kaiser window), run by a uniformly partitioned overlap-save convolver (partition P = N/32).
// Latency = N/2 + P samples. Stereo is a 2x2 zero-phase FIR matrix (out L = LL*inL + LR*inR,
// out R = RL*inL + RR*inR), so any mix of Stereo / L / R / Mid / Side bands is exact with a single
// latency. An output whose filters are identity is a pure delay (no MACs); the input FFT history is
// always kept warm so an output can switch to a filter without a transient.
//
// Filters are designed on a background thread (polling, so the audio thread never blocks or
// signals): the audio thread publishes a parameter snapshot with a seqlock, the designer writes a
// free filter set, the audio thread picks it up at a partition boundary and crossfades over one
// partition. With setSynchronous(true) (offline render, tests) the design runs on the calling
// thread at request time, so results are deterministic.

#include "Fft.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace penta::dsp
{
class LinearPhaseEq
{
public:
    static constexpr int kEntries = 4; // 0 LL, 1 LR, 2 RL, 3 RR
    static constexpr int kSets = 3;
    static constexpr int kParts = 32;  // partitions per filter

    // designFn fills the real (zero-phase) 2x2 response matrix on numBins bins (0..Nyquist), entry
    // order LL, LR, RL, RR (mono: LL only), pre-initialised to identity.
    // Called on the designer thread (or synchronously).
    using DesignFn = std::function<void (const std::vector<float>& params, int numBins,
                                         std::array<std::vector<double>, kEntries>& matrix)>;

    ~LinearPhaseEq() { stopThread(); }

    static int tapsFor (int quality, double fs)
    {
        const int base = quality <= 0 ? 4096 : (quality == 1 ? 8192 : 32768);
        const int mult = fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
        return base * mult;
    }
    static int latencyFor (int quality, double fs) { const int n = tapsFor (quality, fs); return n / 2 + n / kParts; }

    void prepare (double sampleRate, int channels, DesignFn fn, int numParams)
    {
        stopThread();
        fs = sampleRate;
        nCh = std::clamp (channels, 1, 2);
        design = std::move (fn);
        const int nMax = tapsFor (2, fs), pMax = nMax / kParts;
        for (int qi = 0; qi < 3; ++qi)
        {
            const int nq = tapsFor (qi, fs);
            fftP[qi].prepare (2 * (nq / kParts));
            fftPd[qi].prepare (2 * (nq / kParts));
            fftN[qi].prepare (nq);
        }
        for (auto& set : sets)
        {
            for (auto& e : set.spec)
                e.assign ((size_t) (kParts * (pMax + 1)), {});
            set.state.store (kFree);
        }
        for (auto& in : inputs)
        {
            in.fdl.assign ((size_t) (kParts * (pMax + 1)), {});
            in.window.assign ((size_t) (2 * pMax), 0.0f);
            in.delay.assign ((size_t) (nMax / 2 + 2 * pMax + 1), 0.0f);
        }
        for (auto& o : outputs)
        {
            o.cur.assign ((size_t) pMax, 0.0f);
            o.old.assign ((size_t) pMax, 0.0f);
        }
        accY.assign ((size_t) (pMax + 1), {});
        timeBuf.assign ((size_t) (2 * pMax), 0.0f);
        for (auto& m : dMat)
            m.assign ((size_t) (nMax / 2 + 1), 0.0);
        dSpec.assign ((size_t) (nMax / 2 + 1), {});
        dTime.assign ((size_t) nMax, 0.0);
        dShift.assign ((size_t) nMax, 0.0);
        dPart.assign ((size_t) (2 * pMax), 0.0f);
        dPartSpec.assign ((size_t) (pMax + 1), {});
        snapshot.reset (new std::atomic<float>[(size_t) std::max (1, numParams)]);
        nParams = numParams;
        designerParams.assign ((size_t) std::max (1, numParams), 0.0f);
        configure (quality);
        if (! synchronous)
            startThread();
    }

    void setSynchronous (bool s)
    {
        if (s == synchronous)
            return;
        synchronous = s;
        if (s)
            stopThread();
        else if (design)
            startThread();
    }

    // call only while the output is faded out (the engine handles it); no allocation
    void configure (int q)
    {
        quality = std::clamp (q, 0, 2);
        n = tapsFor (quality, fs);
        p = n / kParts;
        fdlPos = 0;
        inPos = 0;
        activeSet = -1;
        oldSet = -1;
        fading = false;
        for (auto& set : sets)
        {
            const int st = set.state.load();
            if (st == kActive || st == kRetiring || st == kReady)
                set.state.store (kFree);
        }
        for (auto& in : inputs)
        {
            std::fill (in.fdl.begin(), in.fdl.end(), std::complex<float>());
            std::fill (in.window.begin(), in.window.end(), 0.0f);
            std::fill (in.delay.begin(), in.delay.end(), 0.0f);
            in.delayPos = 0;
        }
        for (auto& o : outputs)
        {
            std::fill (o.cur.begin(), o.cur.end(), 0.0f);
            std::fill (o.old.begin(), o.old.end(), 0.0f);
        }
    }

    void reset() { configure (quality); }

    int latency() const noexcept { return n / 2 + p; }
    int currentQuality() const noexcept { return quality; }
    bool hasFilter() const noexcept { return activeSet >= 0; }

    // audio thread: publish a new target (RT-safe; synchronous mode designs right here)
    void request (const std::vector<float>& params) noexcept
    {
        const unsigned s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1, std::memory_order_release);
        for (int i = 0; i < nParams && i < (int) params.size(); ++i)
            snapshot[(size_t) i].store (params[(size_t) i], std::memory_order_relaxed);
        reqQuality.store (quality, std::memory_order_relaxed);
        seq.store (s + 2, std::memory_order_release);
        requestSeq.fetch_add (1, std::memory_order_acq_rel);
        if (synchronous)
            designerStep();
    }

    void process (float* const* ch, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float d[2] {};
            for (int c = 0; c < nCh; ++c)
                d[c] = pushInput (c, ch[c][i]);
            for (int o = 0; o < nCh; ++o)
                ch[o][i] = output (o, d);
            if (++inPos >= p)
            {
                inPos = 0;
                blockBoundary();
            }
        }
    }

private:
    enum SetState { kFree = 0, kWriting, kReady, kActive, kRetiring };

    struct Set
    {
        std::array<std::vector<std::complex<float>>, kEntries> spec;
        std::array<bool, kEntries> nonzero {};  // entry needs MACs
        std::array<bool, 2> outIdentity {};     // output = delayed own input
        int quality = 0;
        unsigned seqNo = 0;
        std::atomic<int> state { kFree };
    };
    struct Input
    {
        std::vector<std::complex<float>> fdl;
        std::vector<float> window, delay;
        int delayPos = 0;
    };
    struct Output
    {
        std::vector<float> cur, old;
    };

    float pushInput (int c, float x) noexcept
    {
        auto& in = inputs[(size_t) c];
        in.window[(size_t) (p + inPos)] = x;
        const int dl = n / 2 + p, ds = (int) in.delay.size();
        in.delay[(size_t) in.delayPos] = x;
        int rp = in.delayPos - dl;
        if (rp < 0)
            rp += ds;
        in.delayPos = (in.delayPos + 1 == ds) ? 0 : in.delayPos + 1;
        return in.delay[(size_t) rp];
    }

    float outFor (int set, int o, const float* delayed, const std::vector<float>& buf) const noexcept
    {
        if (set < 0 || sets[(size_t) set].outIdentity[(size_t) o])
            return delayed[o];
        return buf[(size_t) inPos];
    }

    float output (int o, const float* delayed) noexcept
    {
        const auto& out = outputs[(size_t) o];
        float y = outFor (activeSet, o, delayed, out.cur);
        if (fading)
        {
            const float yo = outFor (oldSet, o, delayed, out.old);
            y = yo + (y - yo) * ((float) (inPos + 1) / (float) p);
        }
        return y;
    }

    void blockBoundary() noexcept
    {
        if (fading)
        {
            fading = false;
            if (oldSet >= 0)
                sets[(size_t) oldSet].state.store (kFree);
            oldSet = -1;
        }
        int best = -1;
        for (int s = 0; s < kSets; ++s)
            if (sets[(size_t) s].state.load (std::memory_order_acquire) == kReady)
            {
                if (sets[(size_t) s].quality != quality)
                {
                    sets[(size_t) s].state.store (kFree);
                    continue;
                }
                if (best < 0 || (int) (sets[(size_t) s].seqNo - sets[(size_t) best].seqNo) > 0)
                {
                    if (best >= 0)
                        sets[(size_t) best].state.store (kFree);
                    best = s;
                }
                else
                    sets[(size_t) s].state.store (kFree);
            }
        if (best >= 0)
        {
            sets[(size_t) best].state.store (kActive);
            oldSet = activeSet;
            if (oldSet >= 0)
                sets[(size_t) oldSet].state.store (kRetiring);
            activeSet = best;
            fading = true;
        }

        const int nb = p + 1;
        for (int c = 0; c < nCh; ++c)
        {
            auto& in = inputs[(size_t) c];
            fftP[quality].forward (in.window.data(), in.fdl.data() + (size_t) fdlPos * (size_t) nb);
            std::copy (in.window.begin() + p, in.window.begin() + 2 * p, in.window.begin());
        }
        for (int o = 0; o < nCh; ++o)
        {
            convolve (activeSet, o, outputs[(size_t) o].cur);
            if (fading)
                convolve (oldSet, o, outputs[(size_t) o].old);
        }
        fdlPos = (fdlPos + 1) % kParts;
    }

    void convolve (int set, int o, std::vector<float>& out) noexcept
    {
        if (set < 0 || sets[(size_t) set].outIdentity[(size_t) o])
            return;
        const auto& st = sets[(size_t) set];
        const int nb = p + 1;
        std::fill (accY.begin(), accY.begin() + nb, std::complex<float>());
        for (int c = 0; c < nCh; ++c)
        {
            const int e = o * 2 + c; // LL LR RL RR
            if (! st.nonzero[(size_t) e])
                continue;
            const auto& h = st.spec[(size_t) e];
            const auto& in = inputs[(size_t) c];
            for (int k = 0; k < kParts; ++k)
            {
                int slot = fdlPos - k;
                if (slot < 0)
                    slot += kParts;
                const std::complex<float>* x = in.fdl.data() + (size_t) slot * (size_t) nb;
                const std::complex<float>* hk = h.data() + (size_t) k * (size_t) nb;
                for (int b = 0; b < nb; ++b)
                    accY[(size_t) b] += x[b] * hk[b];
            }
        }
        fftP[quality].inverse (accY.data(), timeBuf.data());
        std::copy (timeBuf.begin() + p, timeBuf.begin() + 2 * p, out.begin());
    }

    //== designer ==============================================================
    void startThread()
    {
        if (worker.joinable() || ! design)
            return;
        running.store (true);
        worker = std::thread ([this]
        {
            while (running.load())
            {
                designerStep();
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        });
    }

    void stopThread()
    {
        running.store (false);
        if (worker.joinable())
            worker.join();
    }

    void designerStep()
    {
        const unsigned want = requestSeq.load (std::memory_order_acquire);
        if (want == doneSeq || ! design)
            return;
        int q = 0;
        for (;;)
        {
            const unsigned s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u)
                continue;
            for (int i = 0; i < nParams; ++i)
                designerParams[(size_t) i] = snapshot[(size_t) i].load (std::memory_order_relaxed);
            q = reqQuality.load (std::memory_order_relaxed);
            if (seq.load (std::memory_order_acquire) == s0)
                break;
        }
        int target = -1;
        for (int s = 0; s < kSets; ++s)
        {
            int expected = kFree;
            if (sets[(size_t) s].state.compare_exchange_strong (expected, kWriting))
            {
                target = s;
                break;
            }
        }
        if (target < 0)
            return; // all busy: the request stays pending until the next poll
        doneSeq = want;
        build (sets[(size_t) target], q, designerParams);
        sets[(size_t) target].seqNo = want;
        sets[(size_t) target].state.store (kReady, std::memory_order_release);
    }

    // partition spectra of a time-domain filter (length nq, already centred/windowed)
    void partition (const std::vector<double>& h, int nq, std::vector<std::complex<float>>& spec)
    {
        const int pq = nq / kParts;
        for (int k = 0; k < kParts; ++k)
        {
            std::fill (dPart.begin(), dPart.begin() + 2 * pq, 0.0f);
            for (int i = 0; i < pq; ++i)
                dPart[(size_t) i] = (float) h[(size_t) (k * pq + i)];
            fftForQuality (nq).forward (dPart.data(), dPartSpec.data());
            std::copy (dPartSpec.begin(), dPartSpec.begin() + pq + 1, spec.begin() + (size_t) k * (size_t) (pq + 1));
        }
    }

    // the designer has its own FFT objects (they hold scratch buffers; the audio thread uses fftP)
    RealFft<float>& fftForQuality (int nq)
    {
        for (int qi = 0; qi < 3; ++qi)
            if (tapsFor (qi, fs) == nq)
                return fftPd[qi];
        return fftPd[0];
    }

    void build (Set& set, int q, const std::vector<float>& params)
    {
        const int nq = tapsFor (q, fs), bins = nq / 2 + 1;
        for (int e = 0; e < kEntries; ++e)
            std::fill (dMat[(size_t) e].begin(), dMat[(size_t) e].begin() + bins, (e == 0 || e == 3) ? 1.0 : 0.0);
        design (params, bins, dMat);
        set.quality = q;
        // Tukey window (flat middle, raised-cosine 1/8 at each end): the zero-phase responses of EQ
        // curves decay well inside N/2, so a flat window keeps narrow low bands sharp, while the
        // tapered ends remove the truncation step (a Kaiser window smeared 100 Hz Q 4 by ~2 dB)
        bool diagIdentity[2] { true, true };
        for (int e = 0; e < kEntries; ++e)
        {
            const bool diag = e == 0 || e == 3;
            set.nonzero[(size_t) e] = false;
            if (e / 2 >= nCh || (e % 2) >= nCh)
                continue;
            double dev = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const double m = dMat[(size_t) e][(size_t) k];
                dev = std::max (dev, diag ? std::abs (m - 1.0) : std::abs (m));
                dSpec[(size_t) k] = { m, 0.0 };
            }
            if (dev < 1.2e-5)
                continue; // identity diagonal / zero cross term
            if (diag)
                diagIdentity[e / 3] = false;
            set.nonzero[(size_t) e] = true;
            fftN[q].inverse (dSpec.data(), dTime.data()); // zero-phase impulse response, peak at 0
            for (int i = 0; i < nq; ++i)
            {
                const int src = (i + nq / 2) % nq;
                const int edge = nq / 8;
                double w = 1.0;
                if (i < edge)
                    w = 0.5 - 0.5 * std::cos (3.14159265358979323846 * (i + 0.5) / edge);
                else if (i >= nq - edge)
                    w = 0.5 - 0.5 * std::cos (3.14159265358979323846 * (nq - i - 0.5) / edge);
                dShift[(size_t) i] = dTime[(size_t) src] * w;
            }
            partition (dShift, nq, set.spec[(size_t) e]);
        }
        for (int o = 0; o < 2; ++o)
        {
            const bool cross = nCh > 1 && set.nonzero[(size_t) (o * 2 + (1 - o))];
            set.outIdentity[(size_t) o] = o < nCh && diagIdentity[o] && ! cross;
            if (o < nCh && diagIdentity[o] && cross)
            {
                // identity diagonal next to a cross term: the diagonal must run as a real delta
                std::fill (dShift.begin(), dShift.begin() + nq, 0.0);
                dShift[(size_t) (nq / 2)] = 1.0;
                partition (dShift, nq, set.spec[(size_t) (o * 3)]);
                set.nonzero[(size_t) (o * 3)] = true;
            }
        }
    }

    double fs = 48000.0;
    int nCh = 2, quality = 1, n = 8192, p = 256, fdlPos = 0, inPos = 0;
    int activeSet = -1, oldSet = -1;
    bool fading = false, synchronous = false;
    DesignFn design;
    RealFft<float> fftP[3], fftPd[3];
    RealFft<double> fftN[3];
    std::array<Set, kSets> sets;
    std::array<Input, 2> inputs;
    std::array<Output, 2> outputs;
    std::vector<std::complex<float>> accY;
    std::vector<float> timeBuf;

    // designer-side scratch (allocated in prepare)
    std::array<std::vector<double>, kEntries> dMat;
    std::vector<std::complex<double>> dSpec;
    std::vector<double> dTime, dShift;
    std::vector<float> dPart;
    std::vector<std::complex<float>> dPartSpec;
    std::vector<float> designerParams;

    std::unique_ptr<std::atomic<float>[]> snapshot;
    int nParams = 0;
    std::atomic<unsigned> seq { 0 }, requestSeq { 0 };
    std::atomic<int> reqQuality { 1 };
    unsigned doneSeq = 0;
    std::atomic<bool> running { false };
    std::thread worker;
};
} // namespace penta::dsp
