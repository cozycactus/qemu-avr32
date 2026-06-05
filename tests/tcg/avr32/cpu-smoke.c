#include <stdint.h>

#define PASS_MARK 0x600dca7eU
#define FAIL_MARK 0xbad00badU
#define EXPECTED_RESULT 0xb52ccd45U
#define QEMU_TEST_EXIT_BASE 0xfffff000U

volatile uint32_t avr32_test_result[4] __attribute__((section(".results"), used));

static volatile uint32_t *const qemu_test_exit =
    (volatile uint32_t *)QEMU_TEST_EXIT_BASE;

static inline __attribute__((always_inline)) uint32_t rotl32(uint32_t x,
                                                            unsigned int n)
{
    return (x << n) | (x >> (32U - n));
}

static inline __attribute__((always_inline)) uint32_t run_bench(uint32_t seed)
{
    uint32_t a = seed ^ 0xa5a55a5aU;
    uint32_t b = seed + 0x13579bdfU;
    uint32_t c = 0x10203040U;

    for (uint32_t i = 0; i < 32U; i++) {
        a += (b ^ i) + rotl32(c, i & 15U);
        b ^= rotl32(a, (i + 3U) & 15U);
        c += (a >> 3U) ^ (b << 1U) ^ (i * 0x01010101U);
    }

    return a ^ rotl32(b, 7U) ^ c;
}

int main(void)
{
    uint32_t result = run_bench(0x12345678U);
    uint32_t marker = result == EXPECTED_RESULT ? PASS_MARK : FAIL_MARK;

    avr32_test_result[0] = result;
    avr32_test_result[1] = marker;
    avr32_test_result[2] = EXPECTED_RESULT;
    avr32_test_result[3] = 0U;

    qemu_test_exit[0] = result;
    qemu_test_exit[1] = EXPECTED_RESULT;
    qemu_test_exit[2] = marker;

    return marker == PASS_MARK ? 0 : -1;
}
