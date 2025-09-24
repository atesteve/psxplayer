#pragma once

#define xva_arg(ap, type) xva_arg_impl<type>(ap)

struct xva_list;

template <typename T>
T xva_arg_impl(xva_list& ap);

int vxprintf(void (*func)(const char*, int, void*),
             void* arg,
             const char* format,
             xva_list& ap);
