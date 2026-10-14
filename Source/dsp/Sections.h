#pragma once
// Axell Penta EQ - analog prototype sections, matched digital design, TPT-SVF realisation.
//
// Every filter in the plugin is a cascade of 1st/2nd-order analog sections
//     H(s) = (n0 s^2 + n1 s + n2) / (d0 s^2 + d1 s + d2)      (first order: n0 = d0 = 0)
// with s in radians per sample of the rate the section runs at. A section may be flagged
// `invert` (H -> 1/H): cuts of bells and shelves are the exact reciprocal of the boost.
//
// Digital realisation ("matched", no bilinear cramping): poles by impulse invariance (z = e^s),
// numerator fitted to the analog magnitude (Vicanek 2016 closed forms for peak / LP2 / HP2 / BP2,
// a 3-point DC / wm / Nyquist fit for the rest, 2-point for first order, exact unit-circle zeros for
// notches, matched-Z as the always-valid fallback). Remaining deviation from the analog prototype
// is removed by the correction FIR (Correction.h), see docs/research/dsp-literature.md.
//
// Each digital biquad is mapped onto a TPT state-variable filter (Zavalishin / Simper) with output
// mixing; SVF coefficients interpolate linearly per sample without stability issues, which is what
// makes automation and dynamic bands click-free.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace penta::dsp
{
constexpr double kPi = 3.14159265358979323846;

// exact comparison without -Wfloat-equal noise (used only where exactness is intended)
template <typename T>
constexpr bool same (T a, T b) noexcept { return ! (a < b) && ! (b < a); }

enum class Kind { unity, generic, peak, lp2, hp2, bp2, notch, first };

struct Section
{
    double n0 = 0, n1 = 0, n2 = 1, d0 = 0, d1 = 0, d2 = 1;
    Kind kind = Kind::unity;
    bool invert = false;
    double w0 = 0; // characteristic frequency (rad/sample) used by the fits
    // top of the audio band (20 kHz) in rad/sample of the section's rate; > 0 enables the near-Nyquist
    // refinement of designMatched (set by buildBand; 0 = classic matched design only)
    double wTop = 0;

    bool isFirstOrder() const noexcept { return same (d0, 0.0); }

    // |H(jw)|^2 of the analog prototype
    double magSq (double w) const noexcept
    {
        const double w2 = w * w;
        const double nr = n2 - n0 * w2, ni = n1 * w;
        const double dr = d2 - d0 * w2, di = d1 * w;
        const double num = nr * nr + ni * ni, den = dr * dr + di * di;
        if (invert)
            return den / std::max (num, 1.0e-300);
        return num / std::max (den, 1.0e-300);
    }

    std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> num (n2 - n0 * w * w, n1 * w), den (d2 - d0 * w * w, d1 * w);
        return invert ? den / num : num / den;
    }
};

//==============================================================================
// prototype builders (frequencies in rad/sample, gains in dB)
namespace proto
{
inline Section unity() { return {}; }

// symmetric (reciprocal) bell: (s^2 + s A/Q + 1) / (s^2 + s/(AQ) + 1), A = 10^(|g|/40)
inline Section bell (double w0, double q, double gainDb)
{
    Section s;
    const double A = std::pow (10.0, std::abs (gainDb) / 40.0);
    s.n0 = 1.0 / (w0 * w0); s.n1 = A / (q * w0); s.n2 = 1.0;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (A * q * w0); s.d2 = 1.0;
    s.kind = Kind::peak;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    return s;
}

// constant-pole (parallel) bell: 1 + (g-1) BP = (s^2 + s g/Q + 1)/(s^2 + s/Q + 1); cuts narrower
inline Section parallelBell (double w0, double q, double gainDb)
{
    Section s;
    const double g = std::pow (10.0, gainDb / 20.0);
    s.n0 = 1.0 / (w0 * w0); s.n1 = g / (q * w0); s.n2 = 1.0;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (q * w0); s.d2 = 1.0;
    s.kind = Kind::peak;
    s.w0 = w0;
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    return s;
}

// RBJ-style 2nd-order low shelf, gain at DC, half gain at w0
inline Section lowShelf2 (double w0, double q, double gainDb)
{
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    Section s;
    const double A = std::pow (10.0, std::abs (gainDb) / 40.0), sa = std::sqrt (A);
    const double i2 = 1.0 / (w0 * w0), i1 = 1.0 / w0;
    // A (s^2 + (sqrtA/Q) s + A) / (A s^2 + (sqrtA/Q) s + 1)
    s.n0 = A * i2; s.n1 = A * sa / q * i1; s.n2 = A * A;
    s.d0 = A * i2; s.d1 = sa / q * i1; s.d2 = 1.0;
    s.kind = Kind::generic;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    return s;
}

inline Section highShelf2 (double w0, double q, double gainDb)
{
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    Section s;
    const double A = std::pow (10.0, std::abs (gainDb) / 40.0), sa = std::sqrt (A);
    const double i2 = 1.0 / (w0 * w0), i1 = 1.0 / w0;
    // A (A s^2 + (sqrtA/Q) s + 1) / (s^2 + (sqrtA/Q) s + A)
    s.n0 = A * A * i2; s.n1 = A * sa / q * i1; s.n2 = A;
    s.d0 = i2; s.d1 = sa / q * i1; s.d2 = A;
    s.kind = Kind::generic;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    return s;
}

// 1st-order shelves with geometric midpoint at w0
inline Section lowShelf1 (double w0, double gainDb)
{
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    Section s;
    const double T = std::pow (10.0, std::abs (gainDb) / 20.0), st = std::sqrt (T);
    // (s + w0 sqrtT) / (s + w0 / sqrtT)
    s.n1 = 1.0; s.n2 = w0 * st;
    s.d1 = 1.0; s.d2 = w0 / st;
    s.kind = Kind::first;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    return s;
}

inline Section highShelf1 (double w0, double gainDb)
{
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    Section s;
    const double T = std::pow (10.0, std::abs (gainDb) / 20.0), st = std::sqrt (T);
    // T (s + w0/sqrtT) / (s + w0 sqrtT)
    s.n1 = T; s.n2 = T * w0 / st;
    s.d1 = 1.0; s.d2 = w0 * st;
    s.kind = Kind::first;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    return s;
}

// tilt: -g/2 below, +g/2 above, 0 dB at w0 (first order)
inline Section tilt1 (double w0, double gainDb)
{
    if (std::abs (gainDb) < 1.0e-9)
        return unity();
    Section s;
    const double T = std::pow (10.0, std::abs (gainDb) / 20.0), st = std::sqrt (T);
    // sqrtT (s + w0/sqrtT) / (s + w0 sqrtT)
    s.n1 = st; s.n2 = w0;
    s.d1 = 1.0; s.d2 = w0 * st;
    s.kind = Kind::first;
    s.invert = gainDb < 0.0;
    s.w0 = w0;
    return s;
}

// tilt (2nd order, with Q): RBJ high shelf scaled by 1/A
inline Section tilt2 (double w0, double q, double gainDb)
{
    auto s = highShelf2 (w0, q, std::abs (gainDb));
    if (s.kind == Kind::unity)
        return s;
    const double A = std::pow (10.0, std::abs (gainDb) / 40.0);
    s.n0 /= A; s.n1 /= A; s.n2 /= A;
    s.invert = gainDb < 0.0;
    return s;
}

inline Section lowpass2 (double w0, double q)
{
    Section s;
    s.n0 = 0; s.n1 = 0; s.n2 = 1;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (q * w0); s.d2 = 1.0;
    s.kind = Kind::lp2;
    s.w0 = w0;
    return s;
}

inline Section highpass2 (double w0, double q)
{
    Section s;
    s.n0 = 1.0 / (w0 * w0); s.n1 = 0; s.n2 = 0;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (q * w0); s.d2 = 1.0;
    s.kind = Kind::hp2;
    s.w0 = w0;
    return s;
}

inline Section lowpass1 (double w0)
{
    Section s;
    s.n1 = 0; s.n2 = 1; s.d1 = 1.0 / w0; s.d2 = 1.0;
    s.kind = Kind::first;
    s.w0 = w0;
    return s;
}

inline Section highpass1 (double w0)
{
    Section s;
    s.n1 = 1.0 / w0; s.n2 = 0; s.d1 = 1.0 / w0; s.d2 = 1.0;
    s.kind = Kind::first;
    s.w0 = w0;
    return s;
}

// unity-peak band-pass
inline Section bandpass2 (double w0, double q)
{
    Section s;
    s.n0 = 0; s.n1 = 1.0 / (q * w0); s.n2 = 0;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (q * w0); s.d2 = 1.0;
    s.kind = Kind::bp2;
    s.w0 = w0;
    return s;
}

inline Section notch (double w0, double q)
{
    Section s;
    s.n0 = 1.0 / (w0 * w0); s.n1 = 0; s.n2 = 1.0;
    s.d0 = 1.0 / (w0 * w0); s.d1 = 1.0 / (q * w0); s.d2 = 1.0;
    s.kind = Kind::notch;
    s.w0 = w0;
    return s;
}

// Butterworth section Q for pole pair k (0-based, ascending Q) of an order-n filter. The pairs sit at
// (2k+1)pi/(2n) from the negative real axis for even n, at (k+1)pi/n for odd n (whose real pole is
// the first-order section) - e.g. 3rd order: one pair with Q = 1.
inline double butterQ (int order, int k)
{
    const double theta = (order % 2 == 0) ? (2.0 * k + 1.0) * kPi / (2.0 * order) : (k + 1.0) * kPi / order;
    return 1.0 / (2.0 * std::cos (theta));
}
} // namespace proto

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    // |H|^2 from precomputed phi values (Vicanek eq. 25/26) - cheap inner loop of the correction designer
    double magSq (double p0, double p1, double p2) const noexcept
    {
        const double B0 = (b0 + b1 + b2) * (b0 + b1 + b2), B1 = (b0 - b1 + b2) * (b0 - b1 + b2), B2 = -4.0 * b0 * b2;
        const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4.0 * a2;
        return (B0 * p0 + B1 * p1 + B2 * p2) / std::max (A0 * p0 + A1 * p1 + A2 * p2, 1.0e-300);
    }

    bool isStable() const noexcept
    {
        return std::isfinite (a1) && std::isfinite (a2) && std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
    }
    bool isMinimumPhase() const noexcept
    {
        // zeros inside the unit circle (b0 > 0 normalisation)
        return std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2) && b0 > 0.0 && b0 > std::abs (b2) * (1.0 + 1.0e-12)
            && b0 + b2 > std::abs (b1) * (1.0 + 1.0e-12);
    }
    bool isFinite() const noexcept
    {
        return std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2) && std::isfinite (a1) && std::isfinite (a2);
    }
};

