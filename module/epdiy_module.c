// MicroPython C extension module for the epdiy e-paper display library.
// Target: LilyGo T5 4.7" (ED047TC1, 960x540) on ESP32-D0WQ6.
//
// Usage:
//   import epdiy
//   epd = epdiy.EPD()
//   epd.clear()
//   epd.fill_rect(0, 0, 100, 100, 0)   # black square
//   epd.poweron()
//   epd.update(epdiy.MODE_GC16)
//   epd.poweroff()
//   epd.deinit()
//
// Colors: 0 = black, 15 = white (4-bit grayscale).

#include <math.h>
#include <stdbool.h>

#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "epdiy.h"

#include "fonts/FiraSans/firasans_12.h"
#include "fonts/FiraSans/firasans_20.h"

// ─── Singleton guard ──────────────────────────────────────────────────────────
// Only one EPD instance may be live at a time.
static bool epd_module_in_use = false;

// ─── EPD object type ──────────────────────────────────────────────────────────
typedef struct _epd_obj_t {
    mp_obj_base_t base;
    EpdiyHighlevelState hl;
    EpdFontProperties font_props;
    bool initialized;
} epd_obj_t;

// Raise OSError(EPERM) if the object has been deinitialized.
#define EPD_CHECK_INIT(self) \
    do { if (!(self)->initialized) { mp_raise_OSError(MP_EPERM); } } while (0)

// Convert a Python integer 0-15 to the upper-nibble byte epdiy expects.
static uint8_t color_from_py(mp_obj_t c_obj) {
    int c = mp_obj_get_int(c_obj);
    if (c < 0 || c > 15) {
        mp_raise_ValueError(MP_ERROR_TEXT("color must be 0-15"));
    }
    return (uint8_t)(c << 4);
}

// Read ambient temperature; fall back to 25 °C when no sensor is fitted
// (epd_board_lilygo_t5_47 has get_temperature = NULL -> returns 0.0).
static int get_temperature(void) {
    int t = (int)epd_ambient_temperature();
    return t ? t : 25;
}

// ─── Constructor ──────────────────────────────────────────────────────────────
static mp_obj_t epd_make_new(const mp_obj_type_t *type,
                             size_t n_args, size_t n_kw,
                             const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    if (epd_module_in_use) {
        mp_raise_OSError(MP_EBUSY);
    }
    epd_obj_t *self = mp_obj_malloc(epd_obj_t, type);
    epd_init(&epd_board_lilygo_t5_47, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1560);
    self->hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    self->font_props = epd_font_properties_default();
    self->initialized = true;
    epd_module_in_use = true;
    return MP_OBJ_FROM_PTR(self);
}

// ─── deinit ───────────────────────────────────────────────────────────────────
static mp_obj_t epd_obj_deinit(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    epd_deinit();
    self->initialized = false;
    epd_module_in_use = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_deinit_obj, epd_obj_deinit);

// ─── Power management ─────────────────────────────────────────────────────────
static mp_obj_t epd_obj_poweron(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    epd_poweron();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_poweron_obj, epd_obj_poweron);

static mp_obj_t epd_obj_poweroff(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    epd_poweroff();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_poweroff_obj, epd_obj_poweroff);

// ─── Temperature ──────────────────────────────────────────────────────────────
static mp_obj_t epd_obj_temperature(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    return mp_obj_new_float(epd_ambient_temperature());
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_temperature_obj, epd_obj_temperature);

// ─── Clear ────────────────────────────────────────────────────────────────────
// Performs a full hardware clear cycle (power is managed internally here).
static mp_obj_t epd_obj_clear(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    epd_poweron();
    epd_fullclear(&self->hl, get_temperature());
    epd_poweroff();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_clear_obj, epd_obj_clear);

