#include <display.h>
#include <dragonfs.h>
#include <graphics.h>
#include <joypad.h>
#include <rdpq.h>
#include <asset.h>
#include <surface.h>

#include "game.h"
#include "render.h"

static RflInput read_input(void)
{
    static int last_stick_dir = 0;
    RflInput input = {0};

    joypad_poll();

    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

    int stick_dir = 0;
    if (inputs.stick_x <= -42) {
        stick_dir = -1;
    } else if (inputs.stick_x >= 42) {
        stick_dir = 1;
    }

    input.left = pressed.d_left || pressed.c_left || (stick_dir == -1 && last_stick_dir != -1);
    input.right = pressed.d_right || pressed.c_right || (stick_dir == 1 && last_stick_dir != 1);
    input.jump = pressed.a || pressed.c_up || pressed.d_up;
    input.slide = pressed.b || pressed.z;
    input.squat = held.d_down || held.c_down || held.b || held.z || inputs.stick_y <= -42;
    input.start = pressed.start;

    last_stick_dir = stick_dir;
    return input;
}

int main(void)
{
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    asset_init_compression(2);
    dfs_init(DFS_DEFAULT_LOCATION);
    rdpq_init();
    joypad_init();
    graphics_set_default_font();
    rfl_render_init();
    surface_t zbuffer = surface_alloc(FMT_RGBA16, 320, 240);

    RflGame game;
    rfl_game_init(&game);

    while (1) {
        RflInput input = read_input();
        rfl_game_update(&game, &input);

        surface_t *display = display_get();
        rfl_render(display, &zbuffer, &game);
        display_show(display);
    }

    return 0;
}
