#ifndef SOLOIST_DUCK_SHM_H
#define SOLOIST_DUCK_SHM_H

#include <stdint.h>

#define SOLOIST_DUCK_MAGIC 0x5344554BU /* "SDUK" */
#define SOLOIST_DUCK_DEFAULT_PATH "/tmp/soloist_duck"
#define SOLOIST_DUCK_SHM_SIZE 64

struct soloist_duck_shm {
    uint32_t magic;
    volatile uint32_t active;
    volatile int64_t until_ms; /* CLOCK_MONOTONIC deadline for fail-safe auto-recovery */
};

#endif
