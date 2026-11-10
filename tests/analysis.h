/*
 * analysis.h - signal measurement helpers for the tests.
 *
 * Test code only: it may use double and libm. The engine never may.
 */
#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define AN_PI 3.14159265358979323846

/* MIDI note number to frequency in Hz (A4 = note 69 = 440 Hz). */
static double note_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/*
 * Mean period in samples, from upward zero crossings (linearly
 * interpolated) of one channel of an interleaved stereo buffer. A saw
 * rises through zero once per cycle, so this is its fundamental period.
 * Returns 0 if fewer than two crossings are found.
 */
static double period_from_crossings(const int16_t *stereo, size_t frames,
                                    int channel)
{
    double first = -1, last = -1;
    int count = 0;
    size_t i;
    for (i = 1; i < frames; i++) {
        int a = stereo[(i - 1) * 2 + channel];
        int b = stereo[i * 2 + channel];
        if (a < 0 && b >= 0) {
            double t = (double)(i - 1) + (double)(-a) / (double)(b - a);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    }
    if (count < 2) return 0;
    return (last - first) / (count - 1);
}

/* Peak absolute sample value over a range of frames, both channels. */
static int peak_abs(const int16_t *stereo, size_t from, size_t to)
{
    int peak = 0;
    size_t i;
    for (i = from * 2; i < to * 2; i++) {
        int v = stereo[i] < 0 ? -stereo[i] : stereo[i];
        if (v > peak) peak = v;
    }
    return peak;
}

/* In-place iterative radix-2 complex FFT; n must be a power of two. */
static void fft(double *re, double *im, size_t n)
{
    size_t i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * AN_PI / (double)len;
        size_t k;
        for (i = 0; i < n; i += len) {
            for (k = 0; k < len / 2; k++) {
                double wr = cos(ang * (double)k), wi = sin(ang * (double)k);
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
        }
    }
}

/*
 * Aliasing-to-total energy ratio in dB for a periodic tone with
 * fundamental f0, measured on one channel of n frames (n a power of two).
 * Applies a Blackman-Harris window, then counts as "harmonic" every bin
 * within `guard` bins of an exact multiple of f0 below Nyquist and every
 * bin near DC; all remaining energy is aliasing (or noise).
 */
static double aliasing_db(const int16_t *stereo, size_t n, int channel,
                          double f0, double rate, int guard)
{
    double *re = calloc(n, sizeof *re), *im = calloc(n, sizeof *im);
    double total = 0, alias = 0, bin_hz = rate / (double)n;
    size_t i;
    for (i = 0; i < n; i++) {
        double x = 2.0 * AN_PI * (double)i / (double)(n - 1);
        double w = 0.35875 - 0.48829 * cos(x) + 0.14128 * cos(2 * x)
                 - 0.01168 * cos(3 * x);
        re[i] = w * stereo[i * 2 + channel];
    }
    fft(re, im, n);
    for (i = 1; i < n / 2; i++) {
        double p = re[i] * re[i] + im[i] * im[i];
        double f = (double)i * bin_hz;
        double h = floor(f / f0 + 0.5) * f0;
        double dist = fabs(f - h) / bin_hz;
        total += p;
        if (h > 0 && dist > guard) alias += p;
    }
    free(re);
    free(im);
    if (alias <= 0) return -400;
    return 10.0 * log10(alias / total);
}

/*
 * Amplitude of the component at `hz` in n frames of one channel, from a
 * Blackman-Harris windowed single-bin DFT, scaled so a sine of peak A
 * reads about A. Its main lobe is +-4 bins (rate / n each), so tones
 * further apart than that are measured separately.
 */
static double tone_amp(const int16_t *stereo, size_t n, int channel,
                       double hz, double rate)
{
    double re = 0, im = 0, wsum = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        double x = 2.0 * AN_PI * (double)i / (double)(n - 1);
        double w = 0.35875 - 0.48829 * cos(x) + 0.14128 * cos(2 * x)
                 - 0.01168 * cos(3 * x);
        double ph = 2.0 * AN_PI * hz * (double)i / rate;
        double v = w * stereo[i * 2 + channel];
        re += v * cos(ph);
        im -= v * sin(ph);
        wsum += w;
    }
    return 2.0 * sqrt(re * re + im * im) / wsum;
}

#endif /* ANALYSIS_H */
