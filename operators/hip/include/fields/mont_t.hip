// Copyright Supranational LLC
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.1
#pragma once
#include <cstddef>
#include <cstdint>
#include "pow.hip"

#include <hip/hip_runtime.h>

# define inline __device__ __attribute__((always_inline))
# ifdef __GNUC__
#  define asm __asm__ __volatile__
# else
#  define asm asm volatile
# endif

//
// To instantiate declare modulus as __device__ __constant___ const and
// complement it with its factual bit-length and the corresponding 32-bit
// Motgomery factor. Bit-length has to be such that (N+31)/32 is even
// and not less than 4.
//
// Special note about M0 being declared as uint32_t& [as opposed to just
// uint32_t]. It was noted that if M0 is 0xffffffff, CUDA compiler
// generates suboptimal code for Montgomery reduction. The way to work
// around the problem is to prevent compiler from viewing it as constant.
// For this reason it's suggested to declare the parameter as following:
//
//    __device__ __constant__ /*const*/ my_M0 = <literal>;
//

template<const size_t N, const uint32_t MOD[(N+31)/32], const uint32_t& M0,
         const uint32_t RR[(N+31)/32], const uint32_t ONE[(N+31)/32],
         const uint32_t MODx[(N+31)/32] = MOD>
class alignas(((N+63)/64)&1 ? 8 : 16) mont_t {
public:
    static const size_t nbits = N;
    static constexpr size_t __device__ bit_length() { return N; }
    static const uint32_t degree = 1;
    using mem_t = mont_t;

    mont_t(uint64_t *mem) { memcpy(even, mem, sizeof(even)); } // TO BE JUSTIFIED
protected:
    static const size_t n = (N+31)/32;
    static_assert((n%2) == 0 && n >= 4, "unsupported bit length");
private:
    uint32_t even[n];

    static inline void mul_n(uint32_t* acc, const uint32_t* a, uint32_t bi,
                             size_t n_=n)
    {
        // for (size_t j = 0; j < n_; j += 2)
        //     asm("mul.lo.u32 %0, %2, %3; mul.hi.u32 %1, %2, %3;"
        //         : "=r"(acc[j]), "=r"(acc[j+1])
        //         : "r"(a[j]), "r"(bi));

        // dcu
        /* #pragma unroll
        for (size_t j = 0; j < n_; j += 2) {
            asm (
                "v_mul_lo_u32 %0, %2, %3\n\t"
                "v_mul_hi_u32 %1, %2, %3"
                : "=v"(acc[j]), "=v"(acc[j+1])
                : "v"(a[j]), "v"(bi)
            );
        }*/
	#pragma unroll
	for (size_t j = 0; j < n_; j+=2) {
	    acc[j] = a[j]*bi;
	    acc[j+1] = __umulhi(a[j], bi);
	}
    }

    static inline uint32_t cmad_n(uint32_t* acc, const uint32_t* a, uint32_t bi,
                              size_t n_=n)
    {
        // asm("mad.lo.cc.u32 %0, %2, %3, %0; madc.hi.cc.u32 %1, %2, %3, %1;"
        //     : "+r"(acc[0]), "+r"(acc[1])
        //     : "r"(a[0]), "r"(bi));
        // for (size_t j = 2; j < n_; j += 2)
        //     asm("madc.lo.cc.u32 %0, %2, %3, %0; madc.hi.cc.u32 %1, %2, %3, %1;"
        //         : "+r"(acc[j]), "+r"(acc[j+1])
        //         : "r"(a[j]), "r"(bi));
        // // return carry flag

        // dcu
        uint32_t carry = 0;

        uint64_t p0 = (uint64_t)a[0] * bi;
        acc[0] = __builtin_addc((uint32_t)p0, acc[0], 0, &carry);
        acc[1] = __builtin_addc((uint32_t)(p0 >> 32), acc[1], carry, &carry);

        #pragma unroll
        for (size_t j = 2; j < n_; j += 2) {
            uint64_t p = (uint64_t)a[j] * bi;
            acc[j] = __builtin_addc((uint32_t)p, acc[j], carry, &carry);
            acc[j+1] = __builtin_addc((uint32_t)(p >> 32), acc[j+1], carry, &carry);
        }

	return carry;
    }

    static inline uint32_t cadd_n(uint32_t* acc, const uint32_t* a, size_t n_=n)
    {
        // asm("add.cc.u32 %0, %0, %1;" : "+r"(acc[0]) : "r"(a[0]));
        // for (size_t i = 1; i < n_; i++)
        //     asm("addc.cc.u32 %0, %0, %1;" : "+r"(acc[i]) : "r"(a[i]));
        // // return carry flag

        // dcu
        uint32_t carry = 0;
	acc[0] = __builtin_addc(acc[0], a[0], 0, &carry);

        #pragma unroll
        for (size_t i = 1; i < n_; i++) {
            acc[i] = __builtin_addc(acc[i], a[i], carry, &carry);
        }
	return carry;
    }

    class wide_t {
    private:
        union {
            uint32_t even[2*n];
            mont_t s[2];
        };

    public:
        inline uint32_t& operator[](size_t i)               { return even[i]; }
        inline const uint32_t& operator[](size_t i) const   { return even[i]; }
        inline operator mont_t()
        {
            s[0].mul_by_1();
            return s[0] + s[1];
        }
        inline void final_sub(uint32_t carry, uint32_t* tmp)
        {   s[1].final_sub(carry, tmp);   }

        inline wide_t() {}

    private:
        static inline void mad_row(uint32_t* odd, uint32_t* even,
                                   const uint32_t* a, uint32_t bi, size_t n_=n)
        {
            // cmad_n(odd, a+1, bi, n_-2);
            // asm("madc.lo.cc.u32 %0, %2, %3, 0; madc.hi.u32 %1, %2, %3, 0;"
            //     : "=r"(odd[n_-2]), "=r"(odd[n_-1])
            //     : "r"(a[n_-1]), "r"(bi));

            uint32_t carry_odd = cmad_n(odd, a+1, bi, n_-2);
            uint64_t p = (uint64_t)a[n_-1] * bi;
            odd[n_-2] = __builtin_addc((uint32_t)p, 0, carry_odd, &carry_odd);
            odd[n_-1] = __builtin_addc((uint32_t)(p >> 32), 0, carry_odd, &carry_odd);
            
            uint32_t carry_even = cmad_n(even, a, bi, n_);
            odd[n_-1] += carry_even;
        }

    public:
        inline wide_t(const mont_t& a, const mont_t& b)     //// |a|*|b|
        {
            size_t i = 0;
            uint32_t odd[2*n-2];

            mul_n(even, &a[0], b[0]);
            mul_n(odd,  &a[1], b[0]); 
	    ++i; mad_row(&even[i+1], &odd[i-1], &a[0], b[i]);

            #pragma unroll
            while (i < n-2) {
                ++i; mad_row(&odd[i],    &even[i],  &a[0], b[i]);
                ++i; mad_row(&even[i+1], &odd[i-1], &a[0], b[i]);
            }
            
            uint32_t carry = cadd_n(&even[1], &odd[0], 2*n-2);
     	    even[2*n-1] += carry;
        }

