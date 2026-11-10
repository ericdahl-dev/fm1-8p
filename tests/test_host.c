/* Host tests: WAV writing, MIDI file reading and rendering. */
#include <stdio.h>
#include <string.h>

#include "../host/render.h"
#include "../host/wav.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* Reads a whole file into a malloc'd buffer; returns its size or 0. */
static size_t slurp(const char *path, uint8_t **data)
{
    FILE *f = fopen(path, "rb");
    long n;
    *data = NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc((size_t)n);
    if (fread(*data, 1, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    return (size_t)n;
}

TT_TEST(wav_header_has_requested_rate_and_length)
{
    static int16_t pcm[2 * 1000];
    uint8_t *d;
    size_t n;
    int i;
    for (i = 0; i < 2000; i++) pcm[i] = (int16_t)(i * 7);
    TT_CHECK_EQ(wav_write_file("build/test-header.wav", pcm, 1000, 22050), 0);
    n = slurp("build/test-header.wav", &d);
    TT_CHECK_EQ(n, 44 + 4000);
    TT_CHECK(memcmp(d, "RIFF", 4) == 0);
    TT_CHECK_EQ(le32(d + 4), n - 8);
    TT_CHECK(memcmp(d + 8, "WAVEfmt ", 8) == 0);
    TT_CHECK_EQ(le32(d + 16), 16);    /* fmt chunk size */
    TT_CHECK_EQ(le16(d + 20), 1);     /* PCM */
    TT_CHECK_EQ(le16(d + 22), 2);     /* stereo */
    TT_CHECK_EQ(le32(d + 24), 22050); /* sample rate */
    TT_CHECK_EQ(le32(d + 28), 22050 * 4);
    TT_CHECK_EQ(le16(d + 32), 4);     /* block align */
    TT_CHECK_EQ(le16(d + 34), 16);    /* bits per sample */
    TT_CHECK(memcmp(d + 36, "data", 4) == 0);
    TT_CHECK_EQ(le32(d + 40), 4000);
    TT_CHECK_EQ((int16_t)le16(d + 44 + 2 * 999), 999 * 7);
    free(d);
}

/*
 * Writes a format-0 Standard MIDI File (480 ticks per quarter, default
 * tempo 120 BPM) holding `body`, a track's events without end-of-track.
 */
static void write_smf(const char *path, const uint8_t *body, size_t n)
{
    static const uint8_t hdr[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                                   0x01, 0xE0, 'M', 'T', 'r', 'k' };
    static const uint8_t eot[] = { 0x00, 0xFF, 0x2F, 0x00 };
    uint32_t len = (uint32_t)(n + sizeof eot);
    uint8_t blen[4];
    FILE *f = fopen(path, "wb");
    blen[0] = (uint8_t)(len >> 24); blen[1] = (uint8_t)(len >> 16);
    blen[2] = (uint8_t)(len >> 8);  blen[3] = (uint8_t)len;
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(blen, 1, 4, f);
    fwrite(body, 1, n, f);
    fwrite(eot, 1, sizeof eot, f);
    fclose(f);
}

/* One A4 (note 69), held for two quarter notes = 1 s at 120 BPM. */
static const uint8_t one_note[] = { 0x00, 0x90, 69, 100, 0x87, 0x40, 0x80, 69, 0 };

/* Reads the PCM frames of a WAV written by the host (44-byte header). */
static int16_t *read_wav_pcm(const char *path, uint32_t *frames, uint32_t *rate)
{
    uint8_t *d;
    size_t n = slurp(path, &d), i;
    int16_t *pcm;
    if (n < 44) { free(d); return NULL; }
    *rate = le32(d + 24);
    *frames = le32(d + 40) / 4;
    pcm = malloc((size_t)*frames * 4);
    for (i = 0; i < (size_t)*frames * 2; i++) pcm[i] = (int16_t)le16(d + 44 + 2 * i);
    free(d);
    return pcm;
}

TT_TEST(one_note_midi_file_renders_at_note_pitch)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 500 };
    uint32_t frames, rate;
    int16_t *pcm;
    double period;
    write_smf("build/one-note.mid", one_note, sizeof one_note);
    TT_CHECK_EQ(render_midi_file("build/one-note.mid", "build/one-note.wav", &o), 0);
    pcm = read_wav_pcm("build/one-note.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    TT_CHECK_EQ(rate, 44100);
    /* 1 s of note plus the 500 ms tail */
    TT_CHECK_EQ(frames, 44100 + 22050);
    period = period_from_crossings(pcm + 2 * 4410, 44100 - 2 * 4410, 0);
    free(pcm);
    TT_CHECK_NEAR(period, 44100 / note_hz(69), 44100 / note_hz(69) * 0.0006);
}

/* Format 1: tempo track at 240 BPM, then a note track (running status). */
static void write_two_track_smf(const char *path)
{
    static const uint8_t data[] = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0x01, 0xE0,
        'M', 'T', 'r', 'k', 0, 0, 0, 11,
        0x00, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90, /* 250000 us/quarter */
        0x00, 0xFF, 0x2F, 0x00,
        'M', 'T', 'r', 'k', 0, 0, 0, 19,
        0x00, 0x90, 60, 90,        /* C4 on */
        0x00, 64, 80,              /* E4 on, running status */
        0x83, 0x60, 60, 0,         /* 480 ticks later: C4 off (vel 0) */
        0x83, 0x60, 64, 0,         /* 480 ticks later: E4 off */
        0x00, 0xFF, 0x2F, 0x00
    };
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, sizeof data, f);
    fclose(f);
}

