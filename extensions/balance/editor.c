/* SPDX-License-Identifier: GPL-3.0-only */

/* The Balance screen: the stereo image as a half disc, Left and Right at its
 * arms, with a lobe whose arm on the quieter ear's side shrinks as that ear is
 * turned down, so the image leans towards the louder ear. The wheel moves the
 * position, holding Select puts it back to Center and Menu steps back out.
 * Every change plays at once. */

#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { DEPTHS = 1 }; /* the wheel edits the one value */

enum {
    FIELD_HEIGHT = 27, /* Balance, title left and value right, on the bottom edge */
    FIELD_GAP = 12,    /* the baseline of the half disc to the field row */
    RADIUS = 110,
    TAG_RADIUS = RADIUS + 14, /* the L and R tags sit just outside the arc */
    TAG_SCALE = 7,            /* a gain in Q15 shifted down to Q8 for the tag's place */
    TAG_SIZE = 18,
};

static struct rv_editor editor;

static int position(void)
{
    return rv_clamp(api->setting_get_int(self, KEY_BALANCE), 0, BALANCE_STEPS - 1);
}

static int isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

/* A tag centred on (x, y). */
static void tag(const struct rv_canvas *c, int x, int y, const char *text)
{
    rv_canvas_text(c, x - TAG_SIZE / 2, y - TAG_SIZE / 2, TAG_SIZE, TAG_SIZE, text, 0,
                   RV_ALIGN_CENTER, RV_CANVAS_ACCENT);
}

/* The tag of one ear out past the arc on its lobe arm, whose tangent from the
 * centre line is the ear's gain; a muted ear's arm lies on the centre line, so
 * its tag keeps a little to its side. */
static void tag_on_arm(const struct rv_canvas *c, int baseline, int gain, int left,
                       const char *text)
{
    int w = gain >> TAG_SCALE; /* 0..256 */
    int h = isqrt(TAG_RADIUS * TAG_RADIUS * 65536 / (65536 + w * w));
    int x = h * w / 256;
    if (x < TAG_SIZE / 2)
        x = TAG_SIZE / 2;
    tag(c, left ? c->width / 2 - x : c->width / 2 + x, baseline - h, text);
}

/* One side of a curve on one row: a run from where it was on the row below to
 * where it is now, so a flat stretch stays joined. `left` mirrors it. */
static void side(const struct rv_canvas *c, int y, int from, int to, int thickness,
                 int left, uint8_t color)
{
    int centre = c->width / 2;
    int near = from < to ? from : to;
    int far = from > to ? from : to;
    if (left)
        rv_canvas_rect(c, centre - far - thickness + 1, y, far - near + thickness, 1,
                       color);
    else
        rv_canvas_rect(c, centre + near, y, far - near + thickness, 1, color);
}

/* The half disc row by row: the arc and the 45-degree arms in the grid colour,
 * then the lobe. Each arm of the lobe sits at tan(angle) = that ear's gain
 * from the centre line, so the louder ear keeps the Stereo arm at 45 degrees
 * and the quieter ear's arm closes towards the centre line, down to it when
 * that ear is muted. */
static void draw_image(const struct rv_canvas *c, int baseline, int index)
{
    int centre = c->width / 2;
    int left_gain = 32768, right_gain = 32768;
    if (index < BALANCE_CENTRE)
        right_gain = balance_gain((unsigned int)(BALANCE_CENTRE - index));
    else
        left_gain = balance_gain((unsigned int)(index - BALANCE_CENTRE));
    rv_canvas_header(c, "Stereo image", NULL, 0);
    int d_below = RADIUS, l_below = 0, r_below = 0;
    for (int h = 1; h <= RADIUS; h++) {
        int y = baseline - h;
        int d = isqrt(RADIUS * RADIUS - h * h);
        side(c, y, d_below, d, 1, 1, RV_CANVAS_GRID);
        side(c, y, d_below, d, 1, 0, RV_CANVAS_GRID);
        int l = h * left_gain >> 15, r = h * right_gain >> 15;
        if (l > d)
            l = d;
        if (r > d)
            r = d;
        rv_canvas_rect(c, centre - l, y, l + r + 1, 1, RV_CANVAS_SELECTION);
        if (h <= d) {
            side(c, y, h, h, 1, 1, RV_CANVAS_GRID);
            side(c, y, h, h, 1, 0, RV_CANVAS_GRID);
        }
        side(c, y, h == 1 ? l : l_below, l, 2, 1, RV_CANVAS_ACCENT);
        side(c, y, h == 1 ? r : r_below, r, 2, 0, RV_CANVAS_ACCENT);
        l_below = l;
        r_below = r;
        d_below = d;
    }
    rv_canvas_rect(c, centre - RADIUS, baseline, 2 * RADIUS + 1, 1, RV_CANVAS_GRID);
    tag_on_arm(c, baseline, left_gain, 1, "L");
    tag_on_arm(c, baseline, right_gain, 0, "R");
}

static void draw_field(const struct rv_canvas *c, int height)
{
    static const char *const title = "Balance";
    const char *value = balance_labels[position()];
    uint8_t state = RV_CELL_EDITING;
    rv_canvas_fields(c, height - FIELD_HEIGHT, FIELD_HEIGHT, &title, &value, &state, 1);
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width,
                 int height)
{
    struct rv_canvas c;
    (void)ctx;
    rv_canvas_begin(&c, g, width, height);
    draw_image(&c, height - FIELD_HEIGHT - FIELD_GAP, position());
    draw_field(&c, height);
}

/* Main republishes from setting_changed, so a change plays at once. */
static void set_position(int next)
{
    if (next != position())
        api->setting_set_int(self, KEY_BALANCE, next);
}

static int input(const struct rv_context *ctx, const struct rv_input *input)
{
    (void)ctx;
    switch (rv_editor_input(&editor, input)) {
    case RV_EDITOR_IGNORED:
        return 0;
    case RV_EDITOR_CONSUMED:
        return 1;
    case RV_EDITOR_WHEEL:
        set_position(rv_clamp(position() + input->delta, 0, BALANCE_STEPS - 1));
        break;
    case RV_EDITOR_RESET:
        set_position(BALANCE_CENTRE);
        break;
    default:
        break;
    }
    api->ui_invalidate(self);
    return 1;
}

static void opened(const struct rv_context *ctx)
{
    (void)ctx;
    memset(&editor, 0, sizeof editor);
    editor.depths = DEPTHS;
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("editor", "Balance", draw, input,
                     .transition = RV_TRANSITION_FROM_PANE, .opened = opened),
};

static const struct rv_descriptor module = {
    .size = sizeof module,
    RV_SCREENS(screens),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &module;
}
