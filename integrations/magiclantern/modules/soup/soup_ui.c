/** \file
 * soup_ui.c - the upstream Soup Engine film picker, on the EOS 60D.
 *
 * The screen itself is engine/ui.c, unchanged in spirit from the 5D
 * Classic: a 720x240, 16-colour picture of the stock you are about to
 * shoot. This file is only the 60D's plumbing around it:
 *
 *   OPEN    SET on the shooting screen (the 60D's SET does nothing there by
 *           default), or SET on Soup Film in Magic Lantern's menu.
 *   SHOW    The picker lives inside ML's menu, the way arkanoid.mo does: the
 *           menu keeps the panel lit and Canon's display out of the way,
 *           and menu_redraw_blocked stops ML drawing over us.
 *   COLOUR  16 palette entries (0xE0..0xEF, which ML never uses) are loaded
 *           with upstream's own warm palette while the picker is up, and put
 *           back from Canon's shadow copy when it closes.
 *   DRAW    Each 240-line row of the picker is written to two rows of the
 *           60D's 720x480 bitmap.
 *   KEYS    Wheel or left/right: film. SET: choose it (and turn Film on).
 *           Trash or MENU: close. Half-shutter: close and shoot.
 *
 * GPL v2.
 */
#include <dryos.h>
#include <module.h>
#include <menu.h>
#include <bmp.h>
#include <zebra.h>
#include <propvalues.h>

#include "ui.h"
#include "soup_ui.h"

extern int menu_redraw_blocked;    /* ML: stop menu redraws while set   */
extern int lv;

#define PAL_BASE 0xE0              /* our 16 entries: 0xE0..0xEF        */

/*
 * Upstream's colours, as (opacity << 24) | (Y << 16) | (U << 8) | V.
 *
 * Not the 5D Classic's words: those were tuned by eye on that body's panel
 * and decode as pink paper through the standard conversion. These are
 * computed from upstream's own RGB targets (engine/ui.h) through the Rec.709
 * YUV conversion the DIGIC 4 bitmap layer uses (as modelled by qemu-eos),
 * with Magic Lantern's white level (Y 235), and round-trip to within one
 * level. The opacity byte is taken from the 60D's own white at runtime.
 */
static const uint32_t ui_pal[16] = {
    0x00000000,  /* 0  CLEAR  (never drawn: the paper covers everything) */
    0x0019FE02,  /* 1  INK    #1E1A17 */
    0x0033FC04,  /* 2  SHADOW #3D362F */
    0x004FFA05,  /* 3  SLATE  #5E554A */
    0x0072F806,  /* 4  MID    #857A6B */
    0x0095F806,  /* 5  EDGE   #ABA092 */
    0x00C8F505,  /* 6  PAPER  #E2D9C4 */
    0x00D9F704,  /* 7  HILITE #F2EBDA */
    0x0044EC26,  /* 8  RUST   #8A3B22 */
    0x0059E738,  /* 9  RED    #C04A2E */
    0x0089CF2A,  /* A  AMBER  #DC8A32 */
    0x00ADC811,  /* B  GOLD   #D9BE4A */
    0x0075E8F4,  /* C  OLIVE  #6B8A4E */
    0x005F0DE3,  /* D  TEAL   #357380 */
    0x005C0619,  /* E  PLUM   #8E5670 */
    0x00EB0000,  /* F  WHITE */
};

static se_ui        ui;
static se_ui_stock  ui_list[SOUP_UI_MAX];
static uint8_t     *ui_fb;                 /* SE_UI_FB_SIZE bytes        */

static volatile int ui_open;               /* picker on screen           */
static volatile int ui_req;                /* 1 open, 2 choose, 3 cancel */
static volatile int ui_dirty;

static void pal_set(void)
{
    uint32_t op = LCD_Palette[3 * COLOR_WHITE + 2] & 0xFF000000;
    int i;
    for (i = 1; i < 16; i++) {
        int c = PAL_BASE + i;
        EngDrvOut(LCD_Palette[3 * c],         op | ui_pal[i]);
        EngDrvOut(LCD_Palette[3 * c + 0x300], op | ui_pal[i]);
    }
}

static void pal_restore(void)
{
    int i;
    for (i = 1; i < 16; i++) {
        int c = PAL_BASE + i;
        EngDrvOut(LCD_Palette[3 * c],         LCD_Palette[3 * c + 2]);
        EngDrvOut(LCD_Palette[3 * c + 0x300], LCD_Palette[3 * c + 2]);
    }
}

