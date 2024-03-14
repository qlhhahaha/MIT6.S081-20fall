#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char** argv) {
    int sleep_count = atoi(argv[1]);

    if (argc != 2) {
        fprintf(2, "Usage: sleep <number>\n");
    }

    sleep(sleep_count);

    exit(0);
}