namespace matched_detail
{
inline void phis (double w, double& p0, double& p1, double& p2) noexcept
{
    const double s = std::sin (0.5 * w);
    p1 = s * s;
    p0 = 1.0 - p1;
    p2 = 4.0 * p0 * p1;
}

// impulse-invariant poles of d0 s^2 + d1 s + d2
inline void poles (const Section& s, double& a1, double& a2) noexcept
{
    if (s.isFirstOrder())
    {
        const double r = -s.d2 / s.d1;
        a1 = -std::exp (r);
        a2 = 0.0;
        return;
    }
    const double disc = s.d1 * s.d1 - 4.0 * s.d0 * s.d2;
    if (disc < 0.0)
    {
        const double re = -s.d1 / (2.0 * s.d0);
        // keep the pole angle below Nyquist: impulse invariance would alias it otherwise
        const double im = std::min (std::sqrt (-disc) / (2.0 * s.d0), 0.95 * kPi);
        a1 = -2.0 * std::exp (re) * std::cos (im);
        a2 = std::exp (2.0 * re);
    }
    else
    {
        const double sq = std::sqrt (disc);
        const double r1 = (-s.d1 + sq) / (2.0 * s.d0), r2 = (-s.d1 - sq) / (2.0 * s.d0);
        a1 = -(std::exp (r1) + std::exp (r2));
        a2 = std::exp (r1 + r2);
    }
}

inline double denomSq (double a1, double a2, double p0, double p1, double p2) noexcept
{
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4.0 * a2;
    return A0 * p0 + A1 * p1 + A2 * p2;
}

// minimum-phase numerator from B0..B2 (Vicanek eq. 29), trying both Nyquist signs
inline bool factor (double B0, double B1, double B2, Biquad& q) noexcept
{
    if (! (B0 >= -1.0e-15) || ! (B1 >= -1.0e-15) || ! std::isfinite (B2))
        return false;
    const double r0 = std::sqrt (std::max (0.0, B0)), r1 = std::sqrt (std::max (0.0, B1));
    for (int sgn = 1; sgn >= -1; sgn -= 2)
    {
        const double W = 0.5 * (r0 + sgn * r1);
        const double d = W * W + B2;
        if (d < 0.0 || W <= 0.0)
            continue;
        const double b0 = 0.5 * (W + std::sqrt (d));
        if (b0 <= 0.0)
            continue;
        q.b0 = b0;
        q.b1 = 0.5 * (r0 - sgn * r1);
        q.b2 = -B2 / (4.0 * b0);
        return true;
    }
    return false;
}

inline bool fitGeneric (const Section& s, Biquad& q, double wm) noexcept
{
    double p0, p1, p2;
    const double D0 = (1 + q.a1 + q.a2) * (1 + q.a1 + q.a2), D1 = (1 - q.a1 + q.a2) * (1 - q.a1 + q.a2);
    const double B0 = s.magSq (0.0) * D0;
    const double B1 = s.magSq (kPi) * D1;
    phis (wm, p0, p1, p2);
    const double B2 = (s.magSq (wm) * denomSq (q.a1, q.a2, p0, p1, p2) - B0 * p0 - B1 * p1) / p2;
    return factor (B0, B1, B2, q);
}

inline bool fitPeak (const Section& s, Biquad& q) noexcept
{
    const double w0 = s.w0;
    double p0, p1, p2;
    phis (w0, p0, p1, p2);
    const double A0 = (1 + q.a1 + q.a2) * (1 + q.a1 + q.a2), A1 = (1 - q.a1 + q.a2) * (1 - q.a1 + q.a2), A2 = -4.0 * q.a2;
    const double G2 = s.magSq (w0);
    const double B0 = A0 * s.magSq (0.0);
    const double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * G2;
    const double R2 = (-A0 + A1 + 4.0 * (p0 - p1) * A2) * G2;
    const double B2 = (R1 - R2 * p1 - B0) / (4.0 * p1 * p1);
    const double B1 = R2 + B0 + 4.0 * (p1 - p0) * B2;
    return factor (B0, B1, B2, q);
}

inline bool fitLp2 (const Section& s, Biquad& q) noexcept
{
    double p0, p1, p2;
    phis (s.w0, p0, p1, p2);
    const double A0 = (1 + q.a1 + q.a2) * (1 + q.a1 + q.a2);
    const double B0 = A0 * s.magSq (0.0);
    const double R1 = denomSq (q.a1, q.a2, p0, p1, p2) * s.magSq (s.w0);
    const double B1 = (R1 - B0 * p0) / p1;
    if (! (B1 >= 0.0))
        return false;
    q.b0 = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
    q.b1 = std::sqrt (B0) - q.b0;
    q.b2 = 0.0;
    return std::isfinite (q.b0) && q.b0 > 0.0;
}

inline bool fitHp2 (const Section& s, Biquad& q) noexcept
{
    double p0, p1, p2;
    phis (s.w0, p0, p1, p2);
    q.b0 = std::sqrt (denomSq (q.a1, q.a2, p0, p1, p2) * s.magSq (s.w0)) / (4.0 * p1);
    q.b1 = -2.0 * q.b0;
    q.b2 = q.b0;
    return std::isfinite (q.b0) && q.b0 > 0.0;
}

inline bool fitBp2 (const Section& s, Biquad& q) noexcept
{
    double p0, p1, p2;
    phis (s.w0, p0, p1, p2);
    const double A0 = (1 + q.a1 + q.a2) * (1 + q.a1 + q.a2), A1 = (1 - q.a1 + q.a2) * (1 - q.a1 + q.a2), A2 = -4.0 * q.a2;
    const double G2 = s.magSq (s.w0);
    const double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * G2;
    const double R2 = (-A0 + A1 + 4.0 * (p0 - p1) * A2) * G2;
    const double B2 = (R1 - R2 * p1) / (4.0 * p1 * p1);
    const double B1 = R2 + 4.0 * (p1 - p0) * B2;
    if (! (B1 >= 0.0) || ! (B2 + 0.25 * B1 >= 0.0))
        return false;
    q.b1 = -0.5 * std::sqrt (B1);
    q.b0 = 0.5 * (std::sqrt (B2 + q.b1 * q.b1) - q.b1);
    q.b2 = -q.b0 - q.b1;
    return std::isfinite (q.b0) && q.b0 > 0.0;
}

inline bool fitFirst (const Section& s, Biquad& q) noexcept
{
    const double p = -q.a1;
    const double h0 = std::sqrt (s.magSq (0.0)), hn = std::sqrt (s.magSq (kPi));
    const double sum = h0 * (1.0 - p), diff = hn * (1.0 + p);
    q.b0 = 0.5 * (sum + diff);
    q.b1 = 0.5 * (sum - diff);
    q.b2 = 0.0;
    return std::isfinite (q.b0) && q.b0 > 0.0;
}

// matched-Z: zeros mapped like the poles, gain matched where the analog response is largest
inline void fitMzt (const Section& s, Biquad& q) noexcept
{
    double c0 = 1, c1 = 0, c2 = 0; // numerator polynomial in z^-1
    if (! same (s.n0, 0.0))
    {
        const double disc = s.n1 * s.n1 - 4.0 * s.n0 * s.n2;
        if (disc < 0.0)
        {
            const double re = -s.n1 / (2.0 * s.n0), im = std::min (std::sqrt (-disc) / (2.0 * s.n0), kPi);
            c1 = -2.0 * std::exp (re) * std::cos (im);
            c2 = std::exp (2.0 * re);
        }
        else
        {
            const double sq = std::sqrt (disc);
            const double r1 = (-s.n1 + sq) / (2.0 * s.n0), r2 = (-s.n1 - sq) / (2.0 * s.n0);
            c1 = -(std::exp (r1) + std::exp (r2));
            c2 = std::exp (r1 + r2);
        }
    }
    else if (! same (s.n1, 0.0))
        c1 = -std::exp (-s.n2 / s.n1);
    double bestW = 0.0, best = -1.0;
    for (double w : { 0.0, std::min (s.w0, 0.9 * kPi), 0.5 * kPi, kPi })
    {
        const double m = s.magSq (w);
        if (m > best)
        {
            best = m;
            bestW = w;
        }
    }
    Biquad t = q;
    t.b0 = c0; t.b1 = c1; t.b2 = c2;
    const double mag = std::abs (t.response (bestW));
    const double g = mag > 0.0 ? std::sqrt (best) / mag : 1.0;
    q.b0 = c0 * g; q.b1 = c1 * g; q.b2 = c2 * g;
}

// natural frequency (rad/sample) of c0 s^2 + c1 s + c2 (first order / constant: "none")
inline double naturalFreq (double c0, double c2) noexcept
{
    return c0 > 0.0 && c2 > 0.0 ? std::sqrt (c2 / c0) : -1.0;
}

// cheap fit score: worst |dB| deviation at the frequencies that decide a 2nd-order shape - the pole
// and zero natural frequencies, w0, their geometric mean and an octave outside - below 0.85 pi
// (closer to Nyquist neither fit is exact and the correction FIR removes the broad rest)
inline double keyError (const Section& s, const Biquad& q) noexcept
{
    const double wp = naturalFreq (s.d0, s.d2), wz = naturalFreq (s.n0, s.n2);
    const double pts[] = { wp, wz, s.w0, wp > 0.0 && wz > 0.0 ? std::sqrt (wp * wz) : -1.0,
                           0.5 * std::min (wp, wz), 2.0 * std::max (wp, wz), 0.25 * kPi };
    double worst = 0.0;
    for (double w : pts)
    {
        if (! (w > 1.0e-4 && w <= 0.85 * kPi))
            continue;
        double p0, p1, p2;
        phis (w, p0, p1, p2);
        const double e = std::abs (10.0 * std::log10 (std::max (s.magSq (w), 1.0e-300) / std::max (q.magSq (p0, p1, p2), 1.0e-300)));
        worst = std::max (worst, e);
    }
    return worst;
}
} // namespace matched_detail

