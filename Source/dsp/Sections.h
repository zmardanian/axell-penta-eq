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
