/* #47's first box: every engine param can be reached from a page, edited across its range and shown
 * (interaction.md section 3), checked from the engine's param list, not from the UI's map, so a param
 * left off every page fails here. */

/* Params with no knob yet. (05 BEND RANGE and 07 VOICE MODE are on 0B.) A param added here is one the
 * panel can't edit; say why beside it. */
static const uint16_t REACH_NO_PAGE[] = {
    FM_PARAM_WHEEL_PITCH_CENTS,                 /* #41: waits on its slot on 0B CONTROL (agent1) */
    FM_PARAM_WHEEL_PW_PCT,                      /* #41: likewise */
};

static int reach_exempt(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < sizeof REACH_NO_PAGE / sizeof REACH_NO_PAGE[0]; i++)
        if (REACH_NO_PAGE[i] == id)
            return 1;
    return 0;
}

/* the ends a hard turn each way must land a slot's value on (ui_slot_value's view) */
static void reach_ends(const ui_slot *s, int32_t *lo, int32_t *hi)
{
    const fm_param_info *in = fm_param_info_get(s->id);
    if (s->order) {                             /* a choice: its first and last knob position */
        int32_t max = s->how == UI_HOW_PLAIN ? in->max : 3, j;
        *lo = *hi = INT32_MIN;
        for (j = 0; j < (int32_t)s->n_order; j++)
            if (s->stops >> j & 1u && s->order[j] <= max) {
                if (*lo == INT32_MIN)
                    *lo = s->order[j];
                *hi = s->order[j];
            }
    } else if (s->how == UI_HOW_TIME || s->how == UI_HOW_RATE) {   /* its table's ends, in range */
        uint32_t n = s->how == UI_HOW_TIME ? ui_time_count() : ui_rate_count();
        int32_t a = s->how == UI_HOW_TIME ? ui_time_at(0) : ui_rate_at(0);
        int32_t b = s->how == UI_HOW_TIME ? ui_time_at(n - 1u) : ui_rate_at(n - 1u);
        *lo = a < in->min ? in->min : a;
        *hi = b > in->max ? in->max : b;
    } else if (s->how == UI_HOW_SIZE) {         /* a depth's size: 0 up to its top */
        *lo = 0;
        *hi = in->max;
    } else {
        *lo = in->min;
        *hi = in->max;
    }
}

/* the focus row's text for the slot now: 1 when it shows a value (not empty, not "--") */
static int reach_shown(const ui_slot *s, char *num, char *unit)
{
    ui_value(s->id, ui_slot_value(&ui_u, s), num, unit);
    return num[0] && strcmp(num, "--") != 0;
}

/* SELECT walks every page with something live; on each, every live knob is turned hard down and hard
 * up, and must land on its ends, shown as different values; a choice is then stepped through every
 * stop in order (so X-MOD's SYNC 1 and SYNC 2 count too, not only its ends). Every engine param must
 * be one a live slot sets (a PAIR slot sets two), or on the exception list. */
