// cat::jsmath against the browser: Math.sin / cos / tan / atan2 / pow results recorded from Chromium
// (tools/js_math/record_chromium_math.cjs), plus special cases. Also reports, for information, how
// often this platform's C runtime differs from the browser (that is why the simulation uses jsmath).
#include "cat/js_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if(!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

bool same(double a, double b) {
    if(std::isnan(a) && std::isnan(b)) return true;
    return std::memcmp(&a, &b, sizeof a) == 0;  // also tells +0 from -0
}

std::int64_t ulps(double a, double b) {
    auto key = [](double v) {
        std::int64_t i;
        std::memcpy(&i, &v, sizeof i);
        return i < 0 ? std::numeric_limits<std::int64_t>::min() - i : i;
    };
    const std::int64_t d = key(a) - key(b);
    return d < 0 ? -d : d;
}

double eval(int f, double a, double b) {
    switch(f) {
        case 0: return cat::jsmath::sin(a);
        case 1: return cat::jsmath::cos(a);
        case 2: return cat::jsmath::tan(a);
        case 3: return cat::jsmath::atan2(a, b);
        default: return cat::jsmath::pow(a, b);
    }
}

double eval_crt(int f, double a, double b) {
    switch(f) {
        case 0: return std::sin(a);
        case 1: return std::cos(a);
        case 2: return std::tan(a);
        case 3: return std::atan2(a, b);
        default: return std::pow(a, b);
    }
}

void special_cases() {
    namespace jm = cat::jsmath;
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    check(same(jm::sin(-0.0), -0.0) && same(jm::sin(0.0), 0.0) && std::isnan(jm::sin(inf)) && std::isnan(jm::sin(nan)), "sin special values");
    check(jm::cos(0.0) == 1.0 && std::isnan(jm::cos(-inf)), "cos special values");
    check(same(jm::tan(-0.0), -0.0) && std::isnan(jm::tan(inf)), "tan special values");
    check(jm::sin(1e-300) == 1e-300 && jm::tan(-1e-12) == -1e-12 && jm::cos(1e-9) == 1.0, "tiny arguments");
    check(same(jm::atan2(0.0, -0.0), 3.141592653589793) && same(jm::atan2(-0.0, 0.0), -0.0) && jm::atan2(1.0, 0.0) == 1.5707963267948966,
          "atan2 zeros");
    check(jm::atan2(inf, -inf) == 2.356194490192345 && same(jm::atan2(-1.0, inf), -0.0) && std::isnan(jm::atan2(nan, 1.0)), "atan2 infinities");
    check(jm::pow(nan, 0.0) == 1.0 && std::isnan(jm::pow(1.0, inf)) && std::isnan(jm::pow(-1.0, -inf)) && std::isnan(jm::pow(1.0, nan)),
          "pow ECMAScript special cases");
    check(same(jm::pow(-0.0, 3.0), -0.0) && jm::pow(-0.0, -3.0) == -inf && jm::pow(-0.0, 0.5) == 0.0 && jm::pow(-inf, 0.5) == inf, "pow signed zero / inf");
    check(std::isnan(jm::pow(-8.0, 1.0 / 3.0)) && jm::pow(-2.0, 3.0) == -8.0 && jm::pow(-2.0, 4.0) == 16.0, "pow negative bases");
    check(jm::pow(2.0, 10.0) == 1024.0 && jm::pow(10.0, 15.0) == 1e15 && jm::pow(3.0, 33.0) == 5559060566555523.0, "pow exact integer results");
    check(jm::pow(2.0, 1024.0) == inf && jm::pow(2.0, -1075.0) == 0.0 && jm::pow(2.0, -1074.0) == 5e-324 && jm::pow(0.5, 1e10) == 0.0, "pow overflow / underflow");
    // symmetry and range
    for(double x = -50; x <= 50; x += 0.0137) {
        check(same(jm::sin(-x), -jm::sin(x)) && same(jm::cos(-x), jm::cos(x)) && same(jm::tan(-x), -jm::tan(x)), "odd / even symmetry");
        check(std::fabs(jm::sin(x)) <= 1 && std::fabs(jm::cos(x)) <= 1, "sin / cos range");
        if(failures) return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    special_cases();
    if(argc < 2) {
        std::printf("%s special cases\n", failures ? "FAIL" : "PASS");
        return failures ? 1 : 0;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<double> data;
    double record[4];
    while(in.read(reinterpret_cast<char*>(record), sizeof record)) data.insert(data.end(), record, record + 4);
    if(data.empty()) {
        std::printf("FAIL cannot read %s\n", argv[1]);
        return 1;
    }
    const char* const names[] = {"sin", "cos", "tan", "atan2", "pow"};
    long count[5] = {}, exact[5] = {}, crt_differs[5] = {};
    std::int64_t worst[5] = {};
    int shown = 0;
    for(std::size_t i = 0; i < data.size(); i += 4) {
        const int f = static_cast<int>(data[i]);
        const double a = data[i + 1], b = data[i + 2], expected = data[i + 3];
        const double got = eval(f, a, b);
        ++count[f];
        if(same(got, expected)) {
            ++exact[f];
        } else {
            const std::int64_t u = std::isnan(got) || std::isnan(expected) ? 1000 : ulps(got, expected);
            if(u > worst[f]) worst[f] = u;
            if(shown++ < 10) std::printf("  differs: %s(%.17g, %.17g) browser %.17g native %.17g\n", names[f], a, b, expected, got);
        }
        if(!same(eval_crt(f, a, b), expected)) ++crt_differs[f];
    }
    for(int f = 0; f < 5; ++f) {
        // sin, cos, tan, atan2: the browser is correctly rounded, so every record must match. pow: the
        // browser's (Linux: glibc's, max 0.52 ulp) and the correctly rounded value differ in about
        // 1 of 30,000 arguments; allow 1 ulp there, at most 1 per 1,000 records.
        const bool ok = f < 4 ? exact[f] == count[f] : worst[f] <= 1 && (count[f] - exact[f]) * 1000 <= count[f];
        std::printf("%s %s: %ld records, bit-exact %ld, worst %lld ulp; C runtime differs from the browser on %ld\n",
                    ok ? "PASS" : "FAIL", names[f], count[f], exact[f], static_cast<long long>(worst[f]), crt_differs[f]);
        if(!ok) ++failures;
    }
    std::printf("%s js_math against the browser (%zu records)\n", failures ? "FAIL" : "PASS", data.size() / 4);
    return failures ? 1 : 0;
}
