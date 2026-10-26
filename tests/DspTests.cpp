// Axell Penta EQ - DSP test suite (plain C++17, no JUCE). Implements docs/ENGINEERING_SPEC.md §4.
//   PentaDspTests [--report path] [--no-bench] [--quick]
// Exit code = number of failed checks.
#include "Harness.h"
#include "TestUtil.h"
#include "../Source/Presets.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <new>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

using namespace penta;
using dt::Rig;

// heap allocation counter for the real-time safety test (only counts on a thread that opts in)
namespace
{
thread_local bool tCountAllocs = false;
std::atomic<long> gAllocs { 0 };
} // namespace
#if defined(__GNUC__) && ! defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete" // replaced operator new uses malloc on purpose
#endif
static void* countedAlloc (std::size_t n)
{
    if (tCountAllocs)
        gAllocs.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
bool quick = false;
bool onlyIndependent = false; // --only-independent: section L only (development)

const char* modeName (int m) { return m == 0 ? "ZL" : (m == 1 ? "NP" : "LP"); }

// magnitude spectrum (dB) of an impulse response via FFT, read at frequency hz
struct Spectrum
{
    std::vector<double> mag;
    double fs = 48000.0;
    int n = 0;
    void compute (const std::vector<double>& h, double sampleRate)
    {
        fs = sampleRate;
        n = 1;
        while (n < (int) h.size())
            n <<= 1;
        dsp::RealFft<double> f;
        f.prepare (n);
        std::vector<double> x ((size_t) n, 0.0);
        std::copy (h.begin(), h.end(), x.begin());
        std::vector<std::complex<double>> X ((size_t) (n / 2 + 1));
        f.forward (x.data(), X.data());
        mag.resize (X.size());
        for (size_t k = 0; k < X.size(); ++k)
            mag[k] = dt::db (std::abs (X[k]));
    }
    double at (double hz) const
    {
        const double b = hz / fs * n;
        const int k = (int) std::floor (b);
        const double t = b - k;
        if (k + 1 >= (int) mag.size())
            return mag.back();
        return mag[(size_t) k] * (1.0 - t) + mag[(size_t) (k + 1)] * t;
    }
};

struct Case
{
    std::string name;
    int type;
    float f, g, q;
    int slope;
};

std::vector<Case> magnitudeCases (int mode, double fs)
{
    std::vector<Case> c;
    std::vector<float> freqs { 30, 100, 300, 1000, 3000, 8000, 14000, 19000 };
    if (quick)
        freqs = { 100, 3000, 14000 };
    auto add = [&] (const char* n, int t, float f, float g, float q, int s)
    {
        if (f > 0.45 * fs)
            return;
        char b[128];
        std::snprintf (b, sizeof b, "%s f=%g g=%+g q=%g s=%d", n, f, g, q, kSlopeDb[s]);
        c.push_back ({ b, t, f, g, q, s });
    };
    const bool lin = mode == kLinear;
    // Linear Phase resolves a resonance only if it decays inside the FIR: at Medium (85 ms half
    // length at 48 kHz) that means f >= 40 * Q; steep cuts (Q up to 5 in their sections) >= 300 Hz.
    // Low-frequency accuracy at High resolution is checked separately (testLinearHigh).
    auto linOk = [&] (float f, float q) { return ! lin || (f >= 40.0f * q && f >= 100.0f); };
    for (float f : freqs)
    {
        for (float q : { 0.3f, 1.0f, 4.0f, 12.0f })
        {
            if (! linOk (f, q))
                continue;
            for (float g : { -18.0f, -6.0f, 6.0f, 18.0f })
                add ("bell", kBell, f, g, q, 3);
        }
        for (int s : { 0, 1 })
            for (float q : { 0.5f, 0.7071f, 1.5f })
            {
                if ((s == 0 && q != 0.7071f) || ! linOk (f, q))
                    continue;
                for (float g : { -18.0f, -6.0f, 6.0f, 18.0f })
                {
                    add ("lowshelf", kLowShelf, f, g, q, s);
                    add ("highshelf", kHighShelf, f, g, q, s);
                }
            }
        for (int s = 0; s < 8; ++s)
        {
            if (lin && (f < 100.0f || (s >= 4 && f < 300.0f)))
                continue;
            add ("lowcut", kLowCut, f, 0, 0.7071f, s);
            add ("highcut", kHighCut, f, 0, 0.7071f, s);
        }
        if (linOk (f, 2.0f))
        {
            add ("lowcut-res", kLowCut, f, 0, 2.0f, 1);
            add ("highcut-res", kHighCut, f, 0, 2.0f, 1);
        }
        for (float q : { 1.0f, 8.0f })
            if (! lin || f >= 1000.0f) // a notch's V-shaped null needs a long FIR (High-res test below)
                add ("notch", kNotch, f, 0, q, 3);
        for (int s : { 1, 3 })
            if (linOk (f, 2.0f * (float) s))
                add ("bandpass", kBandPass, f, 0, 2.0f, s);
        for (int s : { 0, 1 })
            for (float g : { -12.0f, 6.0f })
                if (linOk (f, 0.7071f))
                    add ("tilt", kTilt, f, g, 0.7071f, s);
    }
    return c;
}

void testMagnitude (dt::Recorder& r, int mode, int quality = 1, const std::vector<Case>* custom = nullptr)
{
    r.section = std::string ("A. Clean magnitude vs analog (") + modeName (mode) + (custom ? ", High resolution, low bands)" : ")");
    const double fs = r.ctx.fs;
    double worstMain = 0.0, worstDeep = 0.0;
    std::string worstName;
    int fails = 0, n = 0;
    for (const auto& c : (custom ? *custom : magnitudeCases (mode, fs)))
    {
        Rig rig;
        rig.set ("phase", (float) mode);
        rig.set ("lpQuality", (float) quality);
        rig.band (0, c.type, c.f, c.g, c.q, c.slope);
        rig.prepare (fs, 2, 512);
        const int lat = rig.e.getLatencySamples();
        // low, narrow bands ring for a long time (a +18 dB Q 12 bell at 30 Hz has pole Q ~34):
        // the impulse response must be long enough or the measurement itself is truncated
        const int len = (int) ((c.f < 200.0f ? 262144 : 65536) * fs / 48000.0) + lat;
        const int settle = mode == kLinear ? 2 * dsp::LinearPhaseEq::tapsFor (quality, fs) : 0;
        auto h = dt::impulse (rig, len, settle, 0, 0, 512);
        Spectrum sp;
        sp.compute (h, fs);
        CurveModel cm;
        cm.build (rig.p, fs);
        double eMain = 0.0, eDeep = 0.0;
        for (double f : dt::logFreqs (20.0, std::min (20000.0, 0.95 * fs / 2), 240))
        {
            const double t = cm.totalDb (f);
            // steep regions (narrow notches/peaks): exact DFT instead of bin interpolation
            const double up = cm.totalDb (f * 1.003), dn = cm.totalDb (f / 1.003);
            const bool steep = std::abs (up - dn) > 0.2 || std::abs (up + dn - 2.0 * t) > 0.005; // slope or curvature
            const double m = steep ? dt::db (std::abs (dt::dft (h, 2.0 * dt::kPi * f / fs))) : sp.at (f);
            if (t >= -24.0)
                eMain = std::max (eMain, std::abs (m - t));
            else if (t >= -48.0 && (c.type == kLowCut || c.type == kHighCut))
                eDeep = std::max (eDeep, std::abs (m - t));
        }
        ++n;
        const bool ok = eMain <= 0.1 && eDeep <= 0.5;
        if (! ok)
        {
            ++fails;
            if (fails <= 8)
                std::printf ("  [%s %g k] FAIL %s: %.3f dB (deep %.3f)\n", modeName (mode), fs / 1000, c.name.c_str(), eMain, eDeep);
        }
        if (eMain > worstMain)
        {
            worstMain = eMain;
            worstName = c.name;
        }
        worstDeep = std::max (worstDeep, eDeep);
    }
    r.less ("worst error, target >= -24 dB (" + std::to_string (n) + " cases)", worstMain, 0.1001, "dB");
    r.less ("worst error, cuts -24..-48 dB", worstDeep, 0.5, "dB");
    r.equal ("cases outside tolerance", fails, 0, "");
    if (! worstName.empty())
        std::printf ("  [%s %g k] worst case: %s (%.4f dB)\n", modeName (mode), fs / 1000, worstName.c_str(), worstMain);
}

void testLinearHigh (dt::Recorder& r)
{
    // Linear Phase High resolution: low and narrow bands (f >= 10 * Q)
    std::vector<Case> c;
    for (float f : { 30.0f, 60.0f, 100.0f })
        for (float q : { 0.5f, 1.0f, 4.0f })
            if (f >= 10.0f * q)
                for (float g : { -12.0f, 12.0f })
                    c.push_back ({ "bell f=" + std::to_string ((int) f) + " q=" + std::to_string (q) + " g=" + std::to_string ((int) g), kBell, f, g, q, 3 });
    c.push_back ({ "notch 200 Hz q=2", kNotch, 200.0f, 0, 2.0f, 3 });
    c.push_back ({ "low cut 30 Hz 48 dB/oct", kLowCut, 30.0f, 0, 0.7071f, 5 });
    c.push_back ({ "low shelf 40 Hz +9", kLowShelf, 40.0f, 9, 0.7071f, 1 });
    testMagnitude (r, kLinear, 2, &c);
}

//------------------------------------------------------------------------------
// L. textbook references written here, independently of the engine's analog model (CurveModel):
// a mistake shared by the engine and its model (like a wrong Butterworth Q) cannot cancel out.
namespace ref
{
double butterDb (double f, double fc, int order, bool high)
{
    const double r = high ? fc / f : f / fc;
    return -10.0 * std::log10 (1.0 + std::pow (r, 2.0 * order));
}
// RBJ cookbook analog shelves, s normalised to w0; cuts are the reciprocal of the boost
std::complex<double> shelf (double f, double f0, double q, double g, bool low)
{
    const double A = std::pow (10.0, std::abs (g) / 40.0), sa = std::sqrt (A);
    const std::complex<double> s (0.0, f / f0);
    const auto h = low ? A * (s * s + sa / q * s + A) / (A * s * s + sa / q * s + 1.0)
                       : A * (A * s * s + sa / q * s + 1.0) / (s * s + sa / q * s + A);
    return g >= 0.0 ? h : 1.0 / h;
}
double shelfDb (double f, double f0, double q, double g, bool low) { return dt::db (std::abs (shelf (f, f0, q, g, low))); }
double tiltDb (double f, double f0, double q, double g) { return shelfDb (f, f0, q, g, false) - g / 2.0; }
double bandPassDb (double f, double f0, double q, int sections)
{
    const std::complex<double> s (0.0, f / f0);
    return sections * dt::db (std::abs ((s / q) / (s * s + s / q + 1.0)));
}
} // namespace ref

// worst |dB| error of a configured rig against `target` (dB) where target >= floorDb
double refError (Rig& rig, double fs, const std::function<double (double)>& target, double floorDb, double longest, double* atHz = nullptr)
{
    // the whole response, latency included: Natural Phase rings before its latency point
    const int len = (int) (longest * fs) + 2 * rig.e.getLatencySamples();
    const auto hl = dt::impulse (rig, len, 0, 0, 0, 512);
    double worst = 0.0;
    for (double f : dt::logFreqs (20.0, std::min (20000.0, 0.4535 * fs), 160))
    {
        const double t = target (f);
        if (t < floorDb)
            continue;
        const double e = std::abs (dt::db (std::abs (dt::dft (hl, 2.0 * dt::kPi * f / fs))) - t);
        if (e > worst)
        {
            worst = e;
            if (atHz != nullptr)
                *atHz = f;
        }
    }
    return worst;
}

void testIndependent (dt::Recorder& r)
{
    r.section = "L. Independent references (textbook formulas, not the engine's model)";
    const double fs = r.ctx.fs;
    // 1. Butterworth cuts: exact curve at every slope, corner -3.01 dB
    for (int mode : { kZeroLatency, kNatural })
    {
        double worst = 0.0, worstCorner = 0.0;
        std::string wn;
        for (int s = 0; s < 8; ++s)
            for (bool high : { true, false })
                for (double fc : { 100.0, 1000.0, 8000.0 })
                {
                    if (quick && fc == 8000.0)
                        continue;
                    Rig rig;
                    rig.set ("phase", (float) mode);
                    rig.band (0, high ? kLowCut : kHighCut, (float) fc, 0, 0.70710678f, s);
                    rig.prepare (fs, 1, 512);
                    const int order = kSlopeDb[s] / 6;
                    const auto tgt = [&] (double f) { return ref::butterDb (f, fc, order, high); };
                    // spec tolerance: 0.1 dB down to -24 dB, 0.5 dB in the -24..-48 dB skirt (scaled to 0.1)
                    const double e = std::max (refError (rig, fs, tgt, -24.0, 0.5), 0.2 * refError (rig, fs, tgt, -48.0, 0.5));
                    if (e > worst)
                    {
                        worst = e;
                        wn = std::string (high ? "low cut " : "high cut ") + std::to_string (kSlopeDb[s]) + " dB/oct @ " + std::to_string ((int) fc);
                    }
                }
        // the corner itself (exact frequency, not on the log grid)
        for (int s = 1; s < 8; ++s)
        {
            Rig rig;
            rig.set ("phase", (float) mode);
            rig.band (0, kLowCut, 1000.0f, 0, 0.70710678f, s);
            rig.prepare (fs, 1, 512);
            const auto hl = dt::impulse (rig, (int) (0.5 * fs) + 2 * rig.e.getLatencySamples(), 0, 0, 0, 512);
            worstCorner = std::max (worstCorner, std::abs (dt::db (std::abs (dt::dft (hl, 2.0 * dt::kPi * 1000.0 / fs))) + 3.0103));
        }
        r.less (std::string ("Butterworth cuts, all slopes, vs exact curve (>= -24 dB; -48 dB at 0.5 dB, scaled) (") + modeName (mode) + ")", worst, 0.1, "dB");
        r.less (std::string ("Butterworth corner = -3.01 dB, 12..96 dB/oct (") + modeName (mode) + ")", worstCorner, 0.02, "dB");
        if (worst > 0.1)
            std::printf ("  [%g k] worst Butterworth case: %s (%.3f dB)\n", fs / 1000, wn.c_str(), worst);
    }
    // 2. resonant shelves and tilt over the whole Q range, without oversampling at every rate (since
    //    2.0: before, Q > 5 needed the resonance below Nyquist - 2x oversampling at 44.1/48 kHz)
    uint64_t seed = 0x5eed1234u + (uint64_t) fs;
    auto rnd = [&seed] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return (double) (seed >> 11) * (1.0 / 9007199254740992.0); };
    for (int band = 0; band < 2; ++band)
    {
        const bool highQ = band == 1;
        const int os = 0;
        double worst = 0.0;
        std::string wn;
        const int cases = quick ? 12 : 48;
        for (int i = 0; i < cases; ++i)
        {
            const int type = i % 3; // low shelf, high shelf, tilt
            const double f0 = 20.0 * std::pow (1000.0, rnd()), g = -30.0 + 60.0 * rnd();
            const double q = highQ ? 5.0 * std::pow (24.0 / 5.0, rnd()) : 0.1 * std::pow (50.0, rnd());
            Rig rig;
            rig.set ("phase", (float) (i % 2 == 0 ? kZeroLatency : kNatural));
            rig.set ("os", (float) os);
            rig.band (0, type == 0 ? kLowShelf : (type == 1 ? kHighShelf : kTilt), (float) f0, (float) g, (float) q, 1);
            rig.prepare (fs, 1, 512);
            const auto tgt = [&] (double f) { return type == 2 ? ref::tiltDb (f, f0, q, g) : ref::shelfDb (f, f0, q, g, type == 0); };
            double at = 0.0;
            const double e = refError (rig, fs, tgt, -60.0, f0 < 200.0 ? 3.0 : 0.75, &at);
            if (e > worst)
            {
                worst = e;
                char b[160];
                std::snprintf (b, sizeof b, "%s f=%.0f g=%+.1f q=%.2f: %.3f dB @ %.0f Hz", type == 0 ? "low shelf" : (type == 1 ? "high shelf" : "tilt"),
                               f0, g, q, e, at);
                wn = b;
            }
        }
        r.less (highQ ? "shelves/tilt Q 5..24 vs RBJ analog (1x)" : "shelves/tilt Q 0.1..5 vs RBJ analog", worst, 0.1, "dB");
        std::printf ("  [%g k] %s worst: %s\n", fs / 1000, highQ ? "Q 5..24" : "Q 0.1..5", wn.c_str());
    }
    // 2b. near Nyquist at 1x: every type centred 12-30 kHz (the control's range) vs its analog curve -
    //     notch and band-pass at their true centre, also above Nyquist (D25)
    {
        double worst = 0.0;
        std::string wn;
        const int cases = quick ? 10 : 36;
        for (int i = 0; i < cases; ++i)
        {
            const int kinds[] = { kBell, kHighShelf, kLowShelf, kTilt, kNotch, kBandPass };
            const int type = kinds[i % 6];
            const double f0 = 12000.0 * std::pow (2.5, rnd()), g = -24.0 + 48.0 * rnd();
            const double q = type == kBandPass ? 0.3 * std::pow (40.0, rnd()) : 0.2 * std::pow (120.0, rnd());
            const int slope = type == kBandPass ? 1 + (int) (rnd() * 7) : 1;
            Rig rig;
            rig.set ("phase", (float) (i % 2 == 0 ? kZeroLatency : kNatural));
            rig.band (0, type, (float) f0, (float) g, (float) q, slope);
            rig.prepare (fs, 1, 512);
            const int n = (kSlopeDb[slope] + 6) / 12;
            const auto tgt = [&] (double f)
            {
                const std::complex<double> sj (0.0, f / f0);
                switch (type)
                {
                    case kBell:
                    {
                        const double A = std::pow (10.0, std::abs (g) / 40.0);
                        const auto h = (sj * sj + sj * A / q + 1.0) / (sj * sj + sj / (A * q) + 1.0);
                        return dt::db (std::abs (g >= 0 ? h : 1.0 / h));
                    }
                    case kNotch: return dt::db (std::abs ((sj * sj + 1.0) / (sj * sj + sj / q + 1.0)));
                    case kBandPass: return ref::bandPassDb (f, f0, q, n);
                    case kTilt: return ref::tiltDb (f, f0, q, g);
                    default: return ref::shelfDb (f, f0, q, g, type == kLowShelf);
                }
            };
            double at = 0.0;
            const double e = refError (rig, fs, tgt, -24.0, 0.5, &at);
            if (e > worst)
            {
                worst = e;
                char b[160];
                std::snprintf (b, sizeof b, "type %d f=%.0f g=%+.1f q=%.2f slope %d: %.3f dB @ %.0f Hz", type, f0, g, q, kSlopeDb[slope], e, at);
                wn = b;
            }
        }
        r.less ("every type centred 12..30 kHz vs analog (1x, ZL + NP)", worst, 0.1, "dB");
        std::printf ("  [%g k] 12..30 kHz worst: %s\n", fs / 1000, wn.c_str());
    }
    // 2c. Natural Phase: steep, narrow band-pass (96 dB/oct, Q 12) keeps magnitude and analog phase (D26)
    {
        double worst = 0.0, worstPh = 0.0;
        for (double f0 : { 250.0, 1000.0, 4000.0 })
        {
            Rig rig;
            rig.set ("phase", (float) kNatural);
            rig.band (0, kBandPass, (float) f0, 0, 12.0f, 7);
            rig.prepare (fs, 1, 512);
            const auto h = dt::impulse (rig, (int) (2.0 * fs), 0, 0, 0, 512);
            const int lat = rig.e.getLatencySamples();
            for (double f : dt::logFreqs (20.0, std::min (20000.0, 0.4535 * fs), 400))
            {
                const std::complex<double> sj (0.0, f / f0);
                const auto a = std::pow ((sj / 12.0) / (sj * sj + sj / 12.0 + 1.0), 8);
                if (dt::db (std::abs (a)) < -24.0)
                    continue;
                const auto d = dt::dft (h, 2.0 * dt::kPi * f / fs) * std::polar (1.0, 2.0 * dt::kPi * f / fs * lat);
                worst = std::max (worst, std::abs (dt::db (std::abs (d)) - dt::db (std::abs (a))));
                if (dt::db (std::abs (a)) >= -20.0)
                    worstPh = std::max (worstPh, std::abs (std::arg (d / a)) * 180.0 / dt::kPi);
            }
        }
        r.less ("Natural Phase band-pass 96 dB/oct Q 12 vs analog magnitude", worst, 0.1, "dB");
        r.less ("Natural Phase band-pass 96 dB/oct Q 12 vs analog phase (>= -20 dB)", worstPh, 3.0, "deg");
    }
    // 2d. a dynamic band near Nyquist (its fit is tracked every control block): stable and bounded
    {
        Rig rig;
        rig.band (0, kHighShelf, 19500.0f, 9.0f, 12.0f, 1);
        rig.set (bandId (0, bpDyn), 1);
        rig.set (bandId (0, bpThr), -40);
        rig.set (bandId (0, bpRange), -18);
        rig.set (bandId (0, bpAtk), 1);
        rig.set (bandId (0, bpRel), 30);
        rig.prepare (fs, 1, 256, false);
        auto b = dt::zeros (1, (int) fs * 2);
        for (int i = 0; i < (int) b[0].size(); ++i) // noise bursts: the gain swings over the whole range
            b[0][(size_t) i] = (float) ((i / 2400) % 2 == 0 ? 0.5 : 0.002) * (float) std::sin (0.7 * i + 0.0003 * i * i);
        rig.run (b, 256);
        double peak = 0.0;
        bool finite = true;
        for (float v : b[0])
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, (double) std::abs (v));
        }
        r.equal ("dynamic high shelf 19.5 kHz Q 12 (tracked fit): output finite", finite ? 1 : 0, 1, "");
        r.less ("dynamic high shelf 19.5 kHz Q 12 (tracked fit): output peak", dt::db (peak), dt::db (0.5) + 30.0, "dBFS"); // resonant shelf overshoot, no blow-up
    }
    // 2e. 3.0 detector source "Ess" (Axell Velvet's sibilance network): a bright vowel (a strong 6.5 kHz partial) and
    // "s" / "sh" noise alternate; a 6.5 kHz dynamic cut keyed by SELF cuts both, keyed by ESS only the noise
    {
        auto essRun = [fs] (int src, double partial, bool esses, double& cutVowel, double& cutEss)
        {
            Rig rig;
            rig.band (0, kBell, 6500.0f, 0.0f, 1.5f);
            rig.set (bandId (0, bpDyn), 1);
            rig.set (bandId (0, bpThr), -50);
            rig.set (bandId (0, bpRange), -12);
            rig.set (bandId (0, bpRatio), 8);
            rig.set (bandId (0, bpAtk), 0.5f);
            rig.set (bandId (0, bpRel), 80);
            rig.set (bandId (0, bpSrc), (float) src);
            rig.prepare (fs, 1, 256, false);
            std::mt19937 g (7);
            std::normal_distribution<double> nd;
            auto bp = [fs] (double f, double q) { const double w = 2 * dt::kPi * f / fs, al = std::sin (w) / (2 * q), a0 = 1 + al;
                                                   return std::array<double, 5> { al / a0, 0.0, -al / a0, -2 * std::cos (w) / a0, (1 - al) / a0 }; };
            const auto cs = bp (7500.0, 2.5), ch = bp (3400.0, 1.5);
            double zs[4] {}, zh[4] {};
            auto biq = [] (const std::array<double, 5>& c, double x, double* z) { const double y = c[0] * x + z[0];
                                                                                 z[0] = c[1] * x - c[3] * y + z[1]; z[1] = c[2] * x - c[4] * y; return y; };
            const size_t seg = (size_t) (0.3 * fs), n = (size_t) (6.0 * fs);
            auto b = dt::zeros (1, (int) n);
            double ph = 0.0;
            const double norm = std::sqrt (fs / 48000.0);
            for (size_t i = 0; i < n; ++i)
            {
                ph += 2 * dt::kPi * 140.0 / fs;
                double v = 0.0;
                for (int h = 1; h < 20; ++h)
                    v += std::sin (h * ph) / h * std::exp (-std::pow ((h * 140.0 - 700.0) / 600.0, 2));
                v = 0.2 * v + partial * std::sin (2 * dt::kPi * 6500.0 * i / fs);   // (partial > 0: a bright vowel)
                const double w0 = nd (g);
                const double ss = biq (cs, biq (cs, w0, zs), zs + 2), sh = biq (ch, biq (ch, w0, zh), zh + 2);
                const double nz = norm * ((i / seg) % 4 == 1 ? ss : sh);
                b[0][i] = (float) ((i / seg) % 2 == 1 ? (esses ? 0.6 * nz : v) : v);
            }
            const auto in = b;
            rig.run (b, 256);
            // the band's region (6.5 kHz band-pass, 4th order), middle 60 % of each segment, from 1 s on
            const auto cb = bp (6500.0, 1.0);
            double zi[4] {}, zo[4] {}, ei[2] {}, eo[2] {};
            for (size_t i = 0; i < n; ++i)
            {
                const double xi = biq (cb, biq (cb, in[0][i], zi), zi + 2), xo = biq (cb, biq (cb, b[0][i], zo), zo + 2);
                const size_t u = i % seg;
                if (i < (size_t) fs || u < seg / 5 || u > seg - seg / 5)
                    continue;
                const int k = (int) ((i / seg) % 2);
                ei[k] += xi * xi;
                eo[k] += xo * xo;
            }
            cutVowel = 10 * std::log10 (eo[0] / ei[0]);
            cutEss = 10 * std::log10 (eo[1] / ei[1]);
        };
        double sv = 0, se = 0, ev = 0, ee = 0, d0 = 0, d1 = 0;
        // a voice with esses: the ESS band cuts the esses about as much as a SELF band does, and not the vowels
        essRun (0, 0.0, true, d0, se);
        essRun (kNumBands + 2, 0.0, true, d1, ee);
        r.less ("dynamic band, ESS key: the esses' 6.5 kHz region is cut", ee, -4.0, "dB");
        r.info ("dynamic band, SELF key: the esses' 6.5 kHz region (for comparison)", se, "dB");
        r.greater ("dynamic band, ESS key: the vowels between the esses are left alone", d1, -0.5, "dB");
        // a bright vowel (a strong 6.5 kHz partial), no esses: SELF cuts it, ESS does not
        essRun (0, 0.05, false, sv, d0);
        essRun (kNumBands + 2, 0.05, false, ev, d1);
        r.less ("dynamic band, SELF key: a bright vowel's 6.5 kHz region is cut", sv, -3.0, "dB");
        r.greater ("dynamic band, ESS key: a bright vowel's 6.5 kHz region is left alone", ev, -0.5, "dB");
    }
    // 3. band-pass: every slope distinct, n = slope/12 (rounded) 2nd-order sections
    {
        double worst = 0.0, prev = 1.0e9;
        bool monotone = true;
        for (int s : { 1, 3, 4, 5, 6, 7 })
        {
            Rig rig;
            rig.band (0, kBandPass, 1000.0f, 0, 1.0f, s);
            rig.prepare (fs, 1, 512);
            const int n = (kSlopeDb[s] + 6) / 12;
            worst = std::max (worst, refError (rig, fs, [&] (double f) { return ref::bandPassDb (f, 1000.0, 1.0, n); }, -60.0, 0.5));
            Rig r2;
            r2.band (0, kBandPass, 1000.0f, 0, 1.0f, s);
            r2.prepare (fs, 1, 512);
            auto h = dt::impulse (r2, (int) (0.5 * fs), 0, 0, 0, 512);
            const double at2k = dt::db (std::abs (dt::dft (h, 2.0 * dt::kPi * 2000.0 / fs)));
            monotone = monotone && at2k < prev - 1.0;
            prev = at2k;
        }
        r.less ("band-pass 12..96 dB/oct vs n cascaded 2nd-order sections", worst, 0.1, "dB");
        r.equal ("band-pass: each steeper slope attenuates more (1 oct out)", monotone ? 1 : 0, 1, "");
    }
    // 4. gain staging lands exactly on the set value (no smoothing residue)
    {
        double worst = 0.0;
        for (auto [id, gDb] : std::vector<std::pair<const char*, float>> { { "outGain", 6.0f }, { "inGain", -12.0f }, { "outGain", -7.3f }, { "inGain", 3.1f } })
        {
            Rig rig;
            rig.set (id, gDb);
            rig.prepare (fs, 1, 512);
            auto b = dt::zeros (1, (int) fs);
            dt::addNoise (b, 0.25, 21);
            auto in = b;
            rig.run (b, 512);
            const double g = std::pow (10.0, gDb / 20.0);
            for (int i = (int) fs / 2; i < (int) fs; ++i)
                worst = std::max (worst, std::abs (b[0][(size_t) i] - g * in[0][(size_t) i]) / (0.25 * g));
        }
        r.less ("input/output gain exact after smoothing (relative error)", worst, 2.0e-7, "");
    }
}

