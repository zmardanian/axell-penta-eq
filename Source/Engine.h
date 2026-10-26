#pragma once
// Axell Penta EQ - the complete processing engine (framework independent).
//
// Signal flow and design rationale: docs/ENGINEERING_SPEC.md. Everything runs in control segments
// of at most 16 base-rate samples whose boundaries sit on absolute sample positions, so the output
// is bit-identical for any host block size. No allocation, locks or I/O in process().

#include "Oversampling.h"
#include "Params.h"
#include "dsp/Bands.h"
#include "dsp/Chain.h"
#include "dsp/Correction.h"
#include "dsp/LinearPhase.h"
#include "dsp/Rack.h"
#include "dsp/Spectral.h"

#include <axell/dsp/SibilanceDetector.h>   // 3.0: the "Ess" detector source (Axell Velvet 3.0's LIVE network, causal)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64)
 #include <xmmintrin.h>
 #define PENTA_HAS_SSE 1
#endif

namespace penta
{
constexpr int kCtrl = 16;               // control block (base samples)
constexpr int kAnalysisInterval = 512;  // static analysis / correction redesign (base samples)
constexpr int kNumSlots = kNumBands + 2; // 0 = HP, 1..12 = bands, 13 = LP
constexpr int kMaxOs = 4;

//==============================================================================
// single-producer / single-consumer float FIFO (audio thread -> GUI analyzer)
class SpscFifo
{
public:
    void init (int capacity)
    {
        buf.assign ((size_t) capacity, 0.0f);
        cap = capacity;
        w.store (0);
        r.store (0);
    }
    void push (const float* x, int n) noexcept
    {
        if (cap == 0)
            return;
        int wp = w.load (std::memory_order_relaxed);
        const int rp = r.load (std::memory_order_acquire);
        const int free = cap - 1 - ((wp - rp + cap) % cap);
        n = std::min (n, free);
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) wp] = x[i];
            wp = (wp + 1 == cap) ? 0 : wp + 1;
        }
        w.store (wp, std::memory_order_release);
    }
    int pull (float* out, int maxN) noexcept
    {
        if (cap == 0)
            return 0;
        int rp = r.load (std::memory_order_relaxed);
        const int wp = w.load (std::memory_order_acquire);
        int n = 0;
        while (rp != wp && n < maxN)
        {
            out[n++] = buf[(size_t) rp];
            rp = (rp + 1 == cap) ? 0 : rp + 1;
        }
        r.store (rp, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> buf;
    int cap = 0;
    std::atomic<int> w { 0 }, r { 0 };
};

struct EngineMeters
{
    float inPeak[2] {}, outPeak[2] {};
    std::array<float, kNumBands> delta {};
    float autoGainDb = 0.0f;
};

//==============================================================================
// Static curve model (analog prototypes) for the GUI and the auto-gain estimate.
struct CurveModel
{
    std::array<dsp::ChainDesign, kNumSlots> slot;
    std::array<bool, kNumSlots> on {};
    std::array<int, kNumSlots> place {};
    std::array<dsp::ChainDesign, dsp::kNumModules> modPre, modPost;
    std::array<bool, dsp::kNumModules> modOn {};
    double fs = 48000.0;

    // deltaDb: optional dynamic gain per band (live curve)
    void build (const std::vector<float>& p, double sampleRate, const float* deltaDb = nullptr)
    {
        fs = sampleRate;
        for (int s = 0; s < kNumSlots; ++s)
        {
            dsp::BandShape b;
            bool active = false;
            slotShape (p, s, b, active, place[(size_t) s]);
            if (deltaDb != nullptr && s >= 1 && s <= kNumBands)
                b.gainDb += deltaDb[s - 1];
            on[(size_t) s] = active;
            dsp::buildBand (b, fs, slot[(size_t) s]);
        }
        for (int m = 0; m < dsp::kNumModules; ++m)
        {
            const auto ms = dsp::rack::read (m, p);
            modOn[(size_t) m] = ms.on;
            dsp::rack::build (m, ms, fs, modPre[(size_t) m], modPost[(size_t) m]);
        }
    }

    double slotMagSq (int s, double hz) const noexcept
    {
        return dsp::designMagSq (slot[(size_t) s], 2.0 * dsp::kPi * hz / fs);
    }
    double moduleMagSq (int m, double hz) const noexcept
    {
        const double w = 2.0 * dsp::kPi * hz / fs;
        return dsp::designMagSq (modPre[(size_t) m], w) * dsp::designMagSq (modPost[(size_t) m], w);
    }
    // total (all placements combined, as heard on a centred mono source)
    double totalDb (double hz) const noexcept
    {
        double m = 1.0;
        for (int s = 0; s < kNumSlots; ++s)
            if (on[(size_t) s] && place[(size_t) s] != kSide)
                m *= slotMagSq (s, hz);
        for (int k = 0; k < dsp::kNumModules; ++k)
            if (modOn[(size_t) k])
                m *= moduleMagSq (k, hz);
        return 10.0 * std::log10 (std::max (m, 1.0e-30));
    }

    // read a slot (0 HP, 1..12 bands, 13 LP) from parameter values
    static void slotShape (const std::vector<float>& p, int s, dsp::BandShape& b, bool& active, int& placement)
    {
        const auto& x = idx();
        auto ch = [&p] (int i) { return (int) std::lround (p[(size_t) i]); };
        placement = kStereo;
        b.mode = 0;
        b.gainDb = 0.0;
        if (s == 0 || s == kNumSlots - 1)
        {
            const bool hp = s == 0;
            active = p[(size_t) (hp ? x.hpOn : x.lpOn)] > 0.5f;
            b.type = hp ? kLowCut : kHighCut;
            b.freqHz = p[(size_t) (hp ? x.hpFreq : x.lpFreq)];
            b.slopeIdx = ch (hp ? x.hpSlope : x.lpSlope);
            b.q = p[(size_t) (hp ? x.hpQ : x.lpQ)];
            return;
        }
        const auto& bi = x.band[s - 1];
        active = p[(size_t) bi[bpUsed]] > 0.5f && p[(size_t) bi[bpBypass]] < 0.5f;
        b.type = std::clamp (ch (bi[bpType]), 0, 7);
        b.freqHz = p[(size_t) bi[bpFreq]];
        // Gain Scale (global, 0..200 %) scales every band's static gain
        b.gainDb = p[(size_t) bi[bpGain]] * std::clamp (p[(size_t) x.gainScale], 0.0f, 200.0f) * 0.01f;
        b.q = p[(size_t) bi[bpQ]];
        b.slopeIdx = std::clamp (ch (bi[bpSlope]), 0, 7);
        b.mode = std::clamp (ch (bi[bpMode]), 0, 1);
        placement = std::clamp (ch (bi[bpPlace]), 0, 4);
    }
};

//==============================================================================
class Engine
{
public:
    Engine() : params (ParamValues().v) {}

    void prepare (double sampleRate, int numChannels, int maxBlockSize)
    {
        fs = sampleRate;
        nCh = std::clamp (numChannels, 1, 2);
        maxBlock = std::max (1, maxBlockSize);
        os.prepare (kCtrl, fs);
        cd.prepare (fs);
        const int corrMax = std::max (cd.minPhaseLength(), cd.naturalLength());
        for (auto& l : minLane)
            l.prepare (corrMax, 256);
        for (auto& l : natLane)
            l.prepare (corrMax, 256);
        for (auto& t : firTmp.taps) // sized here so the audio thread never allocates
            t.assign ((size_t) corrMax, 0.0f);
        for (int b = 1; b <= kNumBands; ++b)
            slots[(size_t) b].spec.prepare (fs);
        essDet.prepare (fs, axell::voice::DetectorConfig::live());
        essRelK = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
        essP = essW = 0.0f;
        lp.prepare (fs, nCh, [this] (const std::vector<float>& prm, int bins, std::array<std::vector<double>, 4>& m)
                    { designLinear (prm, bins, m); }, numParams());
        lp.setSynchronous (offline);
        maxLatency = os.latencyFor (2) + std::max (dsp::LinearPhaseEq::latencyFor (2, fs), cd.naturalLatency());
        for (auto& d : dry)
            d.assign ((size_t) (maxLatency + kCtrl + 1), 0.0f);
        for (auto& d : detDelay)
            d.assign ((size_t) (maxLatency + kCtrl + 1), 0.0f);
        // K-weighting (BS.1770 shape) on a pink (log-spaced) grid for the auto-gain estimate
        {
            const auto shelf = dsp::proto::highShelf2 (2.0 * dsp::kPi * 1681.0 / 48000.0, 0.7071, 4.0);
            const auto hp = dsp::proto::highpass2 (2.0 * dsp::kPi * 38.0 / 48000.0, 0.5);
            for (int i = 0; i < kAgPoints; ++i)
            {
                agHz[(size_t) i] = 20.0 * std::pow (1000.0, i / (double) (kAgPoints - 1));
                const double w = 2.0 * dsp::kPi * agHz[(size_t) i] / 48000.0;
                agW[(size_t) i] = shelf.magSq (w) * hp.magSq (w);
            }
        }
        staticNow.reserve (2048);
        lastStatic.reserve (2048);
        preFifo.init (32768);
        postFifo.init (32768);
        scFifo.init (32768);
        prepared = true;
        active.phase = -1; // force configuration
        reset();
        // a fresh start: the wrapper reports this latency right after prepare, before audio runs
        hostLat = latency;
        reqOpen = holding = false;
        latReq.store (pack (kReqIdle, 0, latency), std::memory_order_release);
        gfade = gfadeTgt = 1.0;
    }

    void reset()
    {
        if (! prepared)
            return;
        absPos = 0;
        decodeConfig (want);
        // a configuration whose latency the host does not compensate yet is not applied here: the old one
        // stays, the output starts muted and control() runs the handshake (see hostReady)
        const bool apply = active.phase < 0 || ! tracking() || latencyOf (want) == hostLat;
        if (apply)
            applyConfig (want);
        else
            applyConfig (Config (active));
        gfade = gfadeTgt = apply ? 1.0 : 0.0;
        gWarm = 0;
        smoothSnap = true;
        smoothSnapAg = true;
        for (auto& d : dry)
            std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : detDelay)
            std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        for (int b = 1; b <= kNumBands; ++b)
            slots[(size_t) b].spec.reset();
        soloFade = 0.0;
        soloCur = -1;
        for (auto& r : soloRf)
            r.reset();
    }

    // offline (non-realtime) rendering: linear-phase designs run synchronously -> deterministic
    void setNonRealtime (bool nonRealtime)
    {
        offline = nonRealtime;
        lp.setSynchronous (nonRealtime);
    }

    void setParams (const std::vector<float>& v) noexcept
    {
        const size_t n = std::min (v.size(), params.size());
        for (size_t i = 0; i < n; ++i)
            params[i] = v[i];
    }
    const std::vector<float>& getParams() const noexcept { return params; }

    void setSoloBand (int band) noexcept { soloReq.store (band); } // -1 = off, 0..11
    void setAnalyzerEnabled (bool e) noexcept { analyzerOn.store (e); }

    // any thread: the latency of the active configuration (published by the audio thread)
    int getLatencySamples() const noexcept { return latencyPub.load (std::memory_order_acquire); }

    //== latency handshake with the wrapper (message thread) =====================
    // The engine never plays a configuration whose latency the host does not compensate. To change
    // the latency it fades out, then posts a request; the wrapper reports the latency to the host and
    // acknowledges; only then the new configuration is applied (warm-up, fade-in). If no report comes
    // within 0.5 s the engine withdraws the request - atomically, so a report can never land
    // afterwards - and fades the old configuration back in (it is what the host still compensates);
    // it tries again once the message thread is alive (heartbeat). DECISIONS D24.
    // after prepare: enables the handshake (without it - tests, tools - configurations switch at once)
    void setHostLatency (int samples) noexcept { hostTracking.store (samples >= 0, std::memory_order_release); }
    // the wrapper's timer: one tick (proves the message thread runs)
    void heartbeat() noexcept { beat.fetch_add (1, std::memory_order_acq_rel); }
    // the wrapper's timer: claims a pending request; true -> report `samples` to the host, then call
    // ackLatencyRequest()
    bool takeLatencyRequest (int& samples) noexcept
    {
        auto r = latReq.load (std::memory_order_acquire);
        if (stateOf (r) != kReqPosted)
            return false;
        if (! latReq.compare_exchange_strong (r, pack (kReqReporting, seqOf (r), latOf (r)), std::memory_order_acq_rel))
            return false; // withdrawn meanwhile
        samples = latOf (r);
        return true;
    }
    void ackLatencyRequest() noexcept
    {
        const auto r = latReq.load (std::memory_order_acquire);
        if (stateOf (r) == kReqReporting)
            latReq.store (pack (kReqAcked, seqOf (r), latOf (r)), std::memory_order_release);
    }
    double getSampleRate() const noexcept { return fs; }
    int getNumChannels() const noexcept { return nCh; }

    const EngineMeters& getMeters() const noexcept { return meters; }
    // GUI: the spectral gain curve of band b (0..11) at display point i (dB, 20 Hz .. 20 kHz log-spaced)
    float spectralDisplayDb (int b, int i) const noexcept { return slots[(size_t) (b + 1)].spec.displayDb (i); }
    bool spectralActive (int b) const noexcept { return ! slots[(size_t) (b + 1)].spec.isIdle(); }
    void clearMeters() noexcept
    {
        for (int c = 0; c < 2; ++c)
            meters.inPeak[c] = meters.outPeak[c] = 0.0f;
    }

    SpscFifo& preAnalyzer() noexcept { return preFifo; }
    SpscFifo& postAnalyzer() noexcept { return postFifo; }
    SpscFifo& scAnalyzer() noexcept { return scFifo; }

    // io: numChannels channel pointers (processed in place); sc: optional side-chain input
    void process (float* const* io, int numChannels, int numSamples, const float* const* sc = nullptr,
                  int scChannels = 0) noexcept
    {
        if (! prepared || numSamples <= 0)
            return;
#if PENTA_HAS_SSE
        const unsigned int csr = _mm_getcsr();
        _mm_setcsr (csr | 0x8040); // flush-to-zero + denormals-are-zero
#endif
        const int chs = std::min (numChannels, nCh);
        int pos = 0;
        while (pos < numSamples)
        {
            const int seg = std::min (numSamples - pos, kCtrl - (int) (absPos % kCtrl));
            segment (io, chs, pos, seg, sc, scChannels);
            pos += seg;
            absPos += seg;
        }
#if PENTA_HAS_SSE
        _mm_setcsr (csr);
#endif
    }

private:
    //== configuration (phase mode / oversampling / linear quality) =============
    struct Config
    {
        int phase = 0, osLog2 = 0, quality = 1;
        bool operator== (const Config& o) const { return phase == o.phase && osLog2 == o.osLog2 && quality == o.quality; }
        bool operator!= (const Config& o) const { return ! (*this == o); }
    };

    void decodeConfig (Config& c) const noexcept
    {
        const auto& x = idx();
        c.phase = std::clamp ((int) std::lround (params[(size_t) x.phase]), 0, 2);
        c.osLog2 = std::clamp ((int) std::lround (params[(size_t) x.os]), 0, 2);
        c.quality = std::clamp ((int) std::lround (params[(size_t) x.lpQuality]), 0, 2);
    }

    void applyConfig (const Config& c) noexcept
    {
        active = c;
        osF = c.phase == kLinear ? 1 : (1 << c.osLog2);
        rackOsF = 1 << c.osLog2;
        fsBands = fs * osF;
        fsRack = fs * rackOsF;
        for (auto& s : slots)
        {
            s.designed = false;
            s.chain.resetStates();
            s.det.reset();
            s.env[0] = s.env[1] = 0.0;
            s.delta = 0.0;
        }
        for (int b = 1; b <= kNumBands; ++b)
        {
            auto& sp = slots[(size_t) b].spec;
            sp.setRate (c.phase == kLinear ? 1 : (1 << c.osLog2));
            sp.reset();
            slots[(size_t) b].wType = -1;
        }
        for (auto& m : mods)
        {
            m.designed = false;
            m.pre.resetStates();
            m.post.resetStates();
            m.nl = {};
        }
        os.reset();
        for (auto& l : minLane)
            l.reset();
        for (auto& l : natLane)
            l.reset();
        minOn.fill (false);
        minOff.fill (false);
        natOn.fill (false);
        natOff.fill (false);
        for (auto& l : natLane)
            l.setZero();
        if (c.phase == kNatural)
            for (int e : { 0, 3 })
                natLane[(size_t) e].setIdentity (cd.naturalLatency(), false);
        if (c.phase == kLinear)
            lp.configure (c.quality);
        const int osLat = c.osLog2 > 0 ? os.latencyFor (c.osLog2) : 0;
        latency = latencyOf (c);
        detLatency = c.phase == kLinear ? dsp::LinearPhaseEq::latencyFor (c.quality, fs) : 0;
        if (c.phase == kLinear && rackPre())
            detLatency += osLat;
        latencyPub.store (latency, std::memory_order_release);
        lastStatic.clear();
        forceAnalysis = true;
    }

    int latencyOf (const Config& c) const noexcept
    {
        return (c.osLog2 > 0 ? os.latencyFor (c.osLog2) : 0) + (c.phase == kNatural ? cd.naturalLatency() : 0)
             + (c.phase == kLinear ? dsp::LinearPhaseEq::latencyFor (c.quality, fs) : 0);
    }

    bool tracking() const noexcept { return hostTracking.load (std::memory_order_acquire) && ! offline; }

    // request word: state (3 bits) | sequence (29 bits) | latency (32 bits)
    enum : std::uint64_t { kReqIdle = 0, kReqPosted = 1, kReqReporting = 2, kReqAcked = 3, kReqWithdrawn = 4 };
    static std::uint64_t pack (std::uint64_t st, std::uint64_t seq, int lat) noexcept
    {
        return st | ((seq & 0x1fffffffu) << 3) | ((std::uint64_t) (std::uint32_t) lat << 32);
    }
    static std::uint64_t stateOf (std::uint64_t r) noexcept { return r & 7u; }
    static std::uint64_t seqOf (std::uint64_t r) noexcept { return (r >> 3) & 0x1fffffffu; }
    static int latOf (std::uint64_t r) noexcept { return (int) (std::uint32_t) (r >> 32); }

    // Does the host compensate `need`? Runs the request: posts one only while the output is silent
    // (mayPost), takes the acknowledgement, withdraws a request that is stale (the target changed) or
    // unanswered for 0.5 s; after such a timeout the engine holds the configuration it plays until the
    // message thread shows a heartbeat again.
    bool hostReady (int need, bool mayPost) noexcept
    {
        if (reqOpen)
        {
            auto r = latReq.load (std::memory_order_acquire);
            if (seqOf (r) == reqSeq && stateOf (r) == kReqAcked)
            {
                hostLat = latOf (r);
                reqOpen = false;
            }
            else
            {
                reqWait += kCtrl;
                const bool stale = latOf (r) != need, late = reqWait >= (int) (0.5 * fs);
                // only a request nobody has claimed can be withdrawn; one being reported is awaited
                if ((stale || late) && stateOf (r) == kReqPosted
                    && latReq.compare_exchange_strong (r, pack (kReqWithdrawn, reqSeq, latOf (r)), std::memory_order_acq_rel))
                {
                    reqOpen = false;
                    if (late && ! stale)
                    {
                        holding = true;
                        holdBeat = beat.load (std::memory_order_acquire);
                    }
                }
            }
        }
        if (! reqOpen && hostLat != need && mayPost && ! holding)
        {
            reqSeq = (reqSeq + 1) & 0x1fffffffu;
            latReq.store (pack (kReqPosted, reqSeq, need), std::memory_order_release);
            reqOpen = true;
            reqWait = 0;
        }
        return ! reqOpen && hostLat == need;
    }

    bool rackPre() const noexcept { return std::lround (params[(size_t) idx().rackPos]) == 1; }

    //== per-band state ==========================================================
    static constexpr int kEssSrc = kNumBands + 2;   // the detector source "Ess" (after the bands and External)
    axell::voice::SibilanceDetector essDet;
    float essP = 0.0f, essW = 0.0f, essRelK = 0.001f;
    bool essUsed = false;

    struct Slot
    {
        dsp::Chain chain;
        // targets
        bool tActive = false;
        int tType = kBell, tSlope = 3, tMode = 0, tPlace = kStereo;
        double tF = 1000, tG = 0, tQ = 1;
        bool dyn = false;
        double thr = -24, range = -6, ratio = 4, atk = 5, rel = 120;
        int src = 0;
        // smoothed
        double lf = 10, g = 0, lq = 0;
        int key = -1;
        bool keyPending = false;
        int aType = kBell, aSlope = 3, aMode = 0, aPlace = kStereo; // applied (processing) structure
        double fade = 0, fadeTgt = 0;
        int warm = 0;        // samples (band rate) the new structure runs silently before it fades in
        bool warmed = false; // warm-up done for the current structure
        bool designed = false;
        double dLf = 0, dG = 0, dLq = 0;
        // dynamics
        dsp::RegionFilter det;
        double env[2] {}, delta = 0, detC = 0;
        bool dynLive = false;
        std::array<float, kCtrl * kMaxOs> fadeBuf {};
        // spectral dynamics (Dynamic Mode = Spectral): per-frequency gain as a min-phase FIR after the band
        bool spectral = false, specWasOn = false;
        dsp::SpectralDynamics spec;
        int wType = -1, wSlope = -1;
        double wLf = 0.0, wLq = 0.0;
        std::array<float, kCtrl * kMaxOs> specTmp {};
    };

    struct Mod
    {
        dsp::Chain pre, post;
        dsp::ModuleSettings cur, tgt;
        double fade = 0, fadeTgt = 0;
        bool designed = false;
        dsp::rack::NonlinearState nl;
        std::array<float, kCtrl * kMaxOs> fadeBuf {};
        std::array<std::array<float, kCtrl * kMaxOs>, 2> tmp {};
    };

    // 20 ms, rounded to whole control blocks so state changes land on block boundaries
    int warmSamples() const noexcept
    {
        const int blk = kCtrl * osF;
        return std::max (1, (int) std::lround (0.02 * fsBands / blk)) * blk;
    }

    static int slotKey (const Slot& s) noexcept { return s.tType + 8 * s.tSlope + 64 * s.tMode + 128 * s.tPlace; }

    void readSlotTargets() noexcept
    {
        const auto& x = idx();
        bool ess = false;
        for (int b = 1; b <= kNumBands; ++b)
            ess = ess || (params[(size_t) x.band[b - 1][bpDyn]] > 0.5f
                          && (int) std::lround (params[(size_t) x.band[b - 1][bpSrc]]) == kEssSrc);
        if (ess && ! essUsed)
        {
            essDet.reset();
            essP = essW = 0.0f;
        }
        essUsed = ess;
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& sl = slots[(size_t) s];
            dsp::BandShape b;
            bool act = false;
            int place = kStereo;
            CurveModel::slotShape (params, s, b, act, place);
            sl.tActive = act;
            sl.tType = b.type;
            sl.tSlope = b.slopeIdx;
            sl.tMode = b.mode;
            sl.tPlace = place;
            sl.tF = std::clamp (b.freqHz, 5.0, 60000.0);
            sl.tG = b.gainDb;
            sl.tQ = std::clamp (b.q, 0.05, 40.0);
            if (s >= 1 && s <= kNumBands)
            {
                const auto& bi = x.band[s - 1];
                sl.dyn = params[(size_t) bi[bpDyn]] > 0.5f && dsp::isGainType (sl.tType);
                sl.thr = params[(size_t) bi[bpThr]];
                sl.range = params[(size_t) bi[bpRange]];
                sl.ratio = std::max (1.0f, params[(size_t) bi[bpRatio]]);
                sl.atk = std::max (0.05f, params[(size_t) bi[bpAtk]]);
                sl.rel = std::max (1.0f, params[(size_t) bi[bpRel]]);
                sl.src = std::clamp ((int) std::lround (params[(size_t) bi[bpSrc]]), 0, kEssSrc);
                sl.spectral = sl.dyn && params[(size_t) bi[bpDynMode]] > 0.5f;
            }
            else
                sl.dyn = sl.spectral = false;
        }
    }

    void controlSlot (Slot& s, bool linear, int steps) noexcept
    {
        const int key = slotKey (s);
        const bool tgtOn = s.tActive && (! linear || s.dyn);
        if (key != s.key)
        {
            if (s.fade <= 0.0)
            {
                s.key = key;
                s.keyPending = false;
                s.designed = false;
                s.warm = 0;
                s.warmed = false;
                s.aType = s.tType;
                s.aSlope = s.tSlope;
                s.aMode = s.tMode;
                s.aPlace = s.tPlace;
            }
            else
                s.keyPending = true;
        }
        const bool wantOn = tgtOn && ! s.keyPending;
        if (smoothSnap)
            s.warmed = true;
        if (wantOn && s.fade <= 0.0 && ! s.warmed && s.warm <= 0)
            s.warm = warmSamples(); // a new / re-activated structure starts warm
        s.fadeTgt = (wantOn && s.warm <= 0) ? 1.0 : 0.0;
        if (smoothSnap)
            s.fade = s.fadeTgt;
        if (! wantOn && s.fade <= 0.0)
            s.warmed = false;
        const double tlf = std::log2 (s.tF), tlq = std::log2 (s.tQ);
        if ((s.fade <= 0.0 && s.fadeTgt <= 0.0) || ! s.designed || smoothSnap)
        {
            s.lf = tlf;
            s.g = s.tG;
            s.lq = tlq;
        }
        if (s.fade <= 0.0 && s.fadeTgt <= 0.0 && s.warm <= 0)
        {
            s.designed = false;
            return;
        }
        auto smooth = [this] (double& v, double t, double eps)
        {
            v += (t - v) * smoothCoef;
            if (std::abs (t - v) < eps)
                v = t;
        };
        smooth (s.lf, tlf, 1.0e-6);
        smooth (s.g, s.tG, 1.0e-6);
        smooth (s.lq, tlq, 1.0e-6);
        const double gain = linear ? s.delta : s.g + s.delta;
        if (! s.designed || ! dsp::same (s.lf, s.dLf) || ! dsp::same (gain, s.dG) || ! dsp::same (s.lq, s.dLq))
        {
            dsp::BandShape b;
            b.type = s.aType;
            b.slopeIdx = s.aSlope;
            b.mode = s.aMode;
            b.freqHz = std::exp2 (s.lf);
            b.gainDb = gain;
            b.q = std::exp2 (s.lq);
            dsp::buildBand (b, fsBands, design, ! linear);
            if (! s.designed)
                s.chain.jumpTo (design);
            else
                s.chain.target (design, steps);
            s.designed = true;
            s.dLf = s.lf;
            s.dG = gain;
            s.dLq = s.lq;
        }
    }

    void controlDynamics (int si) noexcept
    {
        auto& s = slots[(size_t) si];
        const bool live = s.dyn && s.tActive;
        if (! live)
        {
            if (s.dynLive)
            {
                s.det.reset();
                s.env[0] = s.env[1] = 0.0;
            }
            s.dynLive = false;
            s.delta = 0.0;
            return;
        }
        // detector region: own band, another band's region, or the external side-chain
        int type = s.tType;
        double f = s.tF, q = s.tQ;
        if (s.src >= 1 && s.src <= kNumBands)
        {
            const auto& o = slots[(size_t) s.src];
            type = o.tType;
            f = o.tF;
            q = o.tQ;
        }
        s.det.set (type, f, q, fs);
        // detector: power average over ~3 cycles of the band (0.5-20 ms); the user's attack and
        // release then act on the gain change itself (dB domain), so they mean what they say
        const double tDet = std::clamp (3.0 / std::max (f, 20.0), 0.0005, 0.02);
        s.detC = 1.0 - std::exp (-1.0 / (tDet * fs));
        if (s.dynLive)
        {
            const double pw = std::max (s.env[0], s.env[1]);
            const double level = 10.0 * std::log10 (2.0 * pw + 1.0e-20);
            const double over = level - s.thr, w = 6.0;
            double o = 0.0;
            if (over >= 0.5 * w)
                o = over;
            else if (over > -0.5 * w)
                o = (over + 0.5 * w) * (over + 0.5 * w) / (2.0 * w);
            const double amount = o * (1.0 - 1.0 / s.ratio);
            // Spectral mode: the band does not move as a whole (any broadband move releases to 0 dB)
            double target = s.spectral ? 0.0 : (s.range < 0.0 ? -1.0 : 1.0) * std::min (std::abs (s.range), amount);
            if (s.src == kEssSrc)
                target *= essW;   // Ess: the band's own level decides how much, the sibilance detector whether
            const double tau = (std::abs (target) > std::abs (s.delta) ? s.atk : s.rel) * 0.001;
            s.delta += (target - s.delta) * (1.0 - std::exp (-(double) kCtrl / (tau * fs)));
        }
        s.dynLive = true;
    }

    void controlModule (int id) noexcept
    {
        auto& m = mods[(size_t) id];
        m.tgt = dsp::rack::read (id, params);
        m.fadeTgt = m.tgt.on ? 1.0 : 0.0;
        if (smoothSnap)
            m.fade = m.fadeTgt;
        if ((m.fade <= 0.0 && m.fadeTgt <= 0.0) || ! m.designed || smoothSnap)
            m.cur = m.tgt;
        if (m.fade <= 0.0 && m.fadeTgt <= 0.0)
        {
            m.designed = false;
            return;
        }
        bool changed = ! m.designed;
        for (int i = 0; i < 16; ++i)
        {
            float& v = m.cur.v[i];
            const float t = m.tgt.v[i];
            if (dsp::same (v, t))
                continue;
            changed = true;
            if (dsp::rack::isSwitch (id, i))
                v = t;
            else if (dsp::rack::isLogValue (id, i) && v > 0.0f && t > 0.0f)
            {
                const double lv = std::log2 (v) + (std::log2 (t) - std::log2 (v)) * smoothCoef;
                v = std::abs (lv - std::log2 (t)) < 1.0e-6 ? t : (float) std::exp2 (lv);
            }
            else
            {
                v += (t - v) * (float) smoothCoef;
                if (std::abs (t - v) < 1.0e-5f)
                    v = t;
            }
        }
        if (changed)
        {
            dsp::rack::build (id, m.cur, fsRack, design, design2);
            if (! m.designed)
            {
                m.pre.jumpTo (design);
                m.post.jumpTo (design2);
            }
            else
            {
                m.pre.target (design, kCtrl * rackOsF);
                m.post.target (design2, kCtrl * rackOsF);
            }
            m.designed = true;
        }
    }

    //== control boundary =========================================================
    void control() noexcept
    {
        smoothCoef = 1.0 - std::exp (-(double) kCtrl / (0.02 * fs));
        // phase mode / OS / quality: fade the output out, switch, fade back in
        decodeConfig (want);
        const bool track = tracking();
        if (holding && beat.load (std::memory_order_acquire) != holdBeat)
            holding = false; // the message thread is alive again: try the switch once more
        if (! track)
            holding = reqOpen = false;
        // while holding, the configuration that plays is the target (the host still compensates it)
        const Config tgt = holding ? active : want;
        const bool ready = ! track || hostReady (latencyOf (tgt), gfade <= 0.0);
        if (tgt != active)
        {
            gfadeTgt = 0.0;
            if (gfade <= 0.0 && ready)
            {
                applyConfig (tgt);
                // run the reset filters and delay lines muted until they hold valid output (the new
                // latency, e.g. 90 ms for Linear Phase) plus 20 ms of filter warm-up - and one more host
                // block after the host was told, which it may have started under the old compensation
                gWarm = (int) (0.02 * fs) + latency + (track ? maxBlock : 0);
            }
        }
        else
            gfadeTgt = ! ready || gWarm > 0 ? 0.0 : 1.0;
        if (gWarm > 0)
            gWarm = std::max (0, gWarm - kCtrl);

        readSlotTargets();
        const bool linear = active.phase == kLinear;
        for (int s = 1; s <= kNumBands; ++s)
            controlDynamics (s);
        for (int s = 0; s < kNumSlots; ++s)
            controlSlot (slots[(size_t) s], linear, kCtrl * osF);
        for (int m = 0; m < dsp::kNumModules; ++m)
            controlModule (m);
        const auto& x = idx();
        driveTgt = std::clamp (params[(size_t) x.drive] * 0.01, 0.0, 1.0);
        drive = smoothSnap ? driveTgt : drive + (driveTgt - drive) * smoothCoef;
        if (std::abs (drive - driveTgt) < 1.0e-6)
            drive = driveTgt;

        // solo
        const int sr = soloReq.load (std::memory_order_relaxed);
        if (sr != soloCur)
        {
            if (soloFade <= 0.0)
            {
                soloCur = sr;
                for (auto& r : soloRf)
                    r.reset();
            }
        }
        if (soloCur >= 0 && soloCur < kNumBands)
        {
            const auto& s = slots[(size_t) (soloCur + 1)];
            for (auto& r : soloRf)
                r.set (s.tType, s.tF, s.tQ, fs);
        }
        soloTgt = (soloCur >= 0 && sr == soloCur) ? 1.0 : 0.0;

        if (forceAnalysis || absPos % kAnalysisInterval == 0)
            analyse();
        smoothSnap = false;
    }

    //== static analysis: correction FIRs, linear-phase request, auto gain ======
    void analyse() noexcept
    {
        // what the correction depends on: the static (non-dynamic) shapes, smoothed
        staticNow.clear();
        for (const auto& s : slots)
        {
            staticNow.push_back (s.fadeTgt);
            staticNow.push_back (s.key);
            staticNow.push_back (s.aPlace);
            staticNow.push_back (s.lf);
            staticNow.push_back (s.g);
            staticNow.push_back (s.lq);
        }
        for (const auto& m : mods)
        {
            staticNow.push_back (m.fadeTgt);
            for (float v : m.cur.v)
                staticNow.push_back (v);
        }
        if (active.phase == kLinear)
            for (float v : params)
                staticNow.push_back (v);
        if (! forceAnalysis && staticNow == lastStatic)
            return;
        forceAnalysis = false;
        lastStatic = staticNow;

        if (active.phase == kLinear)
            lp.request (params);
        else
            designCorrection (active.phase == kNatural);
        computeAutoGain();
    }

    int laneMask (int place) const noexcept
    {
        if (nCh == 1)
            return place == kSide ? 0 : dsp::laneL;
        switch (place)
        {
            case kLeft: return dsp::laneL;
            case kRight: return dsp::laneR;
            case kMid: return dsp::laneM;
            case kSide: return dsp::laneS;
            default: return dsp::laneL | dsp::laneR;
        }
    }

    template <typename Fn>
    void forEachStaticSection (Fn&& fn) noexcept
    {
        // bands (static gain) at the band rate
        for (int si = 0; si < kNumSlots; ++si)
        {
            const auto& s = slots[(size_t) si];
            if (s.fadeTgt <= 0.0)
                continue;
            dsp::BandShape b;
            b.type = s.aType;
            b.slopeIdx = s.aSlope;
            b.mode = s.aMode;
            b.freqHz = std::exp2 (s.lf);
            b.gainDb = s.g;
            b.q = std::exp2 (s.lq);
            dsp::buildBand (b, fsBands, design);
            if (design.parallel)
            {
                const auto comp = dsp::designMatched (design.sect[0]);
                dsp::Realisation e;
                e.q[0] = comp;
                e.q[0].b0 = 1.0 + design.parallelC * comp.b0;
                e.q[0].b1 = comp.a1 + design.parallelC * comp.b1;
                e.q[0].b2 = comp.a2 + design.parallelC * comp.b2;
                fn (s.aPlace, design.parallelEquiv, e, osLog2Bands());
                continue;
            }
            // the chain's own realisations when it runs exactly this static design (a fitted
            // near-Nyquist realisation depends on its tracking history); otherwise a fresh one
            const bool chainIsStatic = s.designed && dsp::same (s.dLf, s.lf) && dsp::same (s.dG, s.g) && dsp::same (s.dLq, s.lq)
                                    && s.chain.size() == design.count && ! s.chain.current().parallel;
            for (int k = 0; k < design.count; ++k)
                fn (s.aPlace, design.sect[(size_t) k],
                    chainIsStatic ? s.chain.realisation (k) : dsp::realise (design.sect[(size_t) k]), osLog2Bands());
        }
        for (int mi = 0; mi < dsp::kNumModules; ++mi)
        {
            const auto& m = mods[(size_t) mi];
            if (m.fadeTgt <= 0.0)
                continue;
            dsp::rack::build (mi, m.cur, fsRack, design, design2);
            for (const auto* d : { &design, &design2 })
                for (int k = 0; k < d->count; ++k)
                    fn ((int) kStereo, d->sect[(size_t) k], dsp::realise (d->sect[(size_t) k]), active.osLog2);
        }
    }

    int osLog2Bands() const noexcept { return active.phase == kLinear ? 0 : active.osLog2; }

    void designCorrection (bool natural) noexcept
    {
        if (! natural)
        {
            cd.clear (false);
            forEachStaticSection ([this] (int place, const dsp::Section& s, const dsp::Realisation& q, int osl)
                                  { cd.add (laneMask (place), s, q, osl); });
            cd.finish (firTmp, false);
            for (int l = 0; l < dsp::kNumLanes; ++l)
            {
                if (firTmp.active[(size_t) l])
                {
                    minLane[(size_t) l].setTaps (firTmp.taps[(size_t) l].data(), firTmp.length, minOn[(size_t) l] && ! smoothSnap);
                    minOn[(size_t) l] = true;
                    minOff[(size_t) l] = false;
                }
                else if (minOn[(size_t) l] && ! minOff[(size_t) l])
                {
                    minLane[(size_t) l].setIdentity (0, true); // fade to flat, then switch off
                    minOff[(size_t) l] = true;
                }
            }
            return;
        }
        cd.clearMatrix (nCh);
        forEachStaticSection ([this] (int place, const dsp::Section& s, const dsp::Realisation& q, int osl)
                              { cd.addMatrix (nCh == 1 ? (place == kSide ? kSide : kStereo) : place, s, q, osl); });
        const bool any = cd.finishMatrix (firTmp);
        for (int e = 0; e < 4; ++e)
        {
            auto& lane = natLane[(size_t) e];
            const bool diag = e == 0 || e == 3;
            const bool xfade = ! smoothSnap; // the first design after a reset applies at once
            if (diag)
            {
                if (any)
                    lane.setTaps (firTmp.taps[(size_t) e].data(), firTmp.length, xfade);
                else
                    lane.setIdentity (cd.naturalLatency(), xfade);
                continue;
            }
            if (any && firTmp.active[(size_t) e])
            {
                lane.setTaps (firTmp.taps[(size_t) e].data(), firTmp.length, natOn[(size_t) e] && xfade);
                natOn[(size_t) e] = true;
                natOff[(size_t) e] = false;
            }
            else if (natOn[(size_t) e] && ! natOff[(size_t) e])
            {
                std::fill (firTmp.taps[(size_t) e].begin(), firTmp.taps[(size_t) e].end(), 0.0f);
                lane.setTaps (firTmp.taps[(size_t) e].data(), firTmp.length, true);
                natOff[(size_t) e] = true;
            }
        }
    }

    // Linear Phase target: 2x2 zero-phase matrix from the analog magnitudes (designer thread!)
    void designLinear (const std::vector<float>& p, int bins, std::array<std::vector<double>, 4>& m) const
    {
        const auto& x = idx();
        const int osl = std::clamp ((int) std::lround (p[(size_t) x.os]), 0, 2);
        const double fsR = fs * (1 << osl);
        dsp::ChainDesign d, d2;
        for (int si = 0; si < kNumSlots; ++si)
        {
            dsp::BandShape b;
            bool act = false;
            int place = kStereo;
            CurveModel::slotShape (p, si, b, act, place);
            if (! act || (nCh == 1 && place == kSide))
                continue;
            if (nCh == 1)
                place = kStereo;
            dsp::buildBand (b, fs, d, false);
            for (int k = 0; k < bins; ++k)
            {
                const double w = dsp::kPi * k / (bins - 1);
                const double h = std::sqrt (dsp::designMagSq (d, w));
                multiplyReal (m, k, place, h);
            }
        }
        // character modules run as IIR filters: fold their residual |analog/digital| into the FIR
        for (int mi = 0; mi < dsp::kNumModules; ++mi)
        {
            const auto ms = dsp::rack::read (mi, p);
            if (! ms.on)
                continue;
            dsp::rack::build (mi, ms, fsR, d, d2);
            for (const auto* dd : { &d, &d2 })
                for (int s = 0; s < dd->count; ++s)
                {
                    const auto& sec = dd->sect[(size_t) s];
                    const auto q = dsp::designMatched (sec);
                    for (int k = 0; k < bins; ++k)
                    {
                        const double w = dsp::kPi * k / (bins - 1) / (1 << osl);
                        const double hd = std::abs (q.response (w));
                        const double r = std::clamp (std::sqrt (sec.magSq (w)) / std::max (hd, 1.0e-30), 0.25, 4.0);
                        multiplyReal (m, k, kStereo, r);
                    }
                }
        }
    }

    static void multiplyReal (std::array<std::vector<double>, 4>& m, int k, int place, double h) noexcept
    {
        double b[4];
        switch (place)
        {
            case kLeft: b[0] = h; b[1] = 0; b[2] = 0; b[3] = 1; break;
            case kRight: b[0] = 1; b[1] = 0; b[2] = 0; b[3] = h; break;
            case kMid: b[0] = 0.5 * (h + 1); b[1] = 0.5 * (h - 1); b[2] = b[1]; b[3] = b[0]; break;
            case kSide: b[0] = 0.5 * (1 + h); b[1] = 0.5 * (1 - h); b[2] = b[1]; b[3] = b[0]; break;
            default: b[0] = h; b[1] = 0; b[2] = 0; b[3] = h; break;
        }
        const size_t i = (size_t) k;
        const double m0 = m[0][i], m1 = m[1][i], m2 = m[2][i], m3 = m[3][i];
        m[0][i] = b[0] * m0 + b[1] * m2;
        m[1][i] = b[0] * m1 + b[1] * m3;
        m[2][i] = b[2] * m0 + b[3] * m2;
        m[3][i] = b[2] * m1 + b[3] * m3;
    }

    void computeAutoGain() noexcept
    {
        // pink (log-spaced) x K-weighted power gain of the static curve, placement-weighted
        double num = 0.0, den = 0.0;
        for (int i = 0; i < kAgPoints; ++i)
        {
            const double hz = agHz[(size_t) i];
            double g = 1.0;
            for (int si = 0; si < kNumSlots; ++si)
            {
                const auto& s = slots[(size_t) si];
                if (s.fadeTgt <= 0.0)
                    continue;
                dsp::BandShape b;
                b.type = s.aType;
                b.slopeIdx = s.aSlope;
                b.mode = s.aMode;
                b.freqHz = std::exp2 (s.lf);
                b.gainDb = s.g;
                b.q = std::exp2 (s.lq);
                dsp::buildBand (b, fs, design);
                const double h = dsp::designMagSq (design, 2.0 * dsp::kPi * hz / fs);
                double share = 1.0; // fraction of the programme energy the band acts on
                switch (s.aPlace)
                {
                    case kLeft: case kRight: share = nCh == 1 ? 1.0 : 0.5; break;
                    case kMid: share = nCh == 1 ? 1.0 : 0.8; break;
                    case kSide: share = nCh == 1 ? 0.0 : 0.2; break;
                    default: break;
                }
                g *= share * h + (1.0 - share);
            }
            for (int mi = 0; mi < dsp::kNumModules; ++mi)
                if (mods[(size_t) mi].fadeTgt > 0.0)
                {
                    dsp::rack::build (mi, mods[(size_t) mi].cur, fs, design, design2);
                    const double w = 2.0 * dsp::kPi * hz / fs;
                    g *= dsp::designMagSq (design, w) * dsp::designMagSq (design2, w);
                }
            num += agW[(size_t) i] * g;
            den += agW[(size_t) i];
        }
        autoGainTgtDb = std::clamp (-10.0 * std::log10 (std::max (num / den, 1.0e-12)), -24.0, 24.0);
        meters.autoGainDb = (float) autoGainTgtDb;
    }

    //== audio ===================================================================
    void runSlotsAndRack (float* const* buf, int n, int F, bool linear) noexcept
    {
        bool ms = false; // current domain of buf (false = L/R)
        auto toMs = [&] (bool toMid)
        {
            if (nCh < 2 || toMid == ms)
                return;
            for (int i = 0; i < n; ++i)
            {
                const float a = buf[0][i], b = buf[1][i];
                if (toMid)
                {
                    buf[0][i] = 0.5f * (a + b);
                    buf[1][i] = 0.5f * (a - b);
                }
                else
                {
                    buf[0][i] = a + b;
                    buf[1][i] = a - b;
                }
            }
            ms = toMid;
        };
        const bool pre = rackPre();
        if (pre && ! linear)
            runRack (buf, n);
        const double fadeStep = 1.0 / (0.008 * fs * F);
        for (int si = 0; si < kNumSlots; ++si)
        {
            auto& s = slots[(size_t) si];
            if (s.fade <= 0.0 && s.fadeTgt <= 0.0 && s.warm <= 0)
                continue;
            if (! s.designed)
                continue;
            const bool warming = s.warm > 0 && s.fade <= 0.0;
            const bool character = s.aMode == 1 && ! linear && (s.aType == kBell || s.aType == kLowShelf || s.aType == kHighShelf);
            const double satK = character ? 2.5 * drive : 0.0;
            int c0 = 0, c1 = nCh - 1;
            if (nCh == 2)
            {
                switch (s.aPlace)
                {
                    case kLeft: toMs (false); c0 = c1 = 0; break;
                    case kRight: toMs (false); c0 = c1 = 1; break;
                    case kMid: toMs (true); c0 = c1 = 0; break;
                    case kSide: toMs (true); c0 = c1 = 1; break;
                    // a Stereo band keeps its filter state in one domain: always L/R
                    default: toMs (false); break;
                }
            }
            else if (s.aPlace == kSide)
                continue;
            if (warming)
            {
                for (int c = c0; c <= c1; ++c)
                {
                    std::copy (buf[c], buf[c] + n, warmBuf.data());
                    s.chain.process (warmBuf.data(), n, c, satK, nullptr);
                    if (si >= 1 && si <= kNumBands)
                        s.spec.process (warmBuf.data(), n, c, nullptr, s.specTmp.data());
                }
                s.chain.advance (n);
                s.warm -= n;
                if (s.warm <= 0)
                {
                    s.warm = 0;
                    s.warmed = true;
                }
                continue;
            }
            const float* f = fillFade (s.fade, s.fadeTgt, fadeStep, n, s.fadeBuf.data());
            // every band keeps its spectral FIR history current (an idle lane only records), so Spectral
            // mode can start at any moment without a transient
            const bool band = si >= 1 && si <= kNumBands;
            for (int c = c0; c <= c1; ++c)
            {
                s.chain.process (buf[c], n, c, satK, f);
                if (band)
                    s.spec.process (buf[c], n, c, f, s.specTmp.data());
            }
            s.chain.advance (n);
        }
        toMs (false);
        if (! pre && ! linear)
            runRack (buf, n);
    }

    const float* fillFade (double& v, double tgt, double step, int n, float* out) noexcept
    {
        if (dsp::same (v, tgt) && (v >= 1.0))
            return nullptr;
        for (int i = 0; i < n; ++i)
        {
            v = tgt > v ? std::min (tgt, v + step) : std::max (tgt, v - step);
            out[i] = (float) (0.5 - 0.5 * std::cos (dsp::kPi * v)); // raised cosine: no corners
        }
        return out;
    }

    void runRack (float* const* buf, int n) noexcept
    {
        const double fadeStep = 1.0 / (0.008 * fs * rackOsF);
        const double lfg = std::tan (dsp::kPi * 150.0 / fsRack), lfCoef = lfg / (1.0 + lfg);
        for (int mi = 0; mi < dsp::kNumModules; ++mi)
        {
            auto& m = mods[(size_t) mi];
            if ((m.fade <= 0.0 && m.fadeTgt <= 0.0) || ! m.designed)
                continue;
            const float* f = fillFade (m.fade, m.fadeTgt, fadeStep, n, m.fadeBuf.data());
            for (int c = 0; c < nCh; ++c)
            {
                float* t = m.tmp[(size_t) c].data();
                std::copy (buf[c], buf[c] + n, t);
                m.pre.process (t, n, c, 0.0, nullptr);
                for (int i = 0; i < n; ++i)
                    t[i] = (float) dsp::rack::processNonlinear (mi, t[i], drive, m.nl, c, lfCoef);
                m.post.process (t, n, c, 0.0, nullptr);
                if (f == nullptr)
                    std::copy (t, t + n, buf[c]);
                else
                    for (int i = 0; i < n; ++i)
                        buf[c][i] += f[i] * (t[i] - buf[c][i]);
            }
            m.pre.advance (n);
            m.post.advance (n);
        }
    }

    // spectral bands: a band in Spectral mode, or one still fading its spectral gain back to flat
    bool spectralRuns (const Slot& s) const noexcept { return (s.spectral && s.tActive) || ! s.spec.isIdle(); }

    // the band's region (0..1 per analysis bin): its own shape at +12 dB, normalised - spectral gain acts
    // only where the band acts
    void updateSpectralWeights (Slot& s) noexcept
    {
        if (s.aType == s.wType && s.aSlope == s.wSlope && dsp::same (s.lf, s.wLf) && dsp::same (s.lq, s.wLq))
            return;
        s.wType = s.aType;
        s.wSlope = s.aSlope;
        s.wLf = s.lf;
        s.wLq = s.lq;
        dsp::BandShape b;
        b.type = s.aType;
        b.slopeIdx = s.aSlope;
        b.freqHz = std::exp2 (s.lf);
        b.q = std::exp2 (s.lq);
        b.gainDb = 12.0;
        dsp::buildBand (b, fs, design2, false);
        for (int k = 0; k < s.spec.analysisBins(); ++k)
        {
            const double db = 10.0 * std::log10 (std::max (dsp::designMagSq (design2, 2.0 * dsp::kPi * s.spec.binHz (k) / fs), 1.0e-30));
            s.spec.setWeight (k, s.aType == kTilt ? (db + 6.0) / 12.0 : db / 12.0);
        }
    }

    // once per hop (aligned to the absolute position): analyse, apply the law, redesign
    void spectralHop() noexcept
    {
        for (int si = 1; si <= kNumBands; ++si)
        {
            auto& s = slots[(size_t) si];
            const bool on = spectralRuns (s);
            if (on && ! s.specWasOn)
                s.spec.restartDetector(); // its analysis buffer holds old (or no) signal
            s.specWasOn = on;
            if (! on)
                continue;
            updateSpectralWeights (s);
            dsp::SpectralSettings st;
            st.thr = s.thr;
            st.range = s.spectral && s.tActive ? s.range : 0.0; // switched off: release to flat
            if (s.src == kEssSrc)
                st.range *= essW;
            st.ratio = s.ratio;
            st.atkMs = s.atk;
            st.relMs = s.rel;
            s.spec.update (st);
        }
    }

    void detectSpectral (const float* const* in, int n, const float* scMono) noexcept
    {
        std::array<float, kCtrl> a {};
        for (int si = 1; si <= kNumBands; ++si)
        {
            auto& s = slots[(size_t) si];
            if (! spectralRuns (s))
                continue;
            if (s.src == kNumBands + 1 || nCh == 1)
            {
                const float* m = s.src == kNumBands + 1 ? scMono : in[0];
                if (m == nullptr)
                    std::fill (a.begin(), a.begin() + n, 0.0f);
                else
                    std::copy (m, m + n, a.begin());
                s.spec.pushDetector (a.data(), nullptr, n);
                continue;
            }
            switch (s.aPlace)
            {
                case kLeft: s.spec.pushDetector (in[0], nullptr, n); break;
                case kRight: s.spec.pushDetector (in[1], nullptr, n); break;
                case kMid:
                case kSide:
                    for (int i = 0; i < n; ++i)
                        a[(size_t) i] = 0.5f * (in[0][i] + (s.aPlace == kMid ? in[1][i] : -in[1][i]));
                    s.spec.pushDetector (a.data(), nullptr, n);
                    break;
                default: s.spec.pushDetector (in[0], in[1], n); break;
            }
        }
    }

    // 3.0 Ess source: Velvet 3.0's LIVE sibilance network on the detector signal (causal, its frame 6 ms behind);
    // essW = the probability, instant up, 20 ms down, read by controlDynamics / the spectral update every control block
    void detectEss (const float* const* in, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = nCh == 2 ? 0.5f * (in[0][i] + in[1][i]) : in[0][i];
            if (essDet.push (m))
                essP = essDet.out().p;
            essW = essP > essW ? essP : essW + (essP - essW) * essRelK;
        }
    }

    void detect (const float* const* in, int n, const float* scMono) noexcept
    {
        for (int si = 1; si <= kNumBands; ++si)
        {
            auto& s = slots[(size_t) si];
            if (! s.dynLive)
                continue;
            const bool ext = s.src == kNumBands + 1;
            for (int i = 0; i < n; ++i)
            {
                double l0, l1 = 0.0;
                int lanes = 1;
                if (ext)
                    l0 = scMono != nullptr ? scMono[i] : 0.0;
                else if (nCh == 1)
                    l0 = in[0][i];
                else
                {
                    const double a = in[0][i], b = in[1][i];
                    switch (s.aPlace)
                    {
                        case kLeft: l0 = a; break;
                        case kRight: l0 = b; break;
                        case kMid: l0 = 0.5 * (a + b); break;
                        case kSide: l0 = 0.5 * (a - b); break;
                        default: l0 = a; l1 = b; lanes = 2; break;
                    }
                }
                const double v0 = s.det.tick (l0, 0);
                s.env[0] += s.detC * (v0 * v0 - s.env[0]);
                if (lanes == 2)
                {
                    const double v1 = s.det.tick (l1, 1);
                    s.env[1] += s.detC * (v1 * v1 - s.env[1]);
                }
                else
                    s.env[1] = 0.0;
            }
        }
    }

    // one-pole step in float that lands exactly on the target: a plain step stalls short of it once
    // the increment drops below half an ulp (1e-5..1e-4 relative, i.e. a gain left ~0.0005 dB off for
    // good) - when a step no longer moves the value, it is close enough to snap
    static float smoothTo (float v, float t, float a) noexcept
    {
        const float nv = v + (t - v) * a;
        return dsp::same (nv, v) ? t : nv;
    }

    void segment (float* const* io, int chs, int off, int n, const float* const* sc, int scCh) noexcept
    {
        if (absPos % kCtrl == 0)
            control();
        const auto& x = idx();
        const bool linear = active.phase == kLinear;
        const float inGainTgt = dbToGain (params[(size_t) x.inGain]);
        const float outGainTgt = dbToGain (params[(size_t) x.outGain]);
        const float mixTgt = std::clamp (params[(size_t) x.mix] * 0.01f, 0.0f, 1.0f);
        const bool agOn = params[(size_t) x.autoGain] > 0.5f;
        const float agTgt = agOn ? dbToGain ((float) autoGainTgtDb) : 1.0f;
        const float a = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));
        const float aAg = (float) (1.0 - std::exp (-1.0 / (0.1 * fs)));

        // input gain, sanitising, input meter, delays
        float* in[2] = { wbuf[0].data(), wbuf[1].data() };
        for (int i = 0; i < n; ++i)
        {
            inGain = smoothSnap ? inGainTgt : smoothTo (inGain, inGainTgt, a);
            for (int c = 0; c < chs; ++c)
            {
                float v = io[c][off + i];
                if (! std::isfinite (v))
                    v = 0.0f;
                v *= inGain;
                in[c][i] = v;
                meters.inPeak[c] = std::max (meters.inPeak[c], std::abs (v));
            }
            for (int c = chs; c < nCh; ++c)
                in[c][i] = 0.0f;
        }
        const int dsz = (int) dry[0].size();
        for (int i = 0; i < n; ++i)
        {
            int rp = delayPos - latency, rd = delayPos - detLatency;
            if (rp < 0) rp += dsz;
            if (rd < 0) rd += dsz;
            for (int c = 0; c < nCh; ++c)
            {
                dry[(size_t) c][(size_t) delayPos] = in[c][i];
                dryOut[(size_t) c][(size_t) i] = dry[(size_t) c][(size_t) rp];
                detIn[(size_t) c][(size_t) i] = detLatency > 0 ? dry[(size_t) c][(size_t) rd] : in[c][i];
            }
            float scv = 0.0f;
            if (sc != nullptr && scCh > 0)
            {
                for (int c = 0; c < std::min (scCh, 2); ++c)
                    scv += sc[c][off + i];
                scv /= (float) std::min (scCh, 2);
                if (! std::isfinite (scv))
                    scv = 0.0f;
            }
            scRaw[(size_t) i] = scv;
            detDelay[0][(size_t) delayPos] = scv;
            scDet[(size_t) i] = detLatency > 0 ? detDelay[0][(size_t) rd] : scv;
            delayPos = (delayPos + 1 == dsz) ? 0 : delayPos + 1;
        }
        if (analyzerOn.load (std::memory_order_relaxed))
        {
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = nCh == 2 ? 0.5f * (in[0][i] + in[1][i]) : in[0][i];
            preFifo.push (mono.data(), n);
            if (sc != nullptr && scCh > 0)
                scFifo.push (scRaw.data(), n);
        }

        const float* detPtr[2] = { detIn[0].data(), detIn[1].data() };
        if (essUsed)
            detectEss (detPtr, n);
        if (absPos % slots[1].spec.hopSize() == 0)
            spectralHop(); // from the samples before this one: causal, block-size independent
        detect (detPtr, n, scDet.data());
        detectSpectral (detPtr, n, scDet.data());

        // main path (in place on `in`)
        if (! linear)
        {
            const int F = osF;
            float* up[2] {};
            for (int c = 0; c < nCh; ++c)
                up[c] = os.up (in[c], n, c, active.osLog2);
            runSlotsAndRack (up, n * F, F, false);
            for (int c = 0; c < nCh; ++c)
                os.down (in[c], n, c, active.osLog2);
            if (active.phase == kZeroLatency)
                runMinPhase (in, n);
            else
                runNatural (in, n);
        }
        else
        {
            const bool pre = rackPre();
            float* up[2] {};
            if (pre)
            {
                for (int c = 0; c < nCh; ++c)
                    up[c] = os.up (in[c], n, c, active.osLog2);
                runRack (up, n * rackOsF);
                for (int c = 0; c < nCh; ++c)
                    os.down (in[c], n, c, active.osLog2);
            }
            lp.process (in, n);
            runSlotsAndRack (in, n, 1, true); // dynamic delta bands
            if (! pre)
            {
                for (int c = 0; c < nCh; ++c)
                    up[c] = os.up (in[c], n, c, active.osLog2);
                runRack (up, n * rackOsF);
                for (int c = 0; c < nCh; ++c)
                    os.down (in[c], n, c, active.osLog2);
            }
        }

        // auto gain, mix, output gain, global fade, solo
        const double gStep = 1.0 / (0.01 * fs), sStep = 1.0 / (0.01 * fs);
        for (int i = 0; i < n; ++i)
        {
            agGain = smoothSnapAg ? agTgt : smoothTo (agGain, agTgt, aAg);
            mix = smoothSnapAg ? mixTgt : smoothTo (mix, mixTgt, a);
            outGain = smoothSnapAg ? outGainTgt : smoothTo (outGain, outGainTgt, a);
            smoothSnapAg = false;
            gfade = gfadeTgt > gfade ? std::min (gfadeTgt, gfade + gStep) : std::max (gfadeTgt, gfade - gStep);
            soloFade = soloTgt > soloFade ? std::min (soloTgt, soloFade + sStep) : std::max (soloTgt, soloFade - sStep);
            for (int c = 0; c < nCh; ++c)
            {
                const float d = dryOut[(size_t) c][(size_t) i];
                float y = mix >= 1.0f ? in[c][i] * agGain : d + mix * (in[c][i] * agGain - d);
                if (soloFade > 0.0)
                {
                    const float sv = (float) soloRf[(size_t) c].tick (d, 0);
                    y += (float) (0.5 - 0.5 * std::cos (dsp::kPi * soloFade)) * (sv - y);
                }
                y *= outGain * (float) (0.5 - 0.5 * std::cos (dsp::kPi * gfade));
                in[c][i] = y;
            }
        }
        for (int c = 0; c < chs; ++c)
            for (int i = 0; i < n; ++i)
            {
                io[c][off + i] = in[c][i];
                meters.outPeak[c] = std::max (meters.outPeak[c], std::abs (in[c][i]));
            }
        if (analyzerOn.load (std::memory_order_relaxed))
        {
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = nCh == 2 ? 0.5f * (in[0][i] + in[1][i]) : in[0][i];
            postFifo.push (mono.data(), n);
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& sl = slots[(size_t) (b + 1)];
            meters.delta[(size_t) b] = sl.spectral ? sl.spec.maxGainChangeDb() : (float) sl.delta;
        }
    }

    void runMinPhase (float* const* x, int n) noexcept
    {
        // lanes that are off still record their input so they switch on without a transient
        for (int l = 0; l < 2 && l < nCh; ++l)
            if (minOn[(size_t) l])
            {
                minLane[(size_t) l].process (x[l], n);
                if (minOff[(size_t) l] && ! minLane[(size_t) l].isFading())
                    minOn[(size_t) l] = minOff[(size_t) l] = false;
            }
            else
                minLane[(size_t) l].pushOnly (x[l], n);
        if (nCh == 2 && ! minOn[2] && ! minOn[3])
        {
            for (int i = 0; i < n; ++i)
            {
                msTmp[0][(size_t) i] = 0.5f * (x[0][i] + x[1][i]);
                msTmp[1][(size_t) i] = 0.5f * (x[0][i] - x[1][i]);
            }
            minLane[2].pushOnly (msTmp[0].data(), n);
            minLane[3].pushOnly (msTmp[1].data(), n);
        }
        else if (nCh == 2)
        {
            for (int i = 0; i < n; ++i)
            {
                const float a = x[0][i], b = x[1][i];
                x[0][i] = 0.5f * (a + b);
                x[1][i] = 0.5f * (a - b);
            }
            for (int l = 2; l < 4; ++l)
                if (minOn[(size_t) l])
                {
                    minLane[(size_t) l].process (x[l - 2], n);
                    if (minOff[(size_t) l] && ! minLane[(size_t) l].isFading())
                        minOn[(size_t) l] = minOff[(size_t) l] = false;
                }
                else
                    minLane[(size_t) l].pushOnly (x[l - 2], n);
            for (int i = 0; i < n; ++i)
            {
                const float m = x[0][i], s = x[1][i];
                x[0][i] = m + s;
                x[1][i] = m - s;
            }
        }
    }

    void runNatural (float* const* x, int n) noexcept
    {
        if (nCh == 1)
        {
            natLane[0].process (x[0], n);
            return;
        }
        float* yl = natY[0].data();
        float* yr = natY[1].data();
        std::fill (yl, yl + n, 0.0f);
        std::fill (yr, yr + n, 0.0f);
        natLane[0].processAdd (x[0], yl, n);
        natLane[3].processAdd (x[1], yr, n);
        for (int e : { 1, 2 })
        {
            auto& lane = natLane[(size_t) e];
            const float* src = e == 1 ? x[1] : x[0];
            float* dst = e == 1 ? yl : yr;
            if (natOn[(size_t) e])
            {
                lane.processAdd (src, dst, n);
                if (natOff[(size_t) e] && ! lane.isFading())
                    natOn[(size_t) e] = natOff[(size_t) e] = false;
            }
            else
                lane.pushOnly (src, n);
        }
        std::copy (yl, yl + n, x[0]);
        std::copy (yr, yr + n, x[1]);
    }

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    //== members =================================================================
    double fs = 48000.0, fsBands = 48000.0, fsRack = 48000.0;
    int nCh = 2, maxBlock = 512, osF = 1, rackOsF = 1;
    bool prepared = false;
    std::atomic<bool> offline { false }; // set by the host thread, read by the audio thread
    std::vector<float> params;
    Config active, want;
    int latency = 0, detLatency = 0, maxLatency = 0; // audio thread
    std::atomic<int> latencyPub { 0 };
    // latency handshake (see setHostLatency): shared with the wrapper's timer
    std::atomic<bool> hostTracking { false };
    std::atomic<std::uint64_t> latReq { 0 };
    std::atomic<std::uint32_t> beat { 0 };
    // audio thread
    int hostLat = 0, reqWait = 0;
    std::uint64_t reqSeq = 0;
    std::uint32_t holdBeat = 0;
    bool reqOpen = false, holding = false;
    int64_t absPos = 0;

    std::array<Slot, kNumSlots> slots;
    std::array<Mod, dsp::kNumModules> mods;
    dsp::ChainDesign design, design2;
    Oversampler os;
    dsp::CorrectionDesigner cd;
    dsp::FirSet firTmp;
    std::array<dsp::FirLane, 4> minLane, natLane;
    std::array<bool, 4> minOn {}, minOff {}, natOn {}, natOff {};
    dsp::LinearPhaseEq lp;

    std::array<std::vector<float>, 2> dry, detDelay;
    int delayPos = 0;
    std::array<std::array<float, kCtrl>, 2> wbuf {}, dryOut {}, detIn {}, natY {}, msTmp {};
    std::array<float, kCtrl> scRaw {}, scDet {}, mono {};
    std::array<float, kCtrl * kMaxOs> warmBuf {};

    double smoothCoef = 0.0, drive = 0.3, driveTgt = 0.3;
    bool smoothSnap = true, smoothSnapAg = true, forceAnalysis = true;
    float inGain = 1.0f, outGain = 1.0f, mix = 1.0f, agGain = 1.0f;
    double autoGainTgtDb = 0.0;
    double gfade = 1.0, gfadeTgt = 1.0;
    int gWarm = 0;
    std::vector<double> staticNow, lastStatic;

    std::atomic<int> soloReq { -1 };
    int soloCur = -1;
    double soloFade = 0.0, soloTgt = 0.0;
    std::array<dsp::RegionFilter, 2> soloRf;

    static constexpr int kAgPoints = 64;
    std::array<double, kAgPoints> agHz {}, agW {};

    std::atomic<bool> analyzerOn { false };
    SpscFifo preFifo, postFifo, scFifo;
    EngineMeters meters;
};
} // namespace penta
