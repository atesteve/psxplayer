/*
 * UPSE: the unix playstation sound emulator.
 *
 * Filename: upse-loader.h
 * Purpose: libupse: Loader management.
 *
 * Copyright (c) 2008 William Pitcock <nenolod@dereferenced.org>
 *
 * UPSE is free software, released under the GNU General Public License,
 * version 2.
 *
 * A copy of the GNU General Public License, version 2, is included in
 * the UPSE source kit as COPYING.
 *
 * UPSE is offered without any warranty of any kind, explicit or implicit.
 */

#ifndef __UPSE__LIBUPSE__UPSE_LOADER_H__GUARD
#define __UPSE__LIBUPSE__UPSE_LOADER_H__GUARD

typedef struct upse_iofuncs upse_iofuncs_t;
typedef struct upse_module upse_module_t;
typedef struct emulation_config emulation_control_t;

typedef upse_module_t *(*upse_loader_func_t)(void *fileptr, const char *path, const upse_iofuncs_t *iofuncs, emulation_control_t *control);

void upse_loader_add_magic(const char *bytes, int length, int offset, upse_loader_func_t loader);
void upse_loader_del_magic(const char *bytes, int length, int offset);

typedef struct {
    const char *bytes;
    int length;
    int offset;
    upse_loader_func_t func;
} upse_loader_t;

upse_loader_t *upse_loader_prepare_table(void);

#endif