// ─── fill(color) ──────────────────────────────────────────────────────────────
static mp_obj_t epd_obj_fill(mp_obj_t self_in, mp_obj_t color_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    uint8_t color = color_from_py(color_in);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_fill_rect(epd_full_screen(), color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(epd_obj_fill_obj, epd_obj_fill);

// ─── pixel(x, y, color) ───────────────────────────────────────────────────────
static mp_obj_t epd_obj_pixel(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    uint8_t color = color_from_py(args[3]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_pixel(x, y, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_pixel_obj, 4, 4, epd_obj_pixel);

// ─── hline(x, y, w, color) ────────────────────────────────────────────────────
static mp_obj_t epd_obj_hline(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x      = mp_obj_get_int(args[1]);
    int y      = mp_obj_get_int(args[2]);
    int length = mp_obj_get_int(args[3]);
    uint8_t color = color_from_py(args[4]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_hline(x, y, length, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_hline_obj, 5, 5, epd_obj_hline);

// ─── vline(x, y, h, color) ────────────────────────────────────────────────────
static mp_obj_t epd_obj_vline(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x      = mp_obj_get_int(args[1]);
    int y      = mp_obj_get_int(args[2]);
    int length = mp_obj_get_int(args[3]);
    uint8_t color = color_from_py(args[4]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_vline(x, y, length, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_vline_obj, 5, 5, epd_obj_vline);

// ─── line(x0, y0, x1, y1, color) ─────────────────────────────────────────────
static mp_obj_t epd_obj_line(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x0 = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int x1 = mp_obj_get_int(args[3]);
    int y1 = mp_obj_get_int(args[4]);
    uint8_t color = color_from_py(args[5]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_line(x0, y0, x1, y1, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_line_obj, 6, 6, epd_obj_line);

// ─── rect(x, y, w, h, color) ──────────────────────────────────────────────────
static mp_obj_t epd_obj_rect(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    EpdRect r = {
        .x      = mp_obj_get_int(args[1]),
        .y      = mp_obj_get_int(args[2]),
        .width  = mp_obj_get_int(args[3]),
        .height = mp_obj_get_int(args[4]),
    };
    uint8_t color = color_from_py(args[5]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_rect(r, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_rect_obj, 6, 6, epd_obj_rect);

// ─── fill_rect(x, y, w, h, color) ────────────────────────────────────────────
static mp_obj_t epd_obj_fill_rect(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    EpdRect r = {
        .x      = mp_obj_get_int(args[1]),
        .y      = mp_obj_get_int(args[2]),
        .width  = mp_obj_get_int(args[3]),
        .height = mp_obj_get_int(args[4]),
    };
    uint8_t color = color_from_py(args[5]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_fill_rect(r, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_fill_rect_obj, 6, 6, epd_obj_fill_rect);

// ─── circle(x, y, r, color) ───────────────────────────────────────────────────
static mp_obj_t epd_obj_circle(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int r = mp_obj_get_int(args[3]);
    uint8_t color = color_from_py(args[4]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_circle(x, y, r, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_circle_obj, 5, 5, epd_obj_circle);

// ─── fill_circle(x, y, r, color) ─────────────────────────────────────────────
static mp_obj_t epd_obj_fill_circle(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int r = mp_obj_get_int(args[3]);
    uint8_t color = color_from_py(args[4]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_fill_circle(x, y, r, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_fill_circle_obj, 5, 5, epd_obj_fill_circle);

// ─── triangle(x0, y0, x1, y1, x2, y2, color) ─────────────────────────────────
static mp_obj_t epd_obj_triangle(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x0 = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int x1 = mp_obj_get_int(args[3]);
    int y1 = mp_obj_get_int(args[4]);
    int x2 = mp_obj_get_int(args[5]);
    int y2 = mp_obj_get_int(args[6]);
    uint8_t color = color_from_py(args[7]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_draw_triangle(x0, y0, x1, y1, x2, y2, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_triangle_obj, 8, 8, epd_obj_triangle);

// ─── fill_triangle(x0, y0, x1, y1, x2, y2, color) ────────────────────────────
static mp_obj_t epd_obj_fill_triangle(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x0 = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int x1 = mp_obj_get_int(args[3]);
    int y1 = mp_obj_get_int(args[4]);
    int x2 = mp_obj_get_int(args[5]);
    int y2 = mp_obj_get_int(args[6]);
    uint8_t color = color_from_py(args[7]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    epd_fill_triangle(x0, y0, x1, y1, x2, y2, color, fb);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_fill_triangle_obj, 8, 8, epd_obj_fill_triangle);

// ─── Angle helpers ────────────────────────────────────────────────────────────
#ifndef M_PIf
#define M_PIf 3.14159265f
#endif

// Normalize angle in degrees to [0, 360).
static float normalize_angle_f(float a) {
    a = fmodf(a, 360.0f);
    return (a < 0.0f) ? a + 360.0f : a;
}

// True if angle `a` (degrees) lies within the clockwise arc from `start` to `end`.
// `start` and `end` must already be normalized to [0, 360).
static bool angle_in_arc(float a, float start, float end) {
    a = normalize_angle_f(a);
    if (start <= end) {
        return a >= start && a <= end;
    }
    return a >= start || a <= end;  // arc wraps past 360°
}

// ─── arc(x, y, r, start, end, color) ─────────────────────────────────────────
// Draw an arc outline. Angles are in degrees: 0 = right (east), 90 = down,
// increasing clockwise. The arc is drawn from `start` to `end` clockwise.
static mp_obj_t epd_obj_arc(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int   cx    = mp_obj_get_int(args[1]);
    int   cy    = mp_obj_get_int(args[2]);
    int   r     = mp_obj_get_int(args[3]);
    float start = (float)mp_obj_get_float(args[4]);
    float end   = (float)mp_obj_get_float(args[5]);
    uint8_t color = color_from_py(args[6]);
    uint8_t *fb   = epd_hl_get_framebuffer(&self->hl);

    if (r <= 0) return mp_const_none;

    // Full-circle shortcut.
    if (fabsf(end - start) >= 360.0f) {
        epd_draw_circle(cx, cy, r, color, fb);
        return mp_const_none;
    }

    // Angular step: ~one step per pixel on the circumference.
    float step = 180.0f / (M_PIf * (float)r);
    if (step < 0.01f) step = 0.01f;

    float start_n = normalize_angle_f(start);
    float end_n   = normalize_angle_f(end);
    float sweep   = (start_n <= end_n) ? end_n - start_n
                                       : 360.0f - start_n + end_n;
    if (sweep < step) sweep = step;  // draw at least one pixel

    for (float t = 0.0f; t <= sweep + step * 0.5f; t += step) {
        float deg = fmodf(start_n + t, 360.0f);
        float rad = deg * (M_PIf / 180.0f);
        int px = cx + (int)roundf((float)r * cosf(rad));
        int py = cy + (int)roundf((float)r * sinf(rad));
        epd_draw_pixel(px, py, color, fb);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_arc_obj, 7, 7, epd_obj_arc);

// ─── fill_arc(x, y, r, start, end, color) ────────────────────────────────────
// Draw a filled pie wedge (arc + two radii). Same angle convention as arc().
static mp_obj_t epd_obj_fill_arc(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int   cx    = mp_obj_get_int(args[1]);
    int   cy    = mp_obj_get_int(args[2]);
    int   r     = mp_obj_get_int(args[3]);
    float start = (float)mp_obj_get_float(args[4]);
    float end   = (float)mp_obj_get_float(args[5]);
    uint8_t color = color_from_py(args[6]);
    uint8_t *fb   = epd_hl_get_framebuffer(&self->hl);

    if (r <= 0) return mp_const_none;

    // Full-circle shortcut.
    if (fabsf(end - start) >= 360.0f) {
        epd_fill_circle(cx, cy, r, color, fb);
        return mp_const_none;
    }

    float start_n = normalize_angle_f(start);
    float end_n   = normalize_angle_f(end);
    int r2 = r * r;

    for (int py = cy - r; py <= cy + r; py++) {
        if (py < 0 || py >= 540) continue;
        int dy = py - cy;
        for (int px = cx - r; px <= cx + r; px++) {
            if (px < 0 || px >= 960) continue;
            int dx = px - cx;
            if (dx * dx + dy * dy > r2) continue;
            if (dx == 0 && dy == 0) {
                epd_draw_pixel(px, py, color, fb);
                continue;
            }
            float a = atan2f((float)dy, (float)dx) * (180.0f / M_PIf);
            if (angle_in_arc(a, start_n, end_n)) {
                epd_draw_pixel(px, py, color, fb);
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_fill_arc_obj, 7, 7, epd_obj_fill_arc);

// ─── Rounded rectangle helpers ────────────────────────────────────────────────
// Draw selected quadrants of a circle outline using the Bresenham midpoint
// algorithm. `corners` bitmask: 0x1=top-right, 0x2=top-left,
//                               0x4=bottom-left, 0x8=bottom-right.
static void draw_circle_quadrants(int cx, int cy, int r, uint8_t corners,
                                  uint8_t color, uint8_t *fb) {
    int f     = 1 - r;
    int ddx   = 1;
    int ddy   = -2 * r;
    int x     = 0;
    int y     = r;
    while (x <= y) {
        if (corners & 0x1) {  // top-right
            epd_draw_pixel(cx + x, cy - y, color, fb);
            epd_draw_pixel(cx + y, cy - x, color, fb);
        }
        if (corners & 0x2) {  // top-left
            epd_draw_pixel(cx - x, cy - y, color, fb);
            epd_draw_pixel(cx - y, cy - x, color, fb);
        }
        if (corners & 0x4) {  // bottom-left
            epd_draw_pixel(cx - x, cy + y, color, fb);
            epd_draw_pixel(cx - y, cy + x, color, fb);
        }
        if (corners & 0x8) {  // bottom-right
            epd_draw_pixel(cx + x, cy + y, color, fb);
            epd_draw_pixel(cx + y, cy + x, color, fb);
        }
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
    }
}

// ─── round_rect(x, y, w, h, r, color) ────────────────────────────────────────
// Draw a rounded-rectangle outline. `r` is the corner radius.
static mp_obj_t epd_obj_round_rect(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    int h = mp_obj_get_int(args[4]);
    int r = mp_obj_get_int(args[5]);
    uint8_t color = color_from_py(args[6]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);

    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    // Straight edges
    epd_draw_hline(x + r,         y,             w - 2 * r, color, fb);  // top
    epd_draw_hline(x + r,         y + h - 1,     w - 2 * r, color, fb);  // bottom
    epd_draw_vline(x,             y + r,          h - 2 * r, color, fb);  // left
    epd_draw_vline(x + w - 1,     y + r,          h - 2 * r, color, fb);  // right

    // Rounded corners
    if (r > 0) {
        draw_circle_quadrants(x + r,         y + r,         r, 0x2, color, fb);
        draw_circle_quadrants(x + w - r - 1, y + r,         r, 0x1, color, fb);
        draw_circle_quadrants(x + r,         y + h - r - 1, r, 0x4, color, fb);
        draw_circle_quadrants(x + w - r - 1, y + h - r - 1, r, 0x8, color, fb);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_round_rect_obj, 7, 7, epd_obj_round_rect);

// ─── fill_round_rect(x, y, w, h, r, color) ───────────────────────────────────
// Draw a filled rounded rectangle. `r` is the corner radius.
static mp_obj_t epd_obj_fill_round_rect(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    int h = mp_obj_get_int(args[4]);
    int r = mp_obj_get_int(args[5]);
    uint8_t color = color_from_py(args[6]);
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);

    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    // Central rectangle spanning full height
    EpdRect center = { .x = x + r, .y = y, .width = w - 2 * r, .height = h };
    epd_fill_rect(center, color, fb);

    // Left and right corner columns via epdiy's fill helper
    // corners=1 → right side of circle (fills right corners)
    // corners=2 → left side of circle  (fills left corners)
    // delta extends the fill span to bridge both corners vertically
    if (r > 0) {
        epd_fill_circle_helper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color, fb);
        epd_fill_circle_helper(x + r,          y + r, r, 2, h - 2 * r - 1, color, fb);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_fill_round_rect_obj, 7, 7, epd_obj_fill_round_rect);

// ─── set_text_color(fg[, bg]) ─────────────────────────────────────────────────
// Set the foreground (and optionally background) color for write_text.
// Colors are 0-15. bg defaults to 15 (white) when not given.
static mp_obj_t epd_obj_set_text_color(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    self->font_props.fg_color = color_from_py(args[1]) >> 4;
    if (n_args > 2) {
        self->font_props.bg_color = color_from_py(args[2]) >> 4;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_set_text_color_obj, 2, 3, epd_obj_set_text_color);

// ─── reset_text_props() ───────────────────────────────────────────────────────
// Reset all font properties to their defaults (black on transparent, left-aligned).
static mp_obj_t epd_obj_reset_text_props(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    self->font_props = epd_font_properties_default();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_reset_text_props_obj, epd_obj_reset_text_props);

// ─── set_text_align(flags) ────────────────────────────────────────────────────
// Set the text alignment / background flags for write_text.
// Pass one of: epdiy.ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER, DRAW_BACKGROUND,
// or a bitwise OR of multiple values.
static mp_obj_t epd_obj_set_text_align(mp_obj_t self_in, mp_obj_t flags_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    self->font_props.flags = (enum EpdFontFlags)mp_obj_get_int(flags_in);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(epd_obj_set_text_align_obj, epd_obj_set_text_align);

// ─── Font helpers ─────────────────────────────────────────────────────────────
static const EpdFont *font_from_size(int size) {
    if (size == 12) return &FiraSans_12;
    if (size == 20) return &FiraSans_20;
    return NULL;
}

// ─── write_text(x, y, text, size) ────────────────────────────────────────────
// Write a string using FiraSans at the given font size (12 or 20).
// Uses the font properties set by set_text_color() / set_text_align().
// Raises ValueError for unsupported sizes.
static mp_obj_t epd_obj_write_text(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    const char *text = mp_obj_str_get_str(args[3]);
    int size = mp_obj_get_int(args[4]);

    const EpdFont *font = font_from_size(size);
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("font size must be 12 or 20"));
    }

    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);
    enum EpdDrawError err = epd_write_string(font, text, &x, &y, fb, &self->font_props);
    if (err != EPD_DRAW_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_write_text_obj, 5, 5, epd_obj_write_text);

// ─── get_string_rect(x, y, text, size[, margin]) → (x, y, w, h) ──────────────
// Returns the bounding rectangle for the text as a 4-tuple.
// Handles newlines. margin (default 0) is added to width and height.
static mp_obj_t epd_obj_get_string_rect(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x      = mp_obj_get_int(args[1]);
    int y      = mp_obj_get_int(args[2]);
    const char *text = mp_obj_str_get_str(args[3]);
    int size   = mp_obj_get_int(args[4]);
    int margin = (n_args > 5) ? mp_obj_get_int(args[5]) : 0;

    const EpdFont *font = font_from_size(size);
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("font size must be 12 or 20"));
    }
    EpdFontProperties props = epd_font_properties_default();
    EpdRect r = epd_get_string_rect(font, text, x, y, margin, &props);
    mp_obj_t items[4] = {
        mp_obj_new_int(r.x),
        mp_obj_new_int(r.y),
        mp_obj_new_int(r.width),
        mp_obj_new_int(r.height),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_get_string_rect_obj, 5, 6, epd_obj_get_string_rect);

// ─── get_text_bounds(x, y, text, size) → (x1, y1, w, h) ──────────────────────
// Returns the tight bounding box of the text as a 4-tuple.
// Note: does not handle newlines (epdiy limitation).
static mp_obj_t epd_obj_get_text_bounds(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int x      = mp_obj_get_int(args[1]);
    int y      = mp_obj_get_int(args[2]);
    const char *text = mp_obj_str_get_str(args[3]);
    int size   = mp_obj_get_int(args[4]);

    const EpdFont *font = font_from_size(size);
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("font size must be 12 or 20"));
    }
    EpdFontProperties props = epd_font_properties_default();
    int x1, y1, w, h;
    epd_get_text_bounds(font, text, &x, &y, &x1, &y1, &w, &h, &props);
    mp_obj_t items[4] = {
        mp_obj_new_int(x1),
        mp_obj_new_int(y1),
        mp_obj_new_int(w),
        mp_obj_new_int(h),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_get_text_bounds_obj, 5, 5, epd_obj_get_text_bounds);

// ─── font_metrics(size) → (ascender, descender, advance_y) ───────────────────
// Returns vertical font metrics useful for layout.
static mp_obj_t epd_obj_font_metrics(mp_obj_t self_in, mp_obj_t size_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    const EpdFont *font = font_from_size(mp_obj_get_int(size_in));
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("font size must be 12 or 20"));
    }
    mp_obj_t items[3] = {
        mp_obj_new_int(font->ascender),
        mp_obj_new_int(font->descender),
        mp_obj_new_int(font->advance_y),
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_2(epd_obj_font_metrics_obj, epd_obj_font_metrics);

// ─── draw_framebuf(buf, width, height, format, x, y) ─────────────────────────
// Blit a MicroPython-compatible framebuf (or any buffer-protocol object) onto
// the epdiy framebuffer.
//
// buf    – framebuf.FrameBuffer, bytes, or bytearray (read via buffer protocol)
// width  – pixel width of the source buffer
// height – pixel height of the source buffer
// format – pixel format; use the constants below or framebuf.MONO_HMSB etc.:
//            MONO_HMSB = 4  (1 bpp; 0→black, 1→white)
//            GS2_HMSB  = 5  (2 bpp; 0=black … 3=white)
//            GS4_HMSB  = 2  (4 bpp; 0=black … 15=white)
// x, y   – top-left destination position on the display
//
// Pixel extraction follows MicroPython's modframebuf.c conventions exactly.

// Format identifiers — must match framebuf module integer values exactly:
//   framebuf.MONO_HMSB = FRAMEBUF_MHMSB    = 4
//   framebuf.GS2_HMSB  = FRAMEBUF_GS2_HMSB = 5
//   framebuf.GS4_HMSB  = FRAMEBUF_GS4_HMSB = 2  (NOT 6; 6 is GS8)
#define EPDIY_FMT_MONO_HMSB 4
#define EPDIY_FMT_GS2_HMSB  5
#define EPDIY_FMT_GS4_HMSB  2

static mp_obj_t epd_obj_draw_framebuf(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);

    int src_w  = mp_obj_get_int(args[2]);
    int src_h  = mp_obj_get_int(args[3]);
    int format = mp_obj_get_int(args[4]);
    int dst_x  = mp_obj_get_int(args[5]);
    int dst_y  = mp_obj_get_int(args[6]);

    if (src_w <= 0 || src_h <= 0) {
        return mp_const_none;
    }
    if (format != EPDIY_FMT_MONO_HMSB &&
        format != EPDIY_FMT_GS2_HMSB  &&
        format != EPDIY_FMT_GS4_HMSB) {
        mp_raise_ValueError(MP_ERROR_TEXT("format must be MONO_HMSB, GS2_HMSB, or GS4_HMSB"));
    }

    const uint8_t *src = (const uint8_t *)bufinfo.buf;
    uint8_t *fb = epd_hl_get_framebuffer(&self->hl);

    // Pixel stride, matching MicroPython's default when stride is not supplied:
    //   MONO_HMSB: rounded up to a multiple of 8 pixels (1 byte = 8 px)
    //   GS2_HMSB:  rounded up to a multiple of 4 pixels (1 byte = 4 px)
    //   GS4_HMSB:  no rounding (1 byte = 2 px)
    int stride;
    if (format == EPDIY_FMT_MONO_HMSB) {
        stride = (src_w + 7) & ~7;
    } else if (format == EPDIY_FMT_GS2_HMSB) {
        stride = (src_w + 3) & ~3;
    } else {
        stride = src_w;
    }

    for (int row = 0; row < src_h; row++) {
        int py = dst_y + row;
        if (py < 0 || py >= 540) {
            continue;
        }
        for (int col = 0; col < src_w; col++) {
            int px = dst_x + col;
            if (px < 0 || px >= 960) {
                continue;
            }

            uint8_t gray;  // 0 = black, 15 = white
            if (format == EPDIY_FMT_MONO_HMSB) {
                // modframebuf.c: offset = x & 0x07  → bit 0 holds the leftmost pixel.
                int byte_idx = (col + row * stride) >> 3;
                int bit = (src[byte_idx] >> (col & 0x07)) & 1;
                gray = bit ? 15 : 0;  // 0=off(black), 1=on(white)
            } else if (format == EPDIY_FMT_GS2_HMSB) {
                // modframebuf.c: shift = (x & 3) << 1  → bits 1:0 hold the leftmost pixel.
                int byte_idx = (col + row * stride) >> 2;
                int shift    = (col & 0x03) << 1;
                uint8_t val  = (src[byte_idx] >> shift) & 0x03;
                gray = (uint8_t)(val * 5);  // 0→0, 1→5, 2→10, 3→15
            } else {  // GS4_HMSB
                // modframebuf.c: even x → HIGH nibble (>> 4), odd x → LOW nibble (& 0x0F).
                int byte_idx = (col + row * stride) >> 1;
                gray = (col & 1) ? (src[byte_idx] & 0x0F) : (src[byte_idx] >> 4);
            }

            // Write gray (0-15) into the epdiy framebuffer.
            // epdiy layout (verified from epd_draw_pixel / epd_get_pixel source):
            //   even x → LOW  nibble (bits 3:0)
            //   odd  x → HIGH nibble (bits 7:4)
            int epd_byte = (py * 960 + px) / 2;
            if (px & 1) {  // odd → HIGH nibble
                fb[epd_byte] = (fb[epd_byte] & 0x0F) | (uint8_t)(gray << 4);
            } else {       // even → LOW nibble
                fb[epd_byte] = (fb[epd_byte] & 0xF0) | gray;
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_draw_framebuf_obj, 7, 7, epd_obj_draw_framebuf);

// ─── invalidate_area (internal) ───────────────────────────────────────────────
// Make back_fb differ from front_fb in the given area so every pixel in that
// region is treated as dirty by the next epd_hl_update_* call.
static void invalidate_area(epd_obj_t *self, EpdRect area) {
    int width = epd_width();
    uint8_t *front = self->hl.front_fb;
    uint8_t *back  = self->hl.back_fb;
    for (int y = area.y; y < area.y + area.height; y++) {
        int bx_start = area.x / 2;
        int bx_end   = (area.x + area.width - 1) / 2;
        for (int bx = bx_start; bx <= bx_end; bx++) {
            int idx = y * (width / 2) + bx;
            back[idx] = ~front[idx];
        }
    }
}

// ─── refresh([x, y, w, h]) ────────────────────────────────────────────────────
// Invalidate the given area (or the full screen) and push it to the display,
// managing power internally. Use this after clear() to force a full redraw.
static mp_obj_t epd_obj_refresh(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    if (n_args != 1 && n_args != 5) {
        mp_raise_TypeError(MP_ERROR_TEXT("refresh() takes 0 or 4 arguments (x, y, w, h)"));
    }
    EpdRect area = (n_args == 5) ? (EpdRect){
        .x      = mp_obj_get_int(args[1]),
        .y      = mp_obj_get_int(args[2]),
        .width  = mp_obj_get_int(args[3]),
        .height = mp_obj_get_int(args[4]),
    } : epd_full_screen();
    invalidate_area(self, area);
    epd_poweron();
    enum EpdDrawError err = epd_hl_update_area(
        &self->hl, MODE_GC16, get_temperature(), area);
    epd_poweroff();
    if (err != EPD_DRAW_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_refresh_obj, 1, 5, epd_obj_refresh);

// ─── update([mode]) ───────────────────────────────────────────────────────────
// mode defaults to MODE_GL16 (non-flashing, full 16-gray update).
// Must be called between poweron() and poweroff().
static mp_obj_t epd_obj_update(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    int mode = (n_args > 1) ? mp_obj_get_int(args[1]) : MODE_GL16;
    enum EpdDrawError err = epd_hl_update_screen(
        &self->hl, (enum EpdDrawMode)mode, get_temperature());
    if (err != EPD_DRAW_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_update_obj, 1, 2, epd_obj_update);

// ─── set_rotation(rot) / get_rotation() ──────────────────────────────────────
static mp_obj_t epd_obj_set_rotation(mp_obj_t self_in, mp_obj_t rot_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    epd_set_rotation((enum EpdRotation)mp_obj_get_int(rot_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(epd_obj_set_rotation_obj, epd_obj_set_rotation);

static mp_obj_t epd_obj_get_rotation(mp_obj_t self_in) {
    epd_obj_t *self = MP_OBJ_TO_PTR(self_in);
    EPD_CHECK_INIT(self);
    return mp_obj_new_int(epd_get_rotation());
}
static MP_DEFINE_CONST_FUN_OBJ_1(epd_obj_get_rotation_obj, epd_obj_get_rotation);

// ─── update_area(x, y, w, h[, mode]) ─────────────────────────────────────────
// Partial refresh of the given rectangle.
// mode defaults to MODE_GL16. Must be called between poweron() and poweroff().
static mp_obj_t epd_obj_update_area(size_t n_args, const mp_obj_t *args) {
    epd_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    EPD_CHECK_INIT(self);
    EpdRect area = {
        .x      = mp_obj_get_int(args[1]),
        .y      = mp_obj_get_int(args[2]),
        .width  = mp_obj_get_int(args[3]),
        .height = mp_obj_get_int(args[4]),
    };
    int mode = (n_args > 5) ? mp_obj_get_int(args[5]) : MODE_GL16;
    enum EpdDrawError err = epd_hl_update_area(
        &self->hl, (enum EpdDrawMode)mode, get_temperature(), area);
    if (err != EPD_DRAW_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(epd_obj_update_area_obj, 5, 6, epd_obj_update_area);

// ─── EPD type definition ──────────────────────────────────────────────────────
static const mp_rom_map_elem_t epd_obj_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_deinit),      MP_ROM_PTR(&epd_obj_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_poweron),     MP_ROM_PTR(&epd_obj_poweron_obj) },
    { MP_ROM_QSTR(MP_QSTR_poweroff),    MP_ROM_PTR(&epd_obj_poweroff_obj) },
    { MP_ROM_QSTR(MP_QSTR_temperature), MP_ROM_PTR(&epd_obj_temperature_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),       MP_ROM_PTR(&epd_obj_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill),        MP_ROM_PTR(&epd_obj_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),       MP_ROM_PTR(&epd_obj_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline),       MP_ROM_PTR(&epd_obj_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline),       MP_ROM_PTR(&epd_obj_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_line),        MP_ROM_PTR(&epd_obj_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),        MP_ROM_PTR(&epd_obj_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),   MP_ROM_PTR(&epd_obj_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle),      MP_ROM_PTR(&epd_obj_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_circle),      MP_ROM_PTR(&epd_obj_fill_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle),         MP_ROM_PTR(&epd_obj_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_triangle),    MP_ROM_PTR(&epd_obj_fill_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_round_rect),       MP_ROM_PTR(&epd_obj_round_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_round_rect),  MP_ROM_PTR(&epd_obj_fill_round_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc),              MP_ROM_PTR(&epd_obj_arc_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_arc),         MP_ROM_PTR(&epd_obj_fill_arc_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_text_color),   MP_ROM_PTR(&epd_obj_set_text_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_text_align),   MP_ROM_PTR(&epd_obj_set_text_align_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_text_props), MP_ROM_PTR(&epd_obj_reset_text_props_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_text),        MP_ROM_PTR(&epd_obj_write_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_string_rect),  MP_ROM_PTR(&epd_obj_get_string_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_text_bounds),  MP_ROM_PTR(&epd_obj_get_text_bounds_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_metrics),     MP_ROM_PTR(&epd_obj_font_metrics_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_framebuf), MP_ROM_PTR(&epd_obj_draw_framebuf_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_rotation), MP_ROM_PTR(&epd_obj_set_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_rotation), MP_ROM_PTR(&epd_obj_get_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_update),      MP_ROM_PTR(&epd_obj_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_update_area), MP_ROM_PTR(&epd_obj_update_area_obj) },
    { MP_ROM_QSTR(MP_QSTR_refresh),     MP_ROM_PTR(&epd_obj_refresh_obj) },
};
static MP_DEFINE_CONST_DICT(epd_obj_locals_dict, epd_obj_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    epd_type_EPD,
    MP_QSTR_EPD,
    MP_TYPE_FLAG_NONE,
    make_new, epd_make_new,
    locals_dict, &epd_obj_locals_dict
);

// ─── Module globals ───────────────────────────────────────────────────────────
static const mp_rom_map_elem_t epdiy_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_epdiy) },
    { MP_ROM_QSTR(MP_QSTR_EPD),       MP_ROM_PTR(&epd_type_EPD) },
    // Update mode constants (values match EpdDrawMode enum in epdiy.h)
    { MP_ROM_QSTR(MP_QSTR_MODE_DU),   MP_ROM_INT(MODE_DU) },
    { MP_ROM_QSTR(MP_QSTR_MODE_GC16), MP_ROM_INT(MODE_GC16) },
    { MP_ROM_QSTR(MP_QSTR_MODE_GL16), MP_ROM_INT(MODE_GL16) },
    { MP_ROM_QSTR(MP_QSTR_MODE_A2),   MP_ROM_INT(MODE_A2) },
    // Display geometry constants for ED047TC1
    { MP_ROM_QSTR(MP_QSTR_WIDTH),            MP_ROM_INT(960) },
    { MP_ROM_QSTR(MP_QSTR_HEIGHT),           MP_ROM_INT(540) },
    // Font flags for set_text_align()
    { MP_ROM_QSTR(MP_QSTR_DRAW_BACKGROUND),  MP_ROM_INT(EPD_DRAW_BACKGROUND) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_LEFT),       MP_ROM_INT(EPD_DRAW_ALIGN_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_RIGHT),      MP_ROM_INT(EPD_DRAW_ALIGN_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_CENTER),     MP_ROM_INT(EPD_DRAW_ALIGN_CENTER) },
    // Rotation constants for set_rotation() / get_rotation()
    { MP_ROM_QSTR(MP_QSTR_ROT_LANDSCAPE),          MP_ROM_INT(EPD_ROT_LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_ROT_PORTRAIT),           MP_ROM_INT(EPD_ROT_PORTRAIT) },
    { MP_ROM_QSTR(MP_QSTR_ROT_INVERTED_LANDSCAPE), MP_ROM_INT(EPD_ROT_INVERTED_LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_ROT_INVERTED_PORTRAIT),  MP_ROM_INT(EPD_ROT_INVERTED_PORTRAIT) },
    // Framebuf format constants for draw_framebuf() (same values as framebuf module)
    { MP_ROM_QSTR(MP_QSTR_MONO_HMSB), MP_ROM_INT(EPDIY_FMT_MONO_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS2_HMSB),  MP_ROM_INT(EPDIY_FMT_GS2_HMSB) },
    { MP_ROM_QSTR(MP_QSTR_GS4_HMSB),  MP_ROM_INT(EPDIY_FMT_GS4_HMSB) },
};
static MP_DEFINE_CONST_DICT(epdiy_module_globals, epdiy_module_globals_table);

const mp_obj_module_t epdiy_user_cmodule = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&epdiy_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_epdiy, epdiy_user_cmodule);
