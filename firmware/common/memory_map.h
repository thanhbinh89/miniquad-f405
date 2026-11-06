/**
 * @file    memory_map.h
 * @brief   Flash / RAM layout shared by the bootloader, the application, the
 *          linker scripts (preprocessed with the C preprocessor) and tools.
 *
 * Only plain integer literals are allowed in this file (no 'u' suffixes,
 * no casts) because it is also included by the *.ld.in linker scripts.
 *
 * STM32F405RG flash: 1 MB, sectors 0-3 16 KB, sector 4 64 KB, 5-11 128 KB.
 *
 *   0x0800_0000  sector 0   16 KB   bootloader
 *   0x0800_4000  sector 1   16 KB   config bank A (append-only log)
 *   0x0800_8000  sector 2   16 KB   config bank B
 *   0x0800_C000  sector 3.. 976 KB  application: 512 B header + vector table + image
 *
 * SRAM1+SRAM2 (128 KB at 0x2000_0000, DMA capable): .data/.bss, DMA buffers.
 * The last 64 bytes are a no-init mailbox shared by bootloader and app.
 * CCM (64 KB at 0x1000_0000, CPU only, NO DMA): application stack.
 */
#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#define MM_FLASH_BASE           0x08000000
#define MM_FLASH_SIZE           0x00100000

#define MM_BL_BASE              0x08000000
#define MM_BL_SIZE              0x00004000
#define MM_BL_SECTOR            0

#define MM_CFG_A_BASE           0x08004000
#define MM_CFG_A_SECTOR         1
#define MM_CFG_B_BASE           0x08008000
#define MM_CFG_B_SECTOR         2
#define MM_CFG_BANK_SIZE        0x00004000

#define MM_APP_BASE             0x0800C000
#define MM_APP_FIRST_SECTOR     3
#define MM_APP_HEADER_SIZE      0x00000200
#define MM_APP_VECTORS          0x0800C200
#define MM_APP_MAX_SIZE         0x000F4000      /* header + image, up to end of flash */

#define MM_SRAM_BASE            0x20000000
#define MM_SRAM_SIZE            0x00020000
#define MM_SHARED_SIZE          0x00000040
#define MM_SHARED_BASE          0x2001FFC0      /* MM_SRAM_BASE + MM_SRAM_SIZE - MM_SHARED_SIZE */

#define MM_CCM_BASE             0x10000000
#define MM_CCM_SIZE             0x00010000

/* STM32F405 system memory (ROM bootloader, AN2606) */
#define MM_SYSMEM_BASE          0x1FFF0000

/* Factory calibration / device information */
#define MM_UID_BASE             0x1FFF7A10
#define MM_FLASH_SIZE_REG       0x1FFF7A22
#define MM_VREFINT_CAL          0x1FFF7A2A      /* raw @30 C, VDDA = 3.3 V */
#define MM_TS_CAL1              0x1FFF7A2C      /* raw @30 C */
#define MM_TS_CAL2              0x1FFF7A2E      /* raw @110 C */

#endif /* MEMORY_MAP_H */