//------------------------------------------------------------------------------
void testNullAndLatency (dt::Recorder& r)
{
    r.section = "B. Null / latency";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    for (int mode = 0; mode < 3; ++mode)
        for (int os = 0; os < 3; ++os)
        {
            if (quick && os == 1)
                continue;
            Rig rig;
            rig.set ("phase", (float) mode);
            rig.set ("os", (float) os);
            // flat: unused bands, bands at 0 dB (Clean + Character), drive 100, modules off
            rig.band (0, kBell, 1000, 0, 1, 3, 0);
            rig.band (1, kBell, 3000, 0, 2, 3, 1);
            rig.band (2, kLowShelf, 100, 0, 0.7f, 1, 1);
            rig.set ("drive", 100);
            rig.prepare (fs, nCh, 512);
            const int lat = rig.e.getLatencySamples();
            const int settle = mode == kLinear ? 2 * dsp::LinearPhaseEq::tapsFor (1, fs) : lat + 64;
            auto b = dt::zeros (nCh, settle + 8192);
            dt::addNoise (b, 0.5, 7);
            auto in = b;
            rig.run (b, 333);
            double resid = 0.0;
            for (int c = 0; c < nCh; ++c)
                for (int i = settle; i < (int) b[0].size(); ++i)
                    resid = std::max (resid, (double) std::abs (b[(size_t) c][(size_t) i] - in[(size_t) c][(size_t) (i - lat)]));
            const std::string tag = std::string (modeName (mode)) + " OS " + std::to_string (1 << os) + "x";
            if (os == 0)
                r.equal ("flat null residual " + tag, resid, 0.0, "");
            else
                r.info ("flat broadband residual " + tag + " (OS filters band-limit; in-band null below)", dt::db (resid), "dBFS");
            // impulse peak at the reported latency
            Rig ri;
            ri.set ("phase", (float) mode);
            ri.set ("os", (float) os);
            ri.band (0, kBell, 2000, 3, 1);
            ri.prepare (fs, nCh, 512);
            const int l2 = ri.e.getLatencySamples();
            auto h = dt::impulse (ri, l2 + 4096, settle, 0, 0, 200);
            int pk = 0;
            for (int i = 0; i < (int) h.size(); ++i)
                if (std::abs (h[(size_t) i]) > std::abs (h[(size_t) pk]))
                    pk = i;
            r.equal ("impulse peak = reported latency " + tag, pk - l2, 0, "samples");
        }
    // OS band-limits the signal, so the meaningful OS null is in-band: 1 kHz tone
    for (int os = 1; os < 3; ++os)
    {
        Rig rig;
        rig.set ("os", (float) os);
        rig.prepare (fs, nCh, 512);
        const int lat = rig.e.getLatencySamples();
        auto b = dt::zeros (nCh, 16384);
        dt::addSine (b, fs, 1000.0, 0.5);
        auto in = b;
        rig.run (b, 512);
        double resid = 0.0;
        for (int i = 4096; i < 16384; ++i)
            resid = std::max (resid, (double) std::abs (b[0][(size_t) i] - in[0][(size_t) (i - lat)]));
        r.less ("OS " + std::to_string (1 << os) + "x in-band null (1 kHz)", dt::db (resid), -90.0, "dBFS");
    }
    // Mix 0 % = latency-aligned dry, bit-exact
    {
        Rig rig;
        rig.set ("mix", 0);
        rig.set ("phase", 1);
        rig.band (0, kBell, 1000, 12, 1);
        rig.prepare (fs, nCh, 512);
        const int lat = rig.e.getLatencySamples();
        auto b = dt::zeros (nCh, 8192);
        dt::addNoise (b, 0.5, 3);
        auto in = b;
        rig.run (b, 100);
        double resid = 0.0;
        for (int i = lat; i < 8192; ++i)
            resid = std::max (resid, (double) std::abs (b[0][(size_t) i] - in[0][(size_t) (i - lat)]));
        r.equal ("Mix 0 % residual (dry, NP)", resid, 0.0, "");
    }
}

