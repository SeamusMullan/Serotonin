// 64-bit division/modulo for 32-bit systems
typedef unsigned long long u64;
typedef long long s64;

typedef union {
    u64 all;
    struct {
        unsigned int low;
        unsigned int high;
    } s;
} udwords;

u64 __udivmoddi4(u64 num, u64 den, u64 *rem_p) {
    u64 quot = 0, qbit = 1;

    if (den == 0) {
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

u64 __udivdi3(u64 num, u64 den) {
    return __udivmoddi4(num, den, 0);
}

u64 __umoddi3(u64 num, u64 den) {
    u64 v;
    __udivmoddi4(num, den, &v);
    return v;
}

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

s64 __divdi3(s64 num, s64 den) {
    return __divmoddi4(num, den, 0);
}

s64 __moddi3(s64 num, s64 den) {
    s64 v;
    __divmoddi4(num, den, &v);
    return v;
}