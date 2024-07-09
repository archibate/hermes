#include "hermes.hpp"
#include <cstring>
#include <memory>

BENCHMARK(BM_memcpy, {hermes::log_range(1 << 10, 1 << 26, 2)}) {
    size_t n = h.arg(0);
    char *dst = (char *)malloc(n);
    char *src = (char *)malloc(n);
    memset(dst, 0, n);
    memset(src, 0, n);
    for (auto _: h) {
        memcpy(dst, src, n);
        hermes::do_not_optimize(dst);
    }
    h.set_items_processed(h.iterations() * n);
    free(src);
    free(dst);
}

/* BENCHMARK(BM_memcpy_page_align, {hermes::log_range(1 << 18, 1 << 28, 4)}) { */
/*     size_t n = h.arg(0); */
/*     char *dst = (char *)aligned_alloc(4096, n); */
/*     char *src = (char *)aligned_alloc(4096, n); */
/*     memset(dst, 0, n); */
/*     memset(src, 0, n); */
/*     for (auto _: h) { */
/*         memcpy(dst, src, n); */
/*         hermes::do_not_optimize(dst); */
/*     } */
/*     h.set_items_processed(h.iterations() * n); */
/*     free(src); */
/*     free(dst); */
/* } */
/*  */
/* BENCHMARK(BM_memcpy_non_align, {hermes::log_range(1 << 18, 1 << 28, 4)}) { */
/*     size_t n = h.arg(0); */
/*     char *dst = (char *)malloc(n); */
/*     char *src = (char *)malloc(n); */
/*     memset(dst, 0, n); */
/*     memset(src, 0, n); */
/*     for (auto _: h) { */
/*         memcpy(dst, src, n); */
/*         hermes::do_not_optimize(dst); */
/*     } */
/*     h.set_items_processed(h.iterations() * n); */
/*     free(src); */
/*     free(dst); */
/* } */

/* BENCHMARK(BM_read) { */
/*     uintptr_t buf; */
/*     buf = (uintptr_t)&buf; */
/*     uintptr_t register rax asm("rax") = buf; */
/*     for (auto _: h) { */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*         rax = *(volatile uintptr_t *)rax; */
/*     } */
/*     h.set_items_processed(h.iterations() * 8); */
/* } */
/*  */
/* BENCHMARK(BM_write) { */
/*     uintptr_t buf; */
/*     buf = (uintptr_t)&buf; */
/*     uintptr_t register rax asm("rax") = buf; */
/*     for (auto _: h) { */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*         *(volatile uintptr_t *)rax = rax; */
/*     } */
/*     h.set_items_processed(h.iterations() * 8); */
/* } */

int main() {
    std::unique_ptr<hermes::Reporter> rep(hermes::makeMultipleReporter({
            hermes::makeConsoleReporter(),
            hermes::makeSVGReporter("bench.svg"),
    }));
    rep->run_all();
    return 0;
}
