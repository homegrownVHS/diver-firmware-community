# Diver Firmware — Known Issues & Community Bug Reports

Source threads:
- https://community.lzxindustries.net/t/diver-firmware/3405
- https://community.lzxindustries.net/t/diver-discontinuity-seam/1812
- https://community.lzxindustries.net/t/all-about-diver/1455

Firmware made open source late 2022. Repo: https://github.com/lzxindustries/lzxsoftware

---

## Issue 1 — Horizontal Seam / Ramp Discontinuity (THE most-reported bug)

**Symptom:** A visible vertical discontinuity runs through ramp outputs at a fixed horizontal
position, approximately 3/4 across the screen. Visible on all banks. Position is not
affected by any panel control (phase knobs, mirror, etc.). Most obvious on V-ramp but
also visible on H+V and H-V outputs. On analog displays there is also a visible "jitter"
at that line as if a scanline is being skipped.

**Root cause (Lars's analysis, Nov–Dec 2022):**
Multiple theories were discussed:

1. **DMA/HSYNC timing mismatch** — The DMA is restarted from the `data_transmitted_handler`
   callback after each line completes. The HSYNC from TVP5150AM1 then re-enables TIM1 to
   clock the DMA. If the DMA setup latency is variable (jitter), the output start position
   drifts slightly on some lines, creating a vertical shimmer. Lars: *"The last sample of
   line N is showing up as the first sample of line N+1."*

2. **HPHASE_OFFSET calibration** — `HPHASE_OFFSET = 487 - 117 = 370` is hardcoded in
   `main.h`. This offset aligns the ramp wrap-around with the horizontal blanking period.
   If this value is wrong for a given unit, the wrap shows up as a seam inside the active
   video area. Lars noted that CV input DC offset calibration may also be needed per-unit.

3. **DMA buffer over-read** — The DMA transfer is sized `hres + HBLANK + 3 = 527` samples
   (HBLANK=0). The hwave buffer contains valid duplicate data up to index `2*hres - 1 = 1047`.
   Starting the DMA from offset up to `hres-1 = 523`, the DMA can read up to index 1049,
   which is 2 entries past the duplicated data and into uninitialized memory of the buffer.

**Code location:** `stm32f4xx_it.c` → `EXTI15_10_IRQHandler`, `main.cpp` → `data_transmitted_handler`

**Status:** FIXED — commit `3565a7d`. Sentinel index moved to `dma_off + hres + HBLANK` so it always lands after active video regardless of DMA offset. Confirmed working by user.

---

## Issue 2 — Sync Blip / Intermittent Glitch

**Symptom:** A brief glitch/skip in the output waveform at irregular intervals — some users
report every 3–4 seconds, others every 1–2 minutes. Lars confirmed he could reproduce it.
Some users found placing Diver immediately after the sync source in the sync chain reduced
the occurrence.

**Root cause (suspected):**
The EXTI15_10 ISR re-enables TIM1 at the start of every HSYNC event:
```c
__HAL_TIM_ENABLE(&htim1);
```
The DMA transfer for the previous line is stopped in `data_transmitted_handler` by:
```c
TIM1->CR1 = 0;
```
If the DMA from the previous line has *not yet completed* when the next HSYNC fires (i.e.,
render loop caused a late start), TIM1 gets re-enabled while a DMA from the wrong buffer
position is still in progress, producing a one-line glitch in the output. The `dropped_frames`
counter tracks when render misses a frame, but there is no corresponding protection against
the DMA running long.

**Code location:** `stm32f4xx_it.c` → `EXTI15_10_IRQHandler` (line ~242), `main.cpp` → `data_transmitted_handler`

**Status:** Unfixed.

---

## Issue 3 — ADC Noise / Phase CV Jitter / Flickering Edges

**Symptom:** Phase CV input (H-Phase jack) exhibits noise, causing edges in output shapes
to flicker or jitter. Worse in some power supply configurations. Lars: *"We're getting a
little value jitter in the phase CV input. I need to add some better filtering."*

**Root cause:**
1. The H-phase CV is sampled in the EXTI ISR using blocking `HAL_ADC_PollForConversion`
   with a 1ms timeout — inside an IRQ. Any latency or interrupt preemption causes the sample
   to be taken at a slightly wrong time.
2. ADC channel reconfiguration (`HAL_ADC_ConfigChannel`) is called every single HSYNC inside
   the ISR, which is wasteful and introduces timing uncertainty.
3. The current EMA filter:
   ```c
   samples_hphase_cv[sampleWritePtr][linecnt] =
       (sample + prev1 + prev2 + prev3) >> 2;   // 4-tap for linecnt >= 3
   ```
   This is a narrow window (4 lines) and doesn't aggressively filter high-frequency noise.
4. H-phase CV clamping:
   ```c
   if (sample <= 32)  { sample = 32; }
   if (sample >= 709) { sample = 709; }
   sample = (sample >> 1);   // → 16..354
   ```
   The magic numbers 32 and 709 represent the measured ADC floor/ceiling for this input's
   voltage range. These may vary per-unit.

**Code location:** `stm32f4xx_it.c` → `EXTI15_10_IRQHandler` (H-phase CV section, ~lines 344–405)

**Status:** PARTIALLY MITIGATED — commit `729b6e9`. DMA offset snapped to 4-pixel grid (`& ~3u`) to suppress ADC jitter converting to pixel-level edge shimmer. Baseline ADC noise in the ISR (blocking poll, per-line channel reconfigure, narrow 4-tap filter) is still present and contributes to general output jitter. Full fix requires moving ADC sampling out of the ISR or widening the filter.

---

## Issue 4 — State Not Saved on Power Cycle

**Symptom:** Bank selection, button toggle states (Mirror X/Y, Invert, Scroll X/Y, Freeze),
and trigger mappings are all lost on power off.

**Root cause:** No persistence mechanism implemented. Lars stated: *"I have working code for
this already, using some of the internal flash as an EEPROM. So I think we can make this
happen, no problem."*

The STM32F411RC has 256KB of internal flash. The application is small enough to leave at
least one 16KB sector free for EEPROM emulation (using STM32's EEPROM emulation library
or a simple wear-leveled write scheme).

**Code location:** `main.cpp` — state variables: `selected_bank`, `buttons[i].togglestate`,
`trigger_enable_*`, `frozen`, etc.

**Status:** IMPLEMENTED — commits `4bbe4e9` + `d670de2`. Wear-leveled write to Sector 3 (0x0800C000, 16 KB). 512 slots × 32 bytes, 2-min write interval, ~19.5-year lifespan enforced by build-time static_assert. State restores on boot. NOTE: state is only committed to flash after a change AND 2 minutes of uptime — power cycling within that window will lose the most recent change.

---

## Issue 5 — Shapes Not Centered / X-Axis Offset

**Symptom:** When using Diver ramps with shape-generating modules (Doorway, etc.) the shapes
are not centered on screen. The horizontal ramp zero-crossing is offset from center.

**Root cause:** `HPHASE_OFFSET = 370` is a factory-set constant representing the pixel offset
between the TVP5150AM1 HSYNC output and the start of active video on GPIOC. This offset
varies between units due to component tolerances in the sync chain. Per-unit calibration
is needed but not implemented.

**Code location:** `main.h` line: `#define HPHASE_OFFSET (487-117)`
Applied in `main.cpp` waveform generation loops:
```c
sample_index = (sample_index + (hres - hphase_slider) + HPHASE_OFFSET) % hres;
```

**Status:** Unfixed. Would require a calibration mode or user-adjustable offset.

---

## Issue 6 — Module Freeze / Requires Multiple Power Cycles

**Symptom:** Module occasionally does not initialize correctly on power-up. LEDs may light
up but video output is frozen or blank. Repeated power cycles eventually bring it up.

**Root cause (suspected):** Race condition during startup between TVP5150AM1 sync lock and
the STM32 EXTI interrupt being enabled. If `HAL_NVIC_EnableIRQ(EXTI15_10_IRQn)` is called
before the TVP5150AM1 has locked to sync, spurious HSYNC pulses may corrupt the frame/line
counter state. The 250ms `HAL_Delay` before TVP5150AM1_Setup may not be sufficient for all
sync sources.

**Code location:** `main.cpp` → `main()` initialization sequence

**Status:** Unfixed.

---

## Issue 7 — vwave Mirror Boundary — Missing Sample

**Symptom:** Potential field-boundary glitch in the vertical ramp. One sample in the vwave
double-buffer copy is never written.

**Root cause (code bug):** In the vwave rendering loop:
```c
for (uint32_t i = 0; i < vres; i++) {
    vwave[waveWritePtr][i] = sample;                    // forward: indices 0..vres-1
    vwave[waveWritePtr][vres + vres - i] = sample;      // mirror: indices 2*vres..vres+1
}
```
- At `i = 0`: forward writes `[0]`, mirror writes `[2*vres]`
- At `i = vres-1`: forward writes `[vres-1]`, mirror writes `[vres+1]`
- `vwave[waveWritePtr][vres]` is **never written** — it retains stale data from the
  previous frame. The DMA reads this index during the transition between the first and
  second copies of the vwave buffer.

Same bug applies to `hphase_cv`.

**Code location:** `main.cpp` ~line 950
```c
vwave[waveWritePtr][vres + vres - i] = sample;
hphase_cv[waveWritePtr][vres + vres - i] = sample_hphase;
```

**Status:** FIXED — commit `3565a7d`. Boundary sample seeded from index 0 after the loop.

---

## Issue 8 — Variable Dual-Definition Across Translation Units

**Symptom:** Silent data corruption or unexpected behavior if the linker does not merge
the duplicate symbol definitions.

**Root cause:** Numerous global variables (video timing, buffers, pointers) are defined
at file scope in **both** `Core/Src/stm32f4xx_it.c` and `Core/Src/main.cpp`:
- `hsync_event`, `trigger_rising/falling/state`, `evenfield_event`, `oddfield_event`,
  `field`, `vsync`, `linecnt`, `lines_per_*`, `dropped_frames`
- `samples_wave[][]`, `samples_hphase_cv[][]`, `hwave[][]`, `vwave[][]`, `hphase_cv[][]`
- `waveReadPtr`, `waveWritePtr`, `sampleReadPtr`, `sampleWritePtr`, etc.

In C++, having two definitions of the same non-`const` global in different translation
units is undefined behavior. The VisualGDB ARM toolchain may merge them silently (as GCC
does for tentative definitions in C), but this is not guaranteed and is not correct C++.

**Fix:** Define variables once in `stm32f4xx_it.c`; declare them `extern` in a shared
header included by `main.cpp`.

**Status:** FIXED — commit `3565a7d`. Single ownership in `stm32f4xx_it.c`, `extern` declarations in `main.cpp`.

---

## Issue 9 — Frame Tearing from Mid-Session Config Sector Erase

**Symptom:** After running for ~5 minutes, the output suddenly exhibits intense frame
tearing, jitter, and full-screen distortion for approximately 1 second, then recovers.
Occurs once per session on fresh hardware (sector was never erased).

**Root cause:** The first call to `state_maybe_flush()` after the timer elapsed found
Sector 3 (0x0800C000) completely unformatted (all bytes non-0xFF after factory erase
of the code sectors). No blank slot was found, so `erase_config_sector()` fired
mid-session. That function disables `EXTI15_10_IRQn` and `DMA2_Stream5_IRQn` for the
duration of the 16 KB sector erase (~1 second), causing the video pipeline to stall
completely and produce the visible tearing.

**Fix (commit `d670de2`):**
- Added `state_init()`, called at boot **before** `HAL_NVIC_EnableIRQ(EXTI15_10_IRQn)`.
- `state_init()` scans the sector and erases it immediately if no blank slot exists.
- Because video IRQs are not yet enabled at that point, the erase is invisible to the
  output pipeline. All subsequent mid-session writes are fast 32-byte word-writes only.

**Status:** FIXED — commit `d670de2`. Confirmed by user (frame tearing no longer observed).

---

## Summary Table

| # | Issue | Status |
|---|-------|--------|
| 1 | Horizontal seam / ramp discontinuity | **FIXED** `3565a7d` |
| 2 | Sync blip / intermittent glitch | Open |
| 3 | ADC / phase CV noise / flickering edges | Partially mitigated `729b6e9` |
| 4 | State not saved on power cycle | **IMPLEMENTED** `4bbe4e9` + `d670de2` |
| 5 | Shapes not centered / X offset | Open |
| 6 | Module freeze on startup | Open |
| 7 | vwave mirror boundary missing sample | **FIXED** `3565a7d` |
| 8 | Dual variable definition across TUs | **FIXED** `3565a7d` |
| 9 | Frame tearing from mid-session sector erase | **FIXED** `d670de2` |
