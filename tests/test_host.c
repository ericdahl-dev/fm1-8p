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

/*
 * Reads a whole file into a malloc'd buffer. Returns its size, or 0 with
 * *data NULL if the file is missing, empty or unreadable.
 */
static size_t slurp(const char *path, uint8_t **data)
{
    FILE *f = fopen(path, "rb");
    long n = -1;
    *data = NULL;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
        *data = malloc((size_t)n);
        if (*data && fread(*data, 1, (size_t)n, f) != (size_t)n) {
            free(*data);
            *data = NULL;
        }
    }
    fclose(f);
    return *data ? (size_t)n : 0;
}

TT_TEST(slurp_fails_cleanly_on_missing_and_empty_files)
{
    uint8_t *d = (uint8_t *)1;
    FILE *f = fopen("build/empty.bin", "wb");
    fclose(f);
    TT_CHECK_EQ(slurp("build/no-such-file.bin", &d), 0);
    TT_CHECK(d == NULL);
    d = (uint8_t *)1;
    TT_CHECK_EQ(slurp("build/empty.bin", &d), 0);
    TT_CHECK(d == NULL);
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
    if (n < 44 || le32(d + 40) > n - 44) { free(d); return NULL; }
    *rate = le32(d + 24);
    *frames = le32(d + 40) / 4;
    pcm = malloc((size_t)*frames * 4 + 4);
    if (!pcm) { free(d); return NULL; }
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

/*
 * A note off and a note on for the same key at the same tick, in
 * different tracks: the off must come first, or the new note would be
 * released as soon as it starts.
 */
TT_TEST(smf_orders_note_offs_before_note_ons_at_the_same_tick)
{
    static const uint8_t d[] = {
        SMF_HDR(2),
        'M', 'T', 'r', 'k', 0, 0, 0, 9,
        0x83, 0x60, 0x90, 60, 100, EOT,               /* on at 480 */
        'M', 'T', 'r', 'k', 0, 0, 0, 13,
        0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 64, EOT /* 0..480 */
    };
    smf_song song;
    char err[128];
    TT_CHECK_EQ(smf_parse(d, sizeof d, &song, err, sizeof err), 0);
    TT_CHECK_EQ(song.count, 3);
    TT_CHECK_EQ(song.events[0].status, 0x90);
    TT_CHECK_EQ(song.events[1].status, 0x80);
    TT_CHECK_EQ(song.events[2].status, 0x90);
    TT_CHECK_EQ(song.events[1].time_us, song.events[2].time_us);
    smf_free(&song);
}

/*
 * A crafted file whose tick count times tempo is exactly 2^64 used to
 * wrap the song length to 0, slipping past the render length cap.
 * Ticks: 8192 deltas of 2^28 - 1 plus one of 8192 = 2^41; tempo 2^23.
 */
TT_TEST(huge_tick_counts_are_rejected_not_wrapped)
{
    const size_t events = 8192;
    size_t n = 22 + 7 + events * 7 + 5 + 4, pos = 0, i;
    uint8_t *d = malloc(n);
    size_t body = n - 22;
    FILE *f;
    render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
    static const uint8_t hdr[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                                   0, 1, 'M', 'T', 'r', 'k' };
    TT_CHECK(d != NULL);
    memcpy(d, hdr, sizeof hdr);
    pos = sizeof hdr;
    d[pos++] = (uint8_t)(body >> 24); d[pos++] = (uint8_t)(body >> 16);
    d[pos++] = (uint8_t)(body >> 8);  d[pos++] = (uint8_t)body;
    /* tempo 0x800000 us per quarter at tick 0 */
    d[pos++] = 0; d[pos++] = 0xFF; d[pos++] = 0x51; d[pos++] = 3;
    d[pos++] = 0x80; d[pos++] = 0; d[pos++] = 0;
    for (i = 0; i < events; i++) { /* delta 0x0FFFFFFF, empty text */
        d[pos++] = 0xFF; d[pos++] = 0xFF; d[pos++] = 0xFF; d[pos++] = 0x7F;
        d[pos++] = 0xFF; d[pos++] = 0x01; d[pos++] = 0;
    }
    /* delta 8192 = 0xC0 0x00, then end of track */
    d[pos++] = 0xC0; d[pos++] = 0x00; d[pos++] = 0xFF; d[pos++] = 0x01;
    d[pos++] = 0;
    d[pos++] = 0x00; d[pos++] = 0xFF; d[pos++] = 0x2F; d[pos++] = 0x00;
    TT_CHECK_EQ(pos, n);
    f = fopen("build/huge-ticks.mid", "wb");
    fwrite(d, 1, n, f);
    fclose(f);
    free(d);
    TT_CHECK(render_midi_file("build/huge-ticks.mid", "build/huge-ticks.wav", &o) != 0);
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

/* -s osc2_coarse_semis=-12 with osc2 alone renders the note an octave down. */
TT_TEST(render_takes_osc2_coarse_from_the_command_line)
{
    render_opts o;
    uint32_t frames, rate;
    int16_t *pcm;
    double period, want = 44100 / (note_hz(69) / 2);
    memset(&o, 0, sizeof o);
    o.sample_rate = 44100;
    TT_CHECK_EQ(args_parse_setting("osc2_coarse_semis=-12", &o), 0);
    TT_CHECK_EQ(args_parse_setting("osc_mix_permille=1000", &o), 0);
    TT_CHECK_EQ(o.param_id[0], FM_PARAM_OSC2_COARSE_SEMIS);
    TT_CHECK_EQ(o.param_value[0], -12);
    write_smf("build/one-note.mid", one_note, sizeof one_note);
    TT_CHECK_EQ(render_midi_file("build/one-note.mid", "build/coarse.wav", &o), 0);
    pcm = read_wav_pcm("build/coarse.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    period = period_from_crossings(pcm + 2 * 4410, 44100 - 2 * 4410, 0);
    free(pcm);
    TT_CHECK_NEAR(period, want, want * 0.0006);
}

/* A C major triad, bent a whole tone down before it starts, held 1 s. */
static const uint8_t bent_chord[] = {
    0x00, 0xE0, 0x00, 0x00,           /* pitch bend full down */
    0x00, 0x90, 60, 100, 0x00, 64, 100, 0x00, 67, 100,
    0x87, 0x40, 0x80, 60, 0, 0x00, 64, 0, 0x00, 67, 0
};

static double chord_amp(const int16_t *pcm, int note)
{
    return tone_amp(pcm + 2 * 4410, 16384, 0, note_hz(note), 44100);
}

TT_TEST(chord_midi_file_plays_every_note_and_follows_pitch_bend)
{
    static const uint8_t counts[] = { 0 /* default */, 4, 6 };
    size_t c;
    for (c = 0; c < sizeof counts; c++) {
        render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
        uint32_t frames, rate;
        int16_t *pcm;
        double a58, a62, a65, a60;
        o.voice_count = counts[c];
        write_smf("build/chord.mid", bent_chord, sizeof bent_chord);
        TT_CHECK_EQ(render_midi_file("build/chord.mid", "build/chord.wav", &o), 0);
        pcm = read_wav_pcm("build/chord.wav", &frames, &rate);
        TT_CHECK(pcm != NULL);
        TT_CHECK(frames >= 4410 + 16384);
        a58 = chord_amp(pcm, 58);
        a62 = chord_amp(pcm, 62);
        a65 = chord_amp(pcm, 65);
        a60 = chord_amp(pcm, 60);
        free(pcm);
        TT_CHECK(a58 > 500);
        TT_CHECK_NEAR(a62 / a58, 1.0, 0.1);
        TT_CHECK_NEAR(a65 / a58, 1.0, 0.1);
        TT_CHECK(a60 / a58 < 0.05);
    }
}

/*
 * Two channels: channel 2 bends full down first, then channel 1 plays a
 * C major triad and channel 2 plays B-flat 4, all held 1 s.
 */
static const uint8_t two_channels[] = {
    0x00, 0xE1, 0x00, 0x00,
    0x00, 0x90, 60, 100, 0x00, 64, 100, 0x00, 67, 100,
    0x00, 0x91, 70, 100,
    0x87, 0x40, 0x80, 60, 0, 0x00, 64, 0, 0x00, 67, 0, 0x00, 0x81, 70, 0
};

TT_TEST(render_plays_one_midi_channel)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
    uint32_t frames, rate;
    int16_t *pcm;
    double a60, a64, a70, a68, a58;
    write_smf("build/two-ch.mid", two_channels, sizeof two_channels);

    /* default: the channel of the first note on (1), unbent */
    TT_CHECK_EQ(render_midi_file("build/two-ch.mid", "build/two-ch.wav", &o), 0);
    pcm = read_wav_pcm("build/two-ch.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    a60 = chord_amp(pcm, 60);
    a64 = chord_amp(pcm, 64);
    a58 = chord_amp(pcm, 58);
    a70 = chord_amp(pcm, 70);
    a68 = chord_amp(pcm, 68);
    free(pcm);
    TT_CHECK(a60 > 500);
    TT_CHECK_NEAR(a64 / a60, 1.0, 0.1);
    TT_CHECK(a58 / a60 < 0.05); /* channel 2's bend ignored */
    TT_CHECK(a70 / a60 < 0.05); /* channel 2's note ignored */
    TT_CHECK(a68 / a60 < 0.05);

    /* -c 2: only channel 2's note, bent a whole tone down */
    o.channel = 2;
    TT_CHECK_EQ(render_midi_file("build/two-ch.mid", "build/two-ch.wav", &o), 0);
    pcm = read_wav_pcm("build/two-ch.wav", &frames, &rate);
    TT_CHECK(pcm != NULL);
    a60 = chord_amp(pcm, 60);
    a68 = chord_amp(pcm, 68);
    a70 = chord_amp(pcm, 70);
    free(pcm);
    TT_CHECK(a68 > 500);
    TT_CHECK(a60 / a68 < 0.05);
    TT_CHECK(a70 / a68 < 0.05);

    o.channel = 17;
    printf("    (expected error message follows)\n");
    TT_CHECK(render_midi_file("build/two-ch.mid", "build/x.wav", &o) != 0);
}

TT_TEST(render_reports_voice_count_channel_and_level)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
    render_info info;
    char text[160];
    write_smf("build/two-ch.mid", two_channels, sizeof two_channels);
    TT_CHECK_EQ(render_midi("build/two-ch.mid", "build/x.wav", &o, &info), 0);
    TT_CHECK_EQ(info.voice_count, 6);
    TT_CHECK_EQ(info.channel, 1);
    render_describe(&info, text, sizeof text);
    printf("    %s\n", text);
    TT_CHECK(strstr(text, "6 voices") != NULL);
    TT_CHECK(strstr(text, "MIDI channel 1") != NULL);
    TT_CHECK(strstr(text, "1/12") != NULL);
    TT_CHECK(strstr(text, "-21.6 dBFS") != NULL);
    o.voice_count = 1;
    o.channel = 2;
    TT_CHECK_EQ(render_midi("build/two-ch.mid", "build/x.wav", &o, &info), 0);
    render_describe(&info, text, sizeof text);
    TT_CHECK(strstr(text, "1 voice,") != NULL);
    TT_CHECK(strstr(text, "MIDI channel 2") != NULL);
    TT_CHECK(strstr(text, "-6.0 dBFS") != NULL);
    /* in unison one note takes every voice, so it peaks at -6 dBFS */
    o.voice_count = 6;
    o.param_count = 1;
    o.param_id[0] = FM_PARAM_VOICE_MODE;
    o.param_value[0] = FM_VOICE_MODE_UNISON;
    TT_CHECK_EQ(render_midi("build/two-ch.mid", "build/x.wav", &o, &info), 0);
    render_describe(&info, text, sizeof text);
    printf("    %s\n", text);
    TT_CHECK(strstr(text, "6 voices in unison") != NULL);
    TT_CHECK(strstr(text, "1/12") != NULL);
    TT_CHECK(strstr(text, "-6.0 dBFS") != NULL);
    TT_CHECK(strstr(text, "-21.6") == NULL);
}

TT_TEST(render_rejects_a_bad_voice_count)
{
    render_opts o = { .sample_rate = 44100, .tail_ms = 0 };
    o.voice_count = FM_MAX_VOICES + 1;
    write_smf("build/one-note.mid", one_note, sizeof one_note);
    printf("    (expected error message follows)\n");
    TT_CHECK(render_midi_file("build/one-note.mid", "build/x.wav", &o) != 0);
}

static void run_host_tests(void)
{
    TT_RUN(slurp_fails_cleanly_on_missing_and_empty_files);
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
    TT_RUN(smf_orders_note_offs_before_note_ons_at_the_same_tick);
    TT_RUN(huge_tick_counts_are_rejected_not_wrapped);
    TT_RUN(wav_rejects_sample_rate_out_of_range);
    TT_RUN(numeric_options_reject_negative_and_out_of_range_values);
    TT_RUN(parameter_settings_reject_out_of_range_values);
    TT_RUN(render_takes_osc2_coarse_from_the_command_line);
    TT_RUN(chord_midi_file_plays_every_note_and_follows_pitch_bend);
    TT_RUN(render_rejects_a_bad_voice_count);
    TT_RUN(render_plays_one_midi_channel);
    TT_RUN(render_reports_voice_count_channel_and_level);
}
