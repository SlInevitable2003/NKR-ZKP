// Copyright Supranational LLC
// Licensed under the Apache License, Version 2.0.
#pragma once

#include "alt_bn128.hip"

// DCU-safe Fp2 representation. The imported HIP implementation split one Fp2
// value across two adjacent lanes. DCU shuffle behaviour is not reliable enough
// for that representation, so both components deliberately stay in one thread.
namespace alt_bn128 {

class fp2_t {
    fp_mont x[2];

    static __device__ fp_mont inverse_fp(const fp_mont &value)
    {
        if (value.is_zero()) {
            fp_mont zero;
            zero.zero();
            return zero;
        }
        // Fermat inversion, value^(p-2). MOD is odd, so subtracting two only
        // changes the least-significant limb.
        fp_mont result = fp_mont::one();
        for (int bit = static_cast<int>(fp_mont::nbits) - 1; bit >= 0; --bit) {
            result = result ^ 2;
            const size_t limb = static_cast<size_t>(bit) / 32;
            uint32_t exponent_limb = fp_mont::modulus_limb(limb);
            if (limb == 0) exponent_limb -= 2;
            if ((exponent_limb >> (bit % 32)) & 1U) result *= value;
        }
        return result;
    }

public:
    static const uint32_t degree = 2;
    using mem_t = fp2_t;

    __host__ __device__ fp2_t() = default;
    __device__ fp2_t(const fp_mont &c0, const fp_mont &c1) : x{c0, c1} {}

    __device__ void zero() { x[0].zero(); x[1].zero(); }
    __device__ void to() { x[0].to(); x[1].to(); }
    __device__ void from() { x[0].from(); x[1].from(); }
    __device__ bool is_zero() const { return x[0].is_zero() && x[1].is_zero(); }
    __device__ bool is_zero(const fp2_t &value) const
    { return is_zero() && value.is_zero(); }

    static __device__ fp2_t one(int or_zero = 0)
    {
        fp2_t result;
        result.x[0] = fp_mont::one(or_zero);
        result.x[1].zero();
        return result;
    }

    __device__ bool is_one() const { return x[0].is_one() && x[1].is_zero(); }
    __device__ fp2_t &cneg(bool flag)
    {
        x[0].cneg(flag);
        x[1].cneg(flag);
        return *this;
    }
    friend __device__ fp2_t cneg(fp2_t value, bool flag) { return value.cneg(flag); }
    friend __device__ fp2_t czero(const fp2_t &value, int set_zero)
    {
        fp2_t result = value;
        if (set_zero) result.zero();
        return result;
    }
    static __device__ fp2_t csel(const fp2_t &a, const fp2_t &b, int select_a)
    {
        return fp2_t(fp_mont::csel(a.x[0], b.x[0], select_a),
                     fp_mont::csel(a.x[1], b.x[1], select_a));
    }
    friend __device__ bool operator==(const fp2_t &a, const fp2_t &b)
    {
        return (a.x[0] - b.x[0]).is_zero() &&
               (a.x[1] - b.x[1]).is_zero();
    }
    friend __device__ bool operator!=(const fp2_t &a, const fp2_t &b)
    { return !(a == b); }

    friend __device__ fp2_t operator+(const fp2_t &a, const fp2_t &b)
    { return fp2_t(a.x[0] + b.x[0], a.x[1] + b.x[1]); }
    __device__ fp2_t &operator+=(const fp2_t &other)
    { x[0] += other.x[0]; x[1] += other.x[1]; return *this; }
    friend __device__ fp2_t operator-(const fp2_t &a, const fp2_t &b)
    { return fp2_t(a.x[0] - b.x[0], a.x[1] - b.x[1]); }
    friend __device__ fp2_t operator-(const fp2_t &value)
    {
        fp2_t result = value;
        result.cneg(true);
        return result;
    }
    __device__ fp2_t &operator-=(const fp2_t &other)
    { x[0] -= other.x[0]; x[1] -= other.x[1]; return *this; }

    friend __device__ fp2_t operator*(const fp2_t &a, const fp2_t &b)
    {
        const fp_mont ac = a.x[0] * b.x[0];
        const fp_mont bd = a.x[1] * b.x[1];
        const fp_mont cross = (a.x[0] + a.x[1]) * (b.x[0] + b.x[1]);
        return fp2_t(ac - bd, cross - ac - bd);
    }
    __device__ fp2_t &operator*=(const fp2_t &other)
    { return *this = *this * other; }

    __device__ fp2_t &sqr()
    {
        const fp_mont c0 = (x[0] + x[1]) * (x[0] - x[1]);
        const fp_mont c1 = (x[0] * x[1]) << 1;
        x[0] = c0;
        x[1] = c1;
        return *this;
    }
    __device__ fp2_t &operator^=(int power)
    {
        if (power == 2) return sqr();
        __builtin_trap();
        return *this;
    }
    friend __device__ fp2_t operator^(fp2_t value, int power)
    { return value ^= power; }
    friend __device__ fp2_t operator<<(const fp2_t &value, unsigned shift)
    { return fp2_t(value.x[0] << shift, value.x[1] << shift); }
    __device__ fp2_t &operator<<=(unsigned shift)
    { x[0] <<= shift; x[1] <<= shift; return *this; }

    __device__ fp2_t reciprocal() const
    {
        const fp_mont inverse = inverse_fp(x[0] * x[0] + x[1] * x[1]);
        fp_mont negative = x[1];
        negative.cneg(true);
        return fp2_t(x[0] * inverse, negative * inverse);
    }
    friend __device__ fp2_t operator/(int one, const fp2_t &value)
    { if (one != 1) __builtin_trap(); return value.reciprocal(); }
    friend __device__ fp2_t operator/(const fp2_t &a, const fp2_t &b)
    { return a * b.reciprocal(); }
    __device__ fp2_t &operator/=(const fp2_t &other)
    { return *this *= other.reciprocal(); }
};

} // namespace alt_bn128
