#include <stdint.h>

static uint32_t words[4] __attribute__((aligned(16)));
static uint8_t bytes[8] __attribute__((aligned(8)));

static uint32_t load_store_word(uint32_t value)
{
    uint32_t out;

    __asm__ volatile (
        "st.w %1[0], %2\n\t"
        "ld.w %0, %1[0]"
        : "=r"(out)
        : "r"(&words[1]), "r"(value)
        : "memory");

    return out;
}

static uint32_t load_store_halfword(uint32_t value)
{
    uint32_t out;

    __asm__ volatile (
        "st.h %1[0], %2\n\t"
        "ld.uh %0, %1[0]"
        : "=r"(out)
        : "r"(&bytes[2]), "r"(value)
        : "memory");

    return out;
}

static uint32_t load_store_byte(uint32_t value)
{
    uint32_t out;

    __asm__ volatile (
        "st.b %1[0], %2\n\t"
        "ld.ub %0, %1[0]"
        : "=r"(out)
        : "r"(&bytes[5]), "r"(value)
        : "memory");

    return out;
}

int main(void)
{
    if (load_store_word(0x12345678U) != 0x12345678U) {
        return 1;
    }
    if (load_store_halfword(0x0000a55aU) != 0x0000a55aU) {
        return 2;
    }
    if (load_store_byte(0x000000c3U) != 0x000000c3U) {
        return 3;
    }

    return 0;
}