// Matched digital biquad for an analog section (handles `invert`).
inline Biquad designMatched (const Section& in)
{
    using namespace matched_detail;
    Biquad q;
    if (in.kind == Kind::unity)
        return q;
    Section s = in;
    s.invert = false; // design the boost, invert afterwards
    poles (s, q.a1, q.a2);

    const bool high = s.w0 > 0.9 * kPi;
    // Below ~0.025 rad/sample (190 Hz at 48 kHz) the closed-form peak fit loses precision
    // (sin^2(w0/2)^2 ~ 1e-11 cancels); far below Nyquist the matched-Z mapping is exact enough.
    const bool low = s.w0 < 0.025;
    bool ok = false;
    switch (s.kind)
    {
        case Kind::peak:  ok = ! high && ! low && fitPeak (s, q); break;
        case Kind::lp2:   ok = ! high && fitLp2 (s, q); break;
        case Kind::hp2:   ok = ! high && fitHp2 (s, q); break;
        case Kind::bp2:   ok = ! high && fitBp2 (s, q); break;
        case Kind::first: ok = fitFirst (s, q); break;
        case Kind::notch:
        {
            const double w = std::min (s.w0, 0.98 * kPi);
            const double c = -2.0 * std::cos (w);
            const double g = (1.0 + q.a1 + q.a2) / (2.0 + c);
            q.b0 = g; q.b1 = c * g; q.b2 = g;
            ok = true;
            break;
        }
        case Kind::unity:
        case Kind::generic:
        default: break;
    }
    if (! ok && s.kind != Kind::first && ! (low && (s.kind == Kind::peak || s.kind == Kind::generic)))
        ok = fitGeneric (s, q, std::clamp (s.w0, 1.0e-6, 0.75 * kPi));
    const bool needMinPhase = in.invert;
    if (! ok || ! q.isFinite() || (needMinPhase && ! q.isMinimumPhase()))
        fitMzt (s, q);
    else if (s.kind == Kind::generic)
    {
        // the 3-point fit misses resonant shelf / tilt shapes (Q >~ 2: the dip and peak lie between
        // its fit points, far beyond what the correction FIR can repair); matched-Z keeps the pole and
        // zero positions exactly - take whichever is closer at the frequencies that matter
        Biquad m = q;
        fitMzt (s, m);
        if (m.isFinite() && (! needMinPhase || m.isMinimumPhase()) && keyError (s, m) < keyError (s, q))
            q = m;
    }

    if (in.invert)
    {
        // H -> 1/H : swap numerator and denominator, renormalise a0 = 1
        const double b0 = q.b0;
        Biquad r;
        r.b0 = 1.0 / b0; r.b1 = q.a1 / b0; r.b2 = q.a2 / b0;
        r.a1 = q.b1 / b0; r.a2 = q.b2 / b0;
        q = r;
    }
    if (! q.isStable() || ! q.isFinite())
        q = Biquad(); // never happens for valid prototypes; a flat section is the safe fallback
    return q;
}

