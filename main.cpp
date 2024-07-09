#include "hermes.hpp"
#include <cstring>

BENCHMARK(BM_memcpy, {{32, 128, 512, 2048, 4096, 8192, 65536}}) {
    size_t n = h.arg(0);
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

int main() {
    hermes::run_all();
    return 0;
}
