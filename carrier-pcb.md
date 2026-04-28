# CombSense ESP32-S3-WROOM Hive Node — Carrier PCB Design Spec

**Status:** Design locked, ready for layout
**Date:** 2026-04-27
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

### MCU and programming

| Item | Decision | Notes |
|---|---|---|
| MCU module | **ESP32-S3-WROOM-1-N8** | 8MB flash, no PSRAM. All GPIOs free. |
| Antenna | PCB antenna (WROOM-1 variant) | Carrier footprint also accepts WROOM-1U (U.FL) for low-signal installs. Same pinout. |
| USB | USB-C connector + onboard CH340C USB-UART + DTR/RTS auto-reset circuit | Plug-and-play flashing across Mac/Win/Linux. CC pull-downs for USB-C compliance. |
| Buttons | 2× SMD tact: BOOT (IO0) + RESET (EN) | Manual bootloader entry if OTA bricks itself. |
| Status LED | SMD bicolor red/green | One footprint, two GPIOs. Firmware drives boot/associate/sleep/OTA states. |

### Power chain

| Item | Decision | Notes |
|---|---|---|
| Charge controller | **TI BQ24074** | USB + solar inputs, power-path management, thermal regulation, USB-C DPM. **Operating input: 4.35–6.45V. Absolute max: 28V.** No MPPT — compensated by panel sizing. |
| Battery protection | **DW01A + FS8205A** | Over-discharge/over-charge/over-current cutoff. Sits between holder and rest of circuit. Non-negotiable for a kit. |
| 3.3V rail | **TI TPS73633 LDO** | 400mA, ~1µA Iq, fast transient. + 22µF output cap, 100nF bypass, 100µF input bulk. |
| Battery voltage monitor | **1MΩ + 1MΩ divider + 10nF cap to GND at ADC tap** → ADC1 GPIO | ~2.1µA continuous (10× lower than 100k/100k). 10nF cap acts as anti-alias filter against ADC sample noise. No FET-gating needed. |
| Solar reverse-polarity | **"Ideal diode" P-MOSFET** — Drain on Solar+, Source on BQ24074 VIN, Gate via 100kΩ pulldown to GND, Zener clamp (e.g., 12V) gate-to-GND | Body-diode turns on first → V_GS negative → FET enhances → ~milliohm R_DS(on). Zener protects V_GS(max) against high-Voc panels. FET must be rated for V_DS ≥ panel Voc + margin (12V FET fine for ≤7V panel). |
| ESD on USB | USBLC6-2SC6 TVS array | D+/D-/VBUS. |
| ESD on screw terminals | ESD9B/ESD9R on each external input | Cheap insurance for outdoor connectors. |

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

## 5. Pinout — defer to layout

GPIO assignments are not pre-locked; the layout pass picks pins for routing convenience. Constraints to honor:

- **Battery voltage monitor**: must be on ADC1 (GPIO1–10). ADC2 has WiFi conflicts.
- **UART0** (GPIO43 TX / GPIO44 RX): reserved for CH340C bootloader path. Don't reuse for app-level UART.
- **Strapping pins**: GPIO0, GPIO3, GPIO45, GPIO46 — avoid for outputs that must be high/low at boot.
- **USB pins** (GPIO19 / GPIO20): free for general I/O since flashing goes through CH340, not native USB-CDC.
- **microSD SPI bus**: reserve 4 contiguous, routable GPIOs even though socket is unpopulated v1 (so a future populate-only build doesn't need a board respin).

Peripheral pin budget (approximate count of GPIOs needed):

| Peripheral | GPIOs |
|---|---|
| I²S mic (BCLK, LRCL, DOUT) | 3 |
| HX711 (DT, SCK) | 2 |
| DS18B20 (1-Wire DATA) | 1 |
| 8× IR detectors | 8 |
| IR emitter enable | 1 |
| Status LED (bicolor) | 2 |
| Battery monitor (ADC) | 1 |
| microSD SPI (MISO/MOSI/SCK/CS) | 4 |
| **Total** | **22** |

WROOM-1-N8 has ~28–30 freely usable GPIOs after strapping/USB/UART carve-outs. Comfortable margin.

---

## 6. Open items before fab

1. **Verify HX711 module pinout** by photographing the kit's actual board (B07B4DNJ2L). Lock the silkscreen + female header pinout to that exact module so buyers don't plug it in backwards. Currently assumed `GND / DT / SCK / VCC` from the listing photo.
2. **Confirm Adafruit IR break-beam wire count + connector** (bare 3-wire vs 4-wire vs JST). Drives daughtercard terminal block selection.
3. **Lock GPIO assignments** during the layout pass, honoring the constraints in §5.
4. **Enclosure** — 3D-printed weatherproof box, target ~90 × 70 × 35mm to fit carrier + 18650 + headroom for HX711 module. Specify mounting hole pattern matching §2 (4× M3 at corners, 3mm inset).
5. **WROOM-1 antenna keep-out** — apply Espressif's recommended keep-out region around the module's antenna so the PCB-antenna variant works. Same footprint also fits WROOM-1U.
6. **Pick a specific 5V or 6V solar panel SKU** for the kit recommendation (Amazon link + Voc verified ≤7V) per §2.5. Most-common-fail mode if buyers free-source from "12V solar" listings.
7. **Spec the P-FET part** for the reverse-polarity circuit: V_DS ≥ 20V (margin over panel Voc), V_GS(th) low enough to fully enhance with body-diode forward drop, low R_DS(on). Candidates: AO3401, SI2301, DMP3098L. Plus 100kΩ gate pulldown + 12V Zener clamp.

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
