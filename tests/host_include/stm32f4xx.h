/*
 * Host-build stand-in for the CMSIS device header.
 *
 * Hardware independent modules include board.h (for BOARD_ID / BOARD_NAME)
 * which includes stm32f4xx.h. On the host nothing from the device header
 * may actually be used; this empty file only satisfies the include.
 */
#ifndef HOST_STM32F4XX_STUB_H
#define HOST_STM32F4XX_STUB_H
#endif
