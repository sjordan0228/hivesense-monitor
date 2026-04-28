# EasyEDA Schematic Capture — Progress Log

**Status:** Paused mid-build. Pick up by reading this doc, then resuming at "Next step" below.
**Date last touched:** 2026-04-28
**Tool:** EasyEDA Std (browser-based, free), https://easyeda.com
**Reference design:** [carrier-pcb.md](carrier-pcb.md) — full BOM-locked spec for this carrier
**Project name in EasyEDA:** *(fill in if you remember it — e.g., "combsense-hive-carrier")*

---

## How to resume

1. Open EasyEDA at https://easyeda.com → log in → open the schematic project
2. Read this doc end-to-end (~5 min) to remember where we are
3. Skim the "Important learnings" section so you don't fight the same UI quirks twice
4. Start at "Next step"

If you want help, paste this doc + a screenshot of the current schematic state to a new chat session.

---

## Whole-project progress estimate

| Phase | Status |
|---|---|
| Schematic capture | **~20% complete** (1 of 4 sections done) |
| ERC cleanup | Not started |
| PCB layout | Not started |
| DRC + fab file export | Not started |
| JLCPCB order + manufacturing | Not started |

**Overall: ~5% of total work.** Most of the remaining time is PCB layout (~50% of total) and the rest of the schematic.

---

## What's done so far (schematic)

### Section 1: MCU + reset/boot — ~80% complete

Placed and wired in EasyEDA:

- ✅ **U1**: ESP32-S3-WROOM-1-N8 module (LCSC C2913198)
- ✅ **C1**: 10µF 0805 module bulk decoupling (LCSC C15850), wired across 3V3 and GND near pin 2
- ✅ **C2**: 100nF 0603 HF bypass (LCSC C14663), wired across 3V3 and GND near pin 40
- ✅ **C3**: 100nF 0603 HF bypass (LCSC C14663), wired across 3V3 and GND near pin 2
- ✅ **R1**: 10kΩ 0603 EN pull-up (LCSC C25804), top to 3V3, bottom to EN node
- ✅ **C4**: 1µF 0603 EN filter cap (LCSC C15849), top to EN node, bottom to GND
- ✅ **EN node**: pin 3 (EN) wired to R1 bottom and C4 top via a single net
- ✅ **GND netflags**: placed on module pins 1, 40, 41 (EP) and on the GND-side terminal of every cap
- ✅ **3V3 netflags**: placed on module pin 2 and the 3V3-side terminal of every cap (case: `3V3` not `3v3`)
- ✅ **SW1**: TS-1088-AR02016 tact switch (RESET button) — placed, **wiring in progress**
- ✅ **SW2**: TS-1088-AR02016 tact switch (BOOT button) — placed, **wiring in progress**

Still to do in this section:

- 🟡 Finish wiring SW1 (RESET): terminal 1 → EN node, terminal 2 → GND netflag
- 🟡 Finish wiring SW2 (BOOT): terminal 1 → module pin 27 (IO0), terminal 2 → GND netflag
- 🔴 Place the **auto-reset NPN pair**:
  - 2× MMBT3904 NPN transistor (LCSC C20526)
  - 2× 10kΩ 0603 base resistors (LCSC C25804)
  - Schematic note from carrier-pcb.md: **the NPN pair must tie into the SAME GPIO0 and EN nets as the manual buttons — don't route as separate nets.** It's the standard CH340C DTR/RTS auto-reset circuit; placement here is set up for connection to CH340C in Section 2.

### Section 2: USB + programming — 0% complete

To place (~9 components):

- USB-C connector — TYPE-C-31-M-12 (LCSC C165948)
- 2× CC pull-down resistors — 5.1kΩ 0603 (LCSC C23186)
- USBLC6-2SC6 USB ESD protection (LCSC C7519)
- CH340C USB-UART bridge (LCSC C84681)
- 2× 100nF caps for CH340C (VCC + V3 pins, LCSC C14663)
- 2× 0Ω 0603 series resistors for TX/RX (placeholders, LCSC C17168)
- 10µF 0805 VBUS bulk cap (LCSC C15850)
- 100nF 0603 VBUS HF cap (LCSC C14663)

**Schematic-capture note from carrier-pcb.md:** UART cross-over — CH340C TX → ESP32 RX (GPIO44), CH340C RX → ESP32 TX (GPIO43). Easy to get backward.

### Section 3: Power chain — 0% complete

To place (~22 components, the most complex section). Key items:

