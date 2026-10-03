#include <stdint.h>
#include <stdio.h>

int host_add(int a, int b) { return a + b; }
uint64_t host_mix(uint64_t a, uint64_t b) { return a * 10 + b; }
void host_ping(void) { puts("ffi ping: ok"); }
