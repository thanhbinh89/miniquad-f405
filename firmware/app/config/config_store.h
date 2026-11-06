/**
 * @file    config_store.h
 * @brief   Power-loss safe parameter storage in two flash banks.
 *
 * Each bank (16 KB sector) is an append-only log of records:
 *
 *     header { magic, version, count, seq, crc32 }  +  count x { key, value }
 *
 * key = FNV-1a hash of the parameter name, so stored values survive firmware
 * updates that add, remove or reorder parameters. On save, a new record is
 * appended to the active bank; when it is full (or damaged) the other bank
 * is erased and written - the previous record therefore always survives an
 * interrupted save. On load, the valid record with the highest seq wins.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define CFG_RECORD_MAGIC    0x4643514Du     /* "MQCF" */
#define CFG_RECORD_VERSION  1u

typedef struct {
    bool           (*erase)(uint32_t bank);
    bool           (*program)(uint32_t bank, uint32_t offset, const uint32_t *words, uint32_t count);
    const uint8_t *(*view)(uint32_t bank);  /* read-only memory view of a bank */
    uint32_t         bank_size;
} cfg_flash_ops_t;

typedef struct {
    bool     loaded;            /* a valid record was found at boot   */
    uint32_t seq;               /* sequence number of the last record */
    uint32_t bank;              /* bank holding it                    */
    uint32_t used;              /* bytes used in that bank            */
    uint16_t entries_loaded;    /* entries that matched a parameter   */
    uint16_t entries_unknown;   /* stored keys without parameter      */
    uint32_t saves;
    uint32_t erases;
} cfg_store_info_t;

void config_store_init(const cfg_flash_ops_t *ops);

/** Reset g_cfg to defaults, then apply the newest valid record (if any). */
bool config_store_load(void);

/** Write the current g_cfg. Blocking (flash erase may take ~0.5 s). */
bool config_store_save(void);

const cfg_store_info_t *config_store_info(void);

/** Target flash backend (config banks in sectors 1 and 2). */
const cfg_flash_ops_t *config_store_target_ops(void);

#endif /* CONFIG_STORE_H */
