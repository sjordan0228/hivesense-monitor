# IR Junction Daughtercard — Design Spec

**Status:** **Deferred — focus is on the main carrier first. Resume after carrier layout is complete.**
**Date:** 2026-04-28
**Parent doc:** [carrier-pcb.md](carrier-pcb.md)

---

## Role in the system

Small auxiliary PCB that mounts near a hive entrance. Connects to the main carrier ([ESP32-S3-WROOM-1-N8](carrier-pcb.md)) via a single 12-conductor flat ribbon cable. Hosts the 8× IR break-beam pairs that detect bee traffic in/out of the hive.

The "anything physically remote → daughtercard with one cable home-run" pattern is intentional — it isolates 8 noisy long unshielded IR runs from the main carrier and reduces the carrier's edge-connector real estate.

---

## Confirmed inputs (locked decisions from carrier-pcb design)

| Item | Value |
|---|---|
| IR pair part | Adafruit ADA2167, 5mm IR break-beam pair |
| Wires per pair | **5 wires** — emitter R/B (Vcc/GND), detector R/B/W (Vcc/GND/signal) |
| Detector output | Open-collector phototransistor → needs pull-up to 3V3 |
| Number of pairs | 8 |
| Cable home-run | 12-conductor flat ribbon, 2×6 IDC 2.54mm header on both ends |
| Logic level | 3.3V (matches main carrier) |

## Carrier-side constraints (from the locked GPIO pinmap)

The main carrier reserves these pins for the daughtercard:

| Carrier GPIO | Function |
|---|---|
| GPIO33 | IR detector #1 (configured INPUT_PULLUP — but daughtercard pull-up is preferred) |
| GPIO34 | IR detector #2 |
| GPIO35 | IR detector #3 |
| GPIO36 | IR detector #4 |
| GPIO37 | IR detector #5 |
| GPIO38 | IR detector #6 |
| GPIO39 | IR detector #7 |
| GPIO40 | IR detector #8 |
| GPIO41 | IR emitter enable (drives daughtercard P-MOSFET gate) |

**Ribbon conductor order must match GPIO order** so carrier-side IDC routing stays parallel and clean (no trace crossings):

| Ribbon pin | Signal | Carrier GPIO |
|---|---|---|
| 1 | Detector #1 | GPIO33 |
| 2 | Detector #2 | GPIO34 |
| 3 | Detector #3 | GPIO35 |
| 4 | Detector #4 | GPIO36 |
| 5 | Detector #5 | GPIO37 |
| 6 | Detector #6 | GPIO38 |
| 7 | Detector #7 | GPIO39 |
| 8 | Detector #8 | GPIO40 |
| 9 | Emitter enable | GPIO41 |
| 10 | 3V3 (always-on) | — |
| 11 | GND | — |
| 12 | Spare | — |

---

## Daughtercard requirements

### Connectors
- **8× 5-position screw terminal blocks**, 2.54mm pitch — one per IR pair. Buyer wires all 5 wires of pair #1 into terminal #1, etc. PCB internally bridges the shared rails:
  - Emitter V+ → gated rail (controlled by P-MOSFET)
  - Detector V+ → 3V3 always-on (from ribbon)
  - Both GNDs → GND
- **2×6 IDC 2.54mm header** for the ribbon cable home-run

### Power gating
- **P-MOSFET on emitter Vcc rail** — controlled by 3.3V logic-level enable line from main carrier (ribbon pin 9). Switches all 8 emitter LEDs on/off as a group to save power between samples.

### Signal conditioning
- **Pull-up resistors on all 8 detector signal lines** (4.7kΩ–10kΩ to 3V3 typical — verify against ADA2167 datasheet). Recommended placement: **on the daughtercard** near the detectors for lower noise pickup and shorter unshielded signal runs.

### ESD/TVS protection
- TVS or ESD-suppression on each of the 8 detector inputs (outdoor connectors → ESD risk). Strategy options under discussion (see open questions).

### Mechanical
- 2× M3 mounting holes for attachment to a 3D-printed hive entrance bracket
- Form factor target: ~60×40mm (refine per question 1 below)

---

## Open questions (resume here when work picks back up)

1. **Form factor.** Confirm or refine the ~60×40mm estimate. Typical Langstroth hive entrance reducer opening is ~20mm × ~95mm, with frame face area above and around. What's a realistic mounting envelope for the card near (but not blocking) the entrance?

2. **Terminal block layout.** Should the 8× 5-position screw terminals be on one edge (linear, simpler buyer wiring) or split across two edges (compact)? Trade-off analysis welcome.

3. **P-MOSFET part for emitter gate.**
   - Load: 8× IR LEDs at ~20mA each = ~160mA max
   - Gate drive: 3.3V logic-level, low V_GS(th) required
   - JLCPCB Basic Parts tier preferred
   - Candidates: AO3401, SI2301, DMP2305U

4. **TVS / ESD strategy for the 8 detector signal inputs.** Trade-off between:
   - (a) Single TVS array IC (e.g., USBLC6 variants, PESD3V3L5UV) — fewer parts, lower cost
   - (b) Discrete SOT-23 TVS per line — more robust, more board space
   - (c) Just a small filter cap to GND per input — cheapest, marginal protection

5. **Pull-up resistor value.** Confirm 4.7kΩ to 3V3 is correct for ADA2167's open-collector output. Verify against Adafruit's datasheet specifically — don't go from memory.

6. **Pull-up location** — daughtercard vs main carrier. Instinct says daughtercard for noise reasons; confirm.

7. **Waterproofing strategy** for a board mounted near (not inside) a beehive entrance. Conformal coat? Sealed enclosure? Open-air with IPC-rated terminal blocks? What's standard for outdoor agricultural sensor boards?

8. **IR LED forward voltage / current limiting.** Confirm whether the ADA2167 emitter rail needs a series resistor on the daughtercard, or whether the IR LEDs ship with internal current limiting. Check V_F and I_F from the datasheet.

---

## Action when work resumes

1. Web-search ADA2167 datasheet for current specs (V_F, I_F, detector pull-up recommendation, output rise/fall times).
2. Answer questions 1–8 above with specific part numbers and values.
3. Lay out daughtercard in KiCad — 2-layer, ~60×40mm target.
4. Generate fab files for JLCPCB.
5. Update [carrier-pcb.md](carrier-pcb.md) §3 with the locked daughtercard spec.

---

## References

- **Adafruit ADA2167** — IR break-beam pair, 5mm. Web-search the product page + datasheet at resume time.
- **carrier-pcb.md** — main carrier design spec, includes the locked pinmap reserving GPIO33–41 for this daughtercard.
- **JLCPCB Basic Parts library** — P-MOSFET candidate availability check before fab.