    private:
        static inline void qad_row(uint32_t* odd, uint32_t* even,
                                   const uint32_t* a, uint32_t bi, size_t n)
        {
            uint32_t carry_odd = cmad_n(odd, a, bi, n-2);
            uint64_t p = (uint64_t)a[n-2] * bi;
            odd[n-2] = __builtin_addc((uint32_t)p, 0, carry_odd, &carry_odd);
            odd[n-1] = __builtin_addc((uint32_t)(p >> 32), 0, carry_odd, &carry_odd);
            
            uint32_t carry_even = cmad_n(even, a+1, bi, n-2);
            odd[n-1] += carry_even;
        }

    public:
        inline wide_t(const mont_t& a)                      //// |a|**2
        {
            size_t i = 0, j;
            uint32_t odd[2*n-2];

            // perform |a[i]|*|a[j]| for all j>i
            mul_n(even+2, &a[2], a[0], n-2);
            mul_n(odd,    &a[1], a[0], n);

            #pragma unroll
            while (i < n-4) {
                ++i; mad_row(&even[2*i+2], &odd[2*i], &a[i+1], a[i], n-i-1);
                ++i; qad_row(&odd[2*i], &even[2*i+2], &a[i+1], a[i], n-i);
            }

            asm (
                "v_mul_lo_u32 %0, %2, %3\n\t"
                "v_mul_hi_u32 %1, %2, %3"
                : "=v"(even[2*n-4]), "=v"(even[2*n-3])
                : "v"(a[n-1]), "v"(a[n-3])
            );
            uint64_t p = (uint64_t)a[n-2] * a[n-3];
            uint32_t carry = 0;
            odd[2*n-6] = __builtin_addc((uint32_t)p, odd[2*n-6], 0, &carry);
            odd[2*n-5] = __builtin_addc((uint32_t)(p >> 32), odd[2*n-5], carry, &carry); 
            even[2*n-3] += carry;
            
            asm (
                "v_mul_lo_u32 %0, %2, %3\n\t"
                "v_mul_hi_u32 %1, %2, %3"
                : "=v"(odd[2*n-4]), "=v"(odd[2*n-3])
                : "v"(a[n-1]), "v"(a[n-2])
            );
            
            carry = cadd_n(&even[2], &odd[1], 2*n-4);
            even[2*n-2] = odd[2*n-3] + carry;

            even[0] = 0;
            even[1] = __builtin_addc(odd[0], odd[0], 0, &carry);
            #pragma unroll
	    for (j = 2; j < 2*n-1; j++)
                even[j] = __builtin_addc(even[j], even[j], carry, &carry);
            even[j] = carry;

            i = 0;
            p = (uint64_t)a[0] * a[0];
            even[0] = __builtin_addc((uint32_t)p, even[0], 0, &carry);
            even[1] = __builtin_addc((uint32_t)(p >> 32), even[1], carry, &carry);
	    #pragma unroll
            for (++i; i < n; i++) {
                p = (uint64_t)a[i] * a[i];
                even[2*i] = __builtin_addc((uint32_t)p, even[2*i], carry, &carry);
                even[2*i+1] = __builtin_addc((uint32_t)(p >> 32), even[2*i+1], carry, &carry);
            }
        }
    };

private:
    inline operator const uint32_t*() const             { return even;    }
    inline operator uint32_t*()                         { return even;    }

public:
    inline uint32_t& operator[](size_t i)               { return even[i]; }
    inline const uint32_t& operator[](size_t i) const   { return even[i]; }
    inline constexpr size_t len() const                 { return n;       }

    inline mont_t() {}
    inline mont_t(const uint32_t *p)
    {
        for (size_t i = 0; i < n; i++)
            even[i] = p[i];
    }

    template<typename... Ts>
    constexpr mont_t(uint32_t a0, Ts... arr) : even{a0, arr...} {}

    inline void store(uint32_t *p) const
    {
        for (size_t i = 0; i < n; i++)
            p[i] = even[i];
    }

    inline mont_t& operator+=(const mont_t& b)
    {
        uint32_t carry = cadd_n(&even[0], &b[0]);
        final_subc(carry);
        return *this;
    }
    friend inline mont_t operator+(mont_t a, const mont_t& b)
    {   return a += b;   }

    inline mont_t& operator<<=(unsigned l)
    {
        uint32_t carry = 0;
        while (l--) {
            even[0] = __builtin_addc(even[0], even[0], 0, &carry);
            for (size_t i = 1; i < n; i++)
                even[i] = __builtin_addc(even[i], even[i], carry, &carry);
            final_subc(carry);
        }

        return *this;
    }
    friend inline mont_t operator<<(mont_t a, unsigned l)
    {   return a <<= l;   }

    inline mont_t& operator>>=(unsigned r)
    {
        // size_t i;
        // uint32_t tmp[n+1];

        // while (r--) {
        //     tmp[n] = 0 - (even[0]&1);
        //     for (i = 0; i < n; i++)
        //         tmp[i] = MOD[i] & tmp[n];

        //     cadd_n(&tmp[0], &even[0]);
        //     if (N%32 == 0)
        //         asm("addc.u32 %0, 0, 0;" : "=r"(tmp[n]));

        //     for (i = 0; i < n-1; i++)
        //         asm("shf.r.wrap.b32 %0, %1, %2, 1;"
        //             : "=r"(even[i]) : "r"(tmp[i]), "r"(tmp[i+1]));
        //     if (N%32 == 0)
        //         asm("shf.r.wrap.b32 %0, %1, %2, 1;"
        //             : "=r"(even[i]) : "r"(tmp[i]), "r"(tmp[i+1]));
        //     else
        //         even[i] = tmp[i] >> 1;
        // }
	size_t i;
        uint32_t tmp[n + 1];
	uint32_t carry = 0;
        while (r--) {
            tmp[n] = 0 - (even[0] & 1);
            for (i = 0; i < n; i++) {
                tmp[i] = MOD[i] & tmp[n];
            }
            carry = cadd_n(&even[0], &tmp[0]);
            if (N % 32 == 0) {
                tmp[n] = carry;
            }
            #pragma unroll
            for (i = 0; i < n - 1; i++) {
                even[i] = (tmp[i] >> 1) | (tmp[i+1] << 31);
            }
            if (N % 32 == 0) {
                even[i] = (tmp[i] >> 1) | (tmp[i+1] << 31);
            } else {
                even[i] = tmp[i] >> 1;
            }
        }

        return *this;
    }
    friend inline mont_t operator>>(mont_t a, unsigned r)
    {   return a >>= r;   }

