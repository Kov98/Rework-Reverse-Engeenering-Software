/*
 * Rework stage 020 — semantic fixtures for future compiler/decompiler trials.
 * SOURCE ONLY. Do not treat these cases as compiled, tested or disassembled.
 *
 * Build future independent x86/x64 PE/ELF variants with debug and optimized
 * settings. Control-flow shapes are NOT guaranteed after optimization.
 * Keep these functions externally visible when building a shared library
 * or a binary with exported functions.
 */

#include <stdint.h>

volatile int rw20_observable_sink = 0;

extern int rw20_unknown_external(int input);
extern void rw20_may_not_return(void);

int rw20_diamond(int choose, int lhs, int rhs) {
    int selected;
    if (choose != 0)
        selected = lhs + 7;
    else
        selected = rhs - 3;
    return selected ^ choose;
}

int rw20_loop_recurrence(int count) {
    int previous = 1;
    int current = 2;
    for (int i = 0; i < count; ++i) {
        int next = previous + current;
        previous = current;
        current = next;
    }
    return previous ^ current;
}

/* Both x and y are carried across a backedge and exchange values. */
int rw20_backedge_swap(int a, int b, int iterations) {
    int x = a, y = b;
    for (int i = 0; i < iterations; ++i) {
        int saved_x = x;
        x = y;
        y = saved_x;
    }
    return x - y;
}

/* A candidate for an edge from a branch into a multi-predecessor join.
 * A particular machine CFG is not guaranteed by the C source.
 */
int rw20_critical_edge_candidate(int a, int b, int flag) {
    int result = a;
    if (flag != 0) {
        result = b;
        rw20_observable_sink = result;
    }
    if (result > 5)
        result += 3;
    return result;
}

int rw20_volatile_memory(volatile int *address, int value) {
    *address = value;
    int observed = *address;
    return observed + 1;
}

/* Unknown calls must not be marked pure, noreturn, or effects-free. */
int rw20_external_call(int x) {
    rw20_observable_sink = x;
    return rw20_unknown_external(x) + rw20_observable_sink;
}

int rw20_indirect_call(int (*callback)(int), int x) {
    return callback(x);
}

/* This call has UNKNOWN return behavior unless proven by trusted metadata.
 * A compiler may insert platform-specific control behavior.
 */
int rw20_possibly_nonreturning(int x) {
    if (x == 0)
        rw20_may_not_return();
    return x + 1;
}

int rw20_bitwidth_sign(uint32_t a, int32_t b) {
    uint32_t high = a >> 1;
    int32_t negative = b < 0 ? -b : b; /* Avoid INT_MIN in fixture inputs. */
    return (int)(high ^ (uint32_t)negative);
}

int rw20_nested_branch(int a, int b, int c) {
    if (a > 0) {
        if (b < 0)
            return c + 1;
        return b + c;
    }
    if (c == 0)
        return a;
    return a - b;
}
