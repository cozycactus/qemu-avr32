#include <stdint.h>

static uint32_t lsl_imm(uint32_t value)
{
    __asm__ volatile ("lsl %0, 5" : "+r"(value) : : "cc");
    return value;
}

static uint32_t lsr_imm(uint32_t value)
{
    __asm__ volatile ("lsr %0, 7" : "+r"(value) : : "cc");
    return value;
}

static uint32_t lsl_reg(uint32_t value, uint32_t shift)
{
    uint32_t out;

    __asm__ volatile ("lsl %0, %1, %2" : "=r"(out) : "r"(value), "r"(shift) : "cc");
    return out;
}

static uint32_t lsr_reg(uint32_t value, uint32_t shift)
{
    uint32_t out;

    __asm__ volatile ("lsr %0, %1, %2" : "=r"(out) : "r"(value), "r"(shift) : "cc");
    return out;
}

int main(void)
{
    if (lsl_imm(0x00000011U) != 0x00000220U) {
        return 1;
    }
    if (lsr_imm(0x80000100U) != 0x01000002U) {
        return 2;
    }
    if (lsl_reg(0x00000013U, 8U) != 0x00001300U) {
        return 3;
    }
    if (lsr_reg(0xf0000000U, 4U) != 0x0f000000U) {
        return 4;
    }

    return 0;
}