//------------------------------------------------------------------------------
// render with a deterministic automation script; used for block-size invariance and fuzz
dt::Buf renderScripted (double fs, int nCh, int mode, int block, uint64_t seed, int len, bool extreme)
{
    Rig rig;
    rig.set ("phase", (float) mode);
    rig.set ("lpQuality", 0);
    for (int b = 0; b < 6; ++b)
        rig.band (b, b % 8, 200.0f * (float) (b + 1), 6.0f, 1.0f);
    rig.set ("ptOn", 1);
    rig.set ("indOn", 1);
    rig.set ("b1_dyn", 1); // a spectral band (its hops are tied to the absolute sample position)
    rig.set ("b1_dmode", 1);
    rig.set ("b1_thr", -45);
    rig.prepare (fs, nCh, block);
    auto buf = dt::zeros (nCh, len);
    dt::addNoise (buf, extreme ? 0.9 : 0.3, seed);
    std::mt19937_64 g (seed * 31 + 7);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const auto& tab = paramTable();
    for (int pos = 0; pos < len; pos += block)
    {
        if (pos % 1024 == 0)
        {
            // change a few parameters (never phase/os/quality in the invariance test)
            for (int k = 0; k < (extreme ? 12 : 3); ++k)
            {
                const int i = (int) (u (g) * (double) tab.size());
                const auto& s = tab[(size_t) i];
                if (! extreme && (s.id == "phase" || s.id == "os" || s.id == "lpQuality"))
                    continue;
                float v = (float) (s.min + (s.max - s.min) * u (g));
                if (s.type != PType::Float)
                    v = std::round (v);
                if (s.id == "outGain" || s.id == "inGain")
                    v = std::min (v, 6.0f);
                rig.p[(size_t) i] = v;
            }
            rig.e.setParams (rig.p);
        }
        const int m = std::min (block, len - pos);
        float* ptr[2] = { buf[0].data() + pos, nCh > 1 ? buf[1].data() + pos : nullptr };
        rig.e.process (ptr, nCh, m);
    }
    return buf;
}

