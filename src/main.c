// ChargeBay N64 — bouncing box demo (pipeline prover, rdpq API)
// printf goes to emulator stdout via libdragon console.
#include <libdragon.h>
#include <stdio.h>

int main(void)
{
    debug_init_emulog();   // route printf/assert to emulator console (mupen64plus stdout)
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    rdpq_text_register_font(1, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_VAR));

    float x = 40, y = 40, vx = 2.2f, vy = 1.4f;
    long frame = 0;
    printf("[n64] chargebay demo boot: 320x240 rdpq, box bounces\n");

    while (1) {
        surface_t *disp = display_get();
        rdpq_attach(disp, NULL);

        // dusk-blue background
        rdpq_clear((color_t){40, 40, 72, 255});

        x += vx; y += vy;
        if (x < 0)   { x = 0;   vx = -vx; }
        if (x > 304) { x = 304; vx = -vx; }
        if (y < 0)   { y = 0;   vy = -vy; }
        if (y > 224) { y = 224; vy = -vy; }

        // box with border: outer pale, inner dark inset (fill mode required)
        rdpq_set_mode_fill((color_t){232, 232, 240, 255});
        rdpq_fill_rectangle((int)x, (int)y, (int)x + 16, (int)y + 16);
        rdpq_set_fill_color((color_t){32, 32, 48, 255});
        rdpq_fill_rectangle((int)x + 2, (int)y + 2, (int)x + 14, (int)y + 14);

        char msg[48];
        snprintf(msg, sizeof(msg), "frame=%ld box=(%.0f,%.0f)", frame, x, y);
        rdpq_text_print(NULL, 1, 8, 8, msg);

        rdpq_detach_show();

        frame++;
        if ((frame % 60) == 0)
            printf("[n64] frame=%ld box=(%.1f,%.1f)\n", frame, x, y);
    }
}