TT_TEST(render_applies_parameter_settings)
{
    render_opts o;
    uint32_t frames, rate;
    int16_t *pcm;
    int early, later;
    memset(&o, 0, sizeof o);
    o.sample_rate = 44100;
    o.param_count = 2;
    o.param_id[0] = FM_PARAM_AMP_ATTACK_MS;
    o.param_value[0] = 500;
    o.param_id[1] = FM_PARAM_AMP_SUSTAIN;
    o.param_value[1] = 1000;
    write_smf("build/one-note.mid", one_note, sizeof one_note);
    TT_CHECK_EQ(render_midi_file("build/one-note.mid", "build/attack.wav", &o), 0);
    pcm = read_wav_pcm("build/attack.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    early = peak_abs(pcm, 4410 - 150, 4410);   /* ~100 ms */
    later = peak_abs(pcm, 26460 - 150, 26460); /* ~600 ms */
    free(pcm);
    TT_CHECK_NEAR((double)early / later, 0.2, 0.03);
}

TT_TEST(tempo_changes_set_the_song_length)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 250 };
    uint32_t frames, rate;
    int16_t *pcm;
    write_two_track_smf("build/two-track.mid");
    TT_CHECK_EQ(render_midi_file("build/two-track.mid", "build/two-track.wav", &o), 0);
    pcm = read_wav_pcm("build/two-track.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    free(pcm);
    /* 960 ticks at 250 ms per 480 ticks = 0.5 s, plus 0.25 s tail */
    TT_CHECK_EQ(frames, 22050 + 11025);
}

TT_TEST(same_midi_file_renders_byte_identical_wav)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 400 };
    uint8_t *a, *b;
    size_t na, nb;
    write_two_track_smf("build/det.mid");
    TT_CHECK_EQ(render_midi_file("build/det.mid", "build/det-a.wav", &o), 0);
    TT_CHECK_EQ(render_midi_file("build/det.mid", "build/det-b.wav", &o), 0);
    na = slurp("build/det-a.wav", &a);
    nb = slurp("build/det-b.wav", &b);
    TT_CHECK(na > 44);
    TT_CHECK_EQ(na, nb);
    TT_CHECK(memcmp(a, b, na) == 0);
    free(a);
    free(b);
}

TT_TEST(render_rejects_bad_input)
{
    static const uint8_t junk[] = { 'M', 'T', 'h', 'x', 1, 2, 3 };
    render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
    FILE *f = fopen("build/junk.mid", "wb");
    fwrite(junk, 1, sizeof junk, f);
    fclose(f);
    printf("    (expected error messages follow)\n");
    TT_CHECK(render_midi_file("build/junk.mid", "build/junk.wav", &o) != 0);
    TT_CHECK(render_midi_file("build/does-not-exist.mid", "build/x.wav", &o) != 0);
    o.sample_rate = 1000;
    write_smf("build/one-note.mid", one_note, sizeof one_note);
    TT_CHECK(render_midi_file("build/one-note.mid", "build/x.wav", &o) != 0);
}

static void run_host_tests(void)
{
    TT_RUN(wav_header_has_requested_rate_and_length);
    TT_RUN(one_note_midi_file_renders_at_note_pitch);
    TT_RUN(render_applies_parameter_settings);
    TT_RUN(tempo_changes_set_the_song_length);
    TT_RUN(same_midi_file_renders_byte_identical_wav);
    TT_RUN(render_rejects_bad_input);
}