void testBlockInvariance (dt::Recorder& r)
{
    r.section = "C. Block-size invariance";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    for (int mode = 0; mode < 3; ++mode)
    {
        // block 1024 is the automation granularity, so all renders see identical parameter timing
        const auto ref = renderScripted (fs, nCh, mode, 1024, 11, 1024 * 40, false);
        double worst = 0.0;
        for (int blk : { 1, 16, 64, 128, 256, 512 })
        {
            if (quick && blk == 1)
                continue;
            // smaller blocks: set the same parameters at the same sample positions
            Rig rig;
            (void) rig;
            const auto out = [&]
            {
                Rig rg;
                rg.set ("phase", (float) mode);
                rg.set ("lpQuality", 0);
                for (int b = 0; b < 6; ++b)
                    rg.band (b, b % 8, 200.0f * (float) (b + 1), 6.0f, 1.0f);
                rg.set ("ptOn", 1);
                rg.set ("indOn", 1);
                rg.set ("b1_dyn", 1);
                rg.set ("b1_dmode", 1);
                rg.set ("b1_thr", -45);
                rg.prepare (fs, nCh, blk);
                const int len = 1024 * 40;
                auto buf = dt::zeros (nCh, len);
                dt::addNoise (buf, 0.3, 11);
                std::mt19937_64 g (11 * 31 + 7);
                std::uniform_real_distribution<double> u (0.0, 1.0);
                const auto& tab = paramTable();
                for (int pos = 0; pos < len; pos += blk)
                {
                    if (pos % 1024 == 0)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            const int i = (int) (u (g) * (double) tab.size());
                            const auto& s = tab[(size_t) i];
                            if (s.id == "phase" || s.id == "os" || s.id == "lpQuality")
                                continue;
                            float v = (float) (s.min + (s.max - s.min) * u (g));
                            if (s.type != PType::Float)
                                v = std::round (v);
                            if (s.id == "outGain" || s.id == "inGain")
                                v = std::min (v, 6.0f);
                            rg.p[(size_t) i] = v;
                        }
                        rg.e.setParams (rg.p);
                    }
                    const int m = std::min (blk, len - pos);
                    float* ptr[2] = { buf[0].data() + pos, nCh > 1 ? buf[1].data() + pos : nullptr };
                    rg.e.process (ptr, nCh, m);
                }
                return buf;
            }();
            for (int c = 0; c < nCh; ++c)
                for (size_t i = 0; i < out[0].size(); ++i)
                    worst = std::max (worst, (double) std::abs (out[(size_t) c][i] - ref[(size_t) c][i]));
        }
        r.equal (std::string ("max difference vs block 1024 (") + modeName (mode) + ")", worst, 0.0, "");
    }
}

void testFuzz (dt::Recorder& r)
{
    r.section = "D. Fuzz / stress";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    bool finite = true;
    double peak = 0.0;
    for (int mode = 0; mode < 3; ++mode)
        for (uint64_t seed = 1; seed <= (quick ? 2u : 6u); ++seed)
        {
            const auto out = renderScripted (fs, nCh, mode, 97 + (int) seed * 61, seed, 48000 * 2, true);
            finite = finite && dt::allFinite (out);
            for (auto& ch : out)
                peak = std::max (peak, dt::maxAbs (ch));
        }
    r.truth ("random extreme automation: all samples finite", finite);
    r.less ("random extreme automation: peak output", dt::db (peak), 48.0, "dBFS");

    // +24 dBFS input, extreme boosts
    {
        Rig rig;
        rig.band (0, kBell, 100, 30, 0.3f);
        rig.band (1, kHighShelf, 8000, 30, 2.0f, 1, 1);
        rig.set ("drive", 100);
        rig.set ("ptOn", 1);
        rig.set ("ptLowBoost", 10);
        rig.set ("ptHighBoost", 10);
        rig.prepare (fs, nCh, 512);
        auto b = dt::zeros (nCh, 48000);
        dt::addNoise (b, 15.8, 5);
        rig.run (b, 512);
        r.truth ("+24 dBFS noise, extreme boosts: finite", dt::allFinite (b));
    }
    // silence in -> exact silence out, also after loud material (no denormal tails left)
    {
        Rig rig;
        rig.band (0, kBell, 60, 12, 8);
        rig.band (1, kLowCut, 30, 0, 0.7f, 7);
        rig.prepare (fs, nCh, 512);
        auto z = dt::zeros (nCh, 48000);
        rig.run (z, 512);
        double m = 0.0;
        for (auto& ch : z)
            m = std::max (m, dt::maxAbs (ch));
        r.equal ("digital silence in -> out (fresh)", m, 0.0, "");
        auto b = dt::zeros (nCh, 48000);
        dt::addNoise (b, 1.0, 9);
        rig.run (b, 512);
        auto z2 = dt::zeros (nCh, (int) (fs * 4));
        rig.run (z2, 512);
        m = 0.0;
        for (auto& ch : z2)
            m = std::max (m, dt::maxAbs (ch, (int) (fs * 3)));
        r.less ("silence after loud noise, 3-4 s later", dt::db (m), -140.0, "dBFS");
    }
}