//==============================================================================
// Near-Nyquist realisation (DECISIONS D25).
// designMatched ties the poles to impulse invariance (clamped at 0.95 pi) and fits the numerator at a
// few points. Close to Nyquist - resonances, corners or centres near or above it, at 44.1/48 kHz
// without oversampling - no single biquad of that kind follows the analog curve: resonances beyond
// Nyquist fold into the band, and the miss can exceed what the correction FIR repairs. Such "hard"
// sections are realised as two cascaded biquads (4th order) whose poles, zeros and gain are fitted
// to the analog magnitude across the audio band (0 .. wTop = 20 kHz) by Levenberg-Marquardt in
// log magnitude. Between 20 kHz and Nyquist the response may stay below the analog curve but not
// rise more than 3 dB above it. The parametrisation keeps both stages stable (pole radius <= 0.999)
// and, for cuts (inverted designs), minimum phase - by construction, not by checks.
// A fresh design runs a few starts (cold); a changing section (automation, dynamic gain) continues
// from its previous fit (FitTrack) with a few iterations, so redesigns every control block stay cheap.
struct Realisation
{
    std::array<Biquad, 2> q {};
    bool two = false; // q[1] is in use

    std::complex<double> response (double w) const noexcept { return two ? q[0].response (w) * q[1].response (w) : q[0].response (w); }
    double magSq (double p0, double p1, double p2) const noexcept
    {
        return two ? q[0].magSq (p0, p1, p2) * q[1].magSq (p0, p1, p2) : q[0].magSq (p0, p1, p2);
    }
};

struct FitTrack
{
    bool active = false;    // the section is currently realised by the fit
    bool warm = false;      // x holds a fit of this section (also after it stopped being hard)
    int num0 = -1, calls = 0;
    int skip = 0;           // redesigns left before a classic section is checked again (dynamic bands)
    bool restarted = false; // the last realise() did not continue smoothly (cold fit, re-entry)
    double coldErr = 0.0;   // error of the last cold fit: tracking may not drift far beyond it
    std::array<double, 9> x {};
};

namespace fit_detail
{
using matched_detail::phis;
using matched_detail::naturalFreq;

constexpr double kHardOn = 0.5, kHardOff = 0.25; // dB grid error of the classic design (hysteresis)
constexpr double kCeilingDb = 3.0;                // allowed excess over the analog curve above 20 kHz
constexpr int kColdIter1 = 30, kColdIter2 = 40;

struct Grid
{
    static constexpr int kMax = 80;
    int n = 0;
    std::array<double, kMax> p0 {}, p1 {}, p2 {}, t {}, wt {};
    std::array<double, kMax> fixedLn {}; // ln |structural zeros|^2 of stage 0 (see Shape::num0)
    std::array<bool, kMax> ceiling {};   // above the audio band: only exceeding the analog curve counts
    double side = 1.0; // +1: the bound is a ceiling; -1: a floor (the fit is inverted afterwards: a cut)

