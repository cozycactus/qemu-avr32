#include <stdint.h>

#define PASS_MARK 0x600dca7eU
#define FAIL_MARK 0xbad00badU
#define QEMU_TEST_EXIT_BASE 0xfffff000U

extern int main(void);

static volatile uint32_t *const qemu_test_exit =
    (volatile uint32_t *)QEMU_TEST_EXIT_BASE;

void __sys_outc(char c)
{
    (void)c;
}

__attribute__((noreturn))
void _exit(int code)
{
    qemu_test_exit[0] = (uint32_t)code;
    qemu_test_exit[1] = 0U;
    qemu_test_exit[2] = code == 0 ? PASS_MARK : FAIL_MARK;

    for (;;) {
    }
}

__attribute__((section(".text.entry"), used, noreturn))
void _start(void)
{
    _exit(main());
}
