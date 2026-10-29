# OBDojo hardware: parts and wiring

One ESP32, one MCP2003A K-line transceiver, a handful of passives, and the Kawasaki 4-pin lead you already have. Nothing is cut on the bike: KDS plug → Kawasaki-to-OBD2 lead → OBD2 male pigtail → OBDojo.

## Parts

### Already bought (from the September and October build chats)

| Part | Used for | Note |
|---|---|---|
| ESP32-WROOM-32 / 32E dev board (CH340C) | The unit | **Check you have a spare.** The Duke unit uses one |
| MCP2003A-E/SN, SOP-8 | K-line transceiver | Pack of 10 |
| SOP8 → DIP8 adapter board (1.27 / 0.65 mm) | Carrier for the MCP2003A | Solder on the **1.27 mm** side |
| 1N4148 diodes | K-line pull-up diode | |
| Metal film resistor kit | 1 kΩ ×2, 4.7 kΩ, optional 47 kΩ + 10 kΩ, 100 Ω, 100 kΩ | |
| Kawasaki 4-pin KDS → OBD2 female lead | Bike side | Listing didn't name the EX300: test it with a meter first (below) |
| OBD2 male pigtail, bare wires | Unit side | **Check you have a second one.** The Duke unit is soldered to the first |
| 12 V → 3.3 V buck (MP1584EN type) | Power on the bike | **Check for a spare**, the Duke unit has one. Set / confirm 3.3 V before connecting |

Not needed for OBDojo: the SN65HVD230 CAN boards (those are for the Duke and the car).

### Still to get

| Part | Why | Where |
|---|---|---|
| 2.54 mm male header strip | Legs for the SOP8 adapter | Jaycar / Core / AliExpress |
| 100 nF ceramic capacitor ×2 | Decoupling at MCP2003A VBB and at the ESP32 3V3 | Jaycar RC5360 or any 50 V X7R |
| 10 µF 50 V electrolytic | Bulk cap on VBB | Any |
| 1 A blade fuse + inline holder | Protects the bike's 12 V feed | Jaycar / Supercheap |
| 1N5819 or SS14 Schottky | Reverse-polarity diode in front of the buck (a 1N4148 is too small) | Jaycar ZR1020 (1N5819) |
| Breadboard + jumpers | Bench build | If you don't have spares |

### Optional

| Part | When |
|---|---|
| 2N7000 N-MOSFET (Jaycar ZT2400) + 100 Ω + 100 kΩ | Only if Tools > K-line test says the wake-up pulse was cut short (see "The 25 ms problem") |
| SMAJ24A TVS diode | Clamps bike spikes across the 12 V input for a permanent install |
| 8-channel logic analyser | No longer essential: the firmware measures the wake-up pulse itself |

## Wiring, pin to pin

### 1. Bike plug → lead → pigtail

You don't cut anything here, but test the lead before trusting it. Pin numbers come from a generic Kawasaki 4-pin lead, not an EX300 manual.

| KDS 4-pin | Job | Lead's OBD2 pin | Goes to |
|---|---|---|---|
| 1 | K-line (data) | 7 | MCP2003A LBUS |
| 2 | Ground | 4 and 5 | Common ground |
| 3 | Unconfirmed (OBD "L-line" position) | 15 | **Nothing.** Insulate it |
| 4 | +12 V | 16 | Fuse, then VBB / pull-up / buck |

### 2. OBD2 pigtail → OBDojo

| Pigtail wire | Connects to |
|---|---|
| Pin 16 (+12 V) | 1 A fuse → node **VBAT** |
| Pin 7 (K-line) | MCP2003A pin 6 (LBUS) |
| Pin 4 and pin 5 (ground) | Common **GND**: MCP2003A pin 5, ESP32 GND, buck GND |
| Pin 15 | Not connected |
| All other pins | Not connected |

Pigtail colours aren't standard: beep each wire to its pin.

### 3. The VBAT node (after the fuse)

| From | Through | To |
|---|---|---|
| VBAT | (direct) | MCP2003A pin 7 (VBB) |
| VBAT | 1N4148 (anode at VBAT, band toward the resistor) → 1 kΩ | MCP2003A pin 6 (LBUS) = the K-line master pull-up |
| VBAT | 1N5819 (anode at VBAT, band toward the buck) | Buck IN+ |
| MCP2003A pin 7 | 100 nF and 10 µF | GND, right at the chip |

