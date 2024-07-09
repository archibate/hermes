#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if __x86_64__ || __amd64__
#include <x86intrin.h>
#elif _M_AMD64 || _M_IX86
#include <emmintrin.h>
extern "C" int64_t __rdtsc();
#pragma intrinsic(__rdtsc)
#elif _M_ARM64 || _M_ARM64EC
#include <intrin.h>
#elif __powerpc64__
#include <builtins.h>
#else
#include <chrono>
#endif

namespace hermes {

#if __GNUC__ && !__clang__
#define HERMES_OPTIMIZE __attribute__((__optimize__("O3")))
#define HERMES_NO_OPTIMIZE __attribute__((__optimize__("O0")))
#elif _MSC_VER && !__clang__
#define HERMES_OPTIMIZE __pragma(optimize("g", on))
#define HERMES_NO_OPTIMIZE __pragma(optimize("g", off))
#else
#define HERMES_OPTIMIZE
#define HERMES_NO_OPTIMIZE
#endif
#if __GNUC__ || __clang__
#define HERMES_ALWAYS_INLINE __attribute__((__always_inline__))
#define HERMES_NOINLINE __attribute__((__noinline__))
#define HERMES_RESTRICT __restrict
#elif _MSC_VER && !__clang__
#define HERMES_ALWAYS_INLINE __forceinline
#define HERMES_NOINLINE __declspec(noinline)
#define HERMES_RESTRICT __restrict
#else
#define HERMES_ALWAYS_INLINE
#define HERMES_NOINLINE
#define HERMES_RESTRICT
#endif

HERMES_ALWAYS_INLINE HERMES_OPTIMIZE inline void mfence() {
#if __x86_64__ || __amd64__ || _M_AMD64 || _M_IX86
    _mm_mfence();
#elif _M_ARM64 || _M_ARM64EC
    _ReadWriteBarrier();
#elif __aarch64__
    asm volatile("dmb ish" ::: "memory");
#elif __powerpc64__
    __builtin_ppc_isync();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

HERMES_ALWAYS_INLINE HERMES_OPTIMIZE inline void sfence() {
#if __x86_64__ || __amd64__ || _M_AMD64 || _M_IX86
    _mm_sfence();
#elif _M_ARM64 || _M_ARM64EC
    _WriteBarrier();
#elif __aarch64__
    asm volatile ("dmb ishst" ::: "memory");
#elif __powerpc64__
    __builtin_ppc_isync();
#else
    std::atomic_signal_fence(std::memory_order_release);
#endif
}

HERMES_ALWAYS_INLINE HERMES_OPTIMIZE inline void lfence() {
#if __x86_64__ || __amd64__ || _M_AMD64 || _M_IX86
    _mm_lfence();
#elif _M_ARM64 || _M_ARM64EC
    _ReadBarrier();
#elif __aarch64__
    asm volatile ("isb" ::: "memory");
#elif __powerpc64__
    __builtin_ppc_isync();
#else
    std::atomic_signal_fence(std::memory_order_acquire);
#endif
}

HERMES_ALWAYS_INLINE HERMES_OPTIMIZE inline int64_t now() {
#if __x86_64__ || __amd64__ || _M_AMD64 || _M_IX86
    int64_t t = __rdtsc();
    return t;
#elif _M_ARM64 || _M_ARM64EC
    return _ReadStatusRegister(ARM64_PMCCNTR_EL0);
#elif __aarch64__
    return __builtin_arm_rsr64("cntvct_el0");
#elif __powerpc64__
    return __builtin_ppc_get_timebase();
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

struct State {
    int64_t t0 = 0;
    int64_t time_elapsed = 0;
    int64_t max_time = 1000 * 1000 * 500;
    std::vector<int64_t> rec;

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE State() = default;
    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE ~State() = default;

    State(State &&) = delete;
    State &operator=(State &&) = delete;

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void begin() {
        sfence();
        t0 = now();
        lfence();
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void end() {
        mfence();
        end(now());
    }

    HERMES_ALWAYS_INLINE void begin(int64_t t) {
        t0 = t;
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void end(int64_t t) {
        int64_t dt = t - t0;
        time_elapsed += dt;
        rec.push_back(dt);
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE bool next() {
        bool ok = time_elapsed <= max_time;
#if __GNUC__
        return __builtin_expect(ok, 1);
#else
        return ok;
#endif
    }
};

struct Entry {
    void (*func)(State &);
    const char *name;
};

enum class DeviationFilter {
    None,
    Sigma,
    MAD,
};

struct Options {
    DeviationFilter deviationFilter = DeviationFilter::MAD;
#if __x86_64__ || _M_AMD64
#if __GNUC__
    int64_t fixedOverhead = 44;
#else
    int64_t fixedOverhead = 52;
#endif
#else
    int64_t fixedOverhead = 0;
#endif
};

int register_entry(Entry ent);
void run_entry(Entry const &ent, Options const &options = {});
void run_all(Options const &options = {});

void _do_not_optimize_impl(void *p);

template <class T>
HERMES_OPTIMIZE void do_not_optimize(T &&t) {
#if __GNUC__
    asm volatile ("" : "+m" (t) :: "memory");
#else
    _do_not_optimize_impl(std::addressof(t));
#endif
}

#define BENCHMARK_DEFINE(name) \
static int _defbench_##name = ::hermes::register_entry({name, #name});
#define BENCHMARK(name) \
extern "C" void name(::hermes::State &); \
static int _defbench_##name = ::hermes::register_entry({name, #name}); \
extern "C" HERMES_NOINLINE void name(::hermes::State &h)

}
