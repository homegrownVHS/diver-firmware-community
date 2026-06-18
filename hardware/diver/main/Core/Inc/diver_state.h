#ifndef DIVER_STATE_H_
#define DIVER_STATE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Persistent state snapshot written to internal flash (Sector 3, 16 KB).
 *
 * Wear-leveling calculation — STM32F411RC, Sector 3 (0x0800C000, 16 KB):
 *   Slot size          : 32 bytes  →  512 slots per sector
 *   Flash endurance    : 10,000 guaranteed erase cycles (datasheet DS9716)
 *   Total writes       : 512 × 10,000 = 5,120,000
 *   Write interval     : 5 minutes  (STATE_FLUSH_INTERVAL_MS = 300,000 ms)
 *   Writes / year      : (365 × 24 × 60) / 5 = 105,120
 *   Flash lifespan     : 5,120,000 / 105,120 ≈ 48.7 years
 *
 * The sector is NOT in the linker FLASH region (FLASH trimmed to 48 K,
 * ending at 0x0800BFFF), so the toolchain can never place code or rodata
 * in sector 3. Sectors 4-5 (64+128 KB) remain available for code growth
 * by widening the linker FLASH region later if needed.
 *
 * Slot layout (32 bytes):
 *   [magic 4B][sequence 4B][selected_bank 1B][5 toggles 5B][7 triggers 7B][pad 11B]
 *
 * Slots fill sequentially 0→511. When the sector is full it is erased
 * (EXTI/DMA IRQs briefly disabled to avoid ISR stall during the ~1 s
 * erase window) and writing restarts at slot 0.
 */
typedef struct {
    uint32_t magic;                  /* CONFIG_MAGIC when valid          */
    uint32_t sequence;               /* monotonically increasing slot id */
    /* --- user-visible state (13 bytes) -------------------------------- */
    uint8_t  selected_bank;
    uint8_t  toggle_mirrorx;         /* buttons[kButtonMirrorX].togglestate */
    uint8_t  toggle_mirrory;         /* buttons[kButtonMirrorY].togglestate */
    uint8_t  toggle_invert;          /* buttons[kButtonInvert].togglestate  */
    uint8_t  toggle_scrollx;         /* buttons[kButtonScrollX].togglestate */
    uint8_t  toggle_scrolly;         /* buttons[kButtonScrollY].togglestate */
    uint8_t  trigger_enable_freeze;
    uint8_t  trigger_enable_clear;
    uint8_t  trigger_enable_mirrorx;
    uint8_t  trigger_enable_mirrory;
    uint8_t  trigger_enable_scrollx;
    uint8_t  trigger_enable_scrolly;
    uint8_t  trigger_enable_invert;
    /* --- padding to reach exactly 32 bytes ---------------------------- */
    uint8_t  _pad[11];
} DiverState;

/**
 * Scan the flash config sector for the newest valid slot.
 * Returns 1 and populates *out when found; 0 leaves *out untouched.
 * Call once on boot before starting the main loop.
 */
int  state_load(DiverState *out);

/**
 * Call once per video field with the current application state.
 * On the very first call this records a baseline and starts the timer.
 * On subsequent calls it writes to flash at most once per
 * STATE_FLUSH_INTERVAL_MS and only when state has actually changed.
 */
void state_maybe_flush(const DiverState *current);

#ifdef __cplusplus
}
#endif

#endif /* DIVER_STATE_H_ */
