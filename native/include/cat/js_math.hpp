#pragma once
// Platform-independent, correctly rounded sin, cos, tan, atan2 and pow: the results of the browser.
//
// Why: the legacy game runs in Chromium, whose V8 returns correctly rounded results for Math.sin,
// cos, tan, atan2 and pow (checked against Playwright's Chromium 153: every one of 605 inputs on
// which glibc differs was correctly rounded in Chromium, see tests/js_math_tests.cpp). The C runtime
// functions are not correctly rounded and differ per platform: glibc is off by one bit on about
// 0.06 % of arguments, MSVC's CRT elsewhere. Over thousands of 120 Hz ticks one-bit differences grew
// to 1.3e-9 in a Windows replay of a legacy tape (tolerance 1e-9).
//
// Method: argument reduction and evaluation in double-double arithmetic (about 2^-100 relative
// error), then rounding of the double-double sum to the nearest double. The result is the correctly
// rounded one unless the exact value lies within about 2^-100 (relative) of the midpoint between two
// doubles, which for a random argument has a probability of about 2^-47. Only IEEE double additions,
// multiplications and divisions are used (no FMA contraction: the libraries that use this header are
// built with -ffp-contract=off / MSVC /fp:precise), so every compiler and operating system computes
// the same bits. Not covered (rare, still deterministic): sin / cos / tan of |x| > 2^19 * pi/2 use
// fdlibm's reduction (about 2^-100 too), pow results in the subnormal range may differ in the last bit.
//
// The reduction of very large trigonometric arguments is ported from fdlibm 5.3:
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this software is freely granted, provided that
//   this notice is preserved.
// The special cases of pow and atan2 follow fdlibm / ECMAScript. Tables: tools/js_math/gen_tables.py.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cat::jsmath {
namespace detail {

struct dd {
    double hi, lo;
};

#include "cat/generated/js_math_tables.inc"

inline std::int32_t high_word(double x) { return static_cast<std::int32_t>(std::bit_cast<std::uint64_t>(x) >> 32); }
inline std::uint32_t low_word(double x) { return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x)); }
inline double from_words(std::int32_t hi, std::uint32_t lo) {
    return std::bit_cast<double>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32) | lo);
}

// ---- double-double arithmetic (Dekker / Knuth; operands well inside the double range) ----
inline dd two_sum(double a, double b) {
    const double s = a + b, bb = s - a;
    return {s, (a - (s - bb)) + (b - bb)};
}
inline dd fast_two_sum(double a, double b) {  // |a| >= |b| or a == 0
    const double s = a + b;
    return {s, b - (s - a)};
}
inline void split(double a, double& hi, double& lo) {
    const double c = 134217729.0 * a;  // 2^27 + 1
    hi = c - (c - a);
    lo = a - hi;
}
inline dd two_prod(double a, double b) {
    const double p = a * b;
    double ah, al, bh, bl;
    split(a, ah, al);
    split(b, bh, bl);
    return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}
inline dd neg(dd a) { return {-a.hi, -a.lo}; }
inline dd add(dd a, dd b) {
    dd s = two_sum(a.hi, b.hi);
    const dd t = two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = fast_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return fast_two_sum(s.hi, s.lo);
}
inline dd add(dd a, double b) {
    dd s = two_sum(a.hi, b);
    s.lo += a.lo;
    return fast_two_sum(s.hi, s.lo);
}
inline dd sub(dd a, dd b) { return add(a, neg(b)); }
inline dd mul(dd a, dd b) {
    dd p = two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return fast_two_sum(p.hi, p.lo);
}
inline dd mul(dd a, double b) {
    dd p = two_prod(a.hi, b);
    p.lo += a.lo * b;
    return fast_two_sum(p.hi, p.lo);
}
inline dd div(dd a, dd b) {
    const double q1 = a.hi / b.hi;
    dd r = sub(a, mul(b, q1));
    const double q2 = r.hi / b.hi;
    r = sub(r, mul(b, q2));
    const double q3 = r.hi / b.hi;
    return add(fast_two_sum(q1, q2), q3);
}

