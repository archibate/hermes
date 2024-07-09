#include "hermes.hpp"
#include <cstring>

BENCHMARK(BM_memcpy) {
    size_t n = 8192;
    char *dst = (char *)malloc(n);
    char *src = (char *)malloc(n);
    do {
        h.begin();
        memcpy(dst, src, n);
        hermes::do_not_optimize(dst);
        h.end();
    } while (h.next());
    free(src);
    free(dst);
}

BENCHMARK(BM_memcpy_no_opt) {
    size_t n = 8192;
    char *dst = (char *)malloc(n);
    char *src = (char *)malloc(n);
    volatile auto p0 = memcpy;
    do {
        auto p = p0;
        h.begin();
        p(dst, src, n);
        hermes::do_not_optimize(dst);
        h.end();
    } while (h.next());
    free(src);
    free(dst);
}

/* BENCHMARK(BM_write1) { */
/*     int x = 0; */
/*     do { */
/*         h.begin(); */
/*         hermes::do_not_optimize(x); */
/*         x = 1; */
/*         hermes::do_not_optimize(x); */
/*         h.end(); */
/*     } while (h.next()); */
/* } */
/*  */
/* BENCHMARK(BM_nothing) { */
/*     do { */
/*         h.begin(); */
/*         h.end(); */
/*     } while (h.next()); */
/* } */


int main() {
    hermes::run_all();
    return 0;
}
