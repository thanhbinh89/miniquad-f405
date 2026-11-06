/**
 * @file    emu_rom.c
 * @brief   STM32F405 system memory bootloader (USART, AN3155 v3.1) - the
 *          subset the web configurator's recovery flasher uses:
 *          GET, GET_VERSION, GET_ID, READ_MEMORY, GO, WRITE_MEMORY,
 *          EXTENDED_ERASE (sectors and mass erase).
 *
 * The real ROM loader auto-bauds on 0x7F and runs 8E1; on a byte stream
 * those details vanish.
 */
#include <string.h>

#include "emu_internal.h"

#define ACK     0x79u
#define NACK    0x1Fu

typedef enum {
    R_SYNC,         /* waiting for 0x7F                          */
    R_CMD,          /* command byte + complement                 */
    R_ADDR,         /* 4 address bytes (MSB first) + XOR         */
    R_READ_LEN,     /* N, ~N                                     */
    R_WRITE_DATA,   /* N, N+1 bytes, XOR(N, data)                */
    R_ERASE_N,      /* 2 bytes N (MSB first)                     */
    R_ERASE_LIST,   /* 2 (N+1) sector bytes + XOR of everything  */
    R_ERASE_SPECIAL /* checksum after 0xFFFF / 0xFFFE / 0xFFFD   */
} rom_state_t;

static rom_state_t s_st;
static uint8_t     s_cmd;
static uint8_t     s_buf[600];
static uint32_t    s_n;
static uint32_t    s_need;
static uint32_t    s_addr;
static uint16_t    s_erase_n;

static const uint8_t CMDS[] = { 0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x44, 0x63, 0x73, 0x82, 0x92 };

static void out1(uint8_t b)
{
    (void)emu_uart_write(&b, 1u);
}

static void out(const uint8_t *p, size_t n)
{
    (void)emu_uart_write(p, n);
}

void rom_reset(void)
{
    s_st = R_SYNC;
    s_n = 0u;
}

static bool addr_ok(uint32_t a, uint32_t len)
{
    /* flash, system memory / OTP / option bytes, SRAM */
    return (a >= 0x08000000u && a + len <= 0x08100000u) ||
           (a >= 0x1FFF0000u && a + len <= 0x1FFFC010u) ||
           (a >= 0x20000000u && a + len <= 0x20020000u);
}

static void begin(uint8_t cmd)
{
    s_cmd = cmd;
    s_n = 0u;
    switch (cmd) {
    case 0x00: {                                    /* GET */
        uint8_t r[3 + sizeof(CMDS)];
        r[0] = ACK;
        r[1] = (uint8_t)sizeof(CMDS);               /* N = bytes - 1 (version + list) */
        r[2] = 0x31u;
        memcpy(&r[3], CMDS, sizeof(CMDS));
        out(r, sizeof(r));
        out1(ACK);
        s_st = R_CMD;
        break;
    }
    case 0x01: {                                    /* GET VERSION & protection */
        const uint8_t r[] = { ACK, 0x31u, 0x00u, 0x00u, ACK };
        out(r, sizeof(r));
        s_st = R_CMD;
        break;
    }
    case 0x02: {                                    /* GET ID: 0x0413 = F405/F407 */
        const uint8_t r[] = { ACK, 0x01u, 0x04u, 0x13u, ACK };
        out(r, sizeof(r));
        s_st = R_CMD;
        break;
    }
    case 0x11:                                      /* READ MEMORY */
    case 0x21:                                      /* GO */
    case 0x31:                                      /* WRITE MEMORY */
        out1(ACK);
        s_st = R_ADDR;
        s_need = 5u;
        break;
    case 0x44:                                      /* EXTENDED ERASE */
        out1(ACK);
        s_st = R_ERASE_N;
        s_need = 2u;
        break;
    default:                                        /* protection commands: not emulated */
        out1(NACK);
        s_st = R_CMD;
        break;
    }
}

