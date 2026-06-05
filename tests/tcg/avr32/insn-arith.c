#include <stdint.h>

static uint32_t add_rr(uint32_t lhs, uint32_t rhs)
{
    __asm__ volatile ("add %0, %1" : "+r"(lhs) : "r"(rhs) : "cc");
    return lhs;
}

static uint32_t sub_rr(uint32_t lhs, uint32_t rhs)
{
    __asm__ volatile ("sub %0, %1" : "+r"(lhs) : "r"(rhs) : "cc");
    return lhs;
}

static int test_add(void)
{
    if (add_rr(0x12345678U, 0x01020304U) != 0x1336597cU) {
        return 1;
    }
    if (add_rr(0xffffffffU, 1U) != 0U) {
        return 2;
    }
    if (add_rr(0x80000000U, 0x80000000U) != 0U) {
        return 3;
    }
    return 0;
}

static int test_sub(void)
{
    if (sub_rr(0x12345678U, 0x01020304U) != 0x11325374U) {
        return 11;
    }
    if (sub_rr(0U, 1U) != 0xffffffffU) {
        return 12;
    }
    if (sub_rr(0x80000000U, 0x7fffffffU) != 1U) {
        return 13;
    }
    return 0;
}

int main(void)
{
    int ret;

    ret = test_add();
    if (ret != 0) {
        return ret;
    }

    return test_sub();
}
