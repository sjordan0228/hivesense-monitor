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

**Section cost:** ~$3.40/board (MCU module dominates). Status LED is BOM-locked in the sensor terminations + I/O section below.

### USB and programming — BOM-locked

| Item | Decision | Notes |
|---|---|---|
| USB-C connector | **TYPE-C-31-M-12** (LCSC C165948) — 6-pin USB 2.0 variant | 6 active pins (VBUS, GND, D+, D−, CC1, CC2) + TH mechanical tabs. JLCPCB Basic Parts. No USB 3.0, no PD support — intentional. BQ24074 handles power negotiation via DPM. |
| CC1 / CC2 pull-downs | 5.1kΩ 0603 1% ×2 (LCSC C23186) | One per CC pin, both to GND. Orientation-independent UFP detection. |
| USB ESD protection | **USBLC6-2SC6** (LCSC C7519) | TVS array on D+/D−/VBUS. SOT-23-6. |
| USB-UART bridge | **CH340C** (LCSC C84681) | Internal oscillator (no crystal needed). UART0 via GPIO43/44. JLCPCB Basic Parts. |
| CH340C VCC decoupling | 100nF 0603 X7R (LCSC C14663) | At pin 6. |
| CH340C V3 cap | 100nF 0603 X7R (LCSC C14663) | At pin 4 (internal regulator output) — **datasheet-required, do not omit.** |
| CH340C TX/RX series resistors ×2 | 0Ω 0603 (LCSC C17168) | Placeholders. Defaults to 0Ω; swap to 33Ω–100Ω without respin if signal-integrity issues arise. |
| USB-C VBUS bulk cap | 10µF 0805 X7R 10V (LCSC C15850) | At VBUS entry. |
| USB-C VBUS HF cap | 100nF 0603 X7R (LCSC C14663) | High-frequency bypass at VBUS entry. |

**Section cost:** ~$0.72/board.

**Schematic-capture notes:**
1. Auto-reset NPN pair (in MCU/reset/boot section above) ties CH340C DTR/RTS into the **same** GPIO0 / EN nets as the manual BOOT/RESET buttons. Don't route as separate nets.
2. VBUS routing: USB-C VBUS → USBLC6 clamp → bulk/HF caps → BQ24074 USB input pin (**separate** from solar input pin). BQ24074 internally OR's USB and solar inputs — **do not add an external OR-ing diode or FET between them.**
3. UART cross-over: CH340C TX → ESP32 RX (GPIO44); CH340C RX → ESP32 TX (GPIO43). Standard convention but easy to get backward — double-check during schematic capture.

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

(USB ESD and per-terminal ESD parts are BOM-locked in the USB/programming and sensor terminations + I/O sections respectively.)

**Section cost:** ~$1.30/board at qty 20.

### Sensor terminations + I/O — BOM-locked

| Item | Decision | Notes |
|---|---|---|
| Solar screw terminal | KF128 3.5mm 2-pos (LCSC C8251) | Between polyfuse and P-FET. + / −. |
| Mic screw terminal | KF128 3.5mm 5-pos (LCSC C396644) | VCC / GND / BCLK / LRCL / DOUT. **Verify stock at lcsc.com before fab — 5-pos is less common than 2/3-pos. Fallback: 2-pos + 3-pos adjacent.** |
| DS18B20 screw terminal | KF128 3.5mm 3-pos (LCSC C8252) | VCC / DATA / GND. |
| DS18B20 1-Wire pull-up | 4.7kΩ 0603 1% (LCSC C23162) | DATA to VCC on carrier. |
| HX711 plug-in socket | 4-pin 2.54mm female header, right-angle (LCSC C146928) | Pin order: **GND / DT / SCK / VCC** (verified from kit photo). HX711 module mounts flat parallel to carrier. |
| HX711 mechanical retention | 2× M3 unplated holes | Match HX711 module hole spacing (verify during layout). |
| I²C expansion header | 4-pin 2.54mm male header, vertical (LCSC C124388) | SDA / SCL / 3V3 / GND. Silkscreen labels each pin. |
| I²C SDA pull-up | 4.7kΩ 0603 1% (LCSC C23162) | To 3V3. |
| I²C SCL pull-up | 4.7kΩ 0603 1% (LCSC C23162) | To 3V3. |
| IR ribbon header | 2×6 2.54mm shrouded box header, vertical (LCSC C2840) | Polarization-keyed. Ships with matching IDC ribbon cable to the IR daughtercard. |
| microSD socket | TF-01A push-pull (LCSC C91139) — **footprint reserved, unpopulated v1** | JLCPCB skips placement when not in assembly BOM. SPI bus on GPIO10–13 (IO_MUX fast-path). |
| Status LED (bicolor R/G) | 0603 bicolor dual-LED, common anode — KT-0603SURKCGKC or equivalent (LCSC C2837) | Common anode → 3V3. Cathodes → GPIO47 (R) / GPIO48 (G) through 470Ω current-limit resistors. **Active LOW.** |
| LED current-limit resistors ×2 | 470Ω 0603 1% (LCSC C23179) | One per cathode. ~5mA per color at 3V3. |
| ESD protection on screw terminals ×3 | ESD9B5.0ST5G (LCSC C84669) | SOD-923. Place near solar, mic data lines, and DS18B20 screw terminals. IR ribbon ESD handled at daughtercard end (not carrier). |
| 18650 battery holder | **Keystone 1042 PCB-mount** (LCSC C964175) | Premium choice for kit reliability — robust spring contacts, solid retention. ~$0.50 premium over AliExpress generic; worth it for non-maker users. |

**Section cost:** ~$1.65/board.
**Running BOM total (Sections 1–4):** ~$7.07/board.

**Kit-docs note (not a carrier change):** I²S mic cable from carrier to the remote SPH0645 should be twisted-pair or shielded, kept under ~1m if possible. Long unshielded I²S clocks (1.5–3 MHz) pick up noise. Kit instructions should recommend cable type and length.

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