// __kernel_rem_pio2 for prec == 2 (double result in y[0] + y[1]); used only for |x| > 2^19 * pi/2.
inline int kernel_rem_pio2(const double* x, double* y, int e0, int nx) {
    static constexpr std::int32_t ipio2[] = {
        0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041, 0xFE5163,
        0xABDEBB, 0xC561B7, 0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
        0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C,
        0x845F8B, 0xBDF928, 0x3B1FF8, 0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
        0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5, 0xF17B3D, 0x0739F7, 0x8A5292,
        0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
        0x916138, 0xA8248A, 0xAB07EA, 0x9D471C, 0x0DF701, 0xE52E30, 0x79B995, 0x7CCF27, 0xCFB90A,
        0x665016, 0x19AFBA, 0xD4BA14};
    static constexpr double PIo2[] = {
        1.57079625129699707031e+00, 7.54978941586159635335e-08, 5.39030252995776476554e-15,
        3.28200341580791294123e-22, 1.27065575308067607349e-29, 1.22933308981111328932e-36,
        2.73370053816464559624e-44, 2.16741683877804819444e-51};
    constexpr double zero = 0.0, one = 1.0, two24 = 1.67772160000000000000e+07, twon24 = 5.96046447753906250000e-08;
    constexpr int jk = 4;  // init_jk[prec = 2]
    constexpr int jp = jk;
    std::int32_t iq[20];
    double f[20], fq[20], q[20], z, fw;
    int i, j, k, n, ih, carry;

    const int jx = nx - 1;
    int jv = (e0 - 3) / 24;
    if (jv < 0) jv = 0;
    int q0 = e0 - 24 * (jv + 1);
    j = jv - jx;
    const int m = jx + jk;
    for (i = 0; i <= m; i++, j++) f[i] = j < 0 ? zero : static_cast<double>(ipio2[j]);
    for (i = 0; i <= jk; i++) {
        for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
        q[i] = fw;
    }
    int jz = jk;
    for (;;) {  // recompute
        for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
            fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
            iq[i] = static_cast<std::int32_t>(z - two24 * fw);
            z = q[j - 1] + fw;
        }
        z = std::scalbn(z, q0);
        z -= 8.0 * std::floor(z * 0.125);
        n = static_cast<int>(z);
        z -= static_cast<double>(n);
        ih = 0;
        if (q0 > 0) {
            i = iq[jz - 1] >> (24 - q0);
            n += i;
            iq[jz - 1] -= i << (24 - q0);
            ih = iq[jz - 1] >> (23 - q0);
        } else if (q0 == 0) {
            ih = iq[jz - 1] >> 23;
        } else if (z >= 0.5) {
            ih = 2;
        }
        if (ih > 0) {
            n += 1;
            carry = 0;
            for (i = 0; i < jz; i++) {
                j = iq[i];
                if (carry == 0) {
                    if (j != 0) { carry = 1; iq[i] = 0x1000000 - j; }
                } else {
                    iq[i] = 0xffffff - j;
                }
            }
            if (q0 == 1) iq[jz - 1] &= 0x7fffff;
            else if (q0 == 2) iq[jz - 1] &= 0x3fffff;
            if (ih == 2) {
                z = one - z;
                if (carry != 0) z -= std::scalbn(one, q0);
            }
        }
        if (z == zero) {
            j = 0;
            for (i = jz - 1; i >= jk; i--) j |= iq[i];
            if (j == 0) {  // need more terms
                for (k = 1; iq[jk - k] == 0; k++) {}
                for (i = jz + 1; i <= jz + k; i++) {
                    f[jx + i] = static_cast<double>(ipio2[jv + i]);
                    for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
                    q[i] = fw;
                }
                jz += k;
                continue;
            }
        }
        break;
    }
    if (z == 0.0) {
        jz -= 1;
        q0 -= 24;
        while (iq[jz] == 0) { jz--; q0 -= 24; }
    } else {
        z = std::scalbn(z, -q0);
        if (z >= two24) {
            fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
            iq[jz] = static_cast<std::int32_t>(z - two24 * fw);
            jz += 1;
            q0 += 24;
            iq[jz] = static_cast<std::int32_t>(fw);
        } else {
            iq[jz] = static_cast<std::int32_t>(z);
        }
    }
    fw = std::scalbn(one, q0);
    for (i = jz; i >= 0; i--) { q[i] = fw * static_cast<double>(iq[i]); fw *= twon24; }
    for (i = jz; i >= 0; i--) {
        for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) fw += PIo2[k] * q[i + k];
        fq[jz - i] = fw;
    }
    fw = 0.0;
    for (i = jz; i >= 0; i--) fw += fq[i];
    y[0] = ih == 0 ? fw : -fw;
    fw = fq[0] - fw;
    for (i = 1; i <= jz; i++) fw += fq[i];
    y[1] = ih == 0 ? fw : -fw;
    return n & 7;
}

