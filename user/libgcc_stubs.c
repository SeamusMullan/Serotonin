/**
 * @file libgcc_stubs.c
 * @brief 64-bit division and modulo operations for 32-bit systems
 *
 * Provides software implementations of 64-bit arithmetic operations
 * that are not natively supported on 32-bit architectures. These
 * functions are typically required by GCC for 64-bit integer operations.
 */

// 64-bit division/modulo for 32-bit systems
typedef unsigned long long u64;
typedef long long s64;

typedef union {
// cppcheck-suppress unusedStructMember
    u64 all;
    struct {
// cppcheck-suppress unusedStructMember
        unsigned int low;
// cppcheck-suppress unusedStructMember
        unsigned int high;
// cppcheck-suppress unusedStructMember
    } s;
} udwords;

/**
 * @brief Unsigned 64-bit division with optional remainder
 *
 * Performs division of two 64-bit unsigned integers using shift-and-subtract
 * algorithm suitable for systems without native 64-bit division support.
 *
 * @param num Numerator (dividend)
 * @param den Denominator (divisor)
 * @param rem_p Pointer to store remainder (can be NULL if not needed)
 * @return Quotient of num / den
 */
u64 __udivmoddi4(u64 num, u64 den, u64 *rem_p) {
    u64 quot = 0, qbit = 1;

    if (den == 0) {
// cppcheck-suppress zerodivcond
        return 1 / ((unsigned)den);
    }

    while ((s64)den >= 0) {
        den <<= 1;
        qbit <<= 1;
    }

    while (qbit) {
        if (den <= num) {
            num -= den;
            quot += qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }

    if (rem_p)
        *rem_p = num;

    return quot;
}

/**
 * @brief Unsigned 64-bit division
 *
 * @param num Numerator
 * @param den Denominator
 * @return Quotient of num / den
 */
u64 __udivdi3(u64 num, u64 den) {
    return __udivmoddi4(num, den, 0);
}

/**
 * @brief Unsigned 64-bit modulo operation
 *
 * @param num Numerator
 * @param den Denominator
 * @return Remainder of num % den
 */
u64 __umoddi3(u64 num, u64 den) {
    u64 v;
    __udivmoddi4(num, den, &v);
    return v;
}

/**
 * @brief Signed 64-bit division with optional remainder
 *
 * Handles signed division by converting to unsigned operations
 * and adjusting the sign of the result accordingly.
 *
 * @param num Numerator (dividend)
 * @param den Denominator (divisor)
 * @param rem_p Pointer to store remainder (can be NULL if not needed)
 * @return Quotient of num / den
 */
s64 __divmoddi4(s64 num, s64 den, s64 *rem_p) {
    int minus = 0;
    s64 v;

    if (num < 0) {
        num = -num;
        minus = 1;
    }
    if (den < 0) {
        den = -den;
        minus ^= 1;
    }

    v = __udivmoddi4(num, den, (u64 *)rem_p);
    if (minus) {
        v = -v;
        if (rem_p)
            *rem_p = -(*rem_p);
    }

    return v;
}

/**
 * @brief Signed 64-bit division
 *
 * @param num Numerator
 * @param den Denominator
 * @return Quotient of num / den
 */
s64 __divdi3(s64 num, s64 den) {
    return __divmoddi4(num, den, 0);
}

/**
 * @brief Signed 64-bit modulo operation
 *
 * @param num Numerator
 * @param den Denominator
 * @return Remainder of num % den
 */
s64 __moddi3(s64 num, s64 den) {
    s64 v;
    __divmoddi4(num, den, &v);
    return v;
}
