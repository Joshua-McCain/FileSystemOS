#include "softwaredisk.h"
#include <stdio.h>
#include "filesystem.h"

int main(int argc, char *argv[]) {
    if (! check_structure_alignment()) {
        printf("Check failed. Filesystem not initialized and should not be used. \n");
    } else {
        init_software_disk();
    }

}