- BQ24074RGTR charge controller (LCSC C54313)
- ISET 1.13kΩ, ITERM 11kΩ (LCSC C25744), TS divider (2× 10kΩ to VREF), PG/CHG pull-ups (2× 10kΩ to 3V3)
- BQ24074 caps: IN (10µF/25V LCSC C440198), SYS (22µF), BAT (10µF)
- **2× SS14 Schottkies** for the diode-OR (LCSC C2480) — one on USB VBUS path, one on solar path (post-P-FET). Both cathodes tie to BQ24074 IN. **Critical correction from review** — BQ24074 has only ONE input pin and will backfeed without these.
- Solar input fuse: Bourns MF-MSMF050-2 polyfuse (LCSC C181432)
- Solar surge cap: 47µF aluminum electrolytic
- Solar reverse-polarity P-FET: AO3401A (LCSC C15127), with 12V Zener (BZT52C12, LCSC C8062) and 100kΩ gate pulldown
- Solar TVS clamp: SMBJ24CA (LCSC C8978)
- Battery protection: DW01A (LCSC C8396) + FS8205A (LCSC C32254)
- 3.3V LDO: TPS73633DBVR (LCSC C28038) + 1µF input + 22µF output (must be ceramic) + 10nF NR cap
- Battery voltage monitor: 1MΩ + 1MΩ + 10nF cap to GND, tap → GPIO1

### Section 4: Sensor terminations + I/O — 0% complete

To place (~16 components):

- KF128 screw terminals: 2-pos solar (C8251), 5-pos mic (C396644), 3-pos DS18B20 (C8252)
- DS18B20 1-Wire pull-up: 4.7kΩ 0603 (LCSC C23162)
- HX711 plug-in socket: 4-pin 2.54mm female right-angle (LCSC C146928), pin order GND/DT/SCK/VCC (verified from kit photo)
- I²C expansion header: 4-pin 2.54mm male vertical (LCSC C124388) + 2× 4.7kΩ pull-ups
- IR ribbon header: 2×6 2.54mm shrouded box header (LCSC C2840)
- microSD socket: TF-01A (LCSC C91139) — **footprint reserved, do NOT populate** (skip "in BOM" flag during PCBA assembly)
- Status LED: bicolor R/G common-anode 0603 (LCSC C2837) + 2× 470Ω current limit (LCSC C23179)
- ESD9B5.0ST5G ×4 (LCSC C84669): 3× on mic data lines (BCLK, LRCL, DOUT) + 1× on DS18B20 DATA. Skip solar (covered by SMBJ24CA).
- 18650 PCB-mount holder: Keystone 1042 (LCSC C964175)
- **2× 33Ω 0603 series resistors** (LCSC C25104) on I²S BCLK and LRCL — placed close to ESP32 pins, not at the mic terminal. Dampens ringing on the long mic cable.

---

## Next step (when you resume)

**Finish the MCU section** — three things in order:

1. **Wire SW1 (RESET) — 2 wires:**
   - SW1 terminal 1 → click anywhere on the existing EN-node wire (between pin 3 and the R1/C4 junction). EasyEDA auto-creates a junction dot.
   - SW1 terminal 2 → place a `GND` netflag with its terminal touching SW1's pin 2.

2. **Wire SW2 (BOOT) — 2 wires:**
   - Drag SW2 to the right side of the schematic, somewhere near pin 27 (IO0) which is on the right-side pin column.
   - SW2 terminal 1 → wire to module pin 27 outer tip.
   - SW2 terminal 2 → place a `GND` netflag.

3. **Place the auto-reset NPN circuit:**
   - Search LCSC C20526 → drop **2× MMBT3904** NPN transistors near the CH340C area (right side of schematic, where USB will eventually go).
   - Search LCSC C25804 → drop **2× 10kΩ resistors** as the NPN base resistors.
   - The wiring: this circuit is the textbook CH340C DTR/RTS → EN/IO0 auto-reset. Look up "ESP32 auto-reset circuit MMBT3904" for the standard schematic. Both NPNs share their collectors with the manual buttons (one NPN pulls EN to GND, the other pulls IO0 to GND, controlled by DTR/RTS of CH340C).
   - **Don't fully wire yet** — leave it pending until CH340C is placed in Section 2, then connect them together.

Once Section 1 is done, move to Section 2 (USB + programming). The CH340C block is straightforward — most of it is "drop the chip + decoupling caps + USB-C connector with CC pull-downs."

---

## Important learnings — read before resuming

### Color-blindness accessibility (red/green issues)

EasyEDA Std's default UI relies on red (selection) and green (junction dots, net highlights) — both bad for red-green colorblindness. Workarounds we've adopted:

- **Don't trust visual highlight checks.** When you click a wire/netflag, ignore the color change.
- **Verify connections via Properties panel text fields** instead of color highlights. Right-click → Properties (or use the right-side docked panel) and read net names as text.
- **Junction dots are still readable as SHAPES** (filled circles vs. nothing). Color isn't important — the presence/absence of the dot shape is.
- **Snap indicators when placing parts** are also shape-based (open circle/square at snap points) — color isn't needed.
- **At the end of schematic capture, run ERC** (Design → Check / DRC). It produces a text report listing all errors and warnings — much more reliable than visual inspection. Save all detailed verification for ERC.

