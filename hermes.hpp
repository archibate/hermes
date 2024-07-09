#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <list>
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

enum class DeviationFilter {
    None,
    Sigma,
    MAD,
};

struct Options {
    double max_time = 0.5;
    DeviationFilter deviation_filter = DeviationFilter::MAD;
};

struct State {
private:
    friend struct Reporter;

    int64_t t0 = 0;
    int64_t time_elapsed = 0;
    int64_t max_time = 1;
    struct Chunk {
        static const size_t kMaxPerChunk = 65536;
        size_t count = 0;
        Chunk *next = nullptr;
        int64_t records[kMaxPerChunk]{};
    };
    int64_t iteration_count = 0;
    Chunk *rec_chunks = new Chunk();
    Chunk *rec_chunks_tail = rec_chunks;
    int64_t pause_t0 = 0;
    int64_t const *args = nullptr;
    size_t nargs = 0;
    int64_t items_processed = 0;
    DeviationFilter deviation_filter = DeviationFilter::None;

public:
    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE int64_t arg(size_t i) const {
        if (i > nargs)
            return 0;
        return args[i];
    }

    State(Options const &options) {
        set_max_time(options.max_time);
        set_deviation_filter(options.deviation_filter);
    }

    ~State() {
        Chunk *current = rec_chunks;
        while (current != nullptr) {
            Chunk *next = current->next;
            delete current;
            current = next;
        }
    }

    State(State &&) = delete;
    State &operator=(State &&) = delete;

    struct iterator {
    private:
        State &state;
        bool ok;

    public:
        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE iterator(State &state_, bool ok_) : state(state_), ok(ok_) {
            if (ok)
                state.start();
        }

        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE iterator &operator++() {
            state.stop();
            ok = state.next();
            if (ok)
                state.start();
            return *this;
        }

        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE iterator operator++(int) {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE int operator*() const noexcept {
            return 0;
        }

        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE bool operator!=(iterator const &that) const noexcept {
            return ok != that.ok;
        }

        HERMES_ALWAYS_INLINE HERMES_OPTIMIZE bool operator==(iterator const &that) const noexcept {
            return ok == that.ok;
        }
    };

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE iterator begin() {
        return iterator{*this, true};
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE iterator end() {
        return iterator{*this, false};
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void start() {
        sfence();
        t0 = now();
        lfence();
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void pause() {
        pause_t0 = now();
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void resume() {
        int64_t t1 = now();
        t0 -= t1 - pause_t0;
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void stop() {
        mfence();
        stop(now());
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void start(int64_t t) {
        t0 = t;
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE void stop(int64_t t) {
        int64_t dt = t - t0;
        time_elapsed += dt;
        auto &chunk = *rec_chunks_tail;
        chunk.records[chunk.count++] = dt;
        if (chunk.count == chunk.kMaxPerChunk) {
            Chunk *new_node = new Chunk();
            rec_chunks_tail->next = new_node;
            rec_chunks_tail = new_node;
        }
        ++iteration_count;
    }

    HERMES_ALWAYS_INLINE HERMES_OPTIMIZE bool next() {
        bool ok = time_elapsed <= max_time;
#if __GNUC__
        return __builtin_expect(ok, 1);
#else
        return ok;
#endif
    }

    int64_t iterations() const noexcept {
        return iteration_count;
    }

    int64_t times() const noexcept {
        return time_elapsed;
    }

    void set_max_time(double t) {
        max_time = (int64_t)(t * 1000000000);
    }

    void set_deviation_filter(DeviationFilter f) {
        deviation_filter = f;
    }

    void set_items_processed(int64_t num) {
        items_processed = num;
    }
};

struct Entry {
    void (*func)(State &);
    const char *name;
    std::vector<std::vector<int64_t>> args{};
};

int register_entry(Entry ent);

struct Reporter {
    struct Row {
        double med;
        double avg;
        double stddev;
        double min;
        double max;
        int64_t count;
    };

    void run_entry(Entry const &ent, Options const &options = {});
    void run_all(Options const &options = {});

    virtual void report_state(const char *name, State &state);
    virtual void write_report(const char *name, Row const &row) = 0;

    virtual ~Reporter() = default;
};

Reporter *makeConsoleReporter();
Reporter *makeCSVReporter(const char *path);
Reporter *makeSVGReporter(const char *path);
Reporter *makeNullReporter();
Reporter *makeMultipleReporter(std::vector<Reporter *> const &reporters);

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
#define BENCHMARK(name, ...) \
extern "C" void name(::hermes::State &); \
static int _defbench_##name = ::hermes::register_entry({name, #name, __VA_ARGS__}); \
extern "C" HERMES_NOINLINE void name(::hermes::State &h)

std::vector<int64_t> linear_range(int64_t begin, int64_t end, int64_t step = 1);
std::vector<int64_t> log_range(int64_t begin, int64_t end, double factor = 2);

}
