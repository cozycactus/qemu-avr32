#include <stdint.h>

static uint32_t branch_equal(uint32_t lhs, uint32_t rhs)
{
    uint32_t taken;

    __asm__ volatile (
        "mov %0, 0\n\t"
        "cp.w %1, %2\n\t"
        "brne 1f\n\t"
        "mov %0, 1\n"
        "1:"
        : "=&r"(taken)
        : "r"(lhs), "r"(rhs)
        : "cc");

    return taken;
}

static uint32_t branch_not_equal(uint32_t lhs, uint32_t rhs)
{
    uint32_t taken;

    __asm__ volatile (
        "mov %0, 0\n\t"
        "cp.w %1, %2\n\t"
        "breq 1f\n\t"
        "mov %0, 1\n"
        "1:"
        : "=&r"(taken)
        : "r"(lhs), "r"(rhs)
        : "cc");

    return taken;
}

static uint32_t branch_after_add_zero(void)
{
    uint32_t taken;
    uint32_t value = 0xffffffffU;
    uint32_t one = 1U;

    __asm__ volatile (
        "mov %0, 0\n\t"
        "add %1, %2\n\t"
        "brne 1f\n\t"
        "mov %0, 1\n"
        "1:"
        : "=&r"(taken), "+r"(value)
        : "r"(one)
        : "cc");

    return taken;
}

int main(void)
{
    if (branch_equal(0x12345678U, 0x12345678U) != 1U) {
        return 1;
    }
    if (branch_equal(0x12345678U, 0x12345679U) != 0U) {
        return 2;
    }
    if (branch_not_equal(0x12345678U, 0x12345679U) != 1U) {
        return 3;
    }
    if (branch_not_equal(0x12345678U, 0x12345678U) != 0U) {
        return 4;
    }
    if (branch_after_add_zero() != 1U) {
        return 5;
    }

    return 0;
}
