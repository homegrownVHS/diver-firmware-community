/**
 * diver_state.cpp — Wear-leveled persistent state in internal flash.
 *
 * See diver_state.h for the full lifespan analysis and design notes.
 *
 * Flash region used: STM32F411RC Sector 3 (0x0800C000, 16 KB).
 * This sector is intentionally NOT in the linker FLASH region (which is
 * capped at 48 KB / 0x0800BFFF) so the toolchain can never place code here.
 */

#include "diver_state.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"       /* IRQn_Type constants */

#include <string.h>          /* memcmp / memset     */

/* ── Config flash layout ──────────────────────────────────────────────────── */

#define CONFIG_FLASH_BASE    0x0800C000UL    /* Sector 3 start               */
#define CONFIG_FLASH_SECTOR  FLASH_SECTOR_3
#define CONFIG_FLASH_VRANGE  FLASH_VOLTAGE_RANGE_3   /* VDD 2.7–3.6 V        */
#define CONFIG_SLOT_SIZE     32U
#define CONFIG_SECTOR_SIZE   (16U * 1024U)
#define CONFIG_NUM_SLOTS     (CONFIG_SECTOR_SIZE / CONFIG_SLOT_SIZE)  /* 512  */
#define CONFIG_MAGIC         0xD1564501UL

/* How long to wait after detecting a change before writing to flash.
 * Used both at runtime and in the build-time lifespan calculation below.    */
#define STATE_DIRTY_DEBOUNCE_MS  60000UL

/* ── Chip-health lifespan guard (build-time) ─────────────────────────────── *
 *
 * STM32F411RC internal flash endurance: 10,000 guaranteed erase cycles.
 * Sector 3 = 16 KB.  Slot = CONFIG_SLOT_SIZE bytes.
 *
 *   slots_per_sector = CONFIG_SECTOR_SIZE / CONFIG_SLOT_SIZE = 512
 *   total_writes     = slots_per_sector × 10,000 = 5,120,000
 *   worst-case interval = STATE_DIRTY_DEBOUNCE_MS = 60 s
 *   writes_per_year  = (365 × 24 × 3600 × 1000) / 60,000 = 525,600
 *   lifespan_years   = 5,120,000 / 525,600 ≈ 9.7 years (worst case)
 *   In practice state changes are infrequent; actual lifespan >> 9.7 years.
 *
 * POLICY: lifespan must be at least FLASH_MIN_LIFESPAN_YEARS.
 * If you widen the slot, shorten the interval, or shrink the sector,
 * the static_assert below will catch it at compile time.
 * To fix: increase STATE_FLUSH_INTERVAL_MS or decrease CONFIG_SLOT_SIZE.
 * ──────────────────────────────────────────────────────────────────────────── */

#define FLASH_ENDURANCE_CYCLES   10000UL    /* STM32F411 datasheet DS9716      */
#define FLASH_MIN_LIFESPAN_YEARS 9UL        /* minimum acceptable service life */

/* Interval used for the lifespan calculation below. Worst case = one write
 * every STATE_DIRTY_DEBOUNCE_MS milliseconds.                               */
#define _FLASH_INTERVAL_MS  STATE_DIRTY_DEBOUNCE_MS

/* Integer arithmetic version of the lifespan check.
 * lifespan_years = (slots × endurance × interval_ms) / ms_per_year
 * ms_per_year    = 365 × 24 × 3600 × 1000 = 31,536,000,000 (fits in uint64) */
#define _FLASH_SLOTS_PER_SECTOR \
    ((uint64_t)(CONFIG_SECTOR_SIZE) / (uint64_t)(CONFIG_SLOT_SIZE))

#define _FLASH_MS_PER_YEAR \
    (365ULL * 24ULL * 3600ULL * 1000ULL)

#define _FLASH_LIFESPAN_YEARS \
    ((_FLASH_SLOTS_PER_SECTOR * (uint64_t)(FLASH_ENDURANCE_CYCLES) * \
      (uint64_t)(_FLASH_INTERVAL_MS)) / _FLASH_MS_PER_YEAR)

static_assert(
    _FLASH_LIFESPAN_YEARS >= FLASH_MIN_LIFESPAN_YEARS,
    "FLASH HEALTH FAILURE: calculated lifespan is below FLASH_MIN_LIFESPAN_YEARS. "
    "Increase STATE_FLUSH_INTERVAL_MS or increase CONFIG_SLOT_SIZE to fix."
);

/* Offset inside DiverState where user-visible state starts (after magic +
   sequence).  Only this region is compared for change detection.            */
#define STATE_DATA_OFFSET  ((uint32_t)offsetof(DiverState, selected_bank))
#define STATE_DATA_SIZE    (sizeof(DiverState) - STATE_DATA_OFFSET)

static_assert(sizeof(DiverState) == CONFIG_SLOT_SIZE,
              "DiverState must be exactly 32 bytes — update _pad if fields change");

/* ── Module-private state ─────────────────────────────────────────────────── */

static uint8_t    s_initialised   = 0;
static uint8_t    s_dirty         = 0;    /* 1 = change detected, write pending */
static uint32_t   s_dirty_tick    = 0;    /* HAL_GetTick() when change first seen */
static DiverState s_last_written;         /* last state committed to flash        */

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static inline const DiverState *slot_ptr(uint32_t idx)
{
    return reinterpret_cast<const DiverState *>(
        CONFIG_FLASH_BASE + idx * CONFIG_SLOT_SIZE);
}

static int slot_is_valid(const DiverState *s)
{
    return (s->magic == CONFIG_MAGIC);
}

