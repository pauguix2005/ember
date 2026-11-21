#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

namespace ember::dsp
{
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double sqrt2 = 1.41421356237309504880;

/** Topology-preserving-transform state variable filter (Zavalishin), Butterworth damping.
    Runs in double: the crossover and low shelves sit at 20–250 Hz, where float
    coefficients lose precision at high sample rates. */
struct SvfCoeffs
{
    double g = 0.0, h = 0.0, r2 = sqrt2;

    void set (double freq, double sampleRate, double q = 1.0 / sqrt2)
    {
        freq = std::clamp (freq, 1.0, sampleRate * 0.49);
        g  = std::tan (pi * freq / sampleRate);
        r2 = 1.0 / q;
        h  = 1.0 / (1.0 + r2 * g + g * g);
    }
};

struct SvfState
{
    double s1 = 0.0, s2 = 0.0;
    void reset() { s1 = s2 = 0.0; }

    /** Returns low, band and high outputs. */
    inline void tick (const SvfCoeffs& c, double x, double& lp, double& bp, double& hp)
    {
        hp = (x - (c.r2 + c.g) * s1 - s2) * c.h;
        const double v1 = c.g * hp;
        bp = v1 + s1;
        s1 = bp + v1;
        const double v2 = c.g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
    }
};

/** 4th-order Linkwitz-Riley split (two cascaded Butterworth stages per band).
    LP + HP sums to the 2nd-order Butterworth all-pass (flat magnitude). */
struct LinkwitzRiley4
{
    SvfState lpA, lpB, hpA, hpB;

    void reset() { lpA.reset(); lpB.reset(); hpA.reset(); hpB.reset(); }

    inline void process (const SvfCoeffs& c, double x, double& low, double& high)
    {
        double l, b, h, l2, b2, h2;
        lpA.tick (c, x, l, b, h);
        lpB.tick (c, l, l2, b2, h2);
        low = l2;
        hpA.tick (c, x, l, b, h);
        hpB.tick (c, h, l2, b2, h2);
        high = h2;
    }

    inline double highOnly (const SvfCoeffs& c, double x)
    {
        double l, b, h, l2, b2, h2;
        hpA.tick (c, x, l, b, h);
        hpB.tick (c, h, l2, b2, h2);
        return h2;
    }
};

/** All-pass matching an LR4 crossover sum: AP = x - 2·r2·bp of one Butterworth SVF. */
struct AllPass2
{
    SvfState s;
    void reset() { s.reset(); }
    inline double process (const SvfCoeffs& c, double x)
    {
        double l, b, h;
        s.tick (c, x, l, b, h);
        return x - 2.0 * c.r2 * b;
    }
};

/** Low shelf on a TPT SVF (Simper). Unlike a direct-form biquad it stays clean
    when its coefficients are modulated, so WEIGHT can be automated freely. */
struct ShelfCoeffs
{
    double g = 0, k = 1, h = 1, m1 = 0, m2 = 0;

    static ShelfCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        ShelfCoeffs c;
        const double A = std::pow (10.0, gainDb / 40.0);
        c.g = std::tan (pi * std::clamp (f, 5.0, fs * 0.45) / fs) / std::sqrt (A);
        c.k = 1.0 / q;
        c.h = 1.0 / (1.0 + c.g * (c.g + c.k));
        c.m1 = c.k * (A - 1.0);
        c.m2 = A * A - 1.0;
        return c;
    }

    static ShelfCoeffs lerp (const ShelfCoeffs& a, const ShelfCoeffs& b, double t)
    {
        ShelfCoeffs c;
        c.g = a.g + (b.g - a.g) * t;
        c.k = a.k + (b.k - a.k) * t;
        c.h = 1.0 / (1.0 + c.g * (c.g + c.k));
        c.m1 = a.m1 + (b.m1 - a.m1) * t;
        c.m2 = a.m2 + (b.m2 - a.m2) * t;
        return c;
    }
};

struct ShelfSvf
{
    double s1 = 0, s2 = 0;
    void reset() { s1 = s2 = 0; }

    inline double process (const ShelfCoeffs& c, double x)
    {
        const double v3 = x - s2;
        const double v1 = c.h * (s1 + c.g * v3);
        const double v2 = s2 + c.g * v1;
        s1 = 2.0 * v1 - s1;
        s2 = 2.0 * v2 - s2;
        return x + c.m1 * v1 + c.m2 * v2;
    }
};

/** Transposed direct form II biquad in double. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** RBJ cookbook low shelf. */
    void setLowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * std::clamp (f, 5.0, fs * 0.45) / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sa;
        b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
    }

    void setHighPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * pi * std::clamp (f, 5.0, fs * 0.45) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1 + alpha;
        b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }

    void setLowPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * pi * std::clamp (f, 5.0, fs * 0.45) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1 + alpha;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }

    /** Magnitude response at frequency f (for drawing curves). */
    double magnitude (double fs, double f) const
    {
        const double w = 2.0 * pi * f / fs;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

/** First-order DC blocker / high-pass (one pole, one zero). */
struct DcBlocker
{
    double r = 0.9995, x1 = 0, y1 = 0;
    void setCutoff (double fc, double fs) { r = std::exp (-2.0 * pi * fc / fs); }
    void reset() { x1 = y1 = 0; }
    inline double process (double x)
    {
        const double y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }
};

/** Integer delay line with a preallocated ring buffer. */
struct DelayLine
{
    std::vector<float> buf;
    int writePos = 0, delay = 0, mask = 0;

    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 2)
            size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }
    void setDelay (int d) { delay = std::clamp (d, 0, mask - 1); }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); writePos = 0; }

    inline float process (float x)
    {
        buf[(size_t) writePos] = x;
        const float y = buf[(size_t) ((writePos - delay) & mask)];
        writePos = (writePos + 1) & mask;
        return y;
    }
};

/** Exact comparison without -Wfloat-equal noise (used for change detection, not tolerance). */
inline bool same (double a, double b) { return ! (a < b) && ! (b < a); }

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)  { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

/** One-pole smoothing coefficient for a time constant in seconds. */
inline double onePoleCoeff (double seconds, double fs)
{
    return seconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * fs));
}

} // namespace ember::dsp