Try switching EasyEDA to **dark mode** if available in your version (top toolbar → theme). Higher contrast helps.

### EasyEDA Std quirks

- **Right-click → Properties** for a wire only shows visual properties (stroke color, width). To see the **net name**, single-click the wire, then look at the **right-side Properties panel** (separate from the right-click menu).
- **Net names are case-sensitive.** `3V3` and `3v3` are different nets. Always use uppercase: `3V3`, `GND`.
- **Selecting a wire highlights its vertices with green dots** — that's just selection, not connection indicators. Click empty canvas to deselect, then check for any persistent filled dots — those are real junctions.
- **Symbols default to horizontal** when placed. Press **R** while a symbol is on the cursor to rotate 90° (or right-click → Rotate after placement).
- **GND symbol** is in `Place → Netflag (GND)`. Don't confuse with VCC/+5V which are different nets.
- **For 3V3**: there's no built-in "3V3" netflag. Place a VCC netflag and rename it via Properties to `3V3`, then copy-paste to reuse.

### GPIO pinmap reminder (locked from carrier-pcb.md §5)

We're targeting the **ESP32-S3-WROOM-1-N8** (no PSRAM) variant. **Critical:** if you ever swap to N8R8 or N16R8 (octal PSRAM), GPIO33–37 are claimed internally by PSRAM and the IR detector block breaks. Stay on N8.

| GPIO | Function |
|---|---|
| 0 | BOOT button (physical only — strapping pin) |
| 1 | Battery voltage monitor (ADC1_CH0) |
| 4 | I²S BCLK (mic) |
| 5 | I²S LRCL/WS (mic) |
| 6 | I²S DOUT (mic) |
| 8 | I²C SDA |
| 9 | I²C SCL |
| 10 | microSD CS (FSPICS0) — IO_MUX SPI2 fast-path |
| 11 | microSD MOSI (FSPID) |
| 12 | microSD SCK (FSPICLK) |
| 13 | microSD MISO (FSPIQ) |
| 15 | DS18B20 1-Wire DATA |
| 16 | HX711 DT |
| 17 | HX711 SCK |
| 18 | BQ24074 PG (open-drain in, 10kΩ pull-up to 3V3) |
| 21 | BQ24074 CHG (open-drain in, 10kΩ pull-up to 3V3) |
| 33–40 | IR detectors #1–#8 (contiguous block, requires N8 variant) |
| 41 | IR emitter enable (drives daughtercard P-MOSFET gate) |
| 43 | UART0 TX (CH340C — reserved for bootloader path) |
| 44 | UART0 RX (CH340C — reserved for bootloader path) |
| 45, 46 | Strapping pins — leave floating, don't connect |
| 47 | Status LED red (active low) |
| 48 | Status LED green (active low) |

Spare GPIOs available: 2, 3, 7, 14, 19, 20, 42 (avoid GPIO3 — JTAG strap caveat).

### Schematic-capture notes (from carrier-pcb.md)

These are critical wiring decisions that come up across multiple sections:

1. **Auto-reset NPNs share GPIO0/EN nets with the manual buttons** — don't route as separate nets.
2. **VBUS routing (corrected):** USB-C VBUS → USBLC6 clamp → bulk/HF caps → **SS14 Schottky → BQ24074 IN pin (Pin 13)**. Solar follows the same pattern: Solar+ → polyfuse → P-FET ideal-diode → SS14 Schottky → same IN pin. **The two Schottkies are required** — without them, USB backfeeds into a dark solar panel.
3. **UART cross-over:** CH340C TX → ESP32 RX (GPIO44); CH340C RX → ESP32 TX (GPIO43).
4. **Battery monitor uses 1MΩ + 1MΩ + 10nF** (not 100kΩ), placed with the 10nF cap close to the GPIO1 tap.
5. **EP pin (41) of the WROOM module must tie to GND** — already done in Section 1.

---

## References

- [carrier-pcb.md](carrier-pcb.md) — full BOM with LCSC numbers, schematic-capture notes, pinmap, BOM summary
- [breadboard-main.md](breadboard-main.md) — alternate breadboard prototyping path (PCB-free, faster iteration)
- [breadboard-ir.md](breadboard-ir.md) — IR junction breadboard companion
- [ir-daughtercard.md](ir-daughtercard.md) — production IR daughtercard PCB design (deferred)
- EasyEDA: https://easyeda.com (Standard / browser version)
- EasyEDA tutorial: https://docs.easyeda.com (search by topic)
