#ifndef __PSX_EMUCONTROL_H__
#define __PSX_EMUCONTROL_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct emulation_config {
    struct {
        float speed_multiplier;
        struct {
            float vol_multiplier;
            bool mute;
        } channel[24];
    } input;

    struct {

    } output;
} emulation_control_t;

#endif // __PSX_EMUCONTROL_H__
