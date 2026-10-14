#pragma once
// Measurement helpers for the DSP test suite (no JUCE).
#include "../Source/Engine.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace dt
{
using Buf = std::vector<std::vector<float>>;
constexpr double kPi = 3.14159265358979323846;

inline double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-30)); }

struct Rig
{
    penta::Engine e;
    std::vector<float> p = penta::ParamValues().v;
    double fs = 48000.0;
    int nCh = 2;
    int lastChanged = -1;

    void set (const std::string& id, float v)
    {
        const int i = penta::paramIndex (id);
        if (i < 0)
        {
            std::fprintf (stderr, "unknown param %s\n", id.c_str());
            std::abort();
        }
        p[(size_t) i] = v;
        e.setParams (p);
    }
    void band (int b, int type, float freq, float gain, float q, int slopeIdx = 3, int mode = 0, int place = 0)
    {
        using namespace penta;
        set (bandId (b, bpUsed), 1);
        set (bandId (b, bpType), (float) type);
        set (bandId (b, bpFreq), freq);
        set (bandId (b, bpGain), gain);
        set (bandId (b, bpQ), q);
        set (bandId (b, bpSlope), (float) slopeIdx);
        set (bandId (b, bpMode), (float) mode);
        set (bandId (b, bpPlace), (float) place);
    }
    void prepare (double sampleRate, int channels, int block = 512, bool offline = true)
    {
        fs = sampleRate;
        nCh = channels;
        e.setNonRealtime (offline);
        e.setParams (p);
        e.prepare (fs, nCh, block);
        e.setParams (p);
        e.reset();
    }
    // process a whole buffer in blocks
    void run (Buf& b, int block = 512, const Buf* sc = nullptr)
    {
        const int n = (int) b[0].size();
        for (int pos = 0; pos < n; pos += block)
        {
            const int m = std::min (block, n - pos);
            float* ptr[2] = { b[0].data() + pos, b.size() > 1 ? b[1].data() + pos : nullptr };
            const float* sptr[2] = { nullptr, nullptr };
            if (sc != nullptr)
            {
                sptr[0] = (*sc)[0].data() + pos;
                sptr[1] = sc->size() > 1 ? (*sc)[1].data() + pos : sptr[0];
            }
            e.process (ptr, (int) b.size(), m, sc != nullptr ? sptr : nullptr, sc != nullptr ? (int) sc->size() : 0);
        }
    }
};

inline Buf zeros (int nCh, int n) { return Buf ((size_t) nCh, std::vector<float> ((size_t) n, 0.0f)); }

// impulse response of channel `outCh` for an impulse on `inCh`, after `settle` samples of silence
inline std::vector<double> impulse (Rig& r, int len, int settle = 0, int inCh = 0, int outCh = 0, int block = 512)
{
    auto b = zeros (r.nCh, settle + len);
    b[(size_t) inCh][(size_t) settle] = 1.0f;
    r.run (b, block);
    std::vector<double> h ((size_t) len);
    for (int i = 0; i < len; ++i)
        h[(size_t) i] = b[(size_t) outCh][(size_t) (settle + i)];
    return h;
}

inline std::complex<double> dft (const std::vector<double>& h, double w)
{
    // Goertzel-style recurrence would lose precision for long responses; use a rotating phasor
    std::complex<double> acc = 0.0, ph = 1.0;
    const std::complex<double> rot = std::polar (1.0, -w);
    for (size_t i = 0; i < h.size(); ++i)
    {
        if (h[i] != 0.0)
            acc += h[i] * ph;
        ph *= rot;
        if ((i & 1023) == 1023)
            ph /= std::abs (ph);
    }
    return acc;
}

inline std::vector<double> logFreqs (double lo, double hi, int n)
{
    std::vector<double> f ((size_t) n);
    for (int i = 0; i < n; ++i)
        f[(size_t) i] = lo * std::pow (hi / lo, i / (double) (n - 1));
    return f;
}

// deterministic pseudo-random noise / programme material
inline void addNoise (Buf& b, double amp, uint64_t seed)
{
    std::mt19937_64 g (seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    for (auto& ch : b)
        for (auto& v : ch)
            v += (float) (amp * u (g));
}

inline void addSine (Buf& b, double fs, double f, double amp, int from = 0, int to = -1)
{
    for (auto& ch : b)
    {
        const int end = to < 0 ? (int) ch.size() : to;
        for (int i = from; i < end; ++i)
            ch[(size_t) i] += (float) (amp * std::sin (2.0 * kPi * f * i / fs));
    }
}

// amplitude of a sinusoid at f in x[from..from+n)
inline double toneAmp (const std::vector<float>& x, int from, int n, double f, double fs)
{
    std::complex<double> acc = 0.0;
    double ws = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
        acc += w * (double) x[(size_t) (from + i)] * std::polar (1.0, -2.0 * kPi * f * i / fs);
        ws += w;
    }
    return 2.0 * std::abs (acc) / ws;
}

inline double rms (const std::vector<float>& x, int from = 0, int to = -1)
{
    if (to < 0)
        to = (int) x.size();
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / std::max (1, to - from));
}

inline double maxAbs (const std::vector<float>& x, int from = 0, int to = -1)
{
    if (to < 0)
        to = (int) x.size();
    double m = 0.0;
    for (int i = from; i < to; ++i)
        m = std::max (m, (double) std::abs (x[(size_t) i]));
    return m;
}

inline bool allFinite (const Buf& b)
{
    for (auto& ch : b)
        for (float v : ch)
            if (! std::isfinite (v))
                return false;
    return true;
}
} // namespace dt

namespace dt
{
// ITU-R BS.1770 K-weighting at 48 kHz (published coefficients) + pink noise (Kellet filter)
inline double kLoudnessDb (const Buf& b, int from = 0)
{
    double sum = 0.0;
    long n = 0;
    for (const auto& ch : b)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double x = ch[i];
            const double y = 1.53512485958697 * x - 2.69169618940638 * x1 + 1.19839281085285 * x2 + 1.69065929318241 * y1 - 0.73248077421585 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            const double z = y - 2.0 * u1 + u2 + 1.99004745483398 * z1 - 0.99007225036621 * z2;
            u2 = u1; u1 = y; z2 = z1; z1 = z;
            if ((int) i >= from)
            {
                sum += z * z;
                ++n;
            }
        }
    }
    return -0.691 + 10.0 * std::log10 (sum / (double) std::max (1L, n) * (double) b.size() + 1.0e-30);
}

inline void addPink (Buf& b, double rmsDb, uint64_t seed)
{
    std::mt19937_64 g (seed);
    std::normal_distribution<double> nd (0.0, 1.0);
    for (auto& ch : b)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        std::vector<double> tmp (ch.size());
        double ss = 0.0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double w = nd (g);
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            tmp[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            ss += tmp[i] * tmp[i];
        }
        const double k = std::pow (10.0, rmsDb / 20.0) / std::sqrt (ss / (double) ch.size());
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += (float) (tmp[i] * k);
    }
}
} // namespace dt
