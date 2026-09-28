// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/r3000.h"

#include <cstdint>

// clang-format off

union vol_reg_t {
    uint16_t raw;
    struct {
        uint16_t sweep_step  : 2;
        uint16_t sweep_shift : 5;
        uint16_t             : 5;
        uint16_t sweep_phase : 1;
        uint16_t sweep_dir   : 1;
        uint16_t sweep_mode  : 1;
        uint16_t mode        : 1;
    } sweep_mode;
    struct {
        int16_t volume : 15;
        int16_t mode   : 1;
    } direct_mode;
};

static_assert(sizeof(vol_reg_t) == sizeof(uint16_t));

union control_reg_t {
    uint16_t raw;
    struct {
        uint16_t cd_audio_en      : 1;
        uint16_t ext_audio_en     : 1;
        uint16_t cd_audio_reverb  : 1;
        uint16_t ext_audio_reverb : 1;
        uint16_t transfer_mode    : 2;
        uint16_t irq_en           : 1;
        uint16_t reverb_en        : 1;
        uint16_t noise_freq_step  : 2;
        uint16_t noise_freq_shift : 4;
        uint16_t unmute           : 1;
        uint16_t enable           : 1;
    } fields;
};

static_assert(sizeof(control_reg_t::fields) == sizeof(uint16_t));

union status_reg_t {
    uint16_t raw;
    struct {
        uint16_t cd_audio_en      : 1;
        uint16_t ext_audio_en     : 1;
        uint16_t cd_audio_reverb  : 1;
        uint16_t ext_audio_reverb : 1;
        uint16_t transfer_mode    : 2;
        uint16_t irq_flag         : 1;
        uint16_t dma_req          : 1;
        uint16_t dma_write_req    : 1;
        uint16_t dma_read_req     : 1;
        uint16_t transfer_busy    : 1;
        uint16_t capture_phase    : 1;
        uint16_t                  : 4;
    } fields;
};

static_assert(sizeof(status_reg_t::fields) == sizeof(uint16_t));

union adsr_reg_t {
    uint32_t raw;
    struct {
        uint32_t sustain_level : 4;
        uint32_t decay_shift   : 4;
        uint32_t attack_step   : 2;
        uint32_t attack_shift  : 5;
        uint32_t attack_exp    : 1;
        uint32_t release_shift : 5;
        uint32_t release_exp   : 1;
        uint32_t sustain_step  : 2;
        uint32_t sustain_shift : 5;
        uint32_t               : 1;
        uint32_t sustain_dir   : 1;
        uint32_t sustain_exp   : 1;
    } fields;
};

static_assert(sizeof(adsr_reg_t::fields) == sizeof(uint32_t));

// clang-format on

struct spu_compressed_addr_t {
    uint16_t raw;

    size_t get() const { return size_t(raw) << 2; }
    void set(size_t addr) { raw = addr >> 2; }
};

struct voice_registers_t {
    vol_reg_t vol_left;
    vol_reg_t vol_right;
    uint16_t adpcm_sample_rate;
    spu_compressed_addr_t adpcm_start_addr;
    adsr_reg_t adsr;
    int16_t adsr_vol;
    spu_compressed_addr_t adpcm_repeat_addr;
};

union reverb_config_t {
    struct {
        spu_compressed_addr_t d_apf1;
        spu_compressed_addr_t d_apf2;
        int16_t v_iir;
        int16_t v_comb1;
        int16_t v_comb2;
        int16_t v_comb3;
        int16_t v_comb4;
        int16_t v_wall;
        int16_t v_apf1;
        int16_t v_apf2;
        spu_compressed_addr_t m_same[2];
        spu_compressed_addr_t m_comb1[2];
        spu_compressed_addr_t m_comb2[2];
        spu_compressed_addr_t d_same[2];
        spu_compressed_addr_t m_diff[2];
        spu_compressed_addr_t m_comb3[2];
        spu_compressed_addr_t m_comb4[2];
        spu_compressed_addr_t d_diff[2];
        spu_compressed_addr_t m_apf1[2];
        spu_compressed_addr_t m_apf2[2];
        int16_t vol_in[2];
    } n;

    uint16_t r[sizeof(n) / sizeof(uint16_t)];
};

static_assert(sizeof(reverb_config_t) == 64);

struct voice_current_vol_t {
    int16_t left;
    int16_t right;
};

static_assert(sizeof(voice_registers_t) == 16);

inline constexpr unsigned N_VOICES = 24;
inline constexpr size_t SPU_RAM_SIZE = 512 * 1024; // 512 KiB
inline constexpr size_t SPU_RAM_SIZE_WORDS = SPU_RAM_SIZE / sizeof(uint16_t); // 256k words
inline constexpr r3000_ptr_t SPU_BASE = 0x1f801c00;

struct spu_regs_t {
    voice_registers_t voice[N_VOICES];
    int16_t mixer_vol_left;
    int16_t mixer_vol_right;
    int16_t reverb_vol_left;
    int16_t reverb_vol_right;
    uint32_t voice_key_on;
    uint32_t voice_key_off;
    uint32_t voice_fmod_en;
    uint32_t voice_noise_mode;
    uint32_t voice_reverb_on;
    uint16_t _unused1[3];
    spu_compressed_addr_t reverb_base_addr;
    spu_compressed_addr_t irq_addr;
    spu_compressed_addr_t transfer_addr;
    uint16_t transfer_data;
    control_reg_t control;
    uint16_t transfer_control;
    status_reg_t status;
    uint16_t cd_audio_left;
    uint16_t cd_audio_right;
    uint16_t ext_vol_left;
    uint16_t ext_vol_right;
    uint16_t _unused2[4];
    reverb_config_t reverb_config;
    voice_current_vol_t voice_current_vol[N_VOICES];
};
