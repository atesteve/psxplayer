#ifndef __PSX_EMUCONTROL_H__
#define __PSX_EMUCONTROL_H__

#include <stdint.h>
#include <stdbool.h>

/* Copied from kernel.h and compiler-gcc.h */

/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(char[1 - 2 * !!(e)]) - 1)

/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a) \
  BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), typeof(&a[0])))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define SAMPLE_WINDOW_SIZE (1024)

typedef struct emulation_config {
    struct {
        float speed_multiplier;
        struct {
            float vol_multiplier;
            bool mute;
        } channel[24];
    } input;

    struct {
        int16_t window_l[SAMPLE_WINDOW_SIZE];
        int16_t window_r[SAMPLE_WINDOW_SIZE];
        int window_p;

        struct {
            int16_t l[SAMPLE_WINDOW_SIZE];
            int16_t r[SAMPLE_WINDOW_SIZE];
            int p;
        } channel_window[24];
    } output;
} emulation_control_t;

#endif // __PSX_EMUCONTROL_H__