//------------------------------------------------------------------------------
// click test: 100 Hz sine while parameters jump; energy above 6 kHz must stay tiny
void testClicks (dt::Recorder& r)
{
    r.section = "E. Click-free automation and switching";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    auto hfPeak = [&] (const dt::Buf& b, int from)
    {
        // 4th-order high-pass at 6 kHz (two TPT SVF stages)
        double worst = 0.0;
        for (auto& ch : b)
        {
            dsp::RegionFilter a, c;
            a.set (kHighCut, 6000, 0.7, fs);
            c.set (kHighCut, 6000, 0.7, fs);
            for (size_t i = 0; i < ch.size(); ++i)
            {
                const double y = c.tick (a.tick (ch[i], 0), 0);
                if ((int) i >= from)
                    worst = std::max (worst, std::abs (y));
            }
        }
        return worst;
    };
    struct Step { const char* name; std::vector<std::pair<std::string, float>> a, b; double limit = -60.0; };
    const std::vector<Step> steps {
        { "gain jump -12 <-> +12 dB", { { "b1_gain", -12 } }, { { "b1_gain", 12 } } },
        { "freq jump 300 Hz <-> 3 kHz", { { "b1_freq", 300 } }, { { "b1_freq", 3000 } } },
        { "Q jump 0.3 <-> 10", { { "b1_q", 0.3f } }, { { "b1_q", 10 } } },
        { "type bell <-> low shelf", { { "b1_type", 0 } }, { { "b1_type", 1 } } },
        { "band bypass toggle", { { "b1_byp", 0 } }, { { "b1_byp", 1 } } },
        { "band add/remove", { { "b1_used", 1 } }, { { "b1_used", 0 } } },
        { "placement stereo <-> mid", { { "b1_place", 0 } }, { { "b1_place", 3 } } },
        { "slope 12 <-> 48 (low cut)", { { "b2_slope", 1 } }, { { "b2_slope", 5 } } },
        { "module on/off (Passive Tube)", { { "ptOn", 0 } }, { { "ptOn", 1 } } },
        { "Passive Tube low freq 20 <-> 100", { { "ptLowFreq", 0 } }, { { "ptLowFreq", 3 } } },
        { "Console E <-> G", { { "conMode", 0 } }, { { "conMode", 1 } } },
        { "phase mode ZL <-> NP", { { "phase", 0 } }, { { "phase", 1 } } },
        { "phase mode ZL <-> LP", { { "phase", 0 } }, { { "phase", 2 } } },
        { "phase mode NP <-> LP", { { "phase", 1 } }, { { "phase", 2 } } },
        { "oversampling 1x <-> 4x", { { "os", 0 } }, { { "os", 2 } } },
        { "output gain -12 <-> +6", { { "outGain", -12 } }, { { "outGain", 6 } } },
        { "solo-free mix 0 <-> 100 %", { { "mix", 0 } }, { { "mix", 100 } } },
        // near Nyquist (fitted 4th-order realisations, D25): sweeps across the hard / classic boundary
        // a 1-octave jump of a Q 12 resonance glides through the top octave in ~20 ms: at 88.2-192 kHz,
        // where the resonance is inside the band, the moving resonance itself rings (v1.0: -54 dBFS)
        { "high shelf Q 12 +12 dB: 14 kHz <-> 28 kHz", { { "b1_type", 2 }, { "b1_q", 12 }, { "b1_gain", 12 }, { "b1_freq", 14000 } }, { { "b1_freq", 28000 } }, -50.0 },
        { "notch Q 2: 15 kHz <-> 30 kHz", { { "b1_type", 5 }, { "b1_q", 2 }, { "b1_freq", 15000 } }, { { "b1_freq", 30000 } } },
        { "bell 21 kHz Q 8: -18 <-> +18 dB", { { "b1_freq", 21000 }, { "b1_q", 8 }, { "b1_gain", -18 } }, { { "b1_gain", 18 } } },
        { "dynamic mode broadband <-> spectral (100 Hz band, acting)", { { "b1_freq", 100 }, { "b1_dyn", 1 }, { "b1_thr", -40 }, { "b1_range", -12 }, { "b1_dmode", 0 } },
          { { "b1_dmode", 1 } } },
        { "spectral band on/off while acting", { { "b1_freq", 100 }, { "b1_dyn", 1 }, { "b1_dmode", 1 }, { "b1_thr", -40 }, { "b1_range", -12 } },
          { { "b1_dyn", 0 } } },
    };
    for (const auto& st : steps)
    {
        Rig rig;
        rig.band (0, kBell, 1000, 6, 1);
        rig.band (1, kLowCut, 40, 0, 0.7f, 1);
        rig.set ("ptLowBoost", 4);
        rig.set ("conOn", 1);
        rig.set ("conHmfGain", 4);
        for (auto& kv : st.a)
            rig.set (kv.first, kv.second);
        rig.prepare (fs, nCh, 256);
        const int len = (int) fs * 2;
        auto b = dt::zeros (nCh, len);
        dt::addSine (b, fs, 100.0, 0.25);
        for (int pos = 0; pos < len; pos += 256)
        {
            const int blockIdx = pos / 256;
            if (pos > (int) fs / 4 && blockIdx % 40 == 0)
            {
                const bool toB = (blockIdx / 40) % 2 == 1;
                for (auto& kv : (toB ? st.b : st.a))
                    rig.p[(size_t) paramIndex (kv.first)] = kv.second;
                rig.e.setParams (rig.p);
            }
            float* ptr[2] = { b[0].data() + pos, nCh > 1 ? b[1].data() + pos : nullptr };
            rig.e.process (ptr, nCh, std::min (256, len - pos));
        }
        const double hf = hfPeak (b, (int) fs / 4);
        r.less (std::string ("HF burst > 6 kHz: ") + st.name, dt::db (hf), st.limit, "dBFS");
    }
}

//------------------------------------------------------------------------------
void testDynamics (dt::Recorder& r)
{
    r.section = "F. Dynamic bands";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    auto staticCase = [&] (const char* name, float thr, float range, float ratio, float inDb, double expectDb)
    {
        Rig rig;
        rig.band (0, kBell, 2000, 0, 1);
        rig.set ("b1_dyn", 1);
        rig.set ("b1_thr", thr);
        rig.set ("b1_range", range);
        rig.set ("b1_ratio", ratio);
        rig.set ("b1_atk", 2);
        rig.set ("b1_rel", 50);
        rig.prepare (fs, nCh, 512);
        auto b = dt::zeros (nCh, (int) fs);
        dt::addSine (b, fs, 2000.0, std::pow (10.0, inDb / 20.0));
        rig.run (b, 512);
        const double outDb = dt::db (dt::toneAmp (b[0], (int) fs / 2, 8192, 2000.0, fs));
        r.range (std::string ("static gain change: ") + name, outDb - inDb, expectDb - 0.5, expectDb + 0.5, "dB");
    };
    staticCase ("thr -30, range -12, ratio 20, in -20 dBFS", -30, -12, 20, -20, -10.0 * (1 - 1.0 / 20));
    staticCase ("thr -30, range -12, ratio 2, in -20 dBFS", -30, -12, 2, -20, -5.0);
    staticCase ("thr -30, range -4, ratio 20, in -10 dBFS (range limit)", -30, -4, 20, -10, -4.0);
    staticCase ("thr -30, range +6, ratio 4, in -20 dBFS (upward)", -30, 6, 4, -20, 6.0);
    staticCase ("below threshold: no change", -10, -12, 20, -30, 0.0);

    // time constants: 63 % of the final change
    for (auto tc : { std::pair<float, float> { 30.0f, 150.0f }, std::pair<float, float> { 10.0f, 400.0f } })
    {
        Rig rig;
        rig.band (0, kBell, 2000, 0, 1);
        rig.set ("b1_dyn", 1);
        rig.set ("b1_thr", -40);
        rig.set ("b1_range", -12);
        rig.set ("b1_ratio", 20);
        rig.set ("b1_atk", tc.first);
        rig.set ("b1_rel", tc.second);
        rig.prepare (fs, nCh, 16);
        const int on = (int) fs / 2, off = (int) fs * 2, len = (int) fs * 4;
        std::vector<double> d;
        auto b = dt::zeros (nCh, len);
        dt::addSine (b, fs, 2000.0, 0.1, on, off); // -20 dBFS -> 20 dB over -> final -11.4 dB
        for (int pos = 0; pos < len; pos += 16)
        {
            float* ptr[2] = { b[0].data() + pos, nCh > 1 ? b[1].data() + pos : nullptr };
            rig.e.process (ptr, nCh, 16);
            d.push_back (rig.e.getMeters().delta[0]);
        }
        const double fin = d[(size_t) (off / 16 - 1)];
        int ta = -1, tr = -1;
        for (size_t i = (size_t) (on / 16); i < d.size(); ++i)
            if (ta < 0 && d[i] <= 0.632 * fin)
                ta = (int) i * 16 - on;
        for (size_t i = (size_t) (off / 16); i < d.size(); ++i)
            if (tr < 0 && d[i] >= 0.368 * fin)
                tr = (int) i * 16 - off;
        r.range ("attack " + std::to_string ((int) tc.first) + " ms: 63 % time", ta * 1000.0 / fs, tc.first * 0.8, tc.first * 1.2 + 2.0, "ms");
        r.range ("release " + std::to_string ((int) tc.second) + " ms: 63 % time", tr * 1000.0 / fs, tc.second * 0.8, tc.second * 1.2, "ms");
    }

    // external side-chain: band ducks only when the side-chain is loud
    {
        Rig rig;
        rig.band (0, kBell, 60, 0, 1);
        rig.set ("b1_dyn", 1);
        rig.set ("b1_thr", -30);
        rig.set ("b1_range", -9);
        rig.set ("b1_ratio", 20);
        rig.set ("b1_src", (float) (kNumBands + 1));
        rig.prepare (fs, nCh, 512);
        auto b = dt::zeros (nCh, (int) fs * 2);
        dt::addSine (b, fs, 60.0, 0.1);
        auto sc = dt::zeros (1, (int) fs * 2);
        for (int i = (int) fs; i < (int) fs * 2; ++i)
            sc[0][(size_t) i] = (float) (0.5 * std::sin (2.0 * dt::kPi * 60.0 * i / fs));
        rig.run (b, 512, &sc);
        const double quiet = dt::db (dt::toneAmp (b[0], (int) fs / 2, 16384, 60.0, fs)) + 20.0;
        const double loud = dt::db (dt::toneAmp (b[0], (int) fs * 3 / 2, 16384, 60.0, fs)) + 20.0;
        r.range ("external SC quiet: no change", quiet, -0.3, 0.3, "dB");
        r.range ("external SC loud: ducks by range", loud, -9.5, -8.5, "dB");
    }
    // detector from another band's region: cut 3 kHz when 9 kHz is loud
    {
        Rig rig;
        rig.band (0, kBell, 3000, 0, 1);
        rig.band (1, kBell, 9000, 0, 2);
        rig.set ("b1_dyn", 1);
        rig.set ("b1_thr", -30);
        rig.set ("b1_range", -6);
        rig.set ("b1_ratio", 20);
        rig.set ("b1_src", 2); // "Band 2"
        rig.prepare (fs, nCh, 512);
        auto b = dt::zeros (nCh, (int) fs);
        dt::addSine (b, fs, 3000.0, 0.05);
        dt::addSine (b, fs, 9000.0, 0.3);
        rig.run (b, 512);
        const double g3 = dt::db (dt::toneAmp (b[0], (int) fs / 2, 8192, 3000.0, fs)) - dt::db (0.05);
        r.range ("source = Band 2 region: 3 kHz band reacts to 9 kHz", g3, -6.5, -5.5, "dB");
    }

    //== spectral dynamics (Dynamic Mode = Spectral) ===============================================
    // a loud 3 kHz tone and a quiet 2.2 kHz tone inside a 3 kHz bell's region, a loud 300 Hz tone outside
    auto specRun = [&] (int dmode, float range, int place, const dt::Buf* sc, auto&& addInput)
    {
        Rig rig;
        rig.band (0, kBell, 3000, 0, 1, 3, 0, place);
        rig.set ("b1_dyn", 1);
        rig.set ("b1_dmode", (float) dmode);
        rig.set ("b1_thr", -40);
        rig.set ("b1_range", range);
        rig.set ("b1_ratio", 20);
        rig.set ("b1_atk", 5);
        rig.set ("b1_rel", 100);
        if (sc != nullptr)
            rig.set ("b1_src", (float) (kNumBands + 1));
        rig.prepare (fs, nCh, 512);
        auto b = dt::zeros (nCh, (int) (2.5 * fs));
        addInput (b);
        rig.run (b, 512, sc);
        return b;
    };
    auto tone = [&] (const dt::Buf& b, double f, double ref, int ch = 0)
    {
        return dt::db (dt::toneAmp (b[(size_t) ch], (int) (1.5 * fs), (int) (0.8 * fs), f, fs)) - dt::db (ref);
    };
    auto threeTones = [&] (dt::Buf& b)
    {
        dt::addSine (b, fs, 3000.0, 0.5);
        dt::addSine (b, fs, 2200.0, 0.00316);
        dt::addSine (b, fs, 300.0, 0.5);
    };
    {
        const auto sp = specRun (1, -12.0f, kStereo, nullptr, threeTones);
        const auto bb = specRun (0, -12.0f, kStereo, nullptr, threeTones);
        r.range ("spectral: loud tone in the band reduced by the range", tone (sp, 3000.0, 0.5), -12.5, -11.5, "dB");
        r.range ("spectral: quiet tone in the same band untouched", tone (sp, 2200.0, 0.00316), -0.1, 0.1, "dB");
        r.range ("spectral: loud tone outside the band (300 Hz) ~ untouched", tone (sp, 300.0, 0.5), -0.5, 0.05, "dB");
        r.less ("broadband for comparison pulls the quiet tone down (dB)", tone (bb, 2200.0, 0.00316), -6.0, "dB");
        const auto up = specRun (1, 6.0f, kStereo, nullptr, threeTones);
        r.range ("spectral upward (range +6): loud tone raised", tone (up, 3000.0, 0.5), 5.5, 6.5, "dB");
        r.range ("spectral upward: quiet tone untouched", tone (up, 2200.0, 0.00316), -0.1, 0.1, "dB");
    }
    {
        // below threshold everywhere: bit-identical to the static band
        auto quiet = [&] (dt::Buf& b) { dt::addSine (b, fs, 3000.0, 0.001); dt::addSine (b, fs, 500.0, 0.001); };
        const auto sp = specRun (1, -12.0f, kStereo, nullptr, quiet);
        Rig rig;
        rig.band (0, kBell, 3000, 0, 1);
        rig.prepare (fs, nCh, 512);
        auto ref = dt::zeros (nCh, (int) (2.5 * fs));
        quiet (ref);
        rig.run (ref, 512);
        double d = 0.0;
        for (size_t c = 0; c < ref.size(); ++c)
            for (size_t i = 0; i < ref[c].size(); ++i)
                d = std::max (d, (double) std::abs (sp[c][i] - ref[c][i]));
        r.less ("spectral below threshold: output identical to the static band", d, 1.0e-9, "");
    }
    {
        // external side-chain: a loud 3 kHz tone in the side-chain cuts 3 kHz of the programme only
        auto scB = dt::zeros (1, (int) (2.5 * fs));
        dt::addSine (scB, fs, 3000.0, 0.5);
        auto prog = [&] (dt::Buf& b) { dt::addSine (b, fs, 3000.0, 0.01); dt::addSine (b, fs, 2200.0, 0.01); };
        const auto sp = specRun (1, -9.0f, kStereo, &scB, prog);
        r.range ("spectral side-chain: the side-chain's frequency cut in the programme", tone (sp, 3000.0, 0.01), -9.5, -8.5, "dB");
        r.range ("spectral side-chain: other frequencies untouched", tone (sp, 2200.0, 0.01), -0.1, 0.1, "dB");
    }
    if (nCh == 2)
    {
        // a spectral band on the Side: a loud 3 kHz Side tone is cut, the same tone in the Mid is not
        auto ms = [&] (dt::Buf& b)
        {
            for (int i = 0; i < (int) b[0].size(); ++i)
            {
                const double m = 0.5 * std::sin (2.0 * dt::kPi * 3000.0 * i / fs), sd = 0.5 * std::sin (2.0 * dt::kPi * 3000.0 * i / fs + 1.0);
                b[0][(size_t) i] = (float) (m + sd);
                b[1][(size_t) i] = (float) (m - sd);
            }
        };
        const auto sp = specRun (1, -12.0f, kSide, nullptr, ms);
        dt::Buf mid (1, std::vector<float> (sp[0].size())), side (1, std::vector<float> (sp[0].size()));
        for (size_t i = 0; i < sp[0].size(); ++i)
        {
            mid[0][i] = 0.5f * (sp[0][i] + sp[1][i]);
            side[0][i] = 0.5f * (sp[0][i] - sp[1][i]);
        }
        r.range ("spectral on Side: Side tone cut by the range", tone (side, 3000.0, 0.5), -12.5, -11.5, "dB");
        r.range ("spectral on Side: Mid untouched", tone (mid, 3000.0, 0.5), -0.1, 0.1, "dB");
    }
}

