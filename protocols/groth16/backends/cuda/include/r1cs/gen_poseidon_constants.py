#!/usr/bin/env python3
"""
Convert the reference Poseidon constants in poseidonperm_x5_254_3.sage into the
Montgomery-form C++ literal form used by include/fields/alt_bn128.cuh.

Everything is emitted for the *scalar* field fr of alt_bn128 (the sage file's
`prime` is exactly alt_bn128's r), so the constants are typed `fr_t`.

Montgomery form here means: each F_r element c is stored as c * 2^256 mod r,
split into 4 little-endian 64-bit limbs (limb 0 is the least significant), which
is what the `FR_T(vec256, ...)` macro in alt_bn128.cuh expects.

Usage:
    python3 gen_poseidon_constants.py                # print the constants
    python3 gen_poseidon_constants.py --check        # also self-test the conversion

The script only reads the .sage file and writes to stdout; it never touches
poseidon.cuh.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SAGE_PATH = os.path.join(HERE, "poseidonperm_x5_254_3.sage")

R = 1 << 256
LIMMBITS = 64
N_LIMBS = 4

# alt_bn128's scalar field modulus, i.e. the `fr` in include/fields/alt_bn128.cuh.
# The sage file's `prime` must equal this for `fr_t` to be the right type.
ALT_BN128_R = 0x30644E72E131A029B85045B68181585D2833E84879B9709143E1F593F0000001


def note(msg):
    """Diagnostics go to stderr so stdout stays a clean, pasteable constants block."""
    print(msg, file=sys.stderr)


def parse_sage(path):
    """Pull t, R_F, R_P, the round constants and the MDS matrix out of the sage file."""
    src = open(path).read()

    def scalar(name):
        m = re.search(r"^\s*%s\s*=\s*(\d+)\s*$" % name, src, re.M)
        if not m:
            sys.exit("could not find %s in %s" % (name, path))
        return int(m.group(1))

    t = scalar("t")
    r_f = scalar("R_F")
    r_p = scalar("R_P")
    prime = int(re.search(r"^\s*prime\s*=\s*(0x[0-9a-fA-F]+)\s*$", src, re.M).group(1), 16)

    m = re.search(r"round_constants\s*=\s*\[(.*?)\]", src, re.S)
    if not m:
        sys.exit("could not find round_constants in %s" % path)
    round_constants = [int(h, 16) for h in re.findall(r"0x[0-9a-fA-F]+", m.group(1))]

    m = re.search(r"MDS_matrix\s*=\s*\[(.*?)\]\s*$", src, re.S | re.M)
    if not m:
        sys.exit("could not find MDS_matrix in %s" % path)
    rows = re.findall(r"\[(.*?)\]", m.group(1), re.S)
    mds = [[int(h, 16) for h in re.findall(r"0x[0-9a-fA-F]+", row)] for row in rows]

    n_rc = (r_f + r_p) * t
    if len(round_constants) != n_rc:
        sys.exit("expected %d round constants, sage file has %d" % (n_rc, len(round_constants)))
    if len(mds) != t or any(len(row) != t for row in mds):
        sys.exit("expected a %dx%d MDS matrix, sage file has %dx%d"
                 % (t, t, len(mds), len(mds[0]) if mds else 0))
    if prime != ALT_BN128_R:
        sys.exit("sage prime %#x != alt_bn128 r %#x; constants would not be fr_t"
                 % (prime, ALT_BN128_R))

    return prime, t, r_f, r_p, round_constants, mds


def to_montgomery(c, r):
    """F_r element -> the 4 little-endian 64-bit limbs of c * 2^256 mod r."""
    v = (c % r) * R % r
    return tuple((v >> (LIMMBITS * i)) & ((1 << LIMMBITS) - 1) for i in range(N_LIMBS))


def fmt(x, r, sep=","):
    """One FR_T literal. `sep` is "," inside an array initializer, ";" for a
    standalone declaration, matching the style in fields/alt_bn128.cuh."""
    limbs = to_montgomery(x, r)
    return "FR_T(vec256, %s )%s" % (", ".join("0x%016xu" % l for l in limbs), sep)


def emit(r, t, r_f, r_p, round_constants, mds):
    out = []
    w = out.append
    n_rc = len(round_constants)

    w("// Poseidon permutation, x^5 S-box, t=%d, R_F=%d, R_P=%d, over alt_bn128's" % (t, r_f, r_p))
    w("// scalar field fr (254 bits).")
    w("//")
    w("// All constants below are in Montgomery form, i.e. c * 2^256 mod r, matching the")
    w("// representation used by fr_t in fields/alt_bn128.cuh.")
    w("")
    w("constexpr int POSEIDON_T = %d;" % t)
    w("constexpr int POSEIDON_R_F = %d;" % r_f)
    w("constexpr int POSEIDON_R_P = %d;" % r_p)
    w("constexpr int POSEIDON_N_ROUND_CONSTANTS = (POSEIDON_R_F + POSEIDON_R_P) * POSEIDON_T;")
    w("")
    w("// Round constants, consumed in order: R_F/2 full rounds, R_P partial rounds,")
    w("// then R_F/2 full rounds; t constants per round.")
    w("const fr_t poseidon_round_constants[POSEIDON_N_ROUND_CONSTANTS] = {")
    for i in range(0, n_rc, t):
        w("        " + " ".join(fmt(c, r) for c in round_constants[i:i + t]))
    w("};")
    w("")
    w("// Applied as row * state after every round; row-major, same order as the sage source.")
    w("const fr_t poseidon_mds_matrix[POSEIDON_T][POSEIDON_T] = {")
    for row in mds:
        w("        " + " ".join(fmt(c, r) for c in row))
    w("};")
    return "\n".join(out)


def check(r, t, r_f, r_p, round_constants, mds):
    """Re-run the sage permutation in Montgomery form and compare against plain form.

    Multiplying the plain-domain constants by 2^256 must be a homomorphism for
    addition, the x^5 S-box and the MDS product, so Montgomery-perm(x * 2^256) has
    to equal perm(x) * 2^256. Also pins down the limb order against alt_bn128.cuh.
    """
    inv_r = pow(R, -1, r)

    def mont_mul(a, b):
        return a * b * inv_r % r

    # The reference file's own `ALT_BN128_rone` is 2^256 mod r in limb form, so it
    # independently confirms both the limb order and the Montgomery basis.
    expect_rone = (0xAC96341C4FFFFFFB, 0x36FC76959F60CD29, 0x666EA36F7879462E, 0x0E0A77C19A07DF2F)
    got_rone = to_montgomery(1, r)
    if got_rone != expect_rone:
        sys.exit("FAIL: 2^256 mod r is %s, alt_bn128.cuh says %s" % (got_rone, expect_rone))

    def mul(a, b, mont):
        # Integer-level products: plain domain is a*b, Montgomery domain is a*b*R^-1.
        return (mont_mul(a, b) if mont else a * b) % r

    def sbox(a, mont):
        # x^5. The Montgomery representative of x^5 needs Montgomery products, not
        # a plain modpow -- (xR)^5 is x^5 R^5, not x^5 R. This is exactly what
        # fr_t::operator* does in the C++ version.
        if not mont:
            return pow(a, 5, r)
        a2 = mul(a, a, True)
        a4 = mul(a2, a2, True)
        return mul(a4, a, True)

    def perm(state, mont):
        scale = (lambda x: x * R % r) if mont else (lambda x: x % r)
        st = [scale(x) for x in state]
        rc = [scale(c) for c in round_constants]
        m = [[scale(c) for c in row] for row in mds]
        k = 0

        def full(rnds):
            nonlocal k
            for _ in range(rnds):
                st[:] = [st[i] + rc[k + i] for i in range(t)]
                k += t
                st[:] = [sbox(st[i], mont) for i in range(t)]
                st[:] = [sum(mul(m[i][j], st[j], mont) for j in range(t)) % r
                         for i in range(t)]

        def partial(rnds):
            nonlocal k
            for _ in range(rnds):
                st[:] = [st[i] + rc[k + i] for i in range(t)]
                k += t
                st[0] = sbox(st[0], mont)
                st[:] = [sum(mul(m[i][j], st[j], mont) for j in range(t)) % r
                         for i in range(t)]

        full(r_f // 2)
        partial(r_p)
        full(r_f // 2)
        return st

    inp = list(range(t))
    plain = perm(inp, mont=False)
    mont = perm(inp, mont=True)
    mont_limbs = [to_montgomery(v, r) for v in plain]

    def to_int(limbs):
        return sum(l << (LIMMBITS * i) for i, l in enumerate(limbs))

    bad = [i for i in range(t) if to_int(mont_limbs[i]) != mont[i]]
    note("# check: 2^256 mod r matches alt_bn128.cuh ALT_BN128_rone: ok")
    note("# check: sage permutation[0..%d] in Montgomery form == limb form of plain result: %s"
          % (t - 1, "ok" if not bad else "MISMATCH at %s" % bad))
    if bad:
        sys.exit(1)
    note("# check: input %s ->" % inp)
    for i in range(t):
        note("#   plain   [%d] = 0x%064x" % (i, plain[i]))
        note("#   montgomery[%d] = 0x%064x" % (i, mont[i]))


def verify_emitted(text, r, expected):
    """Re-parse the printed C++ literals and check they reduce back to the sage values.

    This validates the text that actually gets pasted into poseidon.cuh, not just the
    intermediate integers: it catches a bad limb split, a bad column order, or a
    malformed/unparseable literal.
    """
    limbs = re.findall(r"FR_T\(vec256,\s*(0x[0-9a-f]+u(?:,\s*0x[0-9a-f]+u){3})\s*\)\s*[,;]", text)
    if len(limbs) != len(expected):
        sys.exit("FAIL: emitted %d literals, expected %d" % (len(limbs), len(expected)))

    inv_r = pow(R, -1, r)
    for i, (lit, want) in enumerate(zip(limbs, expected)):
        parts = [int(p.rstrip("u"), 16) for p in lit.split(",")]
        if len(parts) != N_LIMBS:
            sys.exit("FAIL: literal %d has %d limbs, expected %d" % (i, len(parts), N_LIMBS))
        v = sum(p << (LIMMBITS * j) for j, p in enumerate(parts))
        if v * inv_r % r != want % r:
            sys.exit("FAIL: literal %d reduces to %#x, sage says %#x" % (i, v * inv_r % r, want % r))
    note("# check: all %d emitted literals decode back to the sage values: ok" % len(expected))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sage", default=SAGE_PATH, help="reference sage file (default: %s)" % SAGE_PATH)
    ap.add_argument("--check", action="store_true", help="self-test the conversion before printing")
    args = ap.parse_args()

    r, t, r_f, r_p, round_constants, mds = parse_sage(args.sage)
    text = emit(r, t, r_f, r_p, round_constants, mds)
    if args.check:
        check(r, t, r_f, r_p, round_constants, mds)
        verify_emitted(text, r, round_constants + [c for row in mds for c in row])
    print(text)


if __name__ == "__main__":
    main()