    inline mont_t& operator-=(const mont_t& b)
    {
        // size_t i;
        // uint32_t tmp[n], borrow;

        // asm("sub.cc.u32 %0, %0, %1;" : "+r"(even[0]) : "r"(b[0]));
        // for (i = 1; i < n; i++)
        //     asm("subc.cc.u32 %0, %0, %1;" : "+r"(even[i]) : "r"(b[i]));
        // asm("subc.u32 %0, 0, 0;" : "=r"(borrow));

        // asm("add.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(even[0]), "r"(MOD[0]));
        // for (i = 1; i < n-1; i++)
        //     asm("addc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(even[i]), "r"(MOD[i]));
        // asm("addc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(even[i]), "r"(MOD[i]));

        // asm("{ .reg.pred %top; setp.ne.u32 %top, %0, 0;" :: "r"(borrow));
        // for (i = 0; i < n; i++)
        //     asm("@%top mov.b32 %0, %1;" : "+r"(even[i]) : "r"(tmp[i]));
        // asm("}");
	size_t i;
        uint32_t tmp[n], borrow = 0;

        uint32_t carry_out = 0;
	even[0] = __builtin_subc(even[0], b[0], 0, &carry_out);
        #pragma unroll
        for (i = 1; i < n; i++) {
            even[i] = __builtin_subc(even[i], b[i], carry_out, &carry_out);
        }
        borrow = (-carry_out);
        tmp[0] = __builtin_addc(even[0], MOD[0], 0, &carry_out);
        #pragma unroll
        for (i = 1; i < n; i++) {
            tmp[i] = __builtin_addc(even[i], MOD[i], carry_out, &carry_out);
        }
        #pragma unroll
        for (i = 0; i < n; i++)
            even[i] = borrow ? tmp[i] : even[i];

        return *this;
    }
    friend inline mont_t operator-(mont_t a, const mont_t& b)
    {   return a -= b;   }

#if 1
    inline mont_t& cneg(bool flag)
    {
        // size_t i;
        // uint32_t tmp[n], is_zero = even[0];
        // asm("{ .reg.pred %flag; setp.ne.u32 %flag, %0, 0;" :: "r"((int)flag));

        // asm("sub.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(MOD[0]), "r"(even[0]));
        // for (i = 1; i < n; i++) {
        //     asm("subc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(MOD[i]), "r"(even[i]));
        //     asm("or.b32 %0, %0, %1;" : "+r"(is_zero) : "r"(even[i]));
        // }

        // asm("@%flag setp.ne.u32 %flag, %0, 0;" :: "r"(is_zero));

        // for (i = 0; i < n; i++)
        //     asm("@%flag mov.b32 %0, %1;" : "+r"(even[i]) : "r"(tmp[i]));

        // asm("}");

        size_t i;
        uint32_t tmp[n], is_zero = even[0];

        uint32_t carry = 0;
        tmp[0] = __builtin_subc(MOD[0], even[0], 0, &carry);
        #pragma unroll
        for (i = 0; i < n; i++) {
            tmp[i] = __builtin_subc(MOD[i], even[i], carry, &carry);
            is_zero |= even[i];
        }
        bool do_negate = flag && (is_zero != 0);
        #pragma unroll
        for (i = 0; i < n; i++)
            even[i] = do_negate ? tmp[i] : even[i];
        return *this;
    }
    static inline mont_t cneg(mont_t a, bool flag)
    {   return a.cneg(flag);   }
#else
    static inline mont_t cneg(const mont_t& a, bool flag)
    {
        // size_t i;
        // uint32_t tmp[n], is_zero = a[0];
        // asm("{ .reg.pred %flag; setp.ne.u32 %flag, %0, 0;" :: "r"((int)flag));

        // asm("sub.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(MOD[0]), "r"(a[0]));
        // for (i = 1; i < n; i++) {
        //     asm("subc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(MOD[i]), "r"(a[i]));
        //     asm("or.b32 %0, %0, %1;" : "+r"(is_zero) : "r"(a[i]));
        // }

        // asm("@%flag setp.ne.u32 %flag, %0, 0;" :: "r"(is_zero));

        // mont_t ret = a;
        // for (i = 0; i < n; i++)
        //     asm("@%flag mov.b32 %0, %1;" : "+r"(ret[i]) : "r"(tmp[i]));

        // asm("}");
        // return ret;
        
	uint32_t tmp[n], is_zero = a[0], borrow;
        tmp[0] = __builtin_subc(MOD[0], a[0], 0, &borrow);

        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            tmp[i] = __builtin_subc(MOD[i], a[i], borrow, &borrow);
            is_zero |= a[i];
        }

        bool do_negate = flag && (is_zero != 0);

        mont_t ret = a;
        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            ret[i] = do_negate ? tmp[i] : ret[i];
        }
        return ret;
    }
    inline mont_t& cneg(bool flag)
    {   return *this = cneg(*this, flag);   }
#endif
    inline mont_t operator-() const
    {   return cneg(*this, true);   }

    // make the value "positive" and return the original "sign"
    inline bool abs()
    {
        // size_t i;
        // uint32_t tmp[n], sign;

        // asm("sub.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(MOD[0]), "r"(even[0]));
        // for (i = 1; i < n; i++)
        //     asm("subc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(MOD[i]), "r"(even[i]));

        // sign = tmp[n-1] < even[n-1];

        // asm("{ .reg.pred %flag; setp.ne.u32 %flag, %0, 0;" :: "r"(sign));
        // for (i = 0; i < n; i++)
        //     asm("@%flag mov.b32 %0, %1;" : "+r"(even[i]) : "r"(tmp[i]));
        // asm("}");

        uint32_t tmp[n], sign;
        uint32_t borrow;

        tmp[0] = __builtin_subc(MOD[0], even[0], 0, &borrow);
        #pragma unroll
        for (size_t i = 1; i < n; i++) {
            tmp[i] = __builtin_subc(MOD[i], even[i], borrow, &borrow);
        }

        sign = tmp[n-1] < even[n-1];
        bool do_negate = sign != 0;
        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            even[i] = do_negate ? tmp[i] : even[i];
        }

        return sign;
    }

private:
    static inline void madc_n_rshift(uint32_t* odd, const uint32_t *a, uint32_t bi, uint32_t carry = 0)
    {
        // for (size_t j = 0; j < n-2; j += 2)
        //     asm("madc.lo.cc.u32 %0, %2, %3, %4; madc.hi.cc.u32 %1, %2, %3, %5;"
        //         : "=r"(odd[j]), "=r"(odd[j+1])
        //         : "r"(a[j]), "r"(bi), "r"(odd[j+2]), "r"(odd[j+3]));
        // asm("madc.lo.cc.u32 %0, %2, %3, 0; madc.hi.u32 %1, %2, %3, 0;"
        //     : "=r"(odd[n-2]), "=r"(odd[n-1])
        //     : "r"(a[n-2]), "r"(bi));

        uint64_t p;
        #pragma unroll
        for (size_t j = 0; j < n-2; j+= 2) {
            p = (uint64_t)a[j] * bi;
            odd[j] = __builtin_addc((uint32_t)p, odd[j+2], carry, &carry);
            odd[j+1] = __builtin_addc((uint32_t)(p >> 32), odd[j+3], carry, &carry);
        }
        p = (uint64_t)a[n-2] * bi;
        odd[n-2] = __builtin_addc((uint32_t)p, 0, carry, &carry);
        odd[n-1] = __builtin_addc((uint32_t)(p >> 32), 0, carry, &carry);
    }

    static inline void mad_n_redc(uint32_t *even, uint32_t* odd,
                                  const uint32_t *a, uint32_t bi, bool first=false)
    {
        // if (first) {
        //     mul_n(odd, a+1, bi);
        //     mul_n(even, a,  bi);
        // } else {
        //     asm("add.cc.u32 %0, %0, %1;" : "+r"(even[0]) : "r"(odd[1]));
        //     madc_n_rshift(odd, a+1, bi);
        //     cmad_n(even, a, bi);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
        // }

        // uint32_t mi = even[0] * M0;

        // cmad_n(odd, MOD+1, mi);
        // cmad_n(even, MOD,  mi);
        // asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));

        if (first) {
            mul_n(odd, a + 1, bi);
            mul_n(even, a, bi);
        } else {
            uint32_t carry = 0;
            even[0] = __builtin_addc(even[0], odd[1], 0, &carry);

            madc_n_rshift(odd, a+1, bi, carry);
            uint32_t carry_even = cmad_n(even, a, bi);
            odd[n-1] += carry_even;
        }

        uint32_t mi = even[0] * M0;

        uint32_t c1 = cmad_n(odd, MOD + 1, mi);
        uint32_t c2 = cmad_n(even, MOD, mi);
	odd[n-1] += c2;
    }