// __ieee754_rem_pio2(x, y): x - n * pi/2 = y[0] + y[1]; returns n.
inline int rem_pio2(double x, double* y) {
    static constexpr std::int32_t npio2_hw[] = {
        0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
        0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
        0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
        0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB};
    constexpr double zero = 0.0, half = 0.5, two24 = 1.67772160000000000000e+07,
                     invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
                     pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11,
                     pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
                     pio2_3t = 8.47842766036889956997e-32;
    double z, w, t, r, fn;
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; return 0; }  // |x| <= pi/4
    if (ix < 0x4002d97c) {                                   // |x| < 3pi/4
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3ff921fb) {
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            } else {  // near pi/2: 33 + 53 bits of pi
                z -= pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        }
        z = x + pio2_1;
        if (ix != 0x3ff921fb) {
            y[0] = z + pio2_1t;
            y[1] = (z - y[0]) + pio2_1t;
        } else {
            z += pio2_2;
            y[0] = z + pio2_2t;
            y[1] = (z - y[0]) + pio2_2t;
        }
        return -1;
    }
    if (ix <= 0x413921fb) {  // |x| <= 2^19 * (pi/2), medium size
        t = std::fabs(x);
        const int n = static_cast<int>(t * invpio2 + half);
        fn = static_cast<double>(n);
        r = t - fn * pio2_1;
        w = fn * pio2_1t;  // 1st round, good to 85 bits
        if (n < 32 && ix != npio2_hw[n - 1]) {
            y[0] = r - w;
        } else {
            const std::int32_t j = ix >> 20;
            y[0] = r - w;
            std::int32_t i = j - ((high_word(y[0]) >> 20) & 0x7ff);
            if (i > 16) {  // 2nd iteration, good to 118 bits
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - ((high_word(y[0]) >> 20) & 0x7ff);
                if (i > 49) {  // 3rd iteration, 151 bits
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
        return n;
    }
    if (ix >= 0x7ff00000) { y[0] = y[1] = x - x; return 0; }  // inf or NaN
    // z = scalbn(|x|, ilogb(x) - 23)
    const int e0 = (ix >> 20) - 1046;
    z = from_words(ix - static_cast<std::int32_t>(static_cast<std::uint32_t>(e0) << 20), low_word(x));
    double tx[3];
    for (int i = 0; i < 2; i++) {
        tx[i] = static_cast<double>(static_cast<std::int32_t>(z));
        z = (z - tx[i]) * two24;
    }
    tx[2] = z;
    int nx = 3;
    while (nx > 1 && tx[nx - 1] == zero) nx--;
    const int n = kernel_rem_pio2(tx, y, e0, nx);
    if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
    return n;
}

// ---- trigonometric functions ----

// sin and cos of r (|r| <= pi/4 + 2^-6): table of j/64 plus Taylor series of the rest (|t| <= 2^-7).
inline void sin_cos(dd r, dd& s, dd& c) {
    const bool negative = r.hi < 0;
    if (negative) r = neg(r);
    const int j = static_cast<int>(r.hi * 64.0 + 0.5);
    const dd t = fast_two_sum(r.hi - static_cast<double>(j) / 64.0, r.lo);  // exact (Sterbenz)
    const dd t2 = mul(t, t);
    dd p = kSinCoef[5];
    for (int k = 4; k >= 0; --k) p = add(mul(p, t2), kSinCoef[k]);
    const dd sin_t = add(t, mul(mul(t, t2), p));
    dd q = kCosCoef[6];
    for (int k = 5; k >= 0; --k) q = add(mul(q, t2), kCosCoef[k]);
    const dd cos_t = add(mul(t2, q), 1.0);
    if (j == 0) {
        s = sin_t;
        c = cos_t;
    } else {
        s = add(mul(kSinTable[j], cos_t), mul(kCosTable[j], sin_t));
        c = sub(mul(kCosTable[j], cos_t), mul(kSinTable[j], sin_t));
    }
    if (negative) s = neg(s);
}

// x = n * pi/2 + r with |r| <= pi/4 (slightly more); x finite.
inline int reduce(double x, dd& r) {
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb) {  // |x| <= pi/4
        r = {x, 0.0};
        return 0;
    }
    const double ax = std::fabs(x);
    int n;
    if (ix <= 0x413921fb) {  // |x| <= 2^19 * pi/2: Cody-Waite with six 33-bit pieces of pi/2
        n = static_cast<int>(ax * kTwoOverPi + 0.5);
        const double fn = static_cast<double>(n);
        r = {ax - fn * kPio2Chunk[0], 0.0};  // exact: fn * chunk has <= 53 bits, Sterbenz
        for (int i = 1; i < 6; ++i) r = add(r, -(fn * kPio2Chunk[i]));
    } else {
        double y[2];
        n = rem_pio2(ax, y);
        r = two_sum(y[0], y[1]);
    }
    if (hx < 0) {
        r = neg(r);
        n = -n;
    }
    return n;
}

}  // namespace detail

