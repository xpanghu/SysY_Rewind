#include <stdint.h>

#define UART0_BASE 0x10000000u
#define UART_RBR 0x00u
#define UART_THR 0x00u
#define UART_LSR 0x05u

#define UART_LSR_DR   0x01u
#define UART_LSR_THRE 0x20u

#define TEST_FINISHER_BASE 0x00100000u
#define FINISHER_PASS 0x5555u
#define FINISHER_FAIL 0x3333u

static volatile uint8_t* uart_reg(uint32_t offset)
{
    return (volatile uint8_t*)(uintptr_t)(UART0_BASE + offset);
}

static void uart_putc(char c)
{
    while (((*uart_reg(UART_LSR)) & UART_LSR_THRE) == 0) {
    }
    *uart_reg(UART_THR) = (uint8_t)c;
}

static int uart_getc(void)
{
    while (((*uart_reg(UART_LSR)) & UART_LSR_DR) == 0) {
    }
    return (int)(*uart_reg(UART_RBR));
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_space(int c)
{
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

static int last_char = 0;
static int last_char_valid = 0;

int getch(void)
{
    if (last_char_valid) {
        last_char_valid = 0;
        return last_char;
    }
    return uart_getc();
}

int getint(void)
{
    int c = getch();
    while (is_space(c)) {
        c = getch();
    }

    int is_neg = 0;
    if (c == '-') {
        is_neg = 1;
        c = getch();
    }

    int value = 0;
    while (is_digit(c)) {
        value = value * 10 + (c - '0');
        c = getch();
    }

    last_char = c;
    last_char_valid = 1;
    return is_neg ? -value : value;
}

int getarray(int a[])
{
    int n = getint();
    for (int i = 0; i < n; ++i) {
        a[i] = getint();
    }
    return n;
}

void putch(int ch)
{
    uart_putc((char)ch);
}

void putint(int num)
{
    if (num == 0) {
        putch('0');
        return;
    }

    if (num < 0) {
        putch('-');
        num = -num;
    }

    char digits[16];
    int idx = 0;
    while (num != 0) {
        digits[idx++] = (char)('0' + (num % 10));
        num /= 10;
    }

    while (idx > 0) {
        putch(digits[--idx]);
    }
}

void putarray(int n, int a[])
{
    putint(n);
    putch(':');
    for (int i = 0; i < n; ++i) {
        putch(' ');
        putint(a[i]);
    }
    putch('\n');
}

#define CLINT_BASE   0x02000000u
#define MTIME_ADDR   (CLINT_BASE + 0xBFF8u)

static uint64_t read_mtime(void)
{
    volatile uint32_t *low  = (volatile uint32_t *)(MTIME_ADDR);
    volatile uint32_t *high = (volatile uint32_t *)(MTIME_ADDR + 4);
    uint32_t lo, hi;

    do {
        hi = *high;
        lo = *low;
    } while (hi != *high);

    return ((uint64_t)hi << 32) | lo;
}

static uint64_t timer_start = 0;

void starttime(void)
{
    timer_start = read_mtime();
}

void stoptime(void)
{
    uint64_t end = read_mtime();
    uint64_t diff = end - timer_start;

    // QEMU virt mtime freq = 10 MHz, diff / 10 = microseconds
    uint64_t us = diff / 10;

    putch(35);  // '#'
    putch(32);  // ' '
    putint((int)(us & 0x7FFFFFFF));
    putch('\n');
}

void sysy_shutdown(int code)
{
    volatile uint32_t* finisher = (volatile uint32_t*)(uintptr_t)TEST_FINISHER_BASE;
    if (code == 0) {
        *finisher = FINISHER_PASS;
    } else {
        *finisher = ((uint32_t)code << 16) | FINISHER_FAIL;
    }

    for (;;) {
    }
}