public:
    friend inline mont_t operator*(const mont_t& a, const mont_t& b)
    {
        // if (N%32 == 0) {
        //     return wide_t{a, b};
        // } else {
        //     mont_t even, odd;

        //     #pragma unroll
        //     for (size_t i = 0; i < n; i += 2) {
        //         mad_n_redc(&even[0], &odd[0], &a[0], b[i], i==0);
        //         mad_n_redc(&odd[0], &even[0], &a[0], b[i+1]);
        //     }

        //     // merge |even| and |odd|
        //     cadd_n(&even[0], &odd[1], n-1);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(even[n-1]));

        //     even.final_sub(0, &odd[0]);

        //     return even;
        // }

        if (N % 32 == 0) {
            return wide_t{a, b};
        } else {
            mont_t even, odd;

            #pragma unroll
            for (size_t i = 0; i < n; i += 2) {
                mad_n_redc(&even[0], &odd[0], &a[0], b[i], i==0);
                mad_n_redc(&odd[0], &even[0], &a[0], b[i+1]);
            }

            uint32_t top_carry = cadd_n(&even[0], &odd[1], n-1);
            even[n-1] += top_carry;

            even.final_sub(0, &odd[0]);

            return even;
        }
    }
    inline mont_t& operator*=(const mont_t& a)
    {   return *this = *this * a;   }

    inline mont_t& sqr()
    {   return *this = wide_t{*this};   }

    // raise to a variable power, variable in respect to threadIdx,
    // but mind the ^ operator's precedence!
    inline mont_t& operator^=(uint32_t p)
    {   return pow_byref(*this, p);   }
    friend inline mont_t operator^(mont_t a, uint32_t p)
    {   return a ^= p;   }
    inline mont_t operator()(uint32_t p)
    {   return *this^p;   }

    // raise to a constant power, e.g. x^7, to be unrolled at compile time
    inline mont_t& operator^=(int p)
    {   return pow_byref(*this, p);   }
    friend inline mont_t operator^(mont_t a, int p)
    {   return p == 2 ? (mont_t)wide_t{a} : a ^= p;   }
    inline mont_t operator()(int p)
    {   return *this^p;   }
    friend inline mont_t sqr(const mont_t& a)
    {   return a^2;   }

    inline void to()    { mont_t t = RR * *this; *this = t; }
    inline void to(const uint32_t a[2*n], bool host_order = true)
    {
        size_t i;

        // load the most significant half
        if (host_order) {
            #pragma unroll
            for (i = 0; i < n; i++)
                even[i] = a[n + i];
        } else {
            #pragma unroll
            for (i = 0; i < n; i++)
                even[i] = __builtin_bswap32(a[n-1-i]);
                //asm("prmt.b32 %0, %1, %1, 0x0123;" : "=r"(even[i]) : "r"(a[n - 1 - i]));
        }
        to();

        mont_t lo;

        // load the least significant half
        if (host_order) {
            #pragma unroll
            for (i = 0; i < n; i++)
                lo[i] = a[i];
        } else {
            #pragma unroll
            for (i = 0; i < n; i++)
                lo[i] = __builtin_bswap32(a[2 * n - 1 - i]);
            // asm("prmt.b32 %0, %1, %1, 0x0123;" : "=r"(lo[i]) : "r"(a[2*n - 1 - i]));
        }

        uint32_t carry = cadd_n(&even[0], &lo[0]);
        final_subc(carry);
        to();
    }
    inline void from()  { mont_t t = *this; t.mul_by_1(); *this = t; }
    inline void from(const uint32_t a[2*n], bool host_order = true)
    {
        size_t i;

        // load the least significant half
        if (host_order) {
            #pragma unroll
            for (i = 0; i < n; i++)
                even[i] = a[i];
        } else {
            for (i = 0; i < n; i++)
                even[i] = __builtin_bswap32(a[2 * n - 1 - i]);
            //asm("prmt.b32 %0, %1, 0, 0x0123;" : "=r"(even[i]) : "r"(a[2*n - 1 -i]));
        }
        mul_by_1();

        mont_t hi;

        // load the most significant half
        if (host_order) {
            #pragma unroll
            for (i = 0; i < n; i++)
                hi[i] = a[n + i];
        } else {
            for (i = 0; i < n; i++)
                hi[i] = __builtin_bswap32(a[n - 1 - i]);
            //asm("prmt.b32 %0, %1, 0, 0x0123;" : "=r"(hi[i]) : "r"(a[n - 1 - i]));
        }

        uint32_t carry = cadd_n(&even[0], &hi[0]);
        final_subc(carry);
        to();
    }

    static inline const mont_t& one()
    {   return *reinterpret_cast<const mont_t*>(ONE);   }

    static inline mont_t one(int or_zero)
    {
        // mont_t ret;
        // asm("{ .reg.pred %or_zero;");
        // asm("setp.ne.s32 %or_zero, %0, 0;" : : "r"(or_zero));
        // for (size_t i = 0; i < n; i++)
        //     asm("selp.u32 %0, 0, %1, %or_zero;" : "=r"(ret[i]) : "r"(ONE[i]));
        // asm("}");
        // return ret;

        mont_t ret;
        bool flag = (or_zero != 0);

        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            ret[i] = flag ? 0 : ONE[i];
        }
        
        return ret;
    }

    inline bool is_one() const
    {
#if __CUDACC_VER_MAJOR__ == 12 && __CUDACC_VER_MINOR__ > 3
        uint32_t is_zero = 0;

        for (size_t i = 0; i < n; i++)
            is_zero |= even[i] ^ ONE[i];
#else
        uint32_t is_zero = even[0] ^ ONE[0];

        for (size_t i = 1; i < n; i++)
            is_zero |= even[i] ^ ONE[i];
#endif

        return is_zero == 0;
    }

    __host__ inline bool is_zero() const
    {
        uint32_t is_zero = even[0] | even[1];

        for (size_t i = 2; i < n; i += 2)
            is_zero |= even[i] | even[i+1];

        return is_zero == 0;
    }

    inline bool is_zero(const mont_t& a) const
    {
        uint32_t is_zero = even[0] | a[0];

        for (size_t i = 1; i < n; i++)
            is_zero |= even[i] | a[i];

        return is_zero == 0;
    }

    inline void zero()
    {
        if (n%4 == 0) {
            uint4* p = (uint4*)even;
            for (size_t i=0; i<sizeof(even)/(sizeof(*p)); i++)
                p[i] = make_uint4(0, 0, 0, 0);
        } else {
            uint64_t* p = (uint64_t*)even;
            for (size_t i=0; i<sizeof(even)/(sizeof(uint64_t)); i++)
                p[i] = 0;
        }
    }

    friend inline mont_t czero(const mont_t& a, int set_z)
    {
        // mont_t ret;
        // asm("{ .reg.pred %set_z;");
        // asm("setp.ne.s32 %set_z, %0, 0;" : : "r"(set_z));
        // for (size_t i = 0; i < n; i++)
        //     asm("selp.u32 %0, 0, %1, %set_z;" : "=r"(ret[i]) : "r"(a[i]));
        // asm("}");
        // return ret;

        mont_t ret;
        bool flag = (set_z != 0);

        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            ret[i] = flag ? 0 : a[i];
        }
        return ret;
    }

    static inline mont_t csel(const mont_t& a, const mont_t& b, int sel_a)
    {
        // mont_t ret;
        // asm("{ .reg.pred %sel_a;");
        // asm("setp.ne.s32 %sel_a, %0, 0;" : : "r"(sel_a));
        // for (size_t i = 0; i < n; i++)
        //     asm("selp.u32 %0, %1, %2, %sel_a;" : "=r"(ret[i]) : "r"(a[i]), "r"(b[i]));
        // asm("}");
        // return ret;

        mont_t ret;
        bool flag = (sel_a != 0);

        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            ret[i] = flag ? a[i] : b[i];
        }
        return ret;
    }