inline double sin(double x) {
    const std::int32_t ix = detail::high_word(x) & 0x7fffffff;
    if (ix >= 0x7ff00000) return x - x;  // NaN for +-inf and NaN
    if (ix < 0x3e400000) return x;       // |x| < 2^-27: sin x rounds to x (also +-0)
    detail::dd r, s, c;
    const int n = detail::reduce(x, r);
    detail::sin_cos(r, s, c);
    switch (n & 3) {
        case 0: return s.hi;
        case 1: return c.hi;
        case 2: return -s.hi;
        default: return -c.hi;
    }
}

inline double cos(double x) {
    const std::int32_t ix = detail::high_word(x) & 0x7fffffff;
    if (ix >= 0x7ff00000) return x - x;
    if (ix < 0x3e400000) return 1.0;  // |x| < 2^-27: cos x rounds to 1
    detail::dd r, s, c;
    const int n = detail::reduce(x, r);
    detail::sin_cos(r, s, c);
    switch (n & 3) {
        case 0: return c.hi;
        case 1: return -s.hi;
        case 2: return -c.hi;
        default: return s.hi;
    }
}

inline double tan(double x) {
    const std::int32_t ix = detail::high_word(x) & 0x7fffffff;
    if (ix >= 0x7ff00000) return x - x;
    if (ix < 0x3e400000) return x;  // |x| < 2^-27: tan x rounds to x
    detail::dd r, s, c;
    const int n = detail::reduce(x, r);
    detail::sin_cos(r, s, c);
    return (n & 1) == 0 ? detail::div(s, c).hi : -detail::div(c, s).hi;
}

namespace detail {

// atan(u) for 0 <= u <= 1: atan(j/64) + atan((u - j/64) / (1 + u j/64)), series of the rest.
inline dd atan01(dd u) {
    const int j = static_cast<int>(u.hi * 64.0 + 0.5);
    const double cj = static_cast<double>(j) / 64.0;
    const dd num = fast_two_sum(u.hi - cj, u.lo);  // exact (Sterbenz)
    const dd den = add(mul(u, cj), 1.0);
    const dd w = j == 0 ? u : div(num, den);
    const dd w2 = mul(w, w);
    dd p = kOddRecip[7];  // atan w = w - w^3/3 + w^5/5 - ... = w + w^3 p, p = sum (-1)^(k+1) w^2k / (2k+3)
    for (int k = 6; k >= 0; --k) p = add(mul(p, w2), (k & 1) == 0 ? neg(kOddRecip[k]) : kOddRecip[k]);
    return add(kAtanTable[j], add(w, mul(mul(w, w2), p)));
}

}  // namespace detail