    void add (const Section& s, double w, double weight, bool isCeiling = false) noexcept
    {
        if (n >= kMax || ! (w >= 0.0) || w > kPi)
            return;
        const double a = s.magSq (w);
        if (! (a > 1.0e-15)) // on a zero of the analog curve (DC of a high-pass, a notch): no information
            return;
        const auto i = (size_t) n++;
        phis (w, p0[i], p1[i], p2[i]);
        t[i] = 10.0 * std::log10 (a);
        ceiling[i] = isCeiling;
        // deep regions (notch floor, far skirts) matter less: the spec counts >= -24 dB
        wt[i] = isCeiling ? weight : weight * std::clamp ((t[i] + 60.0) / 30.0, 0.05, 1.0);
    }
};

inline double sectionQ (double c0, double c1, double c2) noexcept
{
    return c1 > 0.0 && c0 > 0.0 && c2 > 0.0 ? std::sqrt (c0 * c2) / c1 : 0.0;
}

// highest characteristic frequency of a section (pole / zero natural frequencies, corner, w0)
inline double topFreq (const Section& s) noexcept
{
    double w = s.w0;
    if (s.isFirstOrder())
    {
        if (s.d1 > 0.0) w = std::max (w, s.d2 / s.d1);
        if (s.n1 > 0.0) w = std::max (w, s.n2 / s.n1);
        return w;
    }
    return std::max ({ w, naturalFreq (s.d0, s.d2), naturalFreq (s.n0, s.n2) });
}

// s: the boost to be fitted; inverted: the realisation will be its reciprocal (then the bound above
// 20 kHz is a floor for the boost, i.e. a ceiling for the cut that is actually heard)
inline void makeGrid (const Section& s, Grid& g, bool inverted = false) noexcept
{
    const double top = s.wTop;
    g.n = 0;
    g.side = inverted ? -1.0 : 1.0;
    g.add (s, 0.0, 1.0);
    constexpr int nl = 30;
    const double lo = 0.004 * kPi;
    for (int i = 0; i < nl; ++i)
        g.add (s, lo * std::pow (top / lo, i / (double) (nl - 1)), 1.0);
    // the top of the band densely: a second stage may otherwise hide a narrow feature between points
    for (int i = 1; i <= 16; ++i)
        g.add (s, top * (0.6 + 0.025 * i), 1.0);
    // between 20 kHz and Nyquist the curve is free below the analog one but must not rise above it -
    // otherwise a pole runs to z = -1 to imitate a resonance beyond Nyquist (+100 dB at Nyquist)
    for (int i = 1; i <= 8; ++i)
        g.add (s, top + 0.125 * i * (kPi - top), 1.0, true);
    if (! s.isFirstOrder())
    {
        const double wp = naturalFreq (s.d0, s.d2), wz = naturalFreq (s.n0, s.n2);
        const double qp = sectionQ (s.d0, s.d1, s.d2), qz = sectionQ (s.n0, s.n1, s.n2);
        for (const auto& [wc, qc] : { std::pair { wp, qp }, std::pair { wz, qz } })
            if (wc > 0.0 && wc < top && qc > 0.7)
                for (int k = -3; k <= 3; ++k)
                    g.add (s, wc * (1.0 + k * 0.25 / qc), 1.0);
    }
}

// K stages; zeros within radius rz (0.999 when the design is inverted: they become poles), poles
// within rp = 0.999 (the SVF realisation needs 1 +- a1 + a2 clearly > 0).
// Stage 0 keeps the section's structural zeros exactly: num0 = 0 free pair, 1 = a zero at DC times a
// free real zero (band-pass, 1st-order high-pass), 2 = the classic numerator fixed (2nd-order
// high-pass: double zero at DC; notch: zeros on the unit circle at its centre).
struct Shape
{
    int K = 1, num0 = 0;
    double rz = 1.0, rp = 0.999;
    double f1 = 0.0, f2 = 0.0; // num0 == 2: fixed numerator 1 + f1 z^-1 + f2 z^-2
    int numParams (int stage) const noexcept { return stage == 0 ? (num0 == 0 ? 2 : (num0 == 1 ? 1 : 0)) : 2; }
    int offset (int stage) const noexcept { return stage == 0 ? 1 : 1 + numParams (0) + 2; }
    int size() const noexcept { return 1 + numParams (0) + 2 + (K == 2 ? 4 : 0); }
};

inline double numMagSq (double c1, double c2, double P0, double P1, double P2) noexcept
{
    return (1.0 + c1 + c2) * (1.0 + c1 + c2) * P0 + (1.0 - c1 + c2) * (1.0 - c1 + c2) * P1 - 4.0 * c2 * P2;
}

// x = { ln gain, stage 0: [zero params] u1 u2, stage 1: v1 v2 u1 u2 };
// c2 = R^2 tanh v2, c1 = R tanh v1 (1 + tanh v2) spans all polynomials with roots inside radius R
inline void rootsToParams (double c1, double c2, double R, double& v1, double& v2) noexcept
{
    constexpr double lim = 0.9995; // a start on the circle would freeze the fit (tanh saturates)
    const double t2 = std::clamp (c2 / (R * R), -lim, lim), t1 = std::clamp (c1 / (R * (1.0 + t2)), -lim, lim);
    v1 = std::atanh (t1);
    v2 = std::atanh (t2);
}

// numerator coefficients (1, c1, c2) of a stage from its parameters
inline void stageNum (const double* x, const Shape& f, int stage, double& c1, double& c2) noexcept
{
    const double* y = x + f.offset (stage);
    const int m = f.numParams (stage);
    if (m == 2)
    {
        const double tz = std::tanh (y[1]);
        c1 = f.rz * std::tanh (y[0]) * (1.0 + tz);
        c2 = f.rz * f.rz * tz;
    }
    else if (m == 1)
    {
        const double c = f.rz * std::tanh (y[0]); // (1 - z^-1)(1 + c z^-1)
        c1 = c - 1.0;
        c2 = -c;
    }
    else
    {
        c1 = f.f1;
        c2 = f.f2;
    }
}

inline void stageFromBiquad (const Biquad& q, const Shape& f, int stage, double* x) noexcept
{
    const double b0 = std::max (q.b0, 1.0e-300);
    double* y = x + f.offset (stage);
    const int m = f.numParams (stage);
    if (m == 2)
        rootsToParams (q.b1 / b0, q.b2 / b0, f.rz, y[0], y[1]);
    else if (m == 1)
        y[0] = std::atanh (std::clamp (-(q.b2 / b0) / f.rz, -0.9995, 0.9995)); // c2 = -c
    rootsToParams (q.a1, q.a2, f.rp, y[m], y[m + 1]);
}

inline Biquad stageToBiquad (const double* x, const Shape& f, int stage, double gain) noexcept
{
    double c1, c2;
    stageNum (x, f, stage, c1, c2);
    const double* u = x + f.offset (stage) + f.numParams (stage);
    const double tp = std::tanh (u[1]);
    Biquad q;
    q.b0 = gain;
    q.b1 = gain * c1;
    q.b2 = gain * c2;
    q.a1 = f.rp * std::tanh (u[0]) * (1.0 + tp);
    q.a2 = f.rp * f.rp * tp;
    return q;
}

// a stage-1 with zeros on its poles: exactly flat, and its SVF form is the identity output mix
inline void cancellingStage (double angle, double radius, const Shape& f, double* x) noexcept
{
    const double c1 = -2.0 * radius * std::cos (angle), c2 = radius * radius;
    double* y = x + f.offset (1);
    rootsToParams (c1, c2, f.rz, y[0], y[1]);
    rootsToParams (c1, c2, f.rp, y[2], y[3]);
}

// weighted dB residuals r (and Jacobian J, row-major n columns); returns the sum of squares
inline double residuals (const double* x, const Grid& g, const Shape& f, double* r, double* J) noexcept
{
    constexpr double k = 4.3429448190325182; // 10 / ln 10
    const int n = f.size();
    struct St
    {
        double c1 = 0, c2 = 0, a1 = 0, a2 = 0;
        double dc1[2] {}, dc2[2] {}; // d c / d (zero params)
        double da1u1 = 0, da1u2 = 0, da2u2 = 0;
        int m = 0, off = 0;
    };
    std::array<St, 2> st {};
    for (int s = 0; s < f.K; ++s)
    {
        auto& e = st[(size_t) s];
        e.m = f.numParams (s);
        e.off = f.offset (s);
        const double* y = x + e.off;
        stageNum (x, f, s, e.c1, e.c2);
        if (e.m == 2)
        {
            const double T1 = std::tanh (y[0]), T2 = std::tanh (y[1]);
            e.dc1[0] = f.rz * (1.0 - T1 * T1) * (1.0 + T2);
            e.dc1[1] = f.rz * T1 * (1.0 - T2 * T2);
            e.dc2[1] = f.rz * f.rz * (1.0 - T2 * T2);
        }
        else if (e.m == 1)
        {
            const double T = std::tanh (y[0]), dc = f.rz * (1.0 - T * T);
            e.dc1[0] = dc;
            e.dc2[0] = -dc;
        }
        const double S1 = std::tanh (y[e.m]), S2 = std::tanh (y[e.m + 1]);
        e.a2 = f.rp * f.rp * S2;
        e.a1 = f.rp * S1 * (1.0 + S2);
        e.da1u1 = f.rp * (1.0 - S1 * S1) * (1.0 + S2);
        e.da1u2 = f.rp * S1 * (1.0 - S2 * S2);
        e.da2u2 = f.rp * f.rp * (1.0 - S2 * S2);
    }
    double sse = 0.0;
    for (int i = 0; i < g.n; ++i)
    {
        const auto u = (size_t) i;
        const double P0 = g.p0[u], P1 = g.p1[u], P2 = g.p2[u];
        double num = 1.0, den = 1.0;
        double* Ji = J != nullptr ? J + i * n : nullptr;
        if (Ji != nullptr)
            std::fill (Ji, Ji + n, 0.0);
        for (int s = 0; s < f.K; ++s)
        {
            const auto& e = st[(size_t) s];
            double N;
            if (s == 0 && f.num0 == 2)
                N = std::max (numMagSq (e.c1, e.c2, P0, P1, P2), 1.0e-300);
            else if (s == 0 && f.num0 == 1)
            {
                const double c = -e.c2, Nf = (1.0 + c) * (1.0 + c) * P0 + (1.0 - c) * (1.0 - c) * P1;
                N = std::max (4.0 * P1 * Nf, 1.0e-300);
                if (Ji != nullptr)
                    Ji[e.off] = (2.0 * (1.0 + c) * P0 - 2.0 * (1.0 - c) * P1) / std::max (Nf, 1.0e-300) * e.dc1[0];
            }
            else
            {
                const double np = 1.0 + e.c1 + e.c2, nm = 1.0 - e.c1 + e.c2;
                N = std::max (np * np * P0 + nm * nm * P1 - 4.0 * e.c2 * P2, 1.0e-300);
                if (Ji != nullptr)
                {
                    const double dN1 = (2.0 * np * P0 - 2.0 * nm * P1) / N, dN2 = (2.0 * np * P0 + 2.0 * nm * P1 - 4.0 * P2) / N;
                    Ji[e.off] = dN1 * e.dc1[0];
                    Ji[e.off + 1] = dN1 * e.dc1[1] + dN2 * e.dc2[1];
                }
            }
            const double dp = 1.0 + e.a1 + e.a2, dm = 1.0 - e.a1 + e.a2;
            const double D = std::max (dp * dp * P0 + dm * dm * P1 - 4.0 * e.a2 * P2, 1.0e-300);
            num *= N;
            den *= D;
            if (Ji != nullptr)
            {
                const double dD1 = (2.0 * dp * P0 - 2.0 * dm * P1) / D, dD2 = (2.0 * dp * P0 + 2.0 * dm * P1 - 4.0 * P2) / D;
                Ji[e.off + e.m] = -dD1 * e.da1u1;
                Ji[e.off + e.m + 1] = -(dD1 * e.da1u2 + dD2 * e.da2u2);
            }
        }
        const double L = 2.0 * x[0] + std::log (std::max (num / den, 1.0e-300));
        const double err = k * L - g.t[u] - (g.ceiling[u] ? g.side * kCeilingDb : 0.0);
        const bool off = g.ceiling[u] && g.side * err <= 0.0;
        const double w = off ? 0.0 : g.wt[u];
        r[i] = w * err;
        sse += r[i] * r[i];
        if (Ji != nullptr)
        {
            Ji[0] = 2.0;
            for (int c = 0; c < n; ++c)
                Ji[c] *= w * k;
        }
    }
    return sse;
}

// Gaussian elimination with partial pivoting, n <= 9
inline bool solve (double (&A)[9][9], double (&b)[9], int n) noexcept
{
    for (int c = 0; c < n; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs (A[r][c]) > std::abs (A[piv][c]))
                piv = r;
        if (! (std::abs (A[piv][c]) > 1.0e-300))
            return false;
        if (piv != c)
        {
            for (int k = 0; k < n; ++k)
                std::swap (A[c][k], A[piv][k]);
            std::swap (b[c], b[piv]);
        }
        for (int r = c + 1; r < n; ++r)
        {
            const double m = A[r][c] / A[c][c];
            for (int k = c; k < n; ++k)
                A[r][k] -= m * A[c][k];
            b[r] -= m * b[c];
        }
    }
    for (int c = n - 1; c >= 0; --c)
    {
        double v = b[c];
        for (int k = c + 1; k < n; ++k)
            v -= A[c][k] * b[k];
        b[c] = v / A[c][c];
    }
    return true;
}