private:
    static inline void mul_by_1_row(uint32_t* even, uint32_t* odd, bool first=false)
    {
//         uint32_t mi;

//         if (first) {
//             mi = even[0] * M0;
//             mul_n(odd, MOD+1, mi);
//             cmad_n(even, MOD,  mi);
//             asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
//         } else {
//             asm("add.cc.u32 %0, %0, %1;" : "+r"(even[0]) : "r"(odd[1]));
// # if 1      // do we trust the compiler to *not* touch the carry flag here?
//             mi = even[0] * M0;
// # else
//             asm("mul.lo.u32 %0, %1, %2;" : "=r"(mi) : "r"(even[0]), "r"(M0));
// # endif
//             madc_n_rshift(odd, MOD+1, mi);
//             cmad_n(even, MOD, mi);
//             asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
//         }

        uint32_t mi, carry;

        if (first) {
            mi = even[0] * M0;
            mul_n(odd, MOD + 1, mi);
            carry = cmad_n(even, MOD, mi);
            odd[n-1] = __builtin_addc(odd[n-1], 0, carry, &carry);
        } else {
            even[0] = __builtin_addc(even[0], odd[1], 0, &carry);
#if 1
            mi = even[0] * M0;
#else
            asm (
                "v_mul_lo_u32 %0, %1, %2"
                : "=v"(mi) : "v"(even[0]), "v"(M0)
            );     
#endif            
            madc_n_rshift(odd, MOD + 1, mi, carry);
            uint32_t carry_even = cmad_n(even, MOD, mi, n);
            odd[n-1] = __builtin_addc(odd[n-1], carry_even, 0, &carry_even);
        }
    }
    inline void mul_by_1()
    {
        // mont_t odd;

        // #pragma unroll
        // for (size_t i = 0; i < n; i += 2) {
        //     mul_by_1_row(&even[0], &odd[0], i==0);
        //     mul_by_1_row(&odd[0], &even[0]);
        // }

        // cadd_n(&even[0], &odd[1], n-1);
        // asm("addc.u32 %0, %0, 0;" : "+r"(even[n-1]));

        mont_t odd;
        #pragma unroll
        for (size_t i = 0; i < n; i += 2) {
            mul_by_1_row(&even[0], &odd[0], i==0);
            mul_by_1_row(&odd[0], &even[0]);
        }
        uint32_t top_carry = cadd_n(&even[0], &odd[1], n-1);
        even[n-1] = __builtin_addc(even[n-1], 0, top_carry, &top_carry);
    }

    inline void final_sub(uint32_t carry, uint32_t* tmp)
    {
        // size_t i;
        // asm("{ .reg.pred %top;");

        // asm("sub.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(even[0]), "r"(MOD[0]));
        // for (i = 1; i < n; i++)
        //     asm("subc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(even[i]), "r"(MOD[i]));
        // if (N%32 == 0)
        //     asm("subc.u32 %0, %0, 0; setp.eq.u32 %top, %0, 0;" : "+r"(carry));
        // else
        //     asm("subc.u32 %0, 0, 0; setp.eq.u32 %top, %0, 0;" : "=r"(carry));

        // for (i = 0; i < n; i++)
        //     asm("@%top mov.b32 %0, %1;" : "+r"(even[i]) : "r"(tmp[i]));

        // asm("}");

        uint32_t borrow = 0;
        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            uint32_t diff = even[i] - MOD[i];
            tmp[i] = diff - borrow;
            borrow = (even[i] < MOD[i]) | (diff < borrow);
        }

        bool top;
        if constexpr (N%32 == 0) {
            top = (carry == borrow);
        } else {
            top = (borrow == 0);
        }

        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            even[i] = top ? tmp[i] : even[i];
        }
    }

    inline void final_subc(uint32_t carry = 0)
    {
        // uint32_t carry, tmp[n];

        // asm("addc.u32 %0, 0, 0;" : "=r"(carry));

        // asm("sub.cc.u32 %0, %1, %2;" : "=r"(tmp[0]) : "r"(even[0]), "r"(MOD[0]));
        // for (size_t i = 1; i < n; i++)
        //     asm("subc.cc.u32 %0, %1, %2;" : "=r"(tmp[i]) : "r"(even[i]), "r"(MOD[i]));
        // asm("subc.u32 %0, %0, 0;" : "+r"(carry));

        // asm("{ .reg.pred %top;");
        // asm("setp.eq.u32 %top, %0, 0;" :: "r"(carry));
        // for (size_t i = 0; i < n; i++)
        //     asm("@%top mov.b32 %0, %1;" : "+r"(even[i]) : "r"(tmp[i]));
        // asm("}");

        uint32_t borrow = 0, tmp[n];
        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            uint32_t diff = even[i] - MOD[i];
            tmp[i] = diff - borrow;
            borrow = (even[i] < MOD[i]) | (diff < borrow);
        }
        bool top = (carry == borrow);
        #pragma unroll
        for (size_t i = 0; i < n; i++) {
            even[i] = top ? tmp[i] : even[i];
        }
    }

    static inline void dot_n_redc(uint32_t *even, uint32_t *odd,
                                  const uint32_t *a, uint32_t bi,
                                  const uint32_t *c, uint32_t di, bool first = false)
    {
        // if (first) {
        //     mul_n(odd, a+1, bi);
        //     cmad_n(odd, c+1, di);
        //     mul_n(even, a, bi);
        //     cmad_n(even, c, di);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
        // } else {
        //     asm("add.cc.u32 %0, %0, %1;" : "+r"(even[0]) : "r"(odd[1]));
        //     madc_n_rshift(odd, a+1, bi);
        //     cmad_n(odd, c+1, di);
        //     cmad_n(even, a, bi);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
        //     cmad_n(even, c, di);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));
        // }

        // uint32_t mi = even[0] * M0;

        // cmad_n(odd, MOD+1, mi);
        // cmad_n(even, MOD, mi);
        // asm("addc.u32 %0, %0, 0;" : "+r"(odd[n-1]));

        if (first) {
            mul_n(odd, a + 1, bi, n - 1);
            cmad_n(odd, c + 1, di, n - 1);
            
            mul_n(even, a, bi, n);
            uint32_t carry = cmad_n(even, c, di, n);
            
            uint64_t final_top = (uint64_t)odd[n - 1] + carry;
            odd[n - 1] = (uint32_t)final_top;
        } else {
            uint64_t sum0 = (uint64_t)even[0] + odd[1];
            even[0] = (uint32_t)sum0;
            uint32_t carry_from_sum0 = (uint32_t)(sum0 >> 32);

            madc_n_rshift(odd, a + 1, bi, 0);
            uint32_t c1 = cmad_n(odd, c + 1, di, n - 1);
            
            uint32_t c2 = cmad_n(even, a, bi, n);
            uint32_t c3 = cmad_n(even, c, di, n);
            
            uint64_t final_top = (uint64_t)odd[n - 1] + c1 + c2 + c3;
            odd[n - 1] = (uint32_t)final_top;
        }

        uint32_t mi = even[0] * M0;

        uint32_t rc1 = cmad_n(odd, MOD + 1, mi, n - 1);
        uint32_t rc2 = cmad_n(even, MOD, mi, n);
        
        uint64_t redc_top = (uint64_t)odd[n - 1] + rc1 + rc2;
        odd[n - 1] = (uint32_t)redc_top;
    }