static void on_byte(uint8_t b)
{
    switch (s_st) {
    case R_SYNC:
        if (b == 0x7Fu) {
            out1(ACK);
            s_st = R_CMD;
            s_n = 0u;
        }
        return;

    case R_CMD:
        s_buf[s_n++] = b;
        if (s_n == 2u) {
            s_n = 0u;
            if ((uint8_t)(s_buf[0] ^ s_buf[1]) != 0xFFu) {
                out1(NACK);
                return;
            }
            begin(s_buf[0]);
        }
        return;

    case R_ADDR:
        s_buf[s_n++] = b;
        if (s_n < s_need) {
            return;
        }
        s_n = 0u;
        if ((uint8_t)(s_buf[0] ^ s_buf[1] ^ s_buf[2] ^ s_buf[3]) != s_buf[4]) {
            out1(NACK);
            s_st = R_CMD;
            return;
        }
        s_addr = ((uint32_t)s_buf[0] << 24) | ((uint32_t)s_buf[1] << 16) | ((uint32_t)s_buf[2] << 8) | s_buf[3];
        if (!addr_ok(s_addr, 1u)) {
            out1(NACK);
            s_st = R_CMD;
            return;
        }
        out1(ACK);
        if (s_cmd == 0x11u) {
            s_st = R_READ_LEN;
            s_need = 2u;
        } else if (s_cmd == 0x31u) {
            s_st = R_WRITE_DATA;
            s_need = 0u;                            /* known after the first byte */
        } else {                                    /* GO */
            s_st = R_SYNC;
            if (emu_flash_sector(s_addr) >= 0) {
                emu_start_from_flash();
            }
        }
        return;

    case R_READ_LEN:
        s_buf[s_n++] = b;
        if (s_n < 2u) {
            return;
        }
        s_n = 0u;
        s_st = R_CMD;
        if ((uint8_t)(s_buf[0] ^ s_buf[1]) != 0xFFu) {
            out1(NACK);
            return;
        }
        {
            uint32_t len = (uint32_t)s_buf[0] + 1u;
            if (!addr_ok(s_addr, len)) {
                out1(NACK);
                return;
            }
            out1(ACK);
            if (s_addr >= 0x08000000u && s_addr < 0x08100000u) {
                out(&emu_flash_mem()[s_addr - 0x08000000u], len);
            } else {
                uint8_t z[256];
                memset(z, 0, sizeof(z));
                out(z, len);
            }
        }
        return;

    case R_WRITE_DATA:
        s_buf[s_n++] = b;
        if (s_n == 1u) {
            s_need = (uint32_t)b + 1u + 2u;         /* N, data, checksum */
            return;
        }
        if (s_n < s_need) {
            return;
        }
        {
            uint32_t len = (uint32_t)s_buf[0] + 1u;
            uint8_t x = 0u;
            for (uint32_t i = 0u; i < len + 1u; i++) {
                x ^= s_buf[i];
            }
            s_n = 0u;
            s_st = R_CMD;
            if (x != s_buf[len + 1u] || emu_flash_sector(s_addr) < 0 || !addr_ok(s_addr, len) ||
                !emu_flash_write(s_addr, &s_buf[1], len)) {
                out1(NACK);
                return;
            }
            out1(ACK);
        }
        return;

    case R_ERASE_N:
        s_buf[s_n++] = b;
        if (s_n < 2u) {
            return;
        }
        s_erase_n = (uint16_t)((s_buf[0] << 8) | s_buf[1]);
        if (s_erase_n >= 0xFFF0u) {
            s_st = R_ERASE_SPECIAL;
            s_need = 3u;
        } else {
            s_st = R_ERASE_LIST;
            s_need = 2u + 2u * ((uint32_t)s_erase_n + 1u) + 1u;
            if (s_need > sizeof(s_buf)) {
                out1(NACK);
                s_st = R_CMD;
                s_n = 0u;
            }
        }
        return;

    case R_ERASE_SPECIAL:
        s_buf[s_n++] = b;
        s_n = 0u;
        s_st = R_CMD;
        if ((uint8_t)(s_buf[0] ^ s_buf[1]) != b || s_erase_n != 0xFFFFu) {
            out1(NACK);                             /* only mass erase exists on the F405 */
            return;
        }
        {
            uint32_t ms = 0u;
            for (int i = 0; i < EMU_SECTORS; i++) {
                ms += emu_flash_erase(i);
            }
            emu_cpu_block(ms / 2u);                 /* mass erase is faster than 12 sector erases */
            out1(ACK);
        }
        return;

    case R_ERASE_LIST:
        s_buf[s_n++] = b;
        if (s_n < s_need) {
            return;
        }
        {
            uint8_t x = 0u;
            for (uint32_t i = 0u; i < s_need - 1u; i++) {
                x ^= s_buf[i];
            }
            s_n = 0u;
            s_st = R_CMD;
            if (x != s_buf[s_need - 1u]) {
                out1(NACK);
                return;
            }
            uint32_t ms = 0u;
            for (uint32_t k = 0u; k <= s_erase_n; k++) {
                uint16_t sct = (uint16_t)((s_buf[2u + 2u * k] << 8) | s_buf[3u + 2u * k]);
                if (sct >= EMU_SECTORS) {
                    out1(NACK);
                    return;
                }
                ms += emu_flash_erase(sct);
            }
            emu_cpu_block(ms);
            out1(ACK);
        }
        return;
    }
}

void rom_tick(void)
{
    uint8_t b;
    while (!emu_cpu_blocked() && emu_uart_tx_free() > 512u && emu_uart_read(&b, 1u) == 1u) {
        on_byte(b);
        if (s_st == R_SYNC && s_cmd == 0x21u) {
            s_cmd = 0u;
            return;                                 /* GO: the board left the ROM */
        }
    }
}
