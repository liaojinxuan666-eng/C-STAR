#include <stdint.h>
struct Pair { int a; int b; };
int host_sum(struct Pair *p) { return p->a + p->b; }
uint64_t host_inc(uint64_t v) { return v + 1; }