public:
    static inline mont_t dot_product(const mont_t& a, const mont_t& b,
                                     const mont_t& c, const mont_t& d)
    {
        // if (N%32 == 0 || N%32 > 30) {
        //     return a*b + c*d;   // can be improved too...
        // } else {
        //     mont_t even, odd;

        //     #pragma unroll
        //     for (size_t i = 0; i < n; i += 2) {
        //         dot_n_redc(&even[0], &odd[0], &a[0], b[i], &c[0], d[i], i==0);
        //         dot_n_redc(&odd[0], &even[0], &a[0], b[i+1], &c[0], d[i+1]);
        //     }

        //     // merge |even| and |odd|
        //     cadd_n(&even[0], &odd[1], n-1);
        //     asm("addc.u32 %0, %0, 0;" : "+r"(even[n-1]));

        //     even.final_sub(0, &odd[0]);

        //     return even;
        // }

        if (N % 32 == 0 || N % 32 > 30) {
            return a * b + c * d;
        } else {
            mont_t even, odd;
            #pragma unroll
            for (size_t i = 0; i < n; i++) {
                even[i] = 0; odd[i] = 0;
            }

            #pragma unroll
            for (size_t i = 0; i < n; i += 2) {
                dot_n_redc(even.even, odd.even, a.even, b[i], c.even, d[i], i == 0);
                if (i + 1 < n) {
                    dot_n_redc(odd.even, even.even, a.even, b[i + 1], c.even, d[i + 1]);
                }
            }

            uint32_t carry = 0;
            #pragma unroll
            for (size_t j = 0; j < n - 1; j++) {
                uint64_t sum = (uint64_t)even[j] + odd[j + 1] + carry;
                even[j] = (uint32_t)sum;
                carry = (uint32_t)(sum >> 32);
            }

            uint64_t final_word = (uint64_t)even[n - 1] + carry;
            even[n - 1] = (uint32_t)final_word;
            uint32_t top_carry = (uint32_t)(final_word >> 32);

            mont_t res_tmp;
            even.final_sub(top_carry, res_tmp.even);

            return even;
        }
    }

    static inline mont_t dot_product(const mont_t a[], const mont_t* b_ptr,
                                     size_t len, size_t stride_b = 1)
    {
        size_t i;
        mont_t tmp;
        wide_t even;
        uint32_t odd[2*n-2];

        for (i = 0; i < 2*n-2; i++)
            even[i] = odd[i] = 0;
        even[i] = even[i+1] = 0;

        #pragma unroll
        for (size_t j = 0; j < len; j++, b_ptr += stride_b) {
            mont_t b = *b_ptr;
            tmp = a[j];
            uint32_t carry_val = 0;

            #pragma unroll
            for (i = 0; i < n; i += 2) {
                uint32_t bi = b[i];
                uint32_t c1 = cmad_n(&even[i], &tmp[0], bi, n);
                uint64_t sum_odd1 = (uint64_t)odd[n+i-1] + carry_val + c1;
                odd[n+i-1] = (uint32_t)sum_odd1;
                uint32_t local_carry1 = (uint32_t)(sum_odd1 >> 32);

                uint32_t c2 = cmad_n(&odd[i], &tmp[1], bi, n);
                carry_val = local_carry1 + c2;

                bi = b[i+1];
                uint32_t c3 = cmad_n(&odd[i], &tmp[0], bi, n);
                uint64_t sum_even1 = (uint64_t)even[n+i+1] + carry_val + c3;
                even[n+i+1] = (uint32_t)sum_even1;
                uint32_t local_carry2 = (uint32_t)(sum_even1 >> 32);

                uint32_t c4 = cmad_n(&even[i+2], &tmp[1], bi, n);
                carry_val = local_carry2 + c4;
            } 
            even.final_sub(carry_val, &tmp[0]);
        }

        uint32_t merge_carry = 0;
        #pragma unroll
        for (i = 0; i < 2*n; i++) {
            uint64_t res = (uint64_t)even[i+1] + odd[i] + merge_carry;
            even[i+1] = (uint32_t)res;
            merge_carry = (uint32_t)(res >> 32);
        }

        uint64_t final_top = (uint64_t)even[2*n-1] + merge_carry;
        even[2*n-1] = (uint32_t)final_top;
        uint32_t final_carry = (uint32_t)(final_top >> 32);

        even.final_sub(final_carry, &tmp[0]);
        return even;

        // #pragma unroll
        // for (size_t j = 0; j < len; j++, b_ptr += stride_b) {
        //     mont_t b = *b_ptr;
        //     tmp = a[j];
        //     carry = 0;

        //     #pragma unroll
        //     for (i = 0; i < n; i += 2) {
        //         uint32_t bi;

        //         cmad_n(&even[i], &tmp[0], bi = b[i]);
        //         asm("addc.u32 %0, %0, 0;" : "+r"(carry));
        //         asm("add.cc.u32 %0, %0, %1; addc.u32 %1, 0, 0;"
        //             : "+r"(odd[n+i-1]), "+r"(carry));
        //         cmad_n(&odd[i], &tmp[1], bi);
        //         asm("addc.u32 %0, %0, 0;" : "+r"(carry));

        //         cmad_n(&odd[i], &tmp[0], bi = b[i+1]);
        //         asm("addc.u32 %0, %0, 0;" : "+r"(carry));
        //         asm("add.cc.u32 %0, %0, %1; addc.u32 %1, 0, 0;"
        //             : "+r"(even[n+i+1]), "+r"(carry));
        //         cmad_n(&even[i+2], &tmp[1], bi);
        //         asm("addc.u32 %0, %0, 0;" : "+r"(carry));
        //     }

        //     // reduce |even| modulo |MOD<<(n*32)|
        //     even.final_sub(carry, &tmp[0]);
        // }

        // // merge |even| and |odd|
        // cadd_n(&even[1], &odd[0], 2*n-2);
        // asm("addc.cc.u32 %0, %0, 0; addc.u32 %1, 0, 0;"
        //     : "+r"(even[2*n-1]), "=r"(carry));

        // // reduce |even| modulo |MOD<<(n*32)|
        // even.final_sub(carry, &tmp[0]);

        // return even; // implicit cast to mont_t performs the reduction
    }

    inline mont_t shfl_down(uint32_t off) const
    {
        mont_t ret;
        //
        for (size_t i = 0; i < n; i++)
            ret[i] = __shfl_down(even[i], off, 32);

        return ret;
    }
    inline mont_t shfl(uint32_t idx, uint32_t mask = 0xffffffff) const
    {
        mont_t ret;
        // 
        for (size_t i = 0; i < n; i++)
            ret[i] = __shfl(even[i], idx, 32);

        return ret;
    }

