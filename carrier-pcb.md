# CombSense ESP32-S3-WROOM Hive Node — Carrier PCB Design Spec

**Status:** Design locked, ready for layout
**Date:** 2026-04-28
**Goal:** Sellable kit. JLCPCB-assembled main carrier with bare ESP32-S3-WROOM-1, plug-in scale module, daughtercard for IR sensors, screw-terminal connections for remote sensors. Customer assembly is limited to plugging in modules, soldering wires to screw terminals, and (one place only) soldering a 5-conductor cable to the SPH0645 mic breakout.

---

## 1. Architecture

The deployed node is **two PCBs** plus off-board sensor modules.

### PCBs

| Board | Size | Role |
|---|---|---|
| Main carrier | 80 × 60mm | MCU, battery, charge/protection/regulation, USB, all sensor terminations except IR |
| IR junction daughtercard | TBD | 8× IR-pair terminals + emitter power gate, mounted near hive entrance |

### Off-board components (kit-supplied or buyer-sourced)

| Component | Source | Connection to carrier |
|---|---|---|
| 18650 Li-ion cell, 3500mAh | Kit | PCB-mount holder on carrier |
| Solar panel — **FellDen 5V 200mA, 110×60mm, 5-pack** ([Amazon B0BML3PR4Z](https://www.amazon.com/dp/B0BML3PR4Z)) — 1W per panel, bare wires; buyer wires 2–3 in parallel for ~2–3W effective harvest | Kit-recommended SKU | Screw terminal on carrier |
| HX711 module + 4× 50kg load cells | Amazon B07B4DNJ2L (kit-recommended) | Plug-in 4-pin header on carrier |
| SPH0645 I²S microphone breakout | Adafruit ADA3421 | 5-conductor cable to screw terminal on carrier (mic side: user direct-solders to breakout pads) |
| DS18B20 stainless temp probe | Generic, bare 3-wire | 3-pos screw terminal on carrier |
| 8× IR break-beam pairs | Adafruit ADA2167 (verify wires) | Screw terminals on IR daughtercard |

---

## 2. Main Carrier — Locked Spec

### MCU and reset/boot — BOM-locked

| Item | Decision | Notes |
|---|---|---|
| MCU module | **ESP32-S3-WROOM-1-N8** (LCSC C2913198) | 8MB flash, no PSRAM. All GPIOs free. Footprint accepts both WROOM-1 (PCB antenna, 25.5×18mm) and WROOM-1U (U.FL, 19.2×18mm) — same pinout, U.FL fits within PCB-antenna outline. **JLCPCB requires an assembly fixture (one-time fee)** for this module. |
| Antenna | PCB antenna (WROOM-1 default) | WROOM-1U swap available for low-signal installs without re-spin. |
| Module bulk decoupling | 10µF 0805 X7R 10V (LCSC C15850) | At module 3V3 input pin. |
| Module HF bypass | 100nF 0603 X7R ×2–3 (LCSC C14663) | Distributed near module pins 2 and 40. |
| EN pull-up | 10kΩ 0603 1% (LCSC C25804) | Required by Espressif reference design — module won't boot without. |
| EN filter cap | 1µF 0603 X7R (LCSC C15849) | RC delay ~10ms with EN pull-up; ensures stable power before reset release. Espressif reference. |
| BOOT button (IO0) | SMD tact 6×6mm, **TS-1187A-B-A** (LCSC C720477) | Locked specific part — don't let JLCPCB auto-substitute (footprint variance breaks layout). |
| RESET button (EN) | Same part as BOOT — TS-1187A-B-A (LCSC C720477) | |
| Auto-reset NPNs ×2 | **MMBT3904** (LCSC C20526) | Classic two-transistor CH340 DTR/RTS → EN/IO0 circuit, Espressif reference. **Schematic note: tie into the same GPIO0 and EN nets as the manual BOOT/RESET buttons — don't route as separate nets.** |
| Auto-reset base resistors ×2 | 10kΩ 0603 1% (LCSC C25804) | Base current limiting on the NPNs. |
| USB-C connector + CH340C | (BOM scrub pending — §2.7 Section 3) | High-level: USB-C jack + CC pull-downs + CH340C USB-UART bridge + auto-reset (above). Plug-and-play flashing across Mac/Win/Linux. |
| Status LED | SMD bicolor red/green | (BOM scrub pending) Two GPIOs (47, 48). Firmware drives boot/associate/sleep/OTA states. |

**Section 2 cost:** ~$3.40/board (MCU module dominates).
**Running BOM total:** ~$4.70/board (power chain + MCU/reset/boot).

### Power chain — BOM-locked

| Item | Decision | Notes |
|---|---|---|
| Charge controller | **TI BQ24074RGTR** (LCSC C54313) | QFN-16, 3×3mm. Operating input 4.35–6.45V, abs max 28V. **LCSC stock tight (~1,066 units at scrub time) — order within 30 days of layout finalization.** USB + solar inputs, power-path management, thermal regulation, USB-C DPM. No MPPT — compensated by panel sizing. |
| BQ24074 — ISET | 1.13kΩ 0603 1% | Sets USB charge current to ~1A (R_ISET = 890/I_chg). Solar caps at panel's 200mA naturally. |
| BQ24074 — ITERM | 11.3kΩ 0603 1% | ~100mA termination current. |
| BQ24074 — TS pin | 10kΩ + 10kΩ divider to VREF | Holds TS at 0.5×VREF — IC reads "thermistor OK, charge normally." No NTC needed. Kit-friendly, avoids gluing a thermistor to the 18650. |
| BQ24074 — PG / CHG | Routed to **GPIO18 (PG)** + **GPIO21 (CHG)**, each with 10kΩ pull-up to 3V3 | Open-drain outputs. Firmware reads for power-source / charging-state visibility. |
| BQ24074 — DPM | Hardwired disable | USB-C handles current limiting upstream. |
| BQ24074 — IN cap | 4.7µF ceramic 25V X7R 0805 | 25V rating for solar surge margin. |
| BQ24074 — SYS cap | 22µF ceramic 10V X7R 0805 | Output bulk per datasheet. |
| BQ24074 — BAT cap | 10µF ceramic 10V X7R 0805 | Battery-line cap. |
| Solar input fuse | Bourns MF-MSMF050-2 polyfuse (LCSC C181432) | 0.5A hold, 1A trip. Between solar screw terminal and P-FET. |
| Solar input surge cap | 47µF aluminum electrolytic | At screw terminal, before polyfuse. Cheap insurance for outdoor wiring. |
| Solar reverse-polarity FET | **AO3401A** (LCSC C15127) | -30V V_DS, 60mΩ R_DS(on), SOT-23. Drain on Solar+, Source on BQ24074 VIN. |
| P-FET gate Zener | BZT52C12 (LCSC C8062) | 12V Zener, gate-to-source clamp. SOD-123. Protects V_GS(max) against high-Voc panels. |
| P-FET gate pulldown | 100kΩ 0603 1% | Gate-to-source. |
| Solar TVS clamp | SMBJ24CA (LCSC C8978) | Bidirectional 24V TVS, post-FET to GND. SMB package. |
| Battery protection IC | **DW01A** (LCSC C8396) | SOT-23-6. JLCPCB Basic Parts. Over-discharge/over-charge/over-current cutoff. |
| Battery protection MOSFETs | **FS8205A** (LCSC C32254) | SOT-23-6 dual N-channel. JLCPCB Basic Parts. Pairs with DW01A per standard reference design. |
| 3.3V LDO | **TI TPS73633DBVR** (LCSC C28038) | SOT-23-5, fixed 3.3V, 1µA Iq, 400mA. |
| TPS73633 — input cap | 1µF ceramic 10V X7R 0603 | Per datasheet. |
| TPS73633 — output cap | 22µF ceramic 10V X7R 0805 | **Stability-critical — must be ceramic, 2.2µF–47µF range. Don't substitute polymer.** |
| TPS73633 — NR cap | 10nF ceramic 0603 | Optional noise-reduction pin cap. Populate. |
| Battery voltage monitor | 1MΩ + 1MΩ divider + 10nF cap to GND at ADC tap → **GPIO1** (ADC1_CH0) | ~2.1µA continuous. 10nF anti-alias filter against ADC sample noise. No FET-gating needed. |
| ESD on USB | USBLC6-2SC6 TVS array | D+/D−/VBUS. |
| ESD on screw terminals | ESD9B/ESD9R on each external input | Cheap insurance for outdoor connectors. |

**Power chain BOM cost:** ~$1.30/board at qty 20.

### Sensor terminations

| Sensor | Connector on carrier | Wires |
|---|---|---|
| Solar panel | 2-pos screw terminal | + / − |
| Microphone | 5-pos screw terminal | VCC / GND / BCLK / LRCL / DOUT |
| Temp probe (DS18B20) | 3-pos screw terminal + 4.7kΩ pull-up between DATA and VCC on carrier | VCC / DATA / GND |
| IR pairs (×8) home-run | 12-pin 2.54mm IDC ribbon header | 8× detector signals + emitter-enable + Vcc + GND (+ 1 spare) |
| HX711 scale | 4-pin 2.54mm right-angle female header + 2× M3 mounting holes | GND / DT / SCK / VCC (verify against kit module silkscreen before fab) |

### Storage

| Item | Decision | Notes |
|---|---|---|
| microSD | Push-pull socket footprint, **unpopulated v1** | JLC charges per placement, not per pad — zero cost when unpopulated. Populate in later batch when audio + collector-down buffering features land. SPI bus reserved on layout. |

### 2.5 Solar panel sizing — important

The BQ24074 has a narrow input window. Picking the wrong panel silently fails the kit.

| Panel spec | Voc (no load) | Works? | Notes |
|---|---|---|---|
| **5V nominal, 1–6W** | 5.5–6V | ✅ **Recommended** | Perfect match. Kit ships with FellDen 5-pack (1W each); buyer wires 2–3 in parallel. |
| **6V nominal, 3–6W** | 6.5–7V | ⚠️ Marginal | At OVP threshold (~6.45–6.7V). Cold-day Voc spikes can clip charging. |
| 9V nominal | ~10V | ❌ | Above OVP — chip enters protection, no charging. |
| **12V nominal** (most common Amazon panel) | 18–22V | ❌ | Way above OVP. Chip survives (28V abs max) but never charges. **Kit silently fails.** |

**Kit-recommended SKU: FellDen 5V 200mA panels, 110×60mm, 5-pack ([Amazon B0BML3PR4Z](https://www.amazon.com/dp/B0BML3PR4Z))**
- 5V nominal, Voc ~5.5–6V (well under BQ24074's 6.45V OVP)
- 200mA × 5V = 1W per panel
- Bare wires (matches kit's screw-terminal pattern)
- 5 panels per pack — wire 2–3 in parallel for ~2–3W effective harvest. ESP32-S3 hive node averages ~5mA load, so 2–3W harvest gives comfortable margin in continental US sun even on cloudy days.

**Kit instructions must explicitly link this SKU** rather than letting buyers free-source. "12V solar" is the default beekeeper assumption; without specific guidance, the wrong choice is the most likely field failure mode.

If support for 12V panels is wanted later, options are:
1. Add a wide-input buck converter ahead of BQ24074 (e.g., MP2459, 4.5–60V → 5V)
2. Switch charge controller to CN3791 (designed for 6–24V solar input, has MPPT)

Both are v2 considerations — v1 sticks with BQ24074 + 5V FellDen panel(s).

### Form factor

| Item | Decision |
|---|---|
| Board dimensions | **80 × 60mm** |
| Mounting holes | 4× M3 (3.2mm dia) at corners, 3mm inset from each edge |
| Layer stackup | 2-layer (default) — bump to 4-layer only if layout density requires it |

---

## 3. IR Junction Daughtercard

**Deferred until main carrier is complete. Full spec + open questions in [ir-daughtercard.md](ir-daughtercard.md).**

Quick summary for carrier-side reservations:
- Mounts near hive entrance. Single 12-conductor ribbon home-run to carrier (2×6 IDC 2.54mm).
- 8× IR break-beam pairs (Adafruit ADA2167, 5 wires each: emitter R/B + detector R/B/W).
- Carrier reserves GPIO33–40 (8 detectors, contiguous) + GPIO41 (emitter enable). Ribbon conductor order must match GPIO order.

---

## 4. Connector Strategy (design rationale)

Consistent pattern across the kit:

- **Bare-wire components** (solar, DS18B20, load cells via HX711, IR pairs) → screw terminals at the carrier or daughtercard.
- **Modules with header pads** (HX711) → through-hole pin headers, plug in directly.
- **One special case:** the SPH0645 microphone. Buyer direct-solders a 5-conductor cable to the breakout pads on the mic side; the cable lands in a screw terminal on the carrier. Keeps the mic side flat enough to fit a 3D-printed mic case inside the hive.

The "anything physically remote → daughtercard with one cable home-run" pattern is used twice: IR pairs and (commodity) HX711 module. Same design language across the kit reduces buyer confusion.

---

## 5. GPIO pinmap — locked

| GPIO | Function | Notes |
|---|---|---|
| 0 | BOOT button (physical only) | Strapping — must be HIGH at boot. Don't reuse. |
| 1 | Battery voltage monitor | ADC1_CH0. 1M/1M divider + 10nF cap to GND at ADC tap. |
| 4 | I²S BCLK (mic) | Group with 5/6. |
| 5 | I²S LRCL/WS (mic) | |
| 6 | I²S DOUT (mic) | |
| 8 | I²C SDA | Arduino default. Call `Wire.begin(8, 9)` explicitly. |
| 9 | I²C SCL | |
| 10 | microSD CS (FSPICS0) | IO_MUX SPI2 fast-path. Was ADC1_CH9 — gives up that channel. |
| 11 | microSD MOSI (FSPID) | IO_MUX SPI2 fast-path. |
| 12 | microSD SCK (FSPICLK) | IO_MUX SPI2 fast-path. |
| 13 | microSD MISO (FSPIQ) | IO_MUX SPI2 fast-path. |
| 15 | DS18B20 1-Wire DATA | + 4.7kΩ pull-up to 3V3 on carrier. |
| 16 | HX711 DT (input) | |
| 17 | HX711 SCK (output) | |
| 18 | BQ24074 PG (power good) | Open-drain input. 10kΩ pull-up to 3V3. |
| 21 | BQ24074 CHG (charging) | Open-drain input. 10kΩ pull-up to 3V3. |
| 33 | IR detector #1 | **GPIO33–40 reserved for 8 IR detectors, contiguous.** Ribbon conductor order matches GPIO order. |
| 34 | IR detector #2 | INPUT_PULLUP; phototransistor pulls LOW when beam broken. |
| 35 | IR detector #3 | |
| 36 | IR detector #4 | |
| 37 | IR detector #5 | |
| 38 | IR detector #6 | |
| 39 | IR detector #7 | |
| 40 | IR detector #8 | |
| 41 | IR emitter enable | Output → P-MOSFET gate on daughtercard. |
| 43 | UART0 TX (CH340) | Reserved for bootloader path. Don't reuse. |
| 44 | UART0 RX (CH340) | Reserved for bootloader path. Don't reuse. |
| 45 | (strapping) | VDD_SPI voltage select at boot. Leave floating. |
| 46 | (strapping) | ROM UART message control at boot. Leave floating. |
| 47 | Status LED red | Active low. |
| 48 | Status LED green | Active low. |

**Spare GPIOs:** 2, 3, 7, 14, 19, 20, 42 (7 free; GPIO3 has JTAG-strap caveat — avoid for general use).

**Constraints honored:**
- ADC1 used for battery monitor (GPIO1).
- UART0 (GPIO43/44) reserved for CH340C.
- Strapping pins (GPIO0/3/45/46) handled correctly.
- microSD on IO_MUX SPI2 fast-path (GPIO10–13) → genuine 80MHz SD speed when populated.
- 8 IR detectors contiguous on GPIO33–40 — matches WROOM-1's high-numbered edge for clean ribbon-cable routing.
- USB pins (GPIO19/20) freed because flashing goes through CH340, not native USB-CDC.

Total assigned: 26 GPIOs (24 peripherals + 2 BQ24074 status). 7 spares for future expansion.

---

## 6. Open items before fab

1. **Enclosure** — 3D-printed weatherproof box, target ~90 × 70 × 35mm to fit carrier + 18650 + headroom for HX711 module. Specify mounting hole pattern matching §2 (4× M3 at corners, 3mm inset).
2. **WROOM-1 antenna keep-out** — apply Espressif's recommended keep-out region around the module's antenna so the PCB-antenna variant works. Same footprint also fits WROOM-1U.
3. **BQ24074 stock watch** — order BQ24074RGTR (LCSC C54313) within 30 days of layout finalization. Stock was tight at scrub time (~1,066 units globally on LCSC). Drop-in alternates if stock dries up: BQ24075 (same family, near-identical). Otherwise re-spin to a different charger family.

### Closed (BOM-locked or otherwise resolved)

- ~~HX711 module pinout~~ — verified GND / DT / SCK / VCC from kit photo.
- ~~IR break-beam wire count~~ — 5 wires per pair (emitter R/B + detector R/B/W). All bare-tinned.
- ~~GPIO assignment~~ — locked in §5.
- ~~Solar panel SKU~~ — FellDen 5V 200mA 5-pack (Amazon B0BML3PR4Z). See §2.5.
- ~~Solar reverse-polarity P-FET~~ — AO3401A (LCSC C15127) per §2 power-chain table.

---

## 7. References

- **ESP32-S3-WROOM-1 datasheet** (Espressif) — pin assignments, antenna keep-out, strapping pins
- **BQ24074 datasheet** (Texas Instruments) — charge controller, power-path, DPM
- **TPS73633 datasheet** (Texas Instruments) — 3.3V LDO, transient response curves
- **DW01A + FS8205A** — Sparkfun-style single-cell Li-ion protection reference
- **Adafruit ADA2167** — IR break-beam pair (verify wire count before daughtercard finalization)
- **Adafruit ADA3421** — SPH0645 I²S MEMS microphone breakout
- **Amazon B07B4DNJ2L** — recommended HX711 + 4× 50kg load cell kit; pinout to be photo-verified before fab
- **CH340C datasheet** (WCH) — USB-UART bridge, auto-reset circuit reference
- **USBLC6-2SC6** (STMicroelectronics) — USB ESD protection
