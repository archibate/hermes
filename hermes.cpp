#include "hermes.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
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

namespace {

std::vector<Entry> &entries() {
    static std::vector<Entry> instance;
    return instance;
}

int64_t get_cpu_freq() {
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

void setup_affinity() {
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

template <class T>
T find_median(T *begin, size_t n) {
    if (n % 2 == 0) {
        std::nth_element(begin, begin + n / 2, begin + n);
        std::nth_element(begin, begin + (n - 1) / 2, begin + n);
        return (begin[(n - 1) / 2] + begin[n / 2]) / 2;
    } else {
        std::nth_element(begin, begin + n / 2, begin + n);
        return begin[n / 2];
    }
}

}

int register_entry(Entry ent) {
    entries().push_back(ent);
    return 1;
}

void Reporter::run_entry(Entry const &ent, Options const &options) {
    size_t nargs = ent.args.size();
    if (nargs != 0) {
        std::vector<size_t> indices(nargs, 0);
        bool done;
        do {
            State state;
            state.max_time = (int64_t)(1000'000'000 * options.max_time);

            std::vector<int64_t> args(nargs);
            state.args = args.data();
            state.nargs = nargs;

            std::string new_name = ent.name;
            for (size_t i = 0; i < nargs; i++) {
                int64_t value = ent.args[i][indices[i]];
                args[i] = value;
                new_name += '/';
                new_name += std::to_string(value);
            }

            ent.func(state);
            report_state(new_name.c_str(), state, options);

            done = true;
            for (size_t i = 0; i < nargs; i++) {
                ++indices[i];
                if (indices[i] >= ent.args[i].size()) {
                    indices[i] = 0;
                    continue;
                } else {
                    done = false;
                    break;
                }
            }
        } while (!done);

    } else {
        State state;
        state.max_time = (int64_t)(1000'000'000 * options.max_time);

        ent.func(state);
        report_state(ent.name, state, options);
    }
}

void Reporter::report_state(const char *name, State &state, Options const &options) {
    int64_t count = 0;
    int64_t max = INT64_MIN;
    int64_t min = INT64_MAX;
    double sum = 0;
    double square_sum = 0;

    std::vector<int64_t> records;
    for (State::Chunk *chunk = &state.rec_chunks; chunk; chunk = chunk->next) {
        for (size_t i = 0; i < chunk->count; ++i) {
            records.push_back(chunk->records[i]);
        }
    }
    for (auto const &x: records) {
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

        size_t nrecs = records.size();
        std::vector<bool> ok(nrecs);

        switch (options.deviationFilter) {
        case DeviationFilter::None:
            break;
        case DeviationFilter::MAD:
            {
                int64_t median = find_median(records.data(), records.size());
                std::vector<int64_t> deviations(nrecs);
                auto dev = deviations.data();
                for (auto const &x: records) {
                    *dev++ = std::abs(x - median);
                }
                int64_t mad = find_median(deviations.data(), deviations.size());
                auto okit = ok.begin();
                for (auto const &x: records) {
                    *okit++ = std::abs(x - median) <= 12 * mad;
                }
            }
            break;

        case DeviationFilter::Sigma:
            {
                auto okit = ok.begin();
                for (auto const &x: records) {
                    *okit++ = std::abs(x - avg) <= 3 * stddev;
                }
            }
            break;
        }

        auto okit = ok.begin();
        for (auto const &x: records) {
            if (*okit++) {
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

    int64_t med = find_median(records.data(), records.size());
    med -= options.fixedOverhead;
    avg -= options.fixedOverhead;
    min -= options.fixedOverhead;
    max -= options.fixedOverhead;

    write_report(name, Reporter::Row{
        med, avg, stddev, min, max, count,
    });
}

void Reporter::run_all(Options const &options) {
    setup_affinity();
    for (Entry const &ent: entries()) {
        run_entry(ent, options);
    }
}

void _do_not_optimize_impl(void *p) {
    (void)p;
}

std::vector<int64_t> linear_range(int64_t begin, int64_t end, int64_t step) {
    std::vector<int64_t> ret;
    for (int64_t i = begin; i <= end; i += step) {
        ret.push_back(i);
    }
    return ret;
}

std::vector<int64_t> log_range(int64_t begin, int64_t end, double factor) {
    std::vector<int64_t> ret;
    if (factor >= 1) {
        int64_t last_i = begin - 1;
        for (double d = begin; d <= end; d *= factor) {
            int64_t i = int64_t(d);
            if (last_i != i)
                ret.push_back(i);
            i = last_i;
        }
    }
    return ret;
}

namespace {

struct ConsoleReporter : Reporter {
    ConsoleReporter() {
        printf("%28s %10s %10s %6s %9s\n", "name", "med", "avg", "std", "n");
        printf("-------------------------------------------------------------------\n");
    }

    void write_report(const char *name, Reporter::Row const &row) override {
        printf("%28s %10ld %10.0lf %6.0lf %9ld\n",
               name, row.med, row.avg, row.stddev, row.count);
    }
};

struct CSVReporter : Reporter {
    FILE *fp;

    CSVReporter(const char *filename) {
        fp = fopen(filename, "w");
        if (!fp)
            abort();
        fprintf(fp, "name,avg,std,min,max,n\n");
    }

    CSVReporter(CSVReporter &&) = delete;

    ~CSVReporter() {
        fclose(fp);
    }

    void write_report(const char *name, Reporter::Row const &row) override {
        fprintf(fp, "%s,%lf,%lf,%ld,%ld,%ld\n",
               name, row.avg, row.stddev, row.min, row.max, row.count);
    }
};

struct SVGReporter : Reporter {
    FILE *fp;

    struct Bar {
        std::string name;
        double value;
        double height;
        double delta_up;
        double delta_down;
        double stddev_max;
        double stddev_min;
    };

    std::vector<Bar> bars;

    SVGReporter(const char *filename) {
        fp = fopen(filename, "w");
        if (!fp)
            abort();
    }

    SVGReporter(SVGReporter &&) = delete;

    ~SVGReporter() {
        double w = 1920;
        double h = 1080;
        fprintf(fp, "<svg viewBox=\"0 0 %lf %lf\" xmlns=\"http://www.w3.org/2000/svg\">\n", w, h);
        fprintf(fp, "<style type=\"text/css\">\n"
                    ".bar {\n"
                    "  stroke: #000000;\n"
                    "  fill: #779977;\n"
                    "}\n"
                    ".tip {\n"
                    "  stroke: #223344;\n"
                    "  fill: none;\n"
                    "}\n"
                    ".stddev {\n"
                    "  stroke: none;\n"
                    "  fill: #223344;\n"
                    "  opacity: 0.25;\n"
                    "}\n"
                    ".label {\n"
                    "  font-family: monospace;\n"
                    "  color: #000000;\n"
                    "  dominant-baseline: central;\n"
                    "  text-anchor: middle;\n"
                    "}\n"
                    ".value {\n"
                    "  font-family: monospace;\n"
                    "  color: #000000;\n"
                    "  dominant-baseline: central;\n"
                    "  text-anchor: middle;\n"
                    "}\n"
                    "</style>\n");
        fprintf(fp, "<rect x=\"0\" y=\"0\" width=\"%lf\" height=\"%lf\" fill=\"lightgray\" />\n", w, h);

        double xscale = (w - 200) / (bars.size() - 1);
        double ymax = 0;
        for (size_t i = 0; i < bars.size(); i++) {
            ymax = std::max(ymax, bars[i].height + bars[i].delta_up);
        }
        double yscale = (h - 120) / ymax;
        for (size_t i = 0; i < bars.size(); i++) {
            double x = 100 + i * xscale;
            double y = h - 60;
            double bar_width = 0.65 * xscale;
            double bar_height = bars[i].height * yscale;
            double avg_width = 0.35 * xscale;
            double tip_width = 0.15 * xscale;
            double tip_height_up = bars[i].delta_up * yscale;
            double tip_height_down = bars[i].delta_down * yscale;
            fprintf(fp, "<rect class=\"bar\" x=\"%lf\" y=\"%lf\" width=\"%lf\" height=\"%lf\" />\n",
                   x - bar_width * 0.5, y - bar_height, bar_width, bar_height);
            fprintf(fp, "<rect class=\"stddev\" x=\"%lf\" y=\"%lf\" width=\"%lf\" height=\"%lf\" />\n",
                    x - avg_width * 0.5, y - bars[i].stddev_max * yscale, avg_width,
                    (bars[i].stddev_max - bars[i].stddev_min) * yscale);
            fprintf(fp, "<line class=\"tip\" x1=\"%lf\" y1=\"%lf\" x2=\"%lf\" y2=\"%lf\" />\n",
                   x, y - bar_height - tip_height_up, x, y - bar_height - tip_height_down);
            fprintf(fp, "<line class=\"tip\" x1=\"%lf\" y1=\"%lf\" x2=\"%lf\" y2=\"%lf\" />\n",
                   x - tip_width * 0.5, y - bar_height - tip_height_up, x + tip_width * 0.5, y - bar_height - tip_height_up);
            fprintf(fp, "<line class=\"tip\" x1=\"%lf\" y1=\"%lf\" x2=\"%lf\" y2=\"%lf\" />\n",
                   x - tip_width * 0.5, y - bar_height - tip_height_down, x + tip_width * 0.5, y - bar_height - tip_height_down);
            fprintf(fp, "<text class=\"value\" x=\"%lf\" y=\"%lf\">%.0lf</text>\n",
                   x, y - bar_height - 20, bars[i].value);
            fprintf(fp, "<text class=\"label\" x=\"%lf\" y=\"%lf\">%s</text>\n",
                   x, h - 30, bars[i].name.c_str());
        }
        fprintf(fp, "</svg>\n");
        fclose(fp);
    }

    void write_report(const char *name, Reporter::Row const &row) override {
        auto axis_scale = [] (double x) {
            if (x <= 0)
                return x;
            return std::log(x);
        };
        auto height = axis_scale(row.med);
        auto height_up = axis_scale(row.max);
        auto height_down = axis_scale(row.min);
        auto stddev_up = axis_scale(row.avg + row.stddev);
        auto stddev_down = axis_scale(row.avg - row.stddev);
        bars.push_back({
            name,
            (double)row.med,
            height,
            height_up - height,
            height_down - height,
            stddev_up,
            stddev_down,
        });
    }
};

struct NullReporter : Reporter {
    void write_report(const char *name, Reporter::Row const &row) override {
        (void)name;
        (void)row;
    }
};

struct MultipleReporter : Reporter {
    std::vector<std::unique_ptr<Reporter>> reporters;

    MultipleReporter(std::vector<Reporter *> const &rs) {
        for (auto *r: rs) {
            reporters.emplace_back(r);
        }
    }

    void write_report(const char *name, Reporter::Row const &row) override {
        for (auto &r: reporters) {
            r->write_report(name, row);
        }
    }
};

}

Reporter *makeConsoleReporter() {
    return new ConsoleReporter();
}

Reporter *makeCSVReporter(const char *path) {
    return new CSVReporter(path);
}

Reporter *makeSVGReporter(const char *path) {
    return new SVGReporter(path);
}

Reporter *makeNullReporter() {
    return new NullReporter();
}

Reporter *makeMultipleReporter(std::vector<Reporter *> const &reporters) {
    return new MultipleReporter(reporters);
}

}