inline double atan2(double y, double x) {
    constexpr double tiny = 1.0e-300, pi_o_4 = 7.8539816339744827900e-01;
    using detail::dd;
    const std::int32_t hx = detail::high_word(x), ix = hx & 0x7fffffff;
    const std::uint32_t lx = detail::low_word(x);
    const std::int32_t hy = detail::high_word(y), iy = hy & 0x7fffffff;
    const std::uint32_t ly = detail::low_word(y);
    if ((static_cast<std::uint32_t>(ix) | ((lx | (0u - lx)) >> 31)) > 0x7ff00000u ||
        (static_cast<std::uint32_t>(iy) | ((ly | (0u - ly)) >> 31)) > 0x7ff00000u)
        return x + y;  // NaN
    const int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);  // 2 * sign(x) + sign(y)
    const double pi = detail::kPi.hi, pi_o_2 = detail::kPiHalf.hi;
    if ((static_cast<std::uint32_t>(iy) | ly) == 0) {  // y == +-0
        switch (m) {
            case 0:
            case 1: return y;
            case 2: return pi + tiny;
            default: return -pi - tiny;
        }
    }
    if ((static_cast<std::uint32_t>(ix) | lx) == 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;  // x == +-0
    if (ix == 0x7ff00000) {
        if (iy == 0x7ff00000) {
            switch (m) {
                case 0: return pi_o_4 + tiny;
                case 1: return -pi_o_4 - tiny;
                case 2: return 3.0 * pi_o_4 + tiny;
                default: return -3.0 * pi_o_4 - tiny;
            }
        }
        switch (m) {
            case 0: return 0.0;
            case 1: return -0.0;
            case 2: return pi + tiny;
            default: return -pi - tiny;
        }
    }
    if (iy == 0x7ff00000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;

    const double abs_x = std::fabs(x), abs_y = std::fabs(y);
    double ax = abs_x, ay = abs_y;
    const int ex = std::ilogb(ax), ey = std::ilogb(ay);
    const int e = ex > ey ? ex : ey;
    if (e > 500 || e < -500) {  // keep the double-double products in range (exact scaling)
        ax = std::ldexp(ax, -e);
        ay = std::ldexp(ay, -e);
    }
    dd z;
    if (ay <= ax) {
        if (ey - ex < -60) z = {abs_y / abs_x, 0.0};  // atan t rounds like t (unscaled: subnormal results)
        else z = detail::atan01(detail::div({ay, 0.0}, {ax, 0.0}));
    } else {
        if (ex - ey < -60) z = detail::add(detail::kPiHalf, -(abs_x / abs_y));
        else z = detail::sub(detail::kPiHalf, detail::atan01(detail::div({ax, 0.0}, {ay, 0.0})));
    }
    if (hx < 0) z = detail::sub(detail::kPi, z);
    return hy < 0 ? -z.hi : z.hi;
}

namespace detail {

// ln(ax) for finite ax > 0.
inline dd log_dd(double ax) {
    int e;
    double m = std::frexp(ax, &e);  // [0.5, 1)
    m *= 2.0;
    e -= 1;
    if (m > 1.4142135623730951) {
        m *= 0.5;
        e += 1;
    }  // m in [sqrt(1/2), sqrt(2)]
    const int j = static_cast<int>(std::floor((m - 1.0) * 64.0 + 0.5));
    const double c = 1.0 + static_cast<double>(j) / 64.0;
    const dd s = div({m - c, 0.0}, two_sum(m, c));  // m - c exact (Sterbenz)
    const dd s2 = mul(s, s);
    dd p = kOddRecip[7];  // ln(m / c) = 2 atanh(s) = 2 (s + s^3/3 + s^5/5 + ...)
    for (int k = 6; k >= 0; --k) p = add(mul(p, s2), kOddRecip[k]);
    const dd ln_mc = mul(add(s, mul(mul(s, s2), p)), 2.0);
    return add(mul(kLn2, static_cast<double>(e)), add(kLnTable[j - kLnTableFirst], ln_mc));
}

// exp(z), rounded to double; |z.hi| <= 760.
inline double exp_dd(dd z) {
    if (z.hi > 709.8) return std::numeric_limits<double>::infinity();
    if (z.hi < -745.2) return 0.0;
    const int n = static_cast<int>(std::floor(z.hi * kSixtyFourOverLn2 + 0.5));
    const double fn = static_cast<double>(n);
    dd r = two_sum(z.hi - fn * kLn2Over64Chunk[0], z.lo);  // exact difference (Sterbenz)
    r = add(r, -(fn * kLn2Over64Chunk[1]));                 // exact product
    r = add(r, -(fn * kLn2Over64Chunk[2]));
    const int j = n & 63, k = (n - j) / 64;
    dd q = kInvFact[10];  // exp r = 1 + r + r^2 (1/2! + r/3! + ... + r^10/12!)
    for (int i = 9; i >= 0; --i) q = add(mul(q, r), kInvFact[i]);
    const dd er = add(add(mul(mul(r, r), q), r), 1.0);
    const dd v = mul(kExp2Table[j], er);
    return std::ldexp(v.hi, k);
}

}  // namespace detail

inline double pow(double x, double y) {
    constexpr double zero = 0.0, one = 1.0;
    using detail::high_word;
    using detail::low_word;
    const std::int32_t hx = high_word(x), hy = high_word(y);
    const std::uint32_t lx = low_word(x), ly = low_word(y);
    const std::int32_t ix = hx & 0x7fffffff, iy = hy & 0x7fffffff;

    if ((static_cast<std::uint32_t>(iy) | ly) == 0) return one;  // x ** +-0 = 1
    if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0) || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0))
        return x + y;  // NaN

    // yisint: 0 = y not an integer, 1 = odd integer, 2 = even integer (only needed for x < 0)
    int yisint = 0;
    if (hx < 0) {
        if (iy >= 0x43400000) {
            yisint = 2;
        } else if (iy >= 0x3ff00000) {
            const int k = (iy >> 20) - 0x3ff;
            if (k > 20) {
                const std::uint32_t j = ly >> (52 - k);
                if ((j << (52 - k)) == ly) yisint = 2 - static_cast<int>(j & 1);
            } else if (ly == 0) {
                const std::int32_t j = iy >> (20 - k);
                if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
            }
        }
    }

    if (ly == 0) {  // special values of y
        if (iy == 0x7ff00000) {  // y is +-inf
            if (((static_cast<std::uint32_t>(ix) - 0x3ff00000u) | lx) == 0) return y - y;  // +-1 ** +-inf is NaN (ECMAScript)
            if (ix >= 0x3ff00000) return hy >= 0 ? y : zero;  // (|x| > 1) ** +-inf = inf, 0
            return hy < 0 ? -y : zero;                         // (|x| < 1) ** -,+inf = inf, 0
        }
        if (iy == 0x3ff00000) return hy < 0 ? one / x : x;     // y is +-1
        if (hy == 0x40000000) return x * x;                     // y is 2
        if (hy == 0x3fe00000 && hx >= 0) return std::sqrt(x);  // y is 0.5, x >= +0
    }

    const double ax = std::fabs(x);
    if (lx == 0 && (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000)) {  // x is +-0, +-inf, +-1
        double z = ax;
        if (hy < 0) z = one / z;
        if (hx < 0) {
            if (((ix - 0x3ff00000) | yisint) == 0) z = std::numeric_limits<double>::quiet_NaN();  // (-1) ** non-int
            else if (yisint == 1) z = -z;
        }
        return z;
    }
    if (hx < 0 && yisint == 0) return std::numeric_limits<double>::quiet_NaN();  // (x < 0) ** non-int
    const double s = (hx < 0 && yisint == 1) ? -one : one;                       // (-ve) ** odd int

    const detail::dd lx_dd = detail::log_dd(ax);
    const double approx = y * lx_dd.hi;
    if (approx > 760.0) return s * std::numeric_limits<double>::infinity();
    if (approx < -760.0) return s * zero;
    return s * detail::exp_dd(detail::mul(lx_dd, y));
}

}  // namespace cat::jsmath
