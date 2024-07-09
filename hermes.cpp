#include "hermes.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>
#if __linux__
#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#elif __APPLE__
#include <mach/mach_time.h>
#elif _WIN32
#include <windows.h>
#endif

namespace hermes {

static std::vector<Entry> &entries() {
    static std::vector<Entry> instance;
    return instance;
}

static int64_t get_cpu_freq() {
#if __linux__
    int fd;
    int result;
    fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd != -1) {
        char buf[4096];
        ssize_t n;
        n = read(fd, buf, sizeof buf);
        if (__builtin_expect(n, 1) > 0) {
            char *mhz = (char *)memmem(buf, n, "cpu MHz", 7);
            if (mhz != NULL) {
                char *endp = buf + n;
                int seen_decpoint = 0;
                int ndigits = 0;
                while (mhz < endp && (*mhz < '0' || *mhz > '9') && *mhz != '\n')
                    ++mhz;
                while (mhz < endp && *mhz != '\n') {
                    if (*mhz >= '0' && *mhz <= '9') {
                        result *= 10;
                        result += *mhz - '0';
                        if (seen_decpoint)
                            ++ndigits;
                    } else if (*mhz == '.')
                        seen_decpoint = 1;

                    ++mhz;
                }
                while (ndigits++ < 6)
                    result *= 10;
            }
        }

        close(fd);
    }

    return result;
#elif __APPLE__
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    return info.denom * 1000000000ULL / info.numer;
#elif _WIN32
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart;
#endif
}

static void setup_affinity() {
    unsigned int cpu = 0;
#if __linux__
    getcpu(&cpu, nullptr);
#elif _WIN32
    cpu = GetCurrentProcessorNumber();
#endif
#if __linux__
    std::string path = "/sys/devices/system/cpu/cpu";
    path += std::to_string(cpu);
    path += "/cpufreq/scaling_governor";
    FILE *fp = fopen(path.c_str(), "r");
    if (fp) {
        char buf[64];
        fgets(buf, sizeof(buf), fp);
        fclose(fp);
        if (strncmp(buf, "performance", 11)) {
            fprintf(stderr, "\033[33;1mWARNING: CPU scaling detected! Run this to disable:\n"
                    "sudo cpupower frequency-set --governor performance\n\033[0m");
            fp = fopen(path.c_str(), "w");
            if (fp) {
                fputs("performance", fp);
                fclose(fp);
            }
        }
    }
#endif
#if __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    sched_setaffinity(gettid(), sizeof(cpuset), &cpuset);
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_BATCH);
    sched_setscheduler(gettid(), SCHED_BATCH, &param);
#elif _WIN32
    SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

int register_entry(Entry ent) {
    entries().push_back(ent);
    return 1;
}

template <class T>
static T find_median(T *begin, size_t n) {
    if (n % 2 == 0) {
        std::nth_element(begin, begin + n / 2, begin + n);
        std::nth_element(begin, begin + (n - 1) / 2, begin + n);
        return (begin[(n - 1) / 2] + begin[n / 2]) / 2;
    } else {
        std::nth_element(begin, begin + n / 2, begin + n);
        return begin[n / 2];
    }
}

void run_entry(Entry const &ent, Options const &options) {
    static const size_t kChunkSize = 1024;

    State state;

    ent.func(state);

    int64_t count = 0;
    int64_t max = INT64_MIN;
    int64_t min = INT64_MAX;
    double sum = 0;
    double square_sum = 0;
    for (int64_t x: state.rec) {
        sum += x;
        square_sum += x * x;
        max = std::max(x, max);
        min = std::min(x, min);
        ++count;
    }

    double avg = sum / count;
    double square_avg = square_sum / count;
    double stddev = std::sqrt(square_avg - avg * avg);

    if (options.deviationFilter != DeviationFilter::None) {
        sum = 0;
        square_sum = 0;
        count = 0;
        max = INT64_MIN;
        min = INT64_MAX;

        std::vector<bool> ok(state.rec.size());

        switch (options.deviationFilter) {
        case DeviationFilter::None:
            break;
        case DeviationFilter::MAD:
            {
                int64_t median = find_median(state.rec.data(), state.rec.size());
                std::vector<int64_t> deviations(state.rec.size());
                for (size_t i = 0; i < state.rec.size(); ++i) {
                    deviations[i] = std::abs(state.rec[i] - median);
                }
                int64_t mad = find_median(deviations.data(), deviations.size());
                for (size_t i = 0; i < state.rec.size(); ++i) {
                    ok[i] = std::abs(state.rec[i] - median) <= 3 * mad;
                }
            }
            break;

        case DeviationFilter::Sigma:
            {
                for (size_t i = 0; i < state.rec.size(); ++i) {
                    ok[i] = std::abs(state.rec[i] - avg) <= 3 * stddev;
                }
            }
            break;
        }

        for (size_t i = 0; i < state.rec.size(); ++i) {
            int64_t x = state.rec[i];
            if (ok[i]) {
                sum += x;
                square_sum += x * x;
                max = std::max(x, max);
                min = std::min(x, min);
                ++count;
            }
        }

        avg = sum / count;
        square_avg = square_sum / count;
        stddev = std::sqrt(square_avg - avg * avg);
    }
    avg -= options.fixedOverhead;
    min -= options.fixedOverhead;
    max -= options.fixedOverhead;
    printf("%20s %10.0lf %6.0lf %10ld %10ld %9ld\n", ent.name, avg, stddev, min, max, count);
}

void run_all(Options const &options) {
    setup_affinity();
    printf("%20s %10s %6s %10s %10s %9s\n", "name", "avg", "std", "min", "max", "n");
    printf("----------------------------------------------------------------------\n");
    for (Entry const &ent: entries()) {
        run_entry(ent, options);
    }
}

void _do_not_optimize_impl(void *p) {
    (void)p;
}

}