//------------------------------------------------------------------------------
void testPlacement (dt::Recorder& r)
{
    r.section = "G. Stereo placement";
    if (r.ctx.nCh != 2)
    {
        // mono: Side bands are inactive, Mid bands act
        Rig rig;
        rig.band (0, kBell, 1000, 12, 1, 3, 0, kSide);
        rig.prepare (r.ctx.fs, 1, 512);
        auto h = dt::impulse (rig, 4096, 0);
        double resid = 0.0;
        for (int i = 0; i < 4096; ++i)
            resid = std::max (resid, std::abs (h[(size_t) i] - (i == 0 ? 1.0 : 0.0)));
        r.equal ("mono: Side band inactive (null)", resid, 0.0, "");
        return;
    }
    const double fs = r.ctx.fs;
    for (int mode = 0; mode < 3; ++mode)
        for (int place : { (int) kLeft, (int) kRight, (int) kMid, (int) kSide })
        {
            Rig rig;
            rig.set ("phase", (float) mode);
            rig.band (0, kBell, 8000, 12, 1, 3, 0, place);
            rig.band (1, kHighShelf, 12000, 9, 0.7f, 1, 0, place);
            rig.prepare (fs, 2, 512);
            const int lat = rig.e.getLatencySamples();
            const int settle = mode == kLinear ? 2 * dsp::LinearPhaseEq::tapsFor (1, fs) : lat + 64;
            auto b = dt::zeros (2, settle + 16384);
            dt::addNoise (b, 0.4, 21);
            for (size_t i = 0; i < b[1].size(); ++i)
                b[1][i] = (float) (0.4 * std::sin (0.37 * (double) i)) + 0.3f * b[1][i];
            auto in = b;
            rig.run (b, 512);
            // the untouched component: L, R, M or S
            auto comp = [&] (const dt::Buf& x, int i, int which) -> double
            {
                const double l = x[0][(size_t) i], rr = x[1][(size_t) i];
                switch (which)
                {
                    case kLeft: return rr;      // a Left band must leave R alone
                    case kRight: return l;
                    case kMid: return 0.5 * (l - rr); // Mid band leaves Side alone
                    default: return 0.5 * (l + rr);
                }
            };
            double resid = 0.0, changed = 0.0;
            for (int i = settle; i < (int) b[0].size(); ++i)
            {
                resid = std::max (resid, std::abs (comp (b, i, place) - comp (in, i - lat, place)));
                const int other = place == kLeft ? kRight : (place == kRight ? kLeft : (place == kMid ? kSide : kMid));
                changed = std::max (changed, std::abs (comp (b, i, other) - comp (in, i - lat, other)));
            }
            const char* pn[] = { "Stereo", "Left", "Right", "Mid", "Side" };
            r.less (std::string ("untouched component, ") + pn[place] + " bands (" + modeName (mode) + ")", dt::db (resid), -100.0, "dBFS");
            r.greater (std::string ("processed component changes, ") + pn[place] + " (" + modeName (mode) + ")", dt::db (changed), -40.0, "dBFS");
        }
}

//------------------------------------------------------------------------------
void testCharacter (dt::Recorder& r)
{
    r.section = "H. Character modules";
    const double fs = r.ctx.fs;
    // Passive Tube LF trick (curve model = what the engine realises in Clean accuracy)
    auto ptCurve = [&] (float boost, float atten, int sel, double& lf, double& dip, double& dipHz)
    {
        Rig rig;
        rig.set ("ptOn", 1);
        rig.set ("ptLowFreq", (float) sel);
        rig.set ("ptLowBoost", boost);
        rig.set ("ptLowAtten", atten);
        CurveModel cm;
        cm.build (rig.p, fs);
        lf = cm.totalDb (30.0);
        dip = 1.0e9;
        for (double f : dt::logFreqs (kPtLowHz[sel], 2000.0, 400))
        {
            const double v = cm.totalDb (f);
            if (v < dip)
            {
                dip = v;
                dipHz = f;
            }
        }
    };
    double lf, dip, dipHz;
    ptCurve (10, 0, 2, lf, dip, dipHz);
    r.range ("Passive Tube 60 Hz boost 10 alone: level at 30 Hz", lf, 11.0, 14.0, "dB");
    ptCurve (10, 5, 2, lf, dip, dipHz);
    r.range ("boost 10 + atten 5: LF bump at 30 Hz", lf, 3.0, 8.0, "dB");
    r.range ("boost 10 + atten 5: dip depth", dip, -6.0, -2.0, "dB");
    r.range ("boost 10 + atten 5: dip position / selection", dipHz / 60.0, 3.0, 8.0, "x");
    const double dip5 = dip;
    ptCurve (10, 10, 2, lf, dip, dipHz);
    r.range ("boost 10 + atten 10: dip up to ~8 dB", dip, -9.5, -6.0, "dB");
    r.truth ("dip deepens with atten", dip < dip5);

    // module linear part realised exactly (zero latency, drive 0)
    for (const char* mod : { "ptOn", "indOn", "conOn", "airOn" })
    {
        Rig rig;
        rig.set ("drive", 0);
        rig.set (mod, 1);
        rig.set ("ptLowBoost", 6); rig.set ("ptLowAtten", 3); rig.set ("ptHighBoost", 5); rig.set ("ptHighAtten", 3);
        rig.set ("indLowGain", 5); rig.set ("indMidGain", 6); rig.set ("indHighGain", 4); rig.set ("indHpf", 2);
        rig.set ("conLfGain", 5); rig.set ("conLmfGain", -4); rig.set ("conHmfGain", 6); rig.set ("conHfGain", 3);
        rig.set ("conMode", 1); rig.set ("conHpf", 80);
        rig.set ("airGain", 8);
        rig.prepare (fs, 2, 512);
        auto h = dt::impulse (rig, (int) (65536 * fs / 48000.0), 0);
        Spectrum sp;
        sp.compute (h, fs);
        CurveModel cm;
        cm.build (rig.p, fs);
        double e = 0.0;
        for (double f : dt::logFreqs (20.0, std::min (20000.0, 0.95 * fs / 2), 200))
            if (cm.totalDb (f) > -24.0)
                e = std::max (e, std::abs (sp.at (f) - cm.totalDb (f)));
        r.less (std::string ("module curve vs model at drive 0: ") + mod, e, 0.1, "dB");
    }

    // harmonics with drive: tube mostly 3rd, inductor saturates lows more than highs
    auto thd = [&] (const char* mod, double f0, double amp, double& h2, double& h3)
    {
        Rig rig;
        rig.set ("drive", 100);
        rig.set (mod, 1);
        rig.set ("os", 2);
        rig.prepare (fs, 1, 512);
        auto b = dt::zeros (1, (int) fs);
        dt::addSine (b, fs, f0, amp);
        rig.run (b, 512);
        const int from = (int) fs / 2, n = 16384;
        const double a1 = dt::toneAmp (b[0], from, n, f0, fs);
        h2 = dt::toneAmp (b[0], from, n, 2 * f0, fs) / a1;
        h3 = dt::toneAmp (b[0], from, n, 3 * f0, fs) / a1;
        return std::sqrt (h2 * h2 + h3 * h3) * 100.0;
    };
    double h2, h3;
    const double tubeThd = thd ("ptOn", 1000, 0.5, h2, h3);
    r.range ("Passive Tube THD, drive 100 %, -6 dBFS 1 kHz", tubeThd, 0.3, 8.0, "%");
    r.greater ("Passive Tube: 3rd harmonic dominates (H3/H2)", h3 / std::max (h2, 1e-12), 1.5, "x");
    const double indLow = thd ("indOn", 50, 0.5, h2, h3);
    const double indHigh = thd ("indOn", 2000, 0.5, h2, h3);
    r.greater ("Inductor: 50 Hz distorts more than 2 kHz", indLow / std::max (indHigh, 1e-9), 2.0, "x");
    const double conThd = thd ("conOn", 1000, 0.5, h2, h3);
    r.range ("Console THD, drive 100 %, -6 dBFS", conThd, 0.1, 6.0, "%");
    // drive 0 = linear
    {
        Rig rig;
        rig.set ("drive", 0);
        rig.set ("ptOn", 1);
        rig.set ("indOn", 1);
        rig.set ("conOn", 1);
        rig.prepare (fs, 1, 512);
        auto b = dt::zeros (1, (int) fs);
        dt::addSine (b, fs, 1000, 0.9);
        rig.run (b, 512);
        const double a1 = dt::toneAmp (b[0], (int) fs / 2, 16384, 1000, fs);
        const double a3 = dt::toneAmp (b[0], (int) fs / 2, 16384, 3000, fs);
        r.less ("drive 0: harmonics", dt::db (a3 / a1), -120.0, "dB");
    }
}

