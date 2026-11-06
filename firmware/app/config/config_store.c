/**
 * @file    config_store.c
 * @brief   Append-only A/B flash log for parameters.
 */
#include "config_store.h"

#include <string.h>
#include "crc.h"
#include "params.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t seq;
    uint32_t crc;           /* CRC-32 over magic..seq and all entries */
} cfg_hdr_t;

typedef struct {
    uint32_t key;
    uint32_t value;
} cfg_entry_t;

_Static_assert(sizeof(cfg_hdr_t) == 16u, "cfg header size");

#define CFG_MAX_ENTRIES     512u
#define REC_ALIGN           16u

static const cfg_flash_ops_t *s_ops;
static cfg_store_info_t       s_info;

typedef struct {
    bool     has_record;
    uint32_t seq;
    uint32_t rec_off;       /* offset of the newest valid record */
    uint32_t end;           /* first free (erased) offset         */
    bool     appendable;    /* log ends cleanly at 'end'          */
} bank_scan_t;

static uint32_t rec_size(uint32_t count)
{
    uint32_t n = (uint32_t)sizeof(cfg_hdr_t) + count * (uint32_t)sizeof(cfg_entry_t);
    return (n + REC_ALIGN - 1u) & ~(REC_ALIGN - 1u);
}

static uint32_t rec_crc(const cfg_hdr_t *h, const uint8_t *entries)
{
    uint32_t c = crc32_update(CRC32_INIT, h, 12u);
    c = crc32_update(c, entries, (size_t)h->count * sizeof(cfg_entry_t));
    return crc32_final(c);
}

static bool region_erased(const uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        if (p[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static void scan_bank(uint32_t bank, bank_scan_t *s)
{
    const uint8_t *base = s_ops->view(bank);
    uint32_t size = s_ops->bank_size;
    uint32_t off = 0u;

    memset(s, 0, sizeof(*s));
    s->appendable = true;

    while (off + sizeof(cfg_hdr_t) <= size) {
        cfg_hdr_t h;
        memcpy(&h, base + off, sizeof(h));
        if (h.magic == 0xFFFFFFFFu) {
            /* end of log - the rest must be blank to append safely */
            s->appendable = region_erased(base + off, size - off);
            break;
        }
        if (h.magic != CFG_RECORD_MAGIC || h.version != CFG_RECORD_VERSION ||
            h.count > CFG_MAX_ENTRIES || off + rec_size(h.count) > size ||
            rec_crc(&h, base + off + sizeof(h)) != h.crc) {
            s->appendable = false;          /* damaged: never write after it */
            break;
        }
        if (!s->has_record || (int32_t)(h.seq - s->seq) > 0) {
            s->has_record = true;
            s->seq = h.seq;
            s->rec_off = off;
        }
        off += rec_size(h.count);
    }
    s->end = off;
}

void config_store_init(const cfg_flash_ops_t *ops)
{
    s_ops = ops;
    memset(&s_info, 0, sizeof(s_info));
}

bool config_store_load(void)
{
    params_reset_defaults();
    s_info.loaded = false;
    if (s_ops == NULL) {
        return false;
    }

    bank_scan_t sc[2];
    scan_bank(0u, &sc[0]);
    scan_bank(1u, &sc[1]);

    int best = -1;
    for (int b = 0; b < 2; b++) {
        if (sc[b].has_record && (best < 0 || (int32_t)(sc[b].seq - sc[best].seq) > 0)) {
            best = b;
        }
    }
    if (best < 0) {
        s_info.bank = 0u;
        s_info.used = sc[0].appendable ? sc[0].end : s_ops->bank_size;
        return false;
    }

    const uint8_t *rec = s_ops->view((uint32_t)best) + sc[best].rec_off;
    cfg_hdr_t h;
    memcpy(&h, rec, sizeof(h));
    const uint8_t *ents = rec + sizeof(h);

    uint16_t n = params_count();
    for (uint32_t e = 0u; e < h.count; e++) {
        cfg_entry_t ent;
        memcpy(&ent, ents + e * sizeof(cfg_entry_t), sizeof(ent));
        bool found = false;
        for (uint16_t i = 0u; i < n; i++) {
            if (params_name_hash(params_def(i)->name) == ent.key) {
                (void)params_set_raw(i, ent.value);     /* clamps to the current range */
                found = true;
                break;
            }
        }
        if (found) {
            s_info.entries_loaded++;
        } else {
            s_info.entries_unknown++;
        }
    }

    s_info.loaded = true;
    s_info.seq = h.seq;
    s_info.bank = (uint32_t)best;
    s_info.used = sc[best].appendable ? sc[best].end : s_ops->bank_size;
    return true;
}

static bool write_record(uint32_t bank, uint32_t off, uint32_t seq)
{
    uint16_t n = params_count();
    cfg_hdr_t h = {
        .magic = CFG_RECORD_MAGIC,
        .version = CFG_RECORD_VERSION,
        .count = n,
        .seq = seq,
        .crc = 0u,
    };

    /* entries are written first into a local buffer to compute the CRC */
    static cfg_entry_t ents[CFG_MAX_ENTRIES];
    for (uint16_t i = 0u; i < n; i++) {
        ents[i].key = params_name_hash(params_def(i)->name);
        ents[i].value = params_get_raw(i);
    }
    h.crc = rec_crc(&h, (const uint8_t *)ents);

    if (!s_ops->program(bank, off, (const uint32_t *)&h, sizeof(h) / 4u)) {
        return false;
    }
    if (!s_ops->program(bank, off + (uint32_t)sizeof(h), (const uint32_t *)ents,
                        (uint32_t)n * 2u)) {
        return false;
    }

    /* read back */
    const uint8_t *rec = s_ops->view(bank) + off;
    cfg_hdr_t chk;
    memcpy(&chk, rec, sizeof(chk));
    return chk.magic == CFG_RECORD_MAGIC && rec_crc(&chk, rec + sizeof(chk)) == chk.crc;
}

bool config_store_save(void)
{
    if (s_ops == NULL) {
        return false;
    }
    uint32_t need = rec_size(params_count());
    if (need > s_ops->bank_size) {
        return false;
    }

    bank_scan_t sc;
    uint32_t bank = s_info.bank;
    scan_bank(bank, &sc);
    uint32_t seq = s_info.seq + 1u;

    bool ok = false;
    if (sc.appendable && sc.end + need <= s_ops->bank_size) {
        ok = write_record(bank, sc.end, seq);
    }
    if (!ok) {
        /* switch to the other bank: erase it, write at offset 0 */
        bank ^= 1u;
        s_info.erases++;
        ok = s_ops->erase(bank) && write_record(bank, 0u, seq);
    }
    if (ok) {
        scan_bank(bank, &sc);
        s_info.bank = bank;
        s_info.seq = seq;
        s_info.used = sc.end;
        s_info.saves++;
        s_info.loaded = true;
    }
    return ok;
}

const cfg_store_info_t *config_store_info(void)
{
    return &s_info;
}
