# A31301 Finger Sensor

A 4×4 Hall-effect tactile sensor array for a robotic hand fingertip, developed at RoMeLa (UCLA).

Sixteen Allegro A31301 3D Hall-effect sensors sit under a soft elastomer layer with an embedded magnet. Pressing the fingertip moves the magnet, and each sensor reports the local X, Y and Z magnetic field over a shared I2C bus. An ESP32-S3 host board reads all 16 sensors.

## Repository structure

```
A31301_Sensor/
├── PCB/
│   ├── MERLIN_Finger_4x4.SchDoc        # Altium schematic
│   ├── MERLIN_Finger_pcb_4x4.PcbDoc    # Altium PCB layout
│   ├── schematic_pdf.pdf               # Schematic for quick viewing
│   └── fab/
│       ├── gerber/                     # Gerber and drill files
│       ├── gerber.zip                  # Upload this to JLCPCB
│       ├── Finger_Sensor_4x4_v2_BOM.xlsx
│       └── Pick Place for MERLIN_Finger_pcb_4x4_v2.csv
└── firmware/                           # PlatformIO project for the ESP32-S3 reader
```

## Board overview

| Item | Value |
|---|---|
| Sensors | 16× A31301EEJASR-XYZ-IC-20 (DFN-10, 3×3 mm), 4×4 grid at 5 mm pitch |
| Interface | I2C, 400 kHz, addresses 0x60 to 0x6F |
| Supply | 3.3 V (VCC and VCC_IO tied together) |
| Connector | FPC1: AFC07-S10ECA-00, 10-pin, 0.5 mm pitch FPC |
| Decoupling | 1× 100 nF per sensor (C1 to C16) |
| Address dividers | 200 kΩ / 100 kΩ on SA0 and SA1 (R1 to R24, bottom side) |
| Power indicator | LED1 with R25 (1 kΩ) |
| I2C pull-ups | Not on this board, provided by the host board |

## I2C address map

Each sensor's address is set by the voltage on its SA0 (pin 2) and SA1 (pin 6). Labels follow `[row x column]`, with row 0 at the top (farthest from the connector) and column 0 on the left, viewed from the top side.

| Level | Wiring | Voltage at 3.3 V |
|---|---|---|
| GND | Tied to GND | 0 V |
| 0.33x | 200 kΩ to VCC_IO, 100 kΩ to GND | 1.1 V |
| 0.67x | 100 kΩ to VCC_IO, 200 kΩ to GND | 2.2 V |
| VCC_IO | Tied to VCC_IO | 3.3 V |

| Chip | Position | SA0 | SA1 | Address | Firmware index |
|---|---|---|---|---|---|
| U1 | [0x0] | GND | GND | 0x60 | S0 |
| U2 | [0x1] | 0.33x | GND | 0x61 | S1 |
| U3 | [0x2] | 0.67x | GND | 0x62 | S2 |
| U4 | [0x3] | VCC_IO | GND | 0x63 | S3 |
| U5 | [1x0] | GND | 0.33x | 0x64 | S4 |
| U6 | [1x1] | 0.33x | 0.33x | 0x65 | S5 |
| U7 | [1x2] | 0.67x | 0.33x | 0x66 | S6 |
| U8 | [1x3] | VCC_IO | 0.33x | 0x67 | S7 |
| U9 | [2x0] | GND | 0.67x | 0x68 | S8 |
| U10 | [2x1] | 0.33x | 0.67x | 0x69 | S9 |
| U11 | [2x2] | 0.67x | 0.67x | 0x6A | S10 |
| U12 | [2x3] | VCC_IO | 0.67x | 0x6B | S11 |
| U13 | [3x0] | GND | VCC_IO | 0x6C | S12 |
| U14 | [3x1] | 0.33x | VCC_IO | 0x6D | S13 |
| U15 | [3x2] | 0.67x | VCC_IO | 0x6E | S14 |
| U16 | [3x3] | VCC_IO | VCC_IO | 0x6F | S15 |

Divider sharing limits: one 0.33x divider can drive up to 4 pins, one 0.67x divider at most 2 pins. Beyond that the pin voltage drifts out of the A31301's detection window and addresses can collide.

## PCB stackup

4-layer, 0.8 mm finished thickness, JLCPCB JLC7628 stackup.

| Layer | Use | Material | Thickness |
|---|---|---|---|
| L1 (Top) | Sensors, caps, GND pour | Copper 1 oz | 0.035 mm |
| | | Prepreg 7628 | 0.2104 mm |
| L2 | Solid GND plane | Copper 0.5 oz | 0.0152 mm |
| | | Core | 0.2 mm |
| L3 | 3.3 V plane | Copper 0.5 oz | 0.0152 mm |
| | | Prepreg 7628 | 0.2104 mm |
| L4 (Bottom) | Divider resistors, I2C, GND pour | Copper 1 oz | 0.035 mm |

Vias: 0.3 mm hole, 0.6 mm pad (0.5 mm pad near sensor pins), tented, direct connect to planes.

## Ordering (JLCPCB)

1. Upload `PCB/fab/gerber.zip`.
2. Select: 4 layers, 0.8 mm, outer copper 1 oz, inner copper 0.5 oz, JLC7628 stackup, min via hole 0.3 mm.
3. Enable PCB Assembly and upload the BOM and pick-and-place files from `PCB/fab/`. Check each part match before confirming.

All passives are 0603 JLCPCB basic parts:

| Part | Value | MPN | LCSC |
|---|---|---|---|
| Capacitor | 100 nF | CC0603KRX7R9BB104 | C14663 |
| Resistor | 100 kΩ | 0603WAF1003T5E | C25803 |
| Resistor | 200 kΩ | 0603WAF2003T5E | C25811 |
| Resistor | 1 kΩ | 0603WAF1001T5E | C21190 |

## Firmware

See [firmware/README.md](firmware/README.md) for build, flashing and data format.

## Design notes

- Copper planes and pours do not distort the static magnet field (copper is non-magnetic).
- Divider resistors are placed on the bottom side, under the sensors. Standard 0603 thick-film resistors are effectively non-magnetic, but nickel-barrier terminations can slightly affect readings. Zero each sensor at rest.
- I2C is routed as a single daisy-chained bus with short stubs to each sensor, not a star.

## Revision history

| Version | Changes |
|---|---|
| v1.0 | Initial release: 4-layer board with GND and 3.3 V planes. |
