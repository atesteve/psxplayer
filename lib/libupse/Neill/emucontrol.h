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
#ifndef _MSC_VER
#define __must_be_array(a) \
  BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), typeof(&a[0])))
#else
#define __must_be_array(a) 0
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define SAMPLE_WINDOW_SIZE (1024)

typedef struct out_channel {
    int16_t l[SAMPLE_WINDOW_SIZE];
    int16_t r[SAMPLE_WINDOW_SIZE];
    int p;
    bool fired;
    uint32_t sample_addr;
    uint32_t loop_addr;
    uint32_t pitch;
} out_channel_t;

typedef enum mem_access_size {
    MEM_ACCESS_WORD,
    MEM_ACCESS_HALF,
    MEM_ACCESS_BYTE,
} mem_access_size_t;

typedef struct upse_module_instance upse_module_instance_t;
typedef void (*jal_hook_ptr)(void* hook_data, upse_module_instance_t *ins);
typedef void (*sw_hook_ptr)(void* hook_data, upse_module_instance_t *ins, mem_access_size_t size, uint32_t addr, uint32_t data);

typedef struct emulation_config {
    struct {
        float speed_multiplier;
        bool endless_play;
        struct {
            float vol_multiplier;
            bool mute;
        } channel[24];
    } input;

    struct {
        int16_t window_l[SAMPLE_WINDOW_SIZE];
        int16_t window_r[SAMPLE_WINDOW_SIZE];
        int window_p;

        out_channel_t channel[24];
    } output;

    struct {
        void* data;
        jal_hook_ptr jal;
        sw_hook_ptr sw;
    } hooks;
} emulation_control_t;

#endif // __PSX_EMUCONTROL_H__
