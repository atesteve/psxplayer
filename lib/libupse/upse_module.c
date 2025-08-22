/*
 * UPSE: the unix playstation sound emulator.
 *
 * Filename: upse-module.h
 * Purpose: libupse: Module loading and probing.
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

#include "upse-internal.h"

#include <assert.h>

static upse_loader_t *upse_loader_table_ = NULL;

upse_loader_func_t
upse_module_probe(void *fileptr, const upse_iofuncs_t *funcs)
{
    int i, previous_offset, previous_length;
    char *bytes = NULL;

    if (!fileptr)
        return NULL;

    if (!upse_loader_table_)
        upse_loader_table_ = upse_loader_prepare_table();

    previous_length = 0;
    previous_offset = upse_loader_table_[0].offset;
    funcs->seek_impl(fileptr, previous_offset, SEEK_SET);

    for (i = 0; upse_loader_table_[i].bytes != NULL; i++)
    {
        upse_loader_t *loader = &upse_loader_table_[i];

        _DEBUG("trying loader with magic: %s length: %d", loader->bytes, loader->length);

        if (loader->offset != previous_offset)
        {
            previous_length = 0;
            funcs->seek_impl(fileptr, previous_offset, SEEK_SET);
        }

        if (loader->length != previous_length || bytes == NULL)
        {
            if (bytes)
                free(bytes);

            bytes = calloc(sizeof(char), loader->length);
            funcs->seek_impl(fileptr, loader->offset, SEEK_SET);
            funcs->read_impl(bytes, loader->length, 1, fileptr);
        }

        if (!memcmp(bytes, loader->bytes, loader->length))
        {
            free(bytes);
            return loader->func;
        }
    }

    return NULL;
}

int
upse_module_is_supported(void *fileptr, const upse_iofuncs_t *funcs)
{
    return upse_module_probe(fileptr, funcs) != NULL;
}

int
upse_file_is_supported(char *file, const upse_iofuncs_t *funcs)
{
    void *fileptr;
    int ret;

    fileptr = funcs->open_impl(file, "rb");
    ret = upse_module_is_supported(fileptr, funcs);
    funcs->close_impl(fileptr);

    return ret;
}

upse_module_t *
upse_module_open(const char *file, const upse_iofuncs_t *funcs)
{
    void *fileptr;
    upse_module_t *ret;
    upse_loader_func_t functor;

    fileptr = funcs->open_impl(file, "rb");

    if (!fileptr)
        return NULL;

    functor = upse_module_probe(fileptr, funcs);

    if (!functor)
    {
        funcs->close_impl(fileptr);
        return NULL;
    }

    funcs->seek_impl(fileptr, 0, SEEK_SET);
    ret = functor(fileptr, file, funcs);
    funcs->close_impl(fileptr);

    return ret;
}

void
upse_module_close(upse_module_t *mod)
{
    if (!mod)
        return;

    upse_free_psf_metadata(mod->metadata); /* XXX */
    upse_ps1_shutdown(&mod->instance);
    free(mod);
}

upse_snapshot_t *
upse_module_take_snapshot(upse_module_t *mod, upse_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        snapshot = (upse_snapshot_t *)malloc(sizeof(upse_snapshot_t));
    }
    upse_ps1_take_snapshot(&mod->instance, snapshot);
    return snapshot;
}

void
upse_module_restore_snapshot(upse_module_t *mod, upse_snapshot_t *snapshot)
{
    upse_ps1_restore_snapshot(&mod->instance, snapshot);
}

void
upse_module_destroy_snapshot(upse_snapshot_t *snapshot)
{
    free(snapshot);
}

void *
upse_snapshot_get_buffer(upse_snapshot_t *snapshot, size_t size)
{
    assert((snapshot->bump_ptr + size <= sizeof(snapshot->storage)) &&
                "Snapshot storage size is not bit enough");
    void *ret = snapshot->storage + snapshot->bump_ptr;
    // Round to the next multiple of 8.
    if (size % 8 != 0) {
        size = (size & (~7ul)) + 8;
    }
    snapshot->bump_ptr += size;
    return ret;
}

extern upse_module_t *upse_load_psf(void *fileptr, const char *path, const upse_iofuncs_t *funcs);
extern upse_module_t *upse_load_psf2(void *fileptr, const char *path, const upse_iofuncs_t *funcs);

void
upse_module_init(void)
{
    upse_loader_add_magic("PSF\x01", 4, 0, upse_load_psf);
    upse_loader_add_magic("PSF\x02", 4, 0, upse_load_psf2);
}