protected:
    template<typename vec_t>
    static inline vec_t shfl_xor(const vec_t& a, uint32_t idx = 1)
    {
        vec_t ret;
        for (size_t i = 0; i < sizeof(vec_t)/sizeof(uint32_t); i++)
            ret[i] = __shfl_xor(a[i], idx, 32);
        return ret;
    }

private:
    // Euclidean inversion based on https://eprint.iacr.org/2020/972
    // and <blst>/src/no_asm.h.

    struct approx_t  { 
    	uint32_t lo, hi; 
    	__host__ __device__ approx_t& operator=(uint64_t val){
    	     this->lo = static_cast<uint32_t>(val & 0xFFFFFFFF);
    	     this->hi = static_cast<uint32_t>((val>>32) & 0xFFFFFFFF);
    	     return *this;
    	}
    };
    struct factorx_t { uint32_t f0, g0; };

    static inline uint32_t lshift_2(uint32_t hi, uint32_t lo, uint32_t i)
    {
        // uint32_t ret;
        // asm("shf.l.clamp.b32 %0, %1, %2, %3;" : "=r"(ret) : "r"(lo), "r"(hi), "r"(i));
        // return ret;
        if (i == 0) return hi;
	if (i < 32)
            return (hi << i) | (lo >> (32 - i));
	return lo << (i-32);
    }

    static inline void ab_approximation_n(approx_t& a_, const mont_t& a,
                                          approx_t& b_, const mont_t& b)
    {
        size_t i = n-1;

        uint32_t a_hi = a[i], a_lo = a[i-1];
        uint32_t b_hi = b[i], b_lo = b[i-1];
	
	bool flag;
	#pragma unroll
	for (i--; --i;) {
	    flag = (a_hi | b_hi) == 0;
	    a_hi = flag ? a_lo : a_hi;
	    b_hi = flag ? b_lo : b_hi;
    	    a_lo = flag ? a[i] : a_lo;
	    b_lo = flag ? b[i] : b_lo; 
	}	
        
        uint32_t off = __clz(a_hi | b_hi);
        /* |off| can be LIMB_T_BITS if all a[2..]|b[2..] were zeros */

        a_ = approx_t{a[0], lshift_2(a_hi, a_lo, off)};
        b_ = approx_t{b[0], lshift_2(b_hi, b_lo, off)};
    }

    static inline void cswap(uint32_t& a, uint32_t& b, uint32_t mask)
    {
        uint32_t xorm = (a ^ b) & mask;
        a ^= xorm;
        b ^= xorm;
    }

    static inline factorx_t inner_loop_x(approx_t a, approx_t b)
    {
        const uint32_t tid = threadIdx.x&1;
        const uint32_t odd = 0 - tid;

        // even thread calculates |f0|,|f1|, odd - |g0|,|g1|
        uint32_t fg0 = tid^1, fg1 = tid;
        uint32_t alo = a.lo, ahi = a.hi, blo = b.lo, bhi = b.hi;

        // in the odd thread |a| and |b| are in reverse order, compensate...
        cswap(alo, blo, odd);
        cswap(ahi, bhi, odd);

        uint64_t a_full = ((uint64_t)ahi << 32) | alo;
        uint64_t b_full = ((uint64_t)bhi << 32) | blo;

        #pragma unroll 2
        for (uint32_t n = 32-2; n--;) {
            bool is_odd = (a_full&1) != 0;
            bool brw = is_odd && (a_full < b_full);
            uint64_t next_a = a_full;
            if (is_odd) {
                next_a = brw ? (b_full - a_full) : (a_full - b_full);
            }

            b_full = brw ? a_full : b_full;

            uint32_t fgx = brw ? fg0 : fg1;
            fg0  = brw ? fg1 : fg0;
            fg0 = is_odd ? (fg0 - fgx) : fg0;
            fg1 = fgx << 1;
            a_full = next_a >> 1;
        }

        // even thread needs |f0|,|g0|, odd - |g1|,|f1|, in this order
        cswap(fg0, fg1, odd);

        fg1 = __shfl_xor(fg1, 1, 32);

        return factorx_t{fg0, fg1};
    }

    static inline factorx_t inner_loop_x(uint32_t a, uint32_t b, uint32_t n)
    {
        const uint32_t tid = threadIdx.x&1;
        const uint32_t odd = 0 - tid;

        // even thread calculates |f0|,|f1|, odd - |g0|,|g1|
        uint32_t fg0 = tid^1, fg1 = tid;

        // in the odd thread |a| and |b| are in reverse order, compensate...
        cswap(a, b, odd);

        #pragma unroll 1
        while (n--) {
            bool is_odd = (a&1) != 0;
            bool borrow = is_odd && (a < b);
            uint32_t a_next = a;

            if (is_odd) {
                a_next = borrow ? (b - a) : (a - b);
            }
            b = borrow ? a : b;
            uint32_t fgx = borrow ? fg0 : fg1;
            fg0  = borrow ? fg1 : fg0;
            fg0 = borrow ? (fg0 - fgx) : fg0;

            fg1 = fgx << 1;
            a = a_next >> 1;

            //asm("}");
        }

        // we care only about |f1| and |g1| in this subroutine
        fg0 = __shfl_xor(fg1, 1, 32);

        return factorx_t{fg1, fg0};
    }

    template<typename vec_t>
    static inline uint32_t cneg_v(vec_t& ret, const vec_t& a, uint32_t neg)
    {
        const size_t n = sizeof(vec_t)/sizeof(uint32_t);

        // asm("xor.b32 %0, %1, %2;"    : "=r"(ret[0]) : "r"(a[0]), "r"(neg));
        // asm("add.cc.u32 %0, %0, %1;" : "+r"(ret[0]) : "r"(neg&1));
        // for (size_t i=1; i<n; i++)
        //     asm("xor.b32 %0, %1, %2; addc.cc.u32 %0, %0, 0;"
        //         : "=r"(ret[i]) : "r"(a[i]), "r"(neg));

        // uint32_t sign;
        // asm("shr.s32 %0, %1, 31;" : "=r"(sign) : "r"(ret[n-1]));
        
        ret[0] = a[0] ^ neg;
        uint32_t carry = 0;
        ret[0] = __builtin_addc(ret[0], neg & 1, 0, &carry);
        #pragma unroll
        for (size_t i = 1; i < n; i++) {
            ret[i] = a[i] ^ neg;
            ret[i] = __builtin_addc(ret[i], 0, carry, &carry);
        }

        uint32_t sign = ((int32_t)ret[n-1]) >> 31;
        return sign;
    }

    static inline void smul_n_shift_x(mont_t& a, uint32_t& f_,
                                      mont_t& b, uint32_t& g_)
    {
        mont_t even, odd;
        uint32_t neg;
        size_t i;

        neg = ((int32_t)f_) >> 31;
        auto f = (f_ ^ neg) - neg;
        (void)cneg_v(a, a, neg);
        mul_n(&even[0], &a[0], f);
        mul_n(&odd[0], &a[1], f);
        odd[n-1] -= f & neg;

        neg = ((int32_t)g_) >> 31;
        auto g = (g_ ^ neg) - neg;
        (void)cneg_v(b, b, neg);
        uint32_t c1 = cmad_n(&even[0], &b[0], g);
        odd[n-1] += c1;
        cmad_n(&odd[0], &b[1], g);
        odd[n-1] -= g & neg;

        c1 = cadd_n(&even[1], &odd[0], n-1);
        odd[n-1] += c1;

        #pragma unroll
        for (i = 0; i < n-1; i++) {
            even[i] = (even[i] >> 30) | (even[i+1] << 2);
        }
        even[i] = (even[i] >> 30) | (odd[i] << 2);

        neg = ((int32_t)odd[i]) >> 31;
        f_ = (f_ ^ neg) - neg;
        g_ = (g_ ^ neg) - neg;
        (void)cneg_v(a, even, neg);
    }

    static inline uint32_t smul_2x(wide_t& u, uint32_t f,
                                   wide_t& v, uint32_t g)
    {
        wide_t even, odd;
        uint32_t neg;

        neg = ((int32_t)f) >> 31;
        f = (f ^ neg) - neg;
        neg = cneg_v(u, u, neg);
        mul_n(&even[0], &u[0], f, 2*n);
        mul_n(&odd[0], &u[1], f, 2*n);
        odd[2*n-1] -= f & neg;

        neg = ((int32_t)g) >> 31;
        g = (g ^ neg) - neg;
        neg = cneg_v(v, v, neg);
        uint32_t c1 = cmad_n(&even[0], &v[0], g, 2*n);
        odd[2*n-1] += c1;
        cmad_n(&odd[0], &v[1], g, 2*n);
        odd[2*n-1] -= g & neg;

        u[0] = even[0];
        u[1] = __builtin_addc(even[1], odd[0], 0, &c1);
        #pragma unroll
        for (size_t i = 2; i < 2*n; i++)
            u[i] = __builtin_addc(even[i], odd[i-1], c1, &c1);
        odd[2*n-1] += c1;

        return odd[2*n-1];
        
    }

