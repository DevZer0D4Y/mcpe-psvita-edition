#pragma once

#include <stdint.h>
#include <stddef.h>
#include <psp2/types.h>

namespace mcpe {

struct LoadedImage {
    void *base;
    uint32_t image_size;
    uint32_t min_vaddr;
    uint32_t max_vaddr;

    SceUID mem_block;

    uint32_t relative_relocs;
    uint32_t unsupported_relocs;
    uint32_t undefined_symbols;
    uint32_t init_entries;

    /* Stage 2 diagnostics. */
    uint32_t plt_relocs;
    uint32_t imports_resolved;
    uint32_t imports_unresolved;
};


int prepare_runtime(void);

 through the MCPE-STAGE log markers.

int load_and_inspect(const char *path, LoadedImage *out);


void unload(LoadedImage *image);

} // namespace mcpe
