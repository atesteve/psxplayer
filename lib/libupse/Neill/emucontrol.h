#ifndef __PSX_EMUCONTROL_H__
#define __PSX_EMUCONTROL_H__

#include <stdint.h>

typedef struct emulation_config {
    struct {
        float speed_multiplier;
    } input;

    struct {

    } output;
} emulation_control_t;

#endif // __PSX_EMUCONTROL_H__
