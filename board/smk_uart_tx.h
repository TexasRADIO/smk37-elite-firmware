/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: the TRS jack is a MIDI OUT, UART1 TX on PC7, 31250 baud. Adapted from FMini-1's hal/smk_uart_tx.h
 * (Copyright (C) 2026 Leo Kuroshita / Hügelton Instruments and contributors, GPL-3.0), which follows the stock
 * firmware's setup as the fm1-to-smk37 kit documents it: IOMAP_CON3 bit 7 clear and [23:20] = 1 (UT1 TX onto an
 * output channel); PC7 pull-up and pull-down, output low, input enabled; UART1 CON0 0x3400, the baud from the
 * 48 MHz clock, then UTEN, no RX. Upstream marks it unconfirmed on a unit: BB–37 turns it on only when MIDI OUT
 * includes TRS. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_gpio.h"

#define SMK_UT1_CON0  (*(volatile uint32_t *)0x12100u)
#define SMK_UT1_CON1  (*(volatile uint32_t *)0x12104u)
#define SMK_UT1_BAUD  (*(volatile uint32_t *)0x12108u)
#define SMK_UT1_BUF   (*(volatile uint32_t *)0x1210Cu)
#define SMK_CLK_CON1  (*(volatile uint32_t *)0x10010u)
#define SMK_IOMAP3    (*(volatile uint32_t *)0x51028u)
#define SMK_TX_PIN    (1u << 7)

FM1_INLINE void smk_uart1_tx_init(void)
{
    SMK_UT1_CON0 = 0x3400u;
    SMK_UT1_CON1 = 0;
    SMK_CLK_CON1 = (SMK_CLK_CON1 & ~(3u << 10)) | (1u << 10);
    SMK_IOMAP3 = (SMK_IOMAP3 & ~(0x80u | (0xFu << 20))) | (1u << 20);
    FM1_PR(FM1_PC, FM1_PU) |= SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_PD) |= SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_DIE) |= SMK_TX_PIN;
    SMK_UT1_BAUD = 48000000u / 31250u / 4u - 1u;
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 = 0x3400u;
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 |= 1u;
}

FM1_INLINE void smk_uart1_tx_byte(uint32_t b)
{
    SMK_UT1_CON0 |= 1u << 13;
    SMK_UT1_BUF = b & 0xFFu;
}

FM1_INLINE uint32_t smk_uart1_tx_done(void)
{
    return (SMK_UT1_CON0 >> 15) & 1u;
}
