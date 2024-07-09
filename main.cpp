#include "hermes.hpp"
#include <cstring>
#include <memory>

BENCHMARK(BM_memcpy, {hermes::log_range(32, 65536, 2)}) {
    size_t n = h.arg(0);
    char *dst = (char *)malloc(n);
    char *src = (char *)malloc(n);
    memset(dst, 0, n);
    memset(src, 0, n);
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
    std::unique_ptr<hermes::Reporter> rep(hermes::makeMultipleReporter({
            hermes::makeConsoleReporter(),
            hermes::makeSVGReporter("/tmp/out.svg"),
    }));
    rep->run_all();
    return 0;
}