// Levenberg-Marquardt on x (size f.size()); returns the final sum of squares
inline double lm (double* x, const Grid& g, const Shape& f, int maxIter) noexcept
{
    const int n = f.size();
    double r[Grid::kMax], rt[Grid::kMax], J[Grid::kMax * 9];
    double sse = residuals (x, g, f, r, J), lambda = 1.0e-3;
    for (int it = 0; it < maxIter && sse > 1.0e-12; ++it)
    {
        double A[9][9] {}, b[9] {};
        for (int i = 0; i < g.n; ++i)
        {
            const double* Ji = J + i * n;
            for (int a = 0; a < n; ++a)
            {
                for (int c = a; c < n; ++c)
                    A[a][c] += Ji[a] * Ji[c];
                b[a] -= Ji[a] * r[i];
            }
        }
        for (int a = 0; a < n; ++a)
            for (int c = 0; c < a; ++c)
                A[a][c] = A[c][a];
        bool improved = false;
        for (int tries = 0; tries < 6 && ! improved; ++tries)
        {
            double Am[9][9], bm[9];
            for (int a = 0; a < n; ++a)
            {
                for (int c = 0; c < n; ++c)
                    Am[a][c] = A[a][c];
                Am[a][a] += lambda * A[a][a] + 1.0e-12;
                bm[a] = b[a];
            }
            if (! solve (Am, bm, n))
            {
                lambda *= 10.0;
                continue;
            }
            double xt[9];
            for (int a = 0; a < n; ++a)
                xt[a] = x[a] + bm[a];
            const double s2 = residuals (xt, g, f, rt, nullptr);
            if (std::isfinite (s2) && s2 < sse)
            {
                std::copy (xt, xt + n, x);
                sse = residuals (x, g, f, r, J);
                lambda = std::max (lambda * 0.3, 1.0e-9);
                improved = true;
            }
            else
                lambda *= 10.0;
        }
        if (! improved)
            break;
    }
    return sse;
}

