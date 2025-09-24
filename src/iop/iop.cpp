#include "iop.h"

std::unordered_map<PSF2::iop_table_key, PSF2::iop_handler> PSF2::builtin_iop_fns = {
    {{"stdio", 4}, &PSF2::iop_printf},
};
