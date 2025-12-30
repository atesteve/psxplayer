#pragma once

#include "core/r3000.h"

#include <cstdint>

// clang-format off

union vol_reg_t {
    uint16_t bits;
    struct {
        uint16_t sweep_step  : 2;
        uint16_t sweep_shift : 5;
        uint16_t _           : 5;
        uint16_t sweep_phase : 1;
        uint16_t sweep_dir   : 1;
        uint16_t sweep_mode  : 1;
        uint16_t mode        : 1;
    } fields;
};

// clang-format on

struct voice_registers_t {
    vol_reg_t vol_left;
    vol_reg_t vol_right;
    uint16_t adpcm_sample_rate;
    uint16_t adpcm_start_addr;
    uint32_t adsr;
    int16_t adsr_vol;
    uint16_t adsr_repeat_addr;
};

union reverb_config_t {
    struct {
        uint16_t fb_src_a;
        uint16_t fb_src_b;
        int16_t iir_alpha;
        int16_t acc_coef_a;
        int16_t acc_coef_b;
        int16_t acc_coef_c;
        int16_t acc_coef_d;
        int16_t iir_coef;
        int16_t fb_alpha;
        int16_t fb_x;
        uint16_t iir_dest_a[2];
        uint16_t acc_src_a[2];
        uint16_t acc_src_b[2];
        uint16_t iir_src_a[2];
        uint16_t iir_dest_b[2];
        uint16_t acc_src_c[2];
        uint16_t acc_src_d[2];
        uint16_t iir_src_b[2];
        uint16_t mix_dest_a[2];
        uint16_t mix_dest_b[2];
        int16_t in_coef[2];
    } n;

    uint16_t r[sizeof(n) / sizeof(uint16_t)];
};

struct voice_current_vol_t {
    int16_t left;
    int16_t right;
};

static_assert(sizeof(voice_registers_t) == 16);

inline constexpr unsigned N_VOICES = 24;
inline constexpr r3000_ptr_t SPU_BASE = 0x1f801c00;

struct spu_regs_t {
    voice_registers_t voice[N_VOICES];
    vol_reg_t vol_left;
    vol_reg_t vol_right;
    int16_t reverb_vol_left;
    int16_t reverb_vol_right;
    uint32_t voice_key_on;
    uint32_t voice_key_off;
    uint32_t voice_pitch_mod_en;
    uint32_t voice_noise_mode;
    uint32_t voice_reberv_on;
    uint16_t _[3]; // Unused
    uint16_t reberv_base_addr;
    uint16_t irq_addr;
    uint16_t transfer_addr;
    uint16_t transfer_data;
    uint16_t control;
    uint16_t transfer_control;
    uint16_t status;
    uint16_t cd_audio_left;
    uint16_t cd_audio_right;
    uint16_t ext_vol_left;
    uint16_t ext_vol_right;
    uint16_t _[4]; // Unused
    reverb_config_t reverb_config;
    voice_current_vol_t voice_current_vol[N_VOICES];
};