// one damped Gauss-Newton step without a trial evaluation (tracking a target that moved a little)
inline void gaussNewtonStep (double* x, const Grid& g, const Shape& f) noexcept
{
    const int n = f.size();
    double r[Grid::kMax], J[Grid::kMax * 9];
    residuals (x, g, f, r, J);
    double A[9][9] {}, b[9] {};
    for (int i = 0; i < g.n; ++i)
    {
        const double* Ji = J + i * n;
        for (int a = 0; a < n; ++a)
        {
            for (int c = a; c < n; ++c)
                A[a][c] += Ji[a] * Ji[c];
            b[a] -= Ji[a] * r[i];
        }
    }
    for (int a = 0; a < n; ++a)
    {
        for (int c = 0; c < a; ++c)
            A[a][c] = A[c][a];
        A[a][a] *= 1.0 + 1.0e-3;
        A[a][a] += 1.0e-12;
    }
    if (solve (A, b, n))
        for (int a = 0; a < n; ++a)
            if (std::isfinite (b[a]))
                x[a] += b[a];
}

// worst weighted |dB| error on the grid (the same measure the fit minimises, ceiling included)
inline double gridError (const Realisation& q, const Grid& g) noexcept
{
    double worst = 0.0;
    for (int i = 0; i < g.n; ++i)
    {
        const auto u = (size_t) i;
        const double d = 10.0 * std::log10 (std::max (q.magSq (g.p0[u], g.p1[u], g.p2[u]), 1.0e-30)) - g.t[u];
        worst = std::max (worst, g.wt[u] * (g.ceiling[u] ? std::max (0.0, g.side * d - kCeilingDb) : std::abs (d)));
    }
    return worst;
}

// dense check of a candidate (cold path only): worst |dB| error in the audio band where the analog
// curve is >= -40 dB, and worst excess beyond the 3 dB bound above it (for a cut: on the reciprocal)
inline double denseError (const Section& s, const Realisation& q, bool inverted) noexcept
{
    double worst = 0.0;
    const double lo = 0.002 * kPi, top = s.wTop;
    for (int i = 0; i <= 160; ++i)
    {
        const double w = i < 120 ? lo * std::pow (top / lo, i / 119.0) : top + (i - 119) / 41.0 * (kPi - top);
        double p0, p1, p2;
        phis (w, p0, p1, p2);
        const double t = 10.0 * std::log10 (std::max (s.magSq (w), 1.0e-30));
        const double d = 10.0 * std::log10 (std::max (q.magSq (p0, p1, p2), 1.0e-30)) - t;
        if (i < 120)
        {
            if (t >= -40.0)
                worst = std::max (worst, std::abs (d));
        }
        else
            worst = std::max (worst, std::max (0.0, (inverted ? -d : d) - kCeilingDb));
    }
    return worst;
}

inline Realisation fromParams (const double* x, const Shape& f) noexcept
{
    Realisation r;
    r.q[0] = stageToBiquad (x, f, 0, std::exp (x[0]));
    r.two = f.K == 2;
    if (r.two)
        r.q[1] = stageToBiquad (x, f, 1, 1.0);
    return r;
}

// structural zeros of a section's digital form (see Shape::num0)
inline int structuralZeros (const Section& s) noexcept
{
    if (s.kind == Kind::hp2)
        return 2;
    if (s.kind == Kind::notch)
        return s.w0 < 0.98 * kPi ? 2 : 0;
    if (s.kind == Kind::bp2)
        return 1;
    if (s.kind == Kind::first)
        return same (s.n2, 0.0) ? 1 : 0; // 1st-order high-pass: zero at DC
    return 0;
}

inline bool usable (const Biquad& q) noexcept
{
    constexpr double eps = 1.0e-7;
    return q.isFinite() && q.isStable() && q.b0 > 0.0 && 1.0 + q.a1 + q.a2 > eps && 1.0 - q.a1 + q.a2 > eps;
}

// bilinear transform prewarped at wc: always stable, the in-band shape right but compressed towards
// Nyquist - a start in the right basin when the analog poles or zeros lie far above Nyquist
inline Biquad bilinear (const Section& s, double wc) noexcept
{
    const double K = wc / std::tan (0.5 * wc), K2 = K * K;
    auto map = [K, K2] (double a, double b, double c, double* o)
    {
        o[0] = a * K2 + b * K + c;
        o[1] = 2.0 * (c - a * K2);
        o[2] = a * K2 - b * K + c;
    };
    double n[3], d[3];
    map (s.n0, s.n1, s.n2, n);
    map (s.d0, s.d1, s.d2, d);
    Biquad q;
    q.b0 = n[0] / d[0]; q.b1 = n[1] / d[0]; q.b2 = n[2] / d[0];
    q.a1 = d[1] / d[0]; q.a2 = d[2] / d[0];
    if (q.b0 < 0.0)
    {
        q.b0 = -q.b0; q.b1 = -q.b1; q.b2 = -q.b2;
    }
    return q;
}

inline Biquad inverted (const Biquad& q) noexcept
{
    Biquad r;
    r.b0 = 1.0 / q.b0; r.b1 = q.a1 / q.b0; r.b2 = q.a2 / q.b0;
    r.a1 = q.b1 / q.b0; r.a2 = q.b2 / q.b0;
    return r;
}
} // namespace fit_detail