### 4. MCP2003A → ESP32

SOP-8 on the adapter board, pin 1 = dot on the chip.

| MCP2003A pin | Name | Connects to | Why |
|---|---|---|---|
| 1 | RXD | ESP32 **GPIO16**, plus **4.7 kΩ to 3V3** | Open-drain output. Without the pull-up the chip switches its driver off |
| 2 | CS | **1 kΩ → ESP32 GPIO4** | Firmware drives it high = transceiver on. Internal pull-down keeps it off at boot |
| 3 | WAKE | Not connected | Internal pull-up |
| 4 | TXD | ESP32 **GPIO17** | |
| 5 | VSS | GND | |
| 6 | LBUS | K-line (pigtail pin 7) + the 1 kΩ / 1N4148 pull-up | |
| 7 | VBB | VBAT (fused 12 V) | 6–27 V |
| 8 | VREN | **Not connected** | Sits at battery voltage. Never connect it to the ESP32 |

### 5. Power to the ESP32

| On the bike | On the bench |
|---|---|
| Buck OUT+ (3.3 V) → ESP32 **3V3** pin, buck OUT− → GND, 100 nF across 3V3–GND | ESP32 on USB from the laptop. **Leave the buck disconnected** so two supplies don't fight |

Either way the MCP2003A's VBB and the pull-up still need the bike's 12 V: the transceiver drives the K-line at battery voltage.

### 6. Optional extras

| Extra | Wiring | Setting |
|---|---|---|
| Battery divider | VBAT → 47 kΩ → **GPIO34** → 10 kΩ → GND | `vbat_pin 34`, `vbat_mult 5.7`. Only needed with the key off: the ECU reports battery voltage itself (register 0A) |
| Init MOSFET | **GPIO25** → 100 Ω → 2N7000 gate; 100 kΩ gate → GND; drain → K-line (LBUS); source → GND | `kl_init 25` |
| Status LED | Onboard LED on GPIO2 | `led_pin 2` |

### ESP32 pin summary

| GPIO | Use |
|---|---|
| 16 | K-line RX (UART2) ← MCP2003A RXD |
| 17 | K-line TX (UART2) → MCP2003A TXD |
| 4 | MCP2003A CS (through 1 kΩ) |
| 25 | Optional init MOSFET gate |
| 34 | Optional battery sense |
| 2 | Status LED |

All of these can be changed in Settings > K-line / ECU if you wire differently. Avoid GPIO 6–11 (flash) and use 34–39 for inputs only.

## The 25 ms problem

KDS fast init holds the K-line low for 25 ms. The MCP2003A turns its transmitter off after "about 25 ms" of continuous dominant (the datasheet gives no minimum), which could cut the wake-up short. The firmware watches RX during the pulse and measures how long the bus really stayed low:

- **Tools > K-line test** shows "Wake-up pulse on the bus: 25.0 ms low". Anything from 23.5 ms up is fine.
- If it says something like "22.1 ms low (needs 25)", fit the init MOSFET (table above) and set `kl_init 25`. The MOSFET pulls the line low on its own, so the transceiver's timer never comes into it.

## Build notes

- **SOP8 adapter:** solder the MCP2003A on the 1.27 mm side, chip dot on pad 1. Tack two opposite corners first, then the rest. Check for bridges with a meter.
- **Common ground:** the bike, the MCP2003A and the ESP32 must share ground, or nothing works.
- **Breadboard is for the bench only.** On the bike, solder it on perfboard and put it in an ASA enclosure, like the Duke unit.

## Checks before the bike

1. **Lead:** with a meter on continuity, KDS socket 1 → OBD pin 7, 2 → OBD 4 and 5, 4 → OBD 16.
2. **Shorts:** ESP32 3V3 to GND should not be a short; VBAT to GND should not be a short.
3. **Unit alone on USB:** it boots, the hotspot **OBDojo** appears, Tools > K-line test says the bus never went low (no 12 V yet, expected).
4. **On the bike, key on, engine off:** plug in, open the web page, run **K-line test**. All four steps should go green. Then run the **Register sweep** once and download the CSV.
5. **Port power:** with the key off, measure KDS pin 4 to ground. If it's 12 V with the key off, either turn on deep sleep in Settings > Power or unplug the unit before a long trip away.