//------------------------------------------------------------------------------
void testMisc (dt::Recorder& r)
{
    r.section = "I. Solo / auto gain / mono";
    const double fs = r.ctx.fs;
    const int nCh = r.ctx.nCh;
    {
        Rig rig;
        rig.band (0, kBell, 1000, 6, 2);
        rig.prepare (fs, nCh, 512);
        rig.e.setSoloBand (0);
        auto b = dt::zeros (nCh, (int) fs);
        dt::addSine (b, fs, 1000.0, 0.1);
        dt::addSine (b, fs, 100.0, 0.1);
        rig.run (b, 512);
        const double a1k = dt::db (dt::toneAmp (b[0], (int) fs / 2, 8192, 1000, fs) / 0.1);
        const double a100 = dt::db (dt::toneAmp (b[0], (int) fs / 2, 8192, 100, fs) / 0.1);
        r.range ("solo band 1 kHz: tone in band passes", a1k, -1.0, 1.0, "dB");
        r.less ("solo band 1 kHz: 100 Hz tone rejected", a100, -20.0, "dB");
    }
    for (auto curve : { std::tuple<int, float, float, float> { kBell, 1000.0f, 6.0f, 0.4f },
                        std::tuple<int, float, float, float> { kHighShelf, 4000.0f, 8.0f, 0.7f },
                        std::tuple<int, float, float, float> { kLowShelf, 150.0f, -6.0f, 0.7f } })
    {
        Rig rig;
        rig.band (0, std::get<0> (curve), std::get<1> (curve), std::get<2> (curve), std::get<3> (curve), 1);
        rig.set ("autoGain", 1);
        rig.prepare (fs, nCh, 512);
        // pink-ish noise: sum of octave-spaced sines with random phases, equal amplitude per octave
        auto b = dt::zeros (nCh, (int) fs * 3);
        std::mt19937 g (5);
        for (double f = 31.25; f < 16000.0; f *= 1.189207)
        {
            const double ph = std::uniform_real_distribution<double> (0, 6.28) (g);
            for (auto& ch : b)
                for (size_t i = 0; i < ch.size(); ++i)
                    ch[i] += (float) (0.03 * std::sin (2 * dt::kPi * f * (double) i / fs + ph));
        }
        // K-weighted-ish loudness: high shelf +4 dB > 2 kHz approximated by RMS of a pre-shelved copy
        auto kw = [&] (const dt::Buf& x)
        {
            dsp::RegionFilter hp;
            hp.set (kHighShelf, 1500, 0.7, fs);
            double s = 0.0;
            for (size_t i = (size_t) fs; i < x[0].size(); ++i)
            {
                const double v = x[0][i];
                const double y = v + 0.585 * hp.tick (v, 0); // ~+4 dB above 1.5 kHz
                s += y * y;
            }
            return 10.0 * std::log10 (s);
        };
        const auto in = b;
        rig.run (b, 512);
        char nm[96];
        std::snprintf (nm, sizeof nm, "auto gain loudness match (type %d, %+.0f dB)", std::get<0> (curve), std::get<2> (curve));
        r.range (nm, kw (b) - kw (in), -1.5, 1.5, "dB");
    }
}

//------------------------------------------------------------------------------
// protected names that must never appear in user-facing preset text (docs/research/clean-room.md)
const char* kProtected[] = { "pultec", "neve", "ssl", "solid state logic", "fabfilter", "pro-q", "maag", "api 5", "api5",
                             "manley", "sonnox", "tdr", "1073", "eqp", "4000 e", "sp-1200", "sp1200", "e-mu", "emu ",
                             "lewitt", "shure" };

void testPresets (std::vector<dt::Result>& all)
{
    dt::Recorder r ({ 48000.0, 2 });
    r.section = "P. Factory presets";
    const auto& list = factoryPresets();
    r.greater ("number of factory presets", (double) list.size(), 29.5, "");
    int perCat[5] {};
    const auto cats = presetCategories();
    bool idsOk = true, textOk = true;
    for (const auto& p : list)
    {
        for (size_t c = 0; c < cats.size(); ++c)
            if (p.category == cats[c])
                ++perCat[c];
        for (const auto& kv : p.values)
            if (paramIndex (kv.first) < 0)
            {
                idsOk = false;
                std::printf ("  preset %s: unknown parameter %s\n", p.name.c_str(), kv.first.c_str());
            }
        std::string text = p.name + " " + p.description;
        std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
        for (const char* w : kProtected)
            if (text.find (w) != std::string::npos && ! (std::string (w) == "lewitt" || std::string (w) == "shure"))
            {
                textOk = false;
                std::printf ("  preset %s mentions '%s'\n", p.name.c_str(), w);
            }
        if (p.description.size() < 30)
            textOk = false;
    }
    r.truth ("every preset value refers to a real parameter id", idsOk);
    r.truth ("names/descriptions: no protected names, description present", textOk);
    r.greater ("Vocals presets", perCat[0], 11.5, "");
    r.greater ("Beats & Instruments presets", perCat[1], 11.5, "");
    r.greater ("Mix Bus & Master presets", perCat[2], 7.5, "");
    r.greater ("Axell Signature presets", perCat[3], 2.5, "");
    double worst = 0.0;
    std::string worstName;
    bool finite = true;
    for (const auto& p : list)
    {
        Rig rig;
        rig.p = p.resolve();
        rig.prepare (48000.0, 2, 512);
        const int lat = rig.e.getLatencySamples();
        auto b = dt::zeros (2, 48000 * 6);
        dt::addPink (b, -18.0, 7);
        const auto in = b;
        rig.run (b, 512);
        finite = finite && dt::allFinite (b);
        const double d = dt::kLoudnessDb (b, 72000 + lat) - dt::kLoudnessDb (in, 72000);
        if (std::abs (d) > std::abs (worst))
        {
            worst = d;
            worstName = p.name;
        }
    }
    r.truth ("all presets: output finite on pink noise", finite);
    r.range ("loudness vs bypass, pink noise -18 dBFS, K-weighted (worst: " + worstName + ")", worst, -0.1, 0.1, "LU");
    for (auto& x : r.results)
        all.push_back (x);
}

//------------------------------------------------------------------------------
void testRealtimeSafety (std::vector<dt::Result>& all)
{
    dt::Recorder r ({ 48000.0, 2 });
    r.section = "K. Real-time safety";
    long worst = 0;
    for (int mode = 0; mode < 3; ++mode)
        for (int os = 0; os < 3; ++os)
        {
            Rig rig;
            rig.set ("phase", (float) mode);
            rig.set ("os", (float) os);
            rig.e.setNonRealtime (false); // realtime path: linear-phase designs on the background thread
            for (int b = 0; b < 12; ++b)
                rig.band (b, b % 8, 80.0f * (float) (b + 1), 3.0f, 1.0f, 3, b % 2, b % 5);
            for (int b = 0; b < 12; b += 3)
                rig.set (bandId (b, bpDyn), 1);
            for (int b = 0; b < 12; b += 6)
                rig.set (bandId (b, bpDynMode), 1); // spectral
            rig.set ("ptOn", 1);
            rig.set ("indOn", 1);
            rig.set ("conOn", 1);
            rig.set ("airOn", 1);
            rig.set ("autoGain", 1);
            rig.prepare (48000.0, 2, 256, false);
            auto buf = dt::zeros (2, 256);
            std::mt19937 g (mode * 7 + os);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            const auto& tab = paramTable();
            // warm-up (first designs), then count every allocation on this thread
            for (int i = 0; i < 50; ++i)
            {
                float* p[2] = { buf[0].data(), buf[1].data() };
                rig.e.process (p, 2, 256);
            }
            gAllocs = 0;
            for (int blk = 0; blk < 600; ++blk)
            {
                if (blk % 5 == 0)
                    for (int k = 0; k < 6; ++k)
                    {
                        const int i = (int) (u (g) * (float) tab.size());
                        const auto& sp = tab[(size_t) i];
                        if (sp.id == "phase" || sp.id == "os" || sp.id == "lpQuality")
                            continue; // mode switches tested below
                        rig.p[(size_t) i] = sp.type == PType::Float ? sp.min + (sp.max - sp.min) * u (g) : std::round (sp.max * u (g));
                    }
                if (blk == 300)
                    rig.p[(size_t) idx().phase] = (float) ((mode + 1) % 3); // a live phase-mode switch
                for (auto& ch : buf)
                    for (auto& v : ch)
                        v = 0.3f * (u (g) - 0.5f);
                tCountAllocs = true;
                rig.e.setParams (rig.p);
                float* p[2] = { buf[0].data(), buf[1].data() };
                rig.e.process (p, 2, 256);
                tCountAllocs = false;
            }
            worst = std::max (worst, gAllocs.load());
            if (gAllocs.load() > 0)
                std::printf ("  allocations in process(): mode %d os %d -> %ld\n", mode, os, gAllocs.load());
        }
    r.equal ("heap allocations inside setParams()+process() (all modes/OS, automation, live mode switch)", (double) worst, 0.0, "");
    for (auto& x : r.results)
        all.push_back (x);
}