// The realisation of an analog section: the matched biquad, or for a hard near-Nyquist section the
// fitted 4th-order pair. `track` (optional) carries the fit from one redesign to the next.
inline Realisation realise (const Section& in, FitTrack* track = nullptr)
{
    using namespace fit_detail;
    Realisation r;
    r.q[0] = designMatched (in);
    const bool was = track != nullptr && track->active;
    if (track != nullptr)
    {
        track->active = false; // set again below while the section stays hard
        track->restarted = false;
        // a section found classic stays classic for the next few redesigns: a dynamic band changes
        // little per control block, and the check costs more than the classic design
        if (! was && track->skip > 0)
        {
            --track->skip;
            return r;
        }
    }
    if (in.kind == Kind::unity || ! (in.wTop > 0.0))
        return r;
    Section s = in;
    s.invert = false; // fit the boost; a cut is its exact reciprocal
    if (topFreq (s) < 0.2 * kPi)
        return r;
    Realisation classic;
    classic.q[0] = in.invert ? designMatched (s) : r.q[0];
    Grid g;
    makeGrid (s, g, in.invert);
    // while tracking, the full checks (still hard? hidden features?) run every 8th redesign
    const bool check = ! was || track->calls % 8 == 0;
    if (check && gridError (classic, g) <= (was ? kHardOff : kHardOn))
    {
        if (track != nullptr)
            track->skip = 31;
        return r;
    }

    Shape f2;
    f2.K = 2;
    f2.rz = in.invert ? 0.999 : 1.0;
    f2.num0 = structuralZeros (s);
    if (f2.num0 == 2)
    {
        f2.f1 = classic.q[0].b1 / classic.q[0].b0;
        f2.f2 = classic.q[0].b2 / classic.q[0].b0;
    }
    std::array<double, 9> best {};
    double bestErr = 1.0e300;
    bool dense = true;
    auto consider = [&] (const std::array<double, 9>& x)
    {
        const auto c = fromParams (x.data(), f2);
        if (! usable (c.q[0]) || ! usable (c.q[1]))
            return;
        if (in.invert && (! usable (inverted (c.q[0])) || ! usable (inverted (c.q[1]))))
            return;
        const double e = dense ? std::max (gridError (c, g), denseError (s, c, in.invert)) : gridError (c, g);
        if (e < bestErr)
        {
            bestErr = e;
            best = x;
        }
    };
    bool cold = true;
    if (track != nullptr && track->warm && track->num0 == f2.num0)
    {
        // continue the previous fit (automation / dynamic gain move the target a little per block;
        // a section that becomes hard again starts from where it left)
        auto x = track->x;
        if (was)
            gaussNewtonStep (x.data(), g, f2);
        else
            lm (x.data(), g, f2, 8);
        dense = check;
        consider (x);
        dense = true;
        if (bestErr <= std::max (0.25, 2.0 * track->coldErr))
            cold = false;
        else
            bestErr = 1.0e300; // lost track: refit from scratch
    }
    if (cold)
    {
        // cold: the best 2nd-order fit from the classic and the matched-Z design, then a second stage
        // starting as an exact pole/zero cancellation (flat) near Nyquist and in the upper band
        Shape f1 = f2;
        f1.K = 1;
        Biquad mz = classic.q[0];
        matched_detail::fitMzt (s, mz);
        // bilinear starts prewarped at the lowest in-band feature and near the top of the band
        double wf = 0.9 * s.wTop;
        for (double w : { s.w0, naturalFreq (s.d0, s.d2), naturalFreq (s.n0, s.n2) })
            if (w > 0.0 && w < wf)
                wf = w;
        const Biquad bl1 = bilinear (s, wf), bl2 = bilinear (s, 0.9 * s.wTop);
        std::array<double, 9> s1 {};
        double s1Err = 1.0e300;
        for (const Biquad* start : std::array<const Biquad*, 4> { &classic.q[0], &mz, &bl1, &bl2 })
        {
            if (! start->isFinite() || ! start->isStable() || ! (start->b0 > 0.0))
                continue;
            std::array<double, 9> x {};
            x[0] = std::log (start->b0);
            stageFromBiquad (*start, f1, 0, x.data());
            lm (x.data(), g, f1, kColdIter1);
            Realisation c = fromParams (x.data(), f1);
            const double e = gridError (c, g);
            if (usable (c.q[0]) && e < s1Err)
            {
                s1Err = e;
                s1 = x;
            }
        }
        if (s1Err >= 1.0e300)
            return r;
        for (double angle : { 0.97 * kPi, 0.75 * kPi })
        {
            auto x = s1; // stage-0 layout is the same for K = 1 and 2
            cancellingStage (angle, 0.85, f2, x.data());
            consider (x); // the 2nd-order fit itself (flat second stage)
            lm (x.data(), g, f2, kColdIter2);
            consider (x);
        }
    }
    if (bestErr >= 1.0e300 || (cold && ! (bestErr < std::max (gridError (classic, g), denseError (s, classic, in.invert)))))
        return r;
    Realisation out = fromParams (best.data(), f2);
    if (in.invert)
        out.q = { inverted (out.q[0]), inverted (out.q[1]) };
    if (track != nullptr)
    {
        track->active = track->warm = true;
        track->restarted = cold || ! was;
        track->num0 = f2.num0;
        track->calls = was ? track->calls + 1 : 1;
        if (cold)
            track->coldErr = bestErr;
        track->x = best;
    }
    return out;
}

//==============================================================================
// TPT state-variable realisation of a biquad. The interpolated quantities are g, k (always > 0)
// and the output mix; a1..a3 are derived per sample, so every intermediate filter is a valid,
// stable SVF (interpolating a1..a3 directly would not be).
struct SvfCoefs
{
    double g = 1, k = 2, m0 = 1, m1 = 0, m2 = 0;
};

inline SvfCoefs toSvf (const Biquad& q) noexcept
{
    const double dz = 1.0 + q.a1 + q.a2, dn = 1.0 - q.a1 + q.a2;
    SvfCoefs c;
    if (! (dz > 0.0) || ! (dn > 0.0))
        return c;
    const double g = std::sqrt (dz / dn);
    const double k = 2.0 * (1.0 - q.a2) / (dn * g);
    const double N2 = q.b0 - q.b1 + q.b2, N1 = 2.0 * (q.b0 - q.b2), N0 = q.b0 + q.b1 + q.b2;
    c.g = g;
    c.k = k;
    c.m0 = N2 / dn;
    c.m1 = N1 * g / dz - c.m0 * k;
    c.m2 = N0 / dz - c.m0;
    return c;
}

struct SvfState
{
    double ic1 = 0, ic2 = 0;
    void reset() noexcept { ic1 = ic2 = 0; }
};

inline double svfTick (const SvfCoefs& c, SvfState& s, double x) noexcept
{
    const double a1 = 1.0 / (1.0 + c.g * (c.g + c.k)), a2 = c.g * a1, a3 = c.g * a2;
    const double v3 = x - s.ic2;
    const double v1 = a1 * s.ic1 + a2 * v3;
    const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return c.m0 * x + c.m1 * v1 + c.m2 * v2;
}

// linear interpolation helper over a control block
struct SvfRamp
{
    SvfCoefs cur, inc, tgt;
    // start a control block: land exactly on the previous target, then ramp to t over `steps` samples
    void begin (const SvfCoefs& t, int steps) noexcept
    {
        cur = tgt;
        tgt = t;
        const double r = 1.0 / std::max (1, steps);
        inc.g = (t.g - cur.g) * r; inc.k = (t.k - cur.k) * r;
        inc.m0 = (t.m0 - cur.m0) * r; inc.m1 = (t.m1 - cur.m1) * r; inc.m2 = (t.m2 - cur.m2) * r;
    }
    void jump (const SvfCoefs& t) noexcept
    {
        cur = tgt = t;
        inc = SvfCoefs { 0, 0, 0, 0, 0 };
    }
    void step() noexcept
    {
        cur.g += inc.g; cur.k += inc.k;
        cur.m0 += inc.m0; cur.m1 += inc.m1; cur.m2 += inc.m2;
    }
};
} // namespace penta::dsp
