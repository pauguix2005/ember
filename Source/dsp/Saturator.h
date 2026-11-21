#pragma once

#include <cmath>
#include <algorithm>

namespace ember::dsp
{
/** EMBER's three character curves. All are odd, unity slope at zero, and bounded.
    They are original shapes named by feel, not models of any specific circuit.

    Tube-ish : tanh                       — smooth, rounded knee
    Tape-ish : (2/π)·atan(π/2·x)          — slowest knee, most gradual compression
    Iron-ish : cubic, x − 4/27·x³ (|x|<1.5) — firmer knee, flat ceiling, more 3rd harmonic

    Each comes with its first antiderivative so the blend can use 1st-order ADAA. */
namespace curves
{
    inline double logCosh (double x)
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
    }

    inline double tubeF (double x)    { return std::tanh (x); }
    inline double tubeAD (double x)   { return logCosh (x); }

    inline constexpr double tapeA = 1.57079632679489661923;   // π/2
    inline double tapeF (double x)    { return (1.0 / tapeA) * std::atan (tapeA * x); }
    inline double tapeAD (double x)
    {
        return (1.0 / tapeA) * (x * std::atan (tapeA * x) - std::log1p (tapeA * tapeA * x * x) / (2.0 * tapeA));
    }

    inline double ironF (double x)
    {
        const double a = std::abs (x);
        if (a >= 1.5)
            return std::copysign (1.0, x);
        return x - (4.0 / 27.0) * x * x * x;
    }
    inline double ironAD (double x)
    {
        const double a = std::abs (x);
        if (a >= 1.5)
            return 0.9375 + (a - 1.5);
        const double x2 = x * x;
        return 0.5 * x2 - x2 * x2 / 27.0;
    }
}

/** Weights for the Character control: 0 = Tube, 0.5 = Tape, 1 = Iron. */
struct CurveBlend
{
    double tube = 1, tape = 0, iron = 0;

    static CurveBlend fromCharacter (double c)
    {
        c = std::clamp (c, 0.0, 1.0);
        CurveBlend b;
        if (c <= 0.5) { b.tape = c * 2.0; b.tube = 1.0 - b.tape; b.iron = 0.0; }
        else          { b.iron = (c - 0.5) * 2.0; b.tape = 1.0 - b.iron; b.tube = 0.0; }
        return b;
    }

    inline double f (double x) const
    {
        double y = 0.0;
        if (tube > 0) y += tube * curves::tubeF (x);
        if (tape > 0) y += tape * curves::tapeF (x);
        if (iron > 0) y += iron * curves::ironF (x);
        return y;
    }

    inline double ad (double x) const
    {
        double y = 0.0;
        if (tube > 0) y += tube * curves::tubeAD (x);
        if (tape > 0) y += tape * curves::tapeAD (x);
        if (iron > 0) y += iron * curves::ironAD (x);
        return y;
    }
};

/** First-order antiderivative anti-aliasing (Parker et al., CCRMA/DAFx 2016).
    Adds a half-sample delay, which is negligible next to the oversampling filters. */
struct AdaaSaturator
{
    double xPrev = 0.0, adPrev = 0.0;
    double lastTube = -1, lastTape = -1, lastIron = -1;
    bool primed = false;

    void reset() { xPrev = adPrev = 0.0; primed = false; }

    /** If the blend differs from the previous sample's, the previous antiderivative is
        re-evaluated with the new curve; otherwise the difference quotient would mix two
        curves and spike whenever dx is small (audible as a click when automating). */
    inline double process (double x, const CurveBlend& blend)
    {
        if (primed && (blend.tube < lastTube || blend.tube > lastTube || blend.tape < lastTape || blend.tape > lastTape
                       || blend.iron < lastIron || blend.iron > lastIron))
            adPrev = blend.ad (xPrev);
        lastTube = blend.tube; lastTape = blend.tape; lastIron = blend.iron;
        const double ad = blend.ad (x);
        if (! primed) { primed = true; xPrev = x; adPrev = ad; return blend.f (x); }

        const double dx = x - xPrev;
        double y;
        if (std::abs (dx) < 1.0e-6)
            y = blend.f (0.5 * (x + xPrev));
        else
            y = (ad - adPrev) / dx;

        xPrev = x;
        adPrev = ad;
        return y;
    }
};

/** Output soft clipper: linear up to −3 dBFS, then a tanh knee
    that never exceeds −0.3 dBFS on the sample values. */
struct SoftClipper
{
    static constexpr double t = 0.7071;     // knee start, −3 dBFS
    static constexpr double c = 0.96605;    // ceiling, −0.3 dBFS
    static constexpr double w = c - t;

    static double f (double x)
    {
        const double a = std::abs (x);
        if (a <= t) return x;
        return std::copysign (t + w * std::tanh ((a - t) / w), x);
    }
    static double ad (double x)
    {
        const double a = std::abs (x);
        if (a <= t) return 0.5 * x * x;
        const double d = a - t;
        return 0.5 * t * t + t * d + w * w * curves::logCosh (d / w);
    }

    /** Plain (non-ADAA) on purpose: 1st-order ADAA averages adjacent samples and would
        dull the top end of everything below the knee. The clipper only acts on peaks. */
    inline double process (double x) const { return std::clamp (f (x), -c, c); }
};

} // namespace ember::dsp
