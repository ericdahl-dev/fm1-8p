/* Host tests: WAV writing, MIDI file reading and rendering. */
#include <stdio.h>
#include <string.h>

#include "../host/args.h"
#include "../host/render.h"
#include "../host/smf.h"
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

/* Parses an in-memory SMF; returns smf_parse's result (song freed). */
static int parse_bytes(const uint8_t *d, size_t n)
{
    smf_song song;
    char err[128];
    int rc = smf_parse(d, n, &song, err, sizeof err);
    if (rc == 0) smf_free(&song);
    return rc;
}

#define SMF_HDR(ntracks) 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, ntracks, 0x01, 0xE0
#define EOT 0x00, 0xFF, 0x2F, 0x00

TT_TEST(smf_accepts_a_well_formed_file)
{
    static const uint8_t ok[] = { SMF_HDR(1), 'M', 'T', 'r', 'k', 0, 0, 0, 12,
                                  0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0, EOT };
    TT_CHECK_EQ(parse_bytes(ok, sizeof ok), 0);
}

TT_TEST(smf_rejects_header_longer_than_file)
{
    static const uint8_t d[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 200, 0, 0, 0, 1,
                                 0x01, 0xE0 };
    TT_CHECK(parse_bytes(d, sizeof d) != 0);
}

TT_TEST(smf_rejects_missing_tracks)
{
    static const uint8_t d[] = { SMF_HDR(2), 'M', 'T', 'r', 'k', 0, 0, 0, 4,
                                 EOT };
    TT_CHECK(parse_bytes(d, sizeof d) != 0);
}

TT_TEST(smf_rejects_status_byte_in_data_position)
{
    /* note on whose velocity byte is 0x90 */
    static const uint8_t d[] = { SMF_HDR(1), 'M', 'T', 'r', 'k', 0, 0, 0, 8,
                                 0x00, 0x90, 60, 0x90, EOT };
    TT_CHECK(parse_bytes(d, sizeof d) != 0);
}

TT_TEST(smf_rejects_tempo_with_wrong_length)
{
    static const uint8_t d[] = { SMF_HDR(1), 'M', 'T', 'r', 'k', 0, 0, 0, 10,
                                 0x00, 0xFF, 0x51, 0x02, 0x07, 0xA1, EOT };
    TT_CHECK(parse_bytes(d, sizeof d) != 0);
}

TT_TEST(wav_rejects_sample_rate_out_of_range)
{
    static int16_t pcm[2 * 4];
    TT_CHECK(wav_write_file("build/bad-rate.wav", pcm, 4, 0) != 0);
    /* 0x40000000 * 4 bytes per frame overflows the 32-bit byte rate */
    TT_CHECK(wav_write_file("build/bad-rate.wav", pcm, 4, 0x40000000u) != 0);
    TT_CHECK_EQ(wav_write_file("build/ok-rate.wav", pcm, 4, 192000), 0);
}

TT_TEST(numeric_options_reject_negative_and_out_of_range_values)
{
    uint32_t u = 7;
    TT_CHECK_EQ(args_parse_u32("44100", &u), 0);
    TT_CHECK_EQ(u, 44100);
    TT_CHECK_EQ(args_parse_u32("4294967295", &u), 0);
    TT_CHECK_EQ(u, 4294967295u);
    TT_CHECK(args_parse_u32("4294967296", &u) != 0);
    TT_CHECK(args_parse_u32("-4294967295", &u) != 0);
    TT_CHECK(args_parse_u32("-1", &u) != 0);
    TT_CHECK(args_parse_u32(" 5", &u) != 0);
    TT_CHECK(args_parse_u32("+5", &u) != 0);
    TT_CHECK(args_parse_u32("", &u) != 0);
    TT_CHECK(args_parse_u32("12x", &u) != 0);
}

TT_TEST(parameter_settings_reject_out_of_range_values)
{
    render_opts o;
    memset(&o, 0, sizeof o);
    TT_CHECK_EQ(args_parse_setting("amp_attack_ms=200", &o), 0);
    TT_CHECK_EQ(o.param_count, 1);
    TT_CHECK_EQ(o.param_id[0], FM_PARAM_AMP_ATTACK_MS);
    TT_CHECK_EQ(o.param_value[0], 200);
    TT_CHECK_EQ(args_parse_setting("amp_sustain_permille=-2147483648", &o), 0);
    TT_CHECK_EQ(o.param_value[1], -2147483647 - 1);
    TT_CHECK(args_parse_setting("amp_attack_ms=4294967296", &o) != 0);
    TT_CHECK(args_parse_setting("amp_attack_ms=99999999999", &o) != 0);
    TT_CHECK(args_parse_setting("amp_attack_ms=2147483648", &o) != 0);
    TT_CHECK(args_parse_setting("amp_attack_ms=", &o) != 0);
    TT_CHECK(args_parse_setting("amp_attack_ms=1.5", &o) != 0);
    TT_CHECK(args_parse_setting("no_such_param=1", &o) != 0);
    TT_CHECK(args_parse_setting("amp_attack_ms", &o) != 0);
    TT_CHECK_EQ(o.param_count, 2);
}

static void run_host_tests(void)
{
    TT_RUN(wav_header_has_requested_rate_and_length);
    TT_RUN(one_note_midi_file_renders_at_note_pitch);
    TT_RUN(render_applies_parameter_settings);
    TT_RUN(tempo_changes_set_the_song_length);
    TT_RUN(same_midi_file_renders_byte_identical_wav);
    TT_RUN(render_rejects_bad_input);
    TT_RUN(smf_accepts_a_well_formed_file);
    TT_RUN(smf_rejects_header_longer_than_file);
    TT_RUN(smf_rejects_missing_tracks);
    TT_RUN(smf_rejects_status_byte_in_data_position);
    TT_RUN(smf_rejects_tempo_with_wrong_length);
    TT_RUN(wav_rejects_sample_rate_out_of_range);
    TT_RUN(numeric_options_reject_negative_and_out_of_range_values);
    TT_RUN(parameter_settings_reject_out_of_range_values);
}
