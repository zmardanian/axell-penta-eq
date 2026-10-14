#pragma once
// Axell Penta EQ - radix-2 FFT (framework independent, allocation only in prepare()).
//
// ComplexFft<T>: in-place iterative radix-2 FFT with precomputed twiddles and bit-reversal table.
// RealFft<T>:    real transform of size N via one complex FFT of size N/2 (standard packing), giving
//                bins 0..N/2. Used by the correction-FIR designer (double) and the linear-phase
//                convolver (float).

#include <cassert>
#include <cmath>
#include <complex>
#include <vector>

namespace penta::dsp
{
template <typename T>
class ComplexFft
{
public:
    using C = std::complex<T>;

    void prepare (int size)
    {
        assert (size >= 2 && (size & (size - 1)) == 0);
        n = size;
        log2n = 0;
        while ((1 << log2n) < n)
            ++log2n;
        tw.resize ((size_t) (n / 2));
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            tw[(size_t) k] = C ((T) std::cos (a), (T) std::sin (a));
        }
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < log2n; ++b)
                if (i & (1 << b))
                    r |= 1 << (log2n - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const noexcept { return n; }

    // forward: X[k] = sum x[n] e^{-j2pi kn/N}; inverse is unscaled (caller divides by N)
    void forward (C* x) const noexcept { run (x, false); }
    void inverse (C* x) const noexcept { run (x, true); }

private:
    void run (C* x, bool inv) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = rev[(size_t) i];
            if (r > i)
                std::swap (x[i], x[r]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    C w = tw[(size_t) (j * step)];
                    if (inv)
                        w = std::conj (w);
                    const C a = x[i + j];
                    const C b = x[i + j + half] * w;
                    x[i + j] = a + b;
                    x[i + j + half] = a - b;
                }
        }
    }

    int n = 0, log2n = 0;
    std::vector<C> tw;
    std::vector<int> rev;
};

template <typename T>
class RealFft
{
public:
    using C = std::complex<T>;

    void prepare (int size)
    {
        assert (size >= 4 && (size & (size - 1)) == 0);
        n = size;
        half.prepare (n / 2);
        buf.assign ((size_t) (n / 2), C());
        w.resize ((size_t) (n / 2));
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            w[(size_t) k] = C ((T) std::cos (a), (T) std::sin (a));
        }
    }

    int size() const noexcept { return n; }
    int numBins() const noexcept { return n / 2 + 1; }

    // in: n real samples, out: n/2 + 1 bins
    void forward (const T* in, C* out) noexcept
    {
        const int h = n / 2;
        for (int i = 0; i < h; ++i)
            buf[(size_t) i] = C (in[2 * i], in[2 * i + 1]);
        half.forward (buf.data());
        out[0] = C (buf[0].real() + buf[0].imag(), 0);
        out[h] = C (buf[0].real() - buf[0].imag(), 0);
        for (int k = 1; k < h; ++k)
        {
            const C a = buf[(size_t) k], b = std::conj (buf[(size_t) (h - k)]);
            const C e = (a + b) * (T) 0.5;
            const C o = (a - b) * C (0, (T) -0.5);
            out[k] = e + w[(size_t) k] * o;
        }
    }

    // in: n/2 + 1 bins (Hermitian spectrum), out: n real samples, scaled by 1/n (true inverse)
    void inverse (const C* in, T* out) noexcept
    {
        const int h = n / 2;
        for (int k = 0; k < h; ++k)
        {
            const C a = in[k], b = std::conj (in[h - k]);
            const C e = (a + b);
            const C o = (a - b) * std::conj (w[(size_t) k]);
            buf[(size_t) k] = (e + C (0, 1) * o) * (T) 0.5;
        }
        half.inverse (buf.data());
        const T s = (T) 1 / (T) h;
        for (int i = 0; i < h; ++i)
        {
            out[2 * i] = buf[(size_t) i].real() * s;
            out[2 * i + 1] = buf[(size_t) i].imag() * s;
        }
    }

private:
    int n = 0;
    ComplexFft<T> half;
    std::vector<C> buf, w;
};
} // namespace penta::dsp