protected:
    /*
     * Even thread holds |a| and |u|, while odd one - |b| and |v|. They need
     * to exchange the values, but then perform the multiplications in
     * parallel. The improvement [for 38x-bit moduli] is >20% and ~5.5KB
     * code size reduction in comparison to a single-threaded version.
     */
    static inline mont_t ct_inverse_mod_x(const mont_t& inp)
    {
	/*if(threadIdx.x < 8)
            printf("lane %d partner %d\n", threadIdx.x,
                __shfl_xor(threadIdx.x,1,32));*/
        if (N%32 != 0 && MOD == MODx) asm("s_trap 2;");

        const uint32_t tid = threadIdx.x&1;
        const uint32_t nbits = 2*n*32;

        mont_t a_b = csel(MOD, inp, tid);
        wide_t u_v, v_u;

        u_v[0] = tid^1;
        v_u[0] = tid;
        #pragma unroll
        for (size_t i=1; i<2*n; i++)
            u_v[i] = v_u[i] = 0;

        #pragma unroll 1
        for (uint32_t i=0; i<nbits/(32-2); i++) {
            mont_t b_a = shfl_xor(a_b);
            approx_t a_, b_;
            ab_approximation_n(a_, a_b, b_, b_a);
            auto fg = inner_loop_x(a_, b_);
            smul_n_shift_x(a_b, fg.f0, b_a, fg.g0);
            (void)smul_2x(u_v, fg.f0, v_u, fg.g0);
            v_u = shfl_xor(u_v);
        }

        uint32_t b_a = __shfl_xor(a_b[0], 1, 32);
        auto fg = inner_loop_x(a_b[0], b_a, nbits%30);
        auto top = (int32_t)smul_2x(u_v, fg.f0, v_u, fg.g0);
        //if (threadIdx.x == 0)
	  //  printf("top in ct_inverse_mod_x %d\n", top);
	assert(top==0|top==1|top==-1 && "top error in ct_inverse_mod_x");
	bool flag = top < 0;
        if (flag) {
	    uint32_t carry = 0;
            u_v[n] = __builtin_addc(u_v[n], MODx[0], 0, &carry);
            #pragma unroll
            for (size_t i = 1; i < n; i++) {
                u_v[n+i] = __builtin_addc(u_v[n+i], MODx[i], carry, &carry);
            }
            top += (int32_t)carry;
        }
        
        auto sign = 0 - top;        /* top is 1, 0 or -1 */
        top |= sign;
        #pragma unroll
        for (size_t i=0; i<n; i++)
            a_b[i] = MODx[i] & top;
        sign >>= 31;
        (void)cneg_v(a_b, a_b, sign);
        cadd_n(&u_v[n], &a_b[0]);

        mont_t ret = u_v;
        ret.to();
        return ret;
    }

public:
    inline mont_t reciprocal() const
    {
        bool a_zero = is_zero();
        mont_t a = csel(ONE, even, a_zero);
	if (a_zero) {
            assert("csel should return 1 for zero input" && a.is_one());
        }
        mont_t b = shfl_xor(a);
        a *= b;                     // a*b
        a = ct_inverse_mod_x(a);    // 1/(a*b)
        a *= b;                     // b/(a*b) == 1/a
	if (!a_zero) {
	    assert("a*a^-1 should be 1" && (a*even).is_one());
	}
        return czero(a, a_zero);
    }
    friend inline mont_t operator/(int one, const mont_t& a)
    {   if (one != 1) asm("s_trap 2;"); return a.reciprocal();   }
    friend inline mont_t operator/(const mont_t& a, const mont_t& b)
    {   return a * b.reciprocal();   }
    inline mont_t& operator/=(const mont_t& a)
    {   return *this *= a.reciprocal();   }

    inline void shfl_bfly(uint32_t laneMask)
    {
        #pragma unroll
        for (size_t i=0; i<n; i++)
            even[i] = __shfl_xor(even[i], laneMask, 32);
    }
};

# undef inline
# undef asm
