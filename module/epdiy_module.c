// SPDX-License-Identifier: MIT
//
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

#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "epdiy.h"

static const char *TAG = "epdiy_mp";

// ─── Singleton guard ──────────────────────────────────────────────────────────
// Only one EPD instance may be live at a time.
static bool epd_module_in_use = false;

// ─── EPD object type ──────────────────────────────────────────────────────────
typedef struct _epd_obj_t {
    mp_obj_base_t base;
    EpdiyHighlevelState hl;
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
// (epd_board_lilygo_t5_47 has get_temperature = NULL → returns 0.0).
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
    int byte_offset = r.y * epd_width() / 2 + r.x / 2;
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
    { MP_ROM_QSTR(MP_QSTR_fill_circle), MP_ROM_PTR(&epd_obj_fill_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_update),      MP_ROM_PTR(&epd_obj_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_update_area), MP_ROM_PTR(&epd_obj_update_area_obj) },
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
    { MP_ROM_QSTR(MP_QSTR_WIDTH),     MP_ROM_INT(960) },
    { MP_ROM_QSTR(MP_QSTR_HEIGHT),    MP_ROM_INT(540) },
};
static MP_DEFINE_CONST_DICT(epdiy_module_globals, epdiy_module_globals_table);

const mp_obj_module_t epdiy_user_cmodule = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&epdiy_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_epdiy, epdiy_user_cmodule);