TT_TEST(ui_every_param_is_reached_edited_and_shown)
{
    static uint8_t seen[UI_PAGES], covered[FM_PARAM_COUNT];
    uint32_t n, k, i, missed = 0, wrong = 0, pages = 0, slots = 0, params = 0;
    ui_setup();
    for (i = 0; i < UI_PAGES; i++)
        seen[i] = 0;
    for (i = 0; i < FM_PARAM_COUNT; i++)
        covered[i] = 0;
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 50);    /* PLAY to EDIT: the first page */
    for (n = 0; n < 2u * UI_PAGES && !seen[ui_u.page]; n++) {
        seen[ui_u.page] = 1;
        pages++;
        for (k = 0; k < 4u; k++) {
            const ui_slot *s = ui_page_slot(&ui_u, k);
            char num_lo[UI_VALUE_LEN], unit_lo[UI_VALUE_LEN], num_hi[UI_VALUE_LEN], unit_hi[UI_VALUE_LEN];
            int32_t lo, hi, got_lo, got_hi;
            if (!s || s->kind != UI_LIVE)
                continue;
            slots++;
            covered[s->id] = 1;
            if (s->how == UI_HOW_PAIR)
                covered[s->id2] = 1;
            reach_ends(s, &lo, &hi);
            ui_event(&ui_u, UI_EV_ENC | (UI_ENC_KNOB1 + k), -1000, 100);
            got_lo = ui_slot_value(&ui_u, s);
            if (!reach_shown(s, num_lo, unit_lo))
                got_lo = INT32_MIN;
            ui_event(&ui_u, UI_EV_ENC | (UI_ENC_KNOB1 + k), 1000, 100);
            got_hi = ui_slot_value(&ui_u, s);
            if (!reach_shown(s, num_hi, unit_hi))
                got_hi = INT32_MIN;
            if (got_lo != lo || got_hi != hi ||
                (strcmp(num_lo, num_hi) == 0 && strcmp(unit_lo, unit_hi) == 0)) {
                printf("  page %s knob %u (%s): ends %ld..%ld, got %ld (%s %s)..%ld (%s %s)\n",
                       UI_PAGE_INFO[ui_u.page].name, (unsigned)(k + 1u), fm_param_info_get(s->id)->name,
                       (long)lo, (long)hi, (long)got_lo, num_lo, unit_lo, (long)got_hi, num_hi, unit_hi);
                wrong++;
            }
            if (s->order) {                     /* a choice: one click at a time, every stop in order */
                int32_t max = s->how == UI_HOW_PLAIN ? fm_param_info_get(s->id)->max : 3, j;
                ui_event(&ui_u, UI_EV_ENC | (UI_ENC_KNOB1 + k), -1000, 100);
                for (j = 0; j < (int32_t)s->n_order; j++) {
                    if (!(s->stops >> j & 1u) || s->order[j] > max)
                        continue;
                    if (ui_slot_value(&ui_u, s) != s->order[j]) {
                        printf("  page %s knob %u (%s): stop %ld is %ld, got %ld\n", UI_PAGE_INFO[ui_u.page].name,
                               (unsigned)(k + 1u), fm_param_info_get(s->id)->name, (long)j, (long)s->order[j],
                               (long)ui_slot_value(&ui_u, s));
                        wrong++;
                        break;
                    }
                    ui_event(&ui_u, UI_EV_ENC | (UI_ENC_KNOB1 + k), 1, 100);
                }
            }
        }
        ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 1, 100);
    }
    for (i = 0; i < FM_PARAM_COUNT; i++)
        params += covered[i];
    printf("    %u pages, %u live slots, %u of %u params\n", (unsigned)pages, (unsigned)slots, (unsigned)params,
           (unsigned)FM_PARAM_COUNT);
    for (i = 0; i < FM_PARAM_COUNT; i++)
        if (!covered[i] && !reach_exempt(i)) {
            printf("  %s: on no live slot\n", fm_param_info_get((uint16_t)i)->name);
            missed++;
        }
    TT_CHECK_EQ(missed, 0u);
    TT_CHECK_EQ(wrong, 0u);
}

/* The exception list is current: each param on it really has no live slot. */
TT_TEST(ui_reach_exceptions_have_no_slot)
{
    uint32_t p, i;
    for (i = 0; i < sizeof REACH_NO_PAGE / sizeof REACH_NO_PAGE[0]; i++) {
        for (p = 0; p < UI_PAGES * 4u; p++)
            TT_CHECK(!(UI_SLOTS[p].kind == UI_LIVE &&
                       (UI_SLOTS[p].id == REACH_NO_PAGE[i] ||
                        (UI_SLOTS[p].how == UI_HOW_PAIR && UI_SLOTS[p].id2 == REACH_NO_PAGE[i]))));
    }
}

static void run_ui_reach_tests(void)
{
    TT_RUN(ui_every_param_is_reached_edited_and_shown);
    TT_RUN(ui_reach_exceptions_have_no_slot);
}
