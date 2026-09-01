#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>

#include <stdio.h>

#include "mcpe_elf.h"

int main(void)
{
    printf(
        "[VITA-MCPE] bootstrap start\n"
    );


    printf(
        "[VITA-MCPE] loading original MCPE binary...\n"
    );

    const int runtime_result = mcpe::prepare_runtime();
    printf(
        "[VITA-MCPE] graphics runtime result=%d\n",
        runtime_result
    );

    if (runtime_result != 0) {
        printf(
            "[VITA-MCPE] graphics bootstrap failed; "
            "see ux0:data/mcpe/hardware_boot.log\n"
        );
        while (1)
            sceDisplayWaitVblankStart();
    }


    mcpe::LoadedImage image;


    int result =
        mcpe::load_and_inspect(
            "app0:data/libminecraftpe.so",
            &image
        );


    printf(
        "[VITA-MCPE] loader result=%d\n",
        result
    );


    if (result == 0) {

        printf(
            "[VITA-MCPE] MCPE image mapped successfully.\n"
        );


        printf(
            "[VITA-MCPE] Stage 2 resolver: "
            "PLT=%u resolved=%u unresolved=%u\n",
            image.plt_relocs,
            image.imports_resolved,
            image.imports_unresolved
        );


        printf(
            "[VITA-MCPE] init_array entries=%u\n",
            image.init_entries
        );


        printf(
            "[VITA-MCPE] Bootstrap returned from the MCPE execution path.\n"
        );

        printf(
            "[VITA-MCPE] Check the last MCPE-STAGE marker for runtime progress.\n"
        );

    } else {

        printf(
            "[VITA-MCPE] MCPE loader failed.\n"
        );


        printf(
            "[VITA-MCPE] Keeping framebuffer alive for diagnostics.\n"
        );
    }


    while (1)
        sceDisplayWaitVblankStart();


    return 0;
}
