#include <stdint.h>

#define UART0_BASE 0x10000000u
#define UART_RBR 0x00u
#define UART_THR 0x00u
#define UART_LSR 0x05u

#define UART_LSR_DR 0x01u
#define UART_LSR_THRE 0x20u
#define ASCII_EOT 0x04u

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
    int c = uart_getc();
    return c == ASCII_EOT ? -1 : c;
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

void starttime(void)
{
    return;
}

void stoptime(void)
{
    return;
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
