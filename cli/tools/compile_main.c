#include "tinypy_cli/cli.h"

int main(int argc, char **argv) {
    int return_value_1 = tinypy_cli_compile_run(argc, argv);
    return return_value_1;
}