//------------------------------------------------------------------------------
void benchmark (std::vector<dt::Result>& all)
{
    struct Bench { const char* name; std::function<void (Rig&)> setup; };
    const std::vector<Bench> benches {
        { "vocal: 7 bands (2 dynamic) + HP, Zero Latency", [] (Rig& r)
          {
              r.set ("hpOn", 1); r.set ("hpFreq", 80);
              r.band (0, kLowShelf, 140, 2.5f, 0.7f, 1); r.band (1, kBell, 450, -3, 1.4f);
              r.band (2, kBell, 2800, 2, 0.8f); r.band (3, kBell, 4600, 2, 0.9f);
              r.band (4, kBell, 8700, 0, 2.5f); r.set ("b5_dyn", 1); r.set ("b5_range", -6);
              r.set ("b2_dyn", 1); r.set ("b2_range", -4);
              r.band (5, kHighShelf, 12000, 2, 0.7f, 1); r.band (6, kBell, 200, 1, 1);
          } },
        { "beat bus: 6 bands + Console + Passive Tube, OS 2x", [] (Rig& r)
          {
              r.band (0, kLowCut, 25, 0, 0.7f, 3); r.band (1, kBell, 60, 2, 1); r.band (2, kBell, 350, -2, 1);
              r.band (3, kBell, 3500, 1.5f, 1); r.band (4, kHighShelf, 10000, 1.5f, 0.7f, 1);
              r.band (5, kLowShelf, 100, 1, 0.7f, 1);
              r.set ("conOn", 1); r.set ("conLfGain", 2); r.set ("ptOn", 1); r.set ("ptLowBoost", 3); r.set ("os", 1);
          } },
        { "master: 8 bands (M/S), Natural Phase + Air", [] (Rig& r)
          {
              r.set ("phase", 1);
              r.band (0, kLowCut, 20, 0, 0.7f, 3); r.band (1, kLowCut, 120, 0, 0.7f, 1, 0, kSide);
              r.band (2, kLowShelf, 80, 0.8f, 0.7f, 1, 0, kMid); r.band (3, kBell, 300, -1, 0.8f);
              r.band (4, kBell, 3000, -1, 1.2f); r.set ("b5_dyn", 1); r.set ("b5_range", -2);
              r.band (5, kHighShelf, 8000, 1, 0.7f, 1, 0, kSide); r.band (6, kBell, 60, 0, 1.5f);
              r.set ("b7_dyn", 1); r.set ("b7_range", -2); r.band (7, kTilt, 1000, 1, 0.7f, 0);
              r.set ("airOn", 1); r.set ("airGain", 2);
          } },
        { "master: same in Linear Phase (Medium)", [] (Rig& r)
          {
              r.set ("phase", 2); r.set ("lpQuality", 1);
              r.band (0, kLowCut, 20, 0, 0.7f, 3); r.band (1, kLowCut, 120, 0, 0.7f, 1, 0, kSide);
              r.band (2, kLowShelf, 80, 0.8f, 0.7f, 1, 0, kMid); r.band (3, kBell, 300, -1, 0.8f);
              r.band (4, kBell, 3000, -1, 1.2f); r.set ("b5_dyn", 1); r.set ("b5_range", -2);
              r.band (5, kHighShelf, 8000, 1, 0.7f, 1, 0, kSide);
          } },
        { "vocal + 2 spectral bands (harshness 3 kHz, sibilance 7 kHz)", [] (Rig& r)
          {
              r.set ("hpOn", 1); r.set ("hpFreq", 80);
              r.band (0, kLowShelf, 140, 2.5f, 0.7f, 1); r.band (1, kBell, 450, -3, 1.4f);
              r.band (2, kBell, 3000, 0, 1.2f); r.set ("b3_dyn", 1); r.set ("b3_dmode", 1); r.set ("b3_thr", -50); r.set ("b3_range", -6);
              r.band (3, kHighShelf, 7000, 0, 0.7f, 1); r.set ("b4_dyn", 1); r.set ("b4_dmode", 1); r.set ("b4_thr", -50); r.set ("b4_range", -8);
              r.band (5, kHighShelf, 12000, 2, 0.7f, 1);
          } },
        { "near Nyquist: 4 fitted bands at 48 kHz (2 dynamic, gain moving)", [] (Rig& r)
          {
              r.band (0, kHighShelf, 19000, 6, 12, 1); r.set ("b1_dyn", 1); r.set ("b1_thr", -50); r.set ("b1_range", -9);
              r.band (1, kBell, 21000, -6, 8); r.set ("b2_dyn", 1); r.set ("b2_thr", -50); r.set ("b2_range", 9);
              r.band (2, kNotch, 25000, 0, 2); r.band (3, kBandPass, 23000, 0, 3, 7);
          } },
        { "worst case: 12 dynamic Character bands + all modules, OS 4x", [] (Rig& r)
          {
              for (int b = 0; b < 12; ++b)
              {
                  r.band (b, kBell, 60.0f * (float) std::pow (1.6, b), 4, 1.5f, 3, 1);
                  r.set (bandId (b, bpDyn), 1);
              }
              r.set ("ptOn", 1); r.set ("indOn", 1); r.set ("conOn", 1); r.set ("airOn", 1); r.set ("os", 2);
          } },
    };
    dt::Recorder rec ({ 48000.0, 2 });
    rec.section = "J. CPU (48 kHz, 128-sample blocks, stereo)";
    for (const auto& b : benches)
    {
        Rig rig;
        b.setup (rig);
        rig.prepare (48000.0, 2, 128, false);
        const int len = 48000 * (quick ? 5 : 30);
        auto buf = dt::zeros (2, len);
        dt::addNoise (buf, 0.3, 1);
        if (std::string (b.name).find ("gain moving") != std::string::npos)
            for (auto& ch : buf) // level steps 8x per second: the dynamic gains move all the time
                for (int i = 0; i < len; ++i)
                    ch[(size_t) i] *= ((i / 6000) % 2 == 0) ? 1.0f : 0.02f;
        const auto t0 = std::chrono::steady_clock::now();
        rig.run (buf, 128);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        rec.info (b.name, 100.0 * sec / (len / 48000.0), "% of one core");
        std::printf ("  CPU %-62s %.2f %%\n", b.name, 100.0 * sec / (len / 48000.0));
    }
    for (auto& x : rec.results)
        all.push_back (x);
}
} // namespace

int main (int argc, char** argv)
{
    std::string report = "tests/report.md";
    bool bench = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--report") == 0 && i + 1 < argc)
            report = argv[++i];
        else if (std::strcmp (argv[i], "--no-bench") == 0)
            bench = false;
        else if (std::strcmp (argv[i], "--quick") == 0)
            quick = true;
        else if (std::strcmp (argv[i], "--only-independent") == 0)
            onlyIndependent = true;
    }
    std::vector<dt::Ctx> configs { { 44100.0, 2 }, { 48000.0, 2 }, { 96000.0, 2 }, { 48000.0, 1 } };
    if (! quick)
    {
        configs.push_back ({ 88200.0, 2 });
        configs.push_back ({ 192000.0, 2 });
    }
    std::vector<dt::Result> all;
    std::mutex mtx;
    std::vector<std::thread> pool;
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& ctx : configs)
        pool.emplace_back ([&, ctx]
        {
            dt::Recorder r (ctx);
            if (onlyIndependent)
            {
                if (ctx.nCh == 2)
                    testIndependent (r);
                std::lock_guard<std::mutex> lk (mtx);
                for (auto& x : r.results)
                    all.push_back (x);
                return;
            }
            const bool full = ctx.nCh == 2 && (ctx.fs == 44100.0 || ctx.fs == 48000.0 || ctx.fs == 96000.0);
            if (full)
            {
                for (int m = 0; m < 3; ++m)
                    testMagnitude (r, m);
                if (! quick)
                    testLinearHigh (r);
            }
            if (ctx.nCh == 2)
                testIndependent (r);
            testNullAndLatency (r);
            testBlockInvariance (r);
            testFuzz (r);
            testClicks (r);
            testDynamics (r);
            testPlacement (r);
            if (ctx.nCh == 2)
            {
                testCharacter (r);
                testMisc (r);
            }
            std::lock_guard<std::mutex> lk (mtx);
            for (auto& x : r.results)
                all.push_back (x);
            std::printf ("config %s done\n", ctx.tag().c_str());
        });
    for (auto& t : pool)
        t.join();
    if (! onlyIndependent)
    {
        testPresets (all);
        testRealtimeSafety (all);
    }
    if (bench && ! onlyIndependent)
        benchmark (all);
    std::vector<std::string> tags;
    for (auto& c : configs)
        tags.push_back (c.tag());
    tags.push_back ("48 k stereo");
    std::sort (tags.begin(), tags.end());
    tags.erase (std::unique (tags.begin(), tags.end()), tags.end());
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    char hdr[256];
    std::snprintf (hdr, sizeof hdr, "Build: %s. Runtime %.0f s.%s", PENTA_TEST_BUILD_INFO, secs, quick ? " QUICK MODE (reduced grid)." : "");
    dt::writeReport (all, tags, report, hdr);
    int fails = 0;
    for (auto& x : all)
        if (! x.pass && ! x.info)
        {
            ++fails;
            std::printf ("FAIL [%s] %s / %s: %s (target %s)\n", x.config.c_str(), x.section.c_str(), x.check.c_str(),
                         dt::fmtNum (x.value).c_str(), x.target.c_str());
        }
    std::printf ("%d checks failed. Report: %s\n", fails, report.c_str());
    return fails;
}