static void ui_blit(void)
{
    uint8_t *b = bmp_vram();
    int x, y;
    if (!b) return;
    for (y = 0; y < SE_UI_H; y++) {
        const uint8_t *src = ui_fb + y * SE_UI_PITCH;
        uint8_t *r0 = b + (2 * y) * BMPPITCH;
        uint8_t *r1 = r0 + BMPPITCH;
        for (x = 0; x < SE_UI_W; x += 2) {
            uint8_t v = src[x >> 1];
            uint8_t lo = v & 15, hi = v >> 4;
            uint8_t c0 = lo ? PAL_BASE + lo : COLOR_EMPTY;
            uint8_t c1 = hi ? PAL_BASE + hi : COLOR_EMPTY;
            r0[x] = c0; r0[x + 1] = c1;
            r1[x] = c0; r1[x + 1] = c1;
        }
    }
}

static void ui_draw(void)
{
    se_ui_fb fb;
    fb.px = ui_fb;
    ui.title = soup_ui_film_on() ? "SOUP" : "SOUP - FILM OFF";
    se_ui_draw(&ui, &fb);
    ui_blit();
    ui_dirty = 0;
}

static void ui_do_open(void)
{
    int i, n = soup_ui_count();

    if (!ui_fb) ui_fb = malloc(SE_UI_FB_SIZE);
    if (!ui_fb) return;
    if (n > SOUP_UI_MAX) n = SOUP_UI_MAX;
    for (i = 0; i < n; i++) {
        ui_list[i].name = soup_ui_name(i);
        ui_list[i].desc = soup_ui_desc(i);
    }
    se_ui_init(&ui);
    se_ui_set_stocks(&ui, ui_list, n);
    ui.stock = soup_ui_current();
    ui.ready = 1;
    ui.hints = "\x01\x02 FILM    SET CHOOSE    DEL CLOSE";

    if (!gui_menu_shown()) {
        gui_open_menu();
        for (i = 0; i < 50 && !gui_menu_shown(); i++) msleep(20);
        if (!gui_menu_shown()) return;
    }
    msleep(100);                    /* let ML finish its own first draw */
    menu_redraw_blocked = 1;
    clrscr();
    pal_set();
    ui_open = 1;
    ui_draw();
}

static void ui_do_close(int choose, int leave_menu)
{
    if (!ui_open) return;
    ui_open = 0;
    if (choose) soup_ui_choose(ui.stock);
    clrscr();
    pal_restore();
    menu_redraw_blocked = 0;
    if (leave_menu && gui_menu_shown()) gui_stop_menu();
}

static void soup_ui_task(void *unused)
{
    (void)unused;
    for (;;) {
        int r = ui_req;
        if (r) {
            ui_req = 0;
            if (r == 1 && !ui_open) ui_do_open();
            else if (r == 2) ui_do_close(1, 1);
            else if (r == 3) ui_do_close(0, 1);
        }
        if (ui_open) {
            if (!gui_menu_shown()) {
                /* the menu went away under us (power save, playback...) */
                ui_open = 1;
                ui_do_close(0, 0);
            } else if (ui_dirty) {
                ui_draw();
            }
            msleep(20);
        } else {
            msleep(50);
        }
    }
}

/* ML menu: SET on "Soup Film" opens the picker (the menu is already up). */
void soup_ui_open_from_menu(void)
{
    ui_req = 1;
}

unsigned int soup_ui_keypress(unsigned int key)
{
    if (!ui_open) {
        /* SET on the plain shooting screen: nothing else uses it there */
        if (key == MODULE_KEY_PRESS_SET && !gui_menu_shown() && !lv &&
            display_idle() && !is_movie_mode() && soup_ui_count() > 0) {
            ui_req = 1;
            return 0;
        }
        return 1;
    }

    switch (key) {
    case MODULE_KEY_WHEEL_LEFT:
    case MODULE_KEY_WHEEL_UP:
    case MODULE_KEY_PRESS_LEFT:
        se_ui_key_press(&ui, SE_KEY_LEFT);
        ui_dirty = 1;
        return 0;
    case MODULE_KEY_WHEEL_RIGHT:
    case MODULE_KEY_WHEEL_DOWN:
    case MODULE_KEY_PRESS_RIGHT:
        se_ui_key_press(&ui, SE_KEY_RIGHT);
        ui_dirty = 1;
        return 0;
    case MODULE_KEY_PRESS_SET:
        ui_req = 2;
        return 0;
    case MODULE_KEY_TRASH:
    case MODULE_KEY_MENU:
        ui_req = 3;
        return 0;
    case MODULE_KEY_PRESS_HALFSHUTTER:
        ui_req = 3;
        return 1;                  /* and let the camera focus */
    default:
        return 0;                  /* nothing else reaches ML's menu */
    }
}

void soup_ui_start(void)
{
    task_create("soup_ui", 0x1c, 0x2000, soup_ui_task, 0);
}