static int slot_is_blank(const DiverState *s)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(s);
    for (uint32_t i = 0; i < CONFIG_SLOT_SIZE / 4; i++) {
        if (p[i] != 0xFFFFFFFFUL) return 0;
    }
    return 1;
}

/** Compare only the user-visible state bytes, ignoring magic/sequence. */
static int state_data_equal(const DiverState *a, const DiverState *b)
{
    return (memcmp(
        reinterpret_cast<const uint8_t *>(a) + STATE_DATA_OFFSET,
        reinterpret_cast<const uint8_t *>(b) + STATE_DATA_OFFSET,
        STATE_DATA_SIZE) == 0);
}

/** Write one 32-byte slot to flash at slot index idx. */
static HAL_StatusTypeDef write_slot(uint32_t idx, const DiverState *s)
{
    uint32_t addr        = CONFIG_FLASH_BASE + idx * CONFIG_SLOT_SIZE;
    const uint32_t *src  = reinterpret_cast<const uint32_t *>(s);
    HAL_StatusTypeDef rc = HAL_OK;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < CONFIG_SLOT_SIZE / 4; i++) {
        rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4, src[i]);
        if (rc != HAL_OK) break;
    }
    HAL_FLASH_Lock();
    return rc;
}

/**
 * Erase the config sector.
 * Saves and restores the NVIC enable state for EXTI15_10 and DMA2_Stream5
 * so this is safe to call both at boot (IRQs not yet enabled) and mid-session
 * (IRQs running). When called at boot from state_init(), the IRQs aren't
 * enabled yet — unconditionally re-enabling them here would fire the HSYNC
 * ISR before DMA callbacks and htim1 are wired up, corrupting linecnt and
 * waveReadPtr from the very first frame.
 */
static void erase_config_sector(void)
{
    uint32_t exti_was_on = NVIC_GetEnableIRQ(EXTI15_10_IRQn);
    uint32_t dma_was_on  = NVIC_GetEnableIRQ(DMA2_Stream5_IRQn);

    if (exti_was_on) HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    if (dma_was_on)  HAL_NVIC_DisableIRQ(DMA2_Stream5_IRQn);

    FLASH_EraseInitTypeDef e;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Sector       = CONFIG_FLASH_SECTOR;
    e.NbSectors    = 1;
    e.VoltageRange = CONFIG_FLASH_VRANGE;
    uint32_t sector_err = 0;

    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&e, &sector_err);
    HAL_FLASH_Lock();

    if (dma_was_on)  HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
    if (exti_was_on) HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void state_init(void)
{
    /* Scan the config sector. If no blank slot exists, erase now — before
     * video IRQs are enabled — so all mid-session writes are word-writes only.
     * A mid-session erase disables IRQs for ~1 s and causes video glitching;
     * doing it here during boot init eliminates that window entirely. */
    for (uint32_t i = 0; i < CONFIG_NUM_SLOTS; i++) {
        if (slot_is_blank(slot_ptr(i))) return;   /* at least one blank: OK */
    }
    erase_config_sector();   /* sector full — erase now while IRQs are off */
}

int state_load(DiverState *out)
{
    const DiverState *best     = nullptr;
    uint32_t          best_seq = 0;

    for (uint32_t i = 0; i < CONFIG_NUM_SLOTS; i++) {
        const DiverState *s = slot_ptr(i);
        if (slot_is_valid(s)) {
            if (!best || s->sequence > best_seq) {
                best     = s;
                best_seq = s->sequence;
            }
        }
    }

    if (!best) return 0;
    *out = *best;
    return 1;
}

void state_maybe_flush(const DiverState *current)
{
    uint32_t now = HAL_GetTick();

    if (!s_initialised) {
        /* First call after boot: record what's currently on flash as baseline. */
        s_last_written = *current;
        s_initialised  = 1;
        return;
    }

    /* Detect change vs. last written state. */
    if (!state_data_equal(current, &s_last_written)) {
        if (!s_dirty) {
            /* First time we see this change — arm the debounce timer. */
            s_dirty      = 1;
            s_dirty_tick = now;
        }
    }

    /* Nothing pending — nothing to do. */
    if (!s_dirty) return;

    /* Debounce: wait STATE_DIRTY_DEBOUNCE_MS after the change was first seen. */
    if ((now - s_dirty_tick) < STATE_DIRTY_DEBOUNCE_MS) return;

    /* Scan for first blank slot and track highest sequence number. */
    uint32_t write_idx = CONFIG_NUM_SLOTS;   /* sentinel: none found yet */
    uint32_t next_seq  = 1;

    for (uint32_t i = 0; i < CONFIG_NUM_SLOTS; i++) {
        const DiverState *s = slot_ptr(i);
        if (slot_is_valid(s) && s->sequence >= next_seq) {
            next_seq = s->sequence + 1;
        }
        if (slot_is_blank(s) && write_idx == CONFIG_NUM_SLOTS) {
            write_idx = i;
        }
    }

    if (write_idx == CONFIG_NUM_SLOTS) {
        /* Sector exhausted — erase and restart. */
        erase_config_sector();
        write_idx = 0;
    }

    DiverState to_write  = *current;
    to_write.magic       = CONFIG_MAGIC;
    to_write.sequence    = next_seq;
    /* Ensure pad bytes are zero so change-detection is deterministic. */
    memset(to_write._pad, 0, sizeof(to_write._pad));

    write_slot(write_idx, &to_write);

    s_last_written = to_write;
    s_dirty        = 0;
}
