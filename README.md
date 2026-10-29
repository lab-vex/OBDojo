<p align="center"><img src="docs/icon.png" width="160" alt="OBDojo icon"></p>

# OBDojo

DIY diagnostic tool for the 2013–2017 Kawasaki Ninja 300 (EX300A / EX300B), built on an ESP32-WROOM-32 with an MCP2003A K-line transceiver. It plugs into the bike's 4-pin KDS diagnostic socket through a Kawasaki-to-OBD2 lead and talks KWP2000 (ISO 14230) to the engine ECU at 10,400 baud, the way the dealer tool does.

Sister project of [OBDuke](https://github.com/lab-vex/obduke) (KTM 390 Duke, CAN). Same web page, app and Torque / tracker support, but over K-line, plus tools for working out the EX300's register map on the bike.

## Download

| | |
|---|---|
| 📱 **Android app** | [**obdojo-app.apk**](https://github.com/lab-vex/obdojo/releases/latest/download/obdojo-app.apk) |
| 🔧 **Firmware (fresh flash)** | [**obdojo-firmware-full.bin**](https://github.com/lab-vex/obdojo/releases/latest/download/obdojo-firmware-full.bin), flash at `0x0` |
| 🔁 **Firmware (update)** | [**obdojo-firmware.bin**](https://github.com/lab-vex/obdojo/releases/latest/download/obdojo-firmware.bin), flash at `0x10000` over an existing install |

These links work once the first release is published (see *Build outputs*). Until then, grab the files from the newest run in the **Actions** tab.

One unit, three ways in:

| Link | Used by | How |
|---|---|---|
| Wi-Fi hotspot | Web page, and the OBDojo app if you pick Wi-Fi | Join `OBDojo` (password `obdojo1234`), open `http://1.1.1.1` |
| Bluetooth Classic | The OBDojo app, Torque Pro (or any ELM327 app), or a Teltonika tracker | Pair once with PIN 1234. One device connected at a time |
| USB serial | Laptop | 115200 baud, same text commands (`help` lists them) |

```
OBDojo/
  firmware/obdojo/   obdojo.ino, obdojo_fw.cpp, web_ui.h  (open obdojo.ino in Arduino IDE)
  android/           Android Studio project for the app
  docs/HARDWARE.md   parts list and pin-to-pin wiring
  test/              firmware tests against a simulated Ninja 300 (run test/run.sh on Linux)
  tools/             CI checks
  .github/workflows/ automatic builds and releases
```

## What's confirmed and what isn't

The K-line side follows what's known to work on other Kawasaki DFI bikes (KDS2Bluetooth, KawaBoxPro, the Z750 / Z1000 / ZX-6R captures): fast init, StartCommunication `81 11 F1 81`, diagnostic session `10 80`, register reads `21 xx`, codes `13 01` / `13 02`, clear `14 01`. **Nobody has published a capture from an EX300 yet**, so:

- The live-data register numbers and scaling are the Z-series ones, set in *Settings > Live data*. Run the **register sweep** and **register monitor** on your bike once and adjust anything that's off.
- The KDS pin-out of the Kawasaki-to-OBD2 lead is generic. Check it with a meter first ([HARDWARE.md](docs/HARDWARE.md#checks-before-the-bike)).
- The MCP2003A may cut the 25 ms wake-up pulse short. The firmware measures it and tells you; a 2N7000 fixes it if so.
- ABS (EX300B) is tried at address 28 on the same K-line, but may only answer on its own connector.

## Build outputs (no Android Studio needed)

Every push to GitHub builds everything automatically:

- **Latest build:** repo **Actions** tab → newest *Build* run → **Artifacts** at the bottom: `obdojo-app` (the APK) and `obdojo-firmware`.
- **Releases:** bump `versionName` in `android/app/build.gradle.kts` and push to `main`. When the Build goes green it publishes release `v<versionName>` by itself with the APK and firmware attached, and the download links above and the app's updater pick it up. Pushes that don't change `versionName` don't make a release.
- **Signing (one-off):** releases need the permanent app signing key in the repo secrets (*Settings > Secrets and variables > Actions*):
  - `OBDOJO_KEYSTORE_B64`: the keystore file, base64 (`base64 -w0 my.jks`)
  - `OBDOJO_KEYSTORE_PASSWORD`: its password (used for the key too)
  - `OBDOJO_KEY_ALIAS` (optional): the key's name. Without it the first key in the keystore is used, so you can reuse the OBDuke keystore as is.

  Until they're set, builds still work (debug-signed) and the release step is skipped with a warning. After adding them, re-run the latest Build and v1.0.0 goes out. Keep a backup of the keystore: if it's lost, every install has to be uninstalled once.

Firmware files:
- `obdojo-firmware-full.bin`: flash at address `0x0` with any ESP flasher (e.g. the Espressif web flasher or `esptool.py write_flash 0x0 obdojo-firmware-full.bin`). Includes the Huge APP partition table.
- `obdojo-firmware.bin`: app only, for flashing over an existing OBDojo install at `0x10000`.

Each push also runs the simulator tests in `test/`: fast init, KWP2000 framing and checksums, the transceiver echo, live data decoding, codes, the sweep / monitor / watch tools, the ELM327 and app links, a key-off/on cycle, the F2 tester fallback and a transceiver that cuts the wake-up at 22 ms.

---

## 1. Hardware

Full parts list (what you already have from the OBDuke build vs. what to get) and every connection pin to pin: **[docs/HARDWARE.md](docs/HARDWARE.md)**.

Short version:

| From | To |
|---|---|
| KDS pin 1 (K-line) → lead's OBD pin 7 | MCP2003A pin 6 LBUS, plus 1 kΩ + 1N4148 pull-up to 12 V |
| KDS pin 2 (ground) → OBD pins 4 / 5 | Common ground |
| KDS pin 4 (+12 V) → OBD pin 16 | 1 A fuse → MCP2003A pin 7 VBB, pull-up, and the 3.3 V buck through a 1N5819 |
| KDS pin 3 → OBD pin 15 | Nothing |
| MCP2003A pin 1 RXD | ESP32 GPIO16, 4.7 kΩ to 3V3 |
| MCP2003A pin 2 CS | 1 kΩ → ESP32 GPIO4 |
| MCP2003A pin 4 TXD | ESP32 GPIO17 |
| MCP2003A pin 8 VREN, pin 3 WAKE | Not connected |

**Status LED** (GPIO2): slow blink = no ECU, solid = ECU talking, fast blink = codes stored, double blink = sweep / monitor running.

---

## 2. Flash the firmware

1. Arduino IDE 2.x with **esp32 by Espressif Systems** in Boards Manager (3.x; CI uses 3.3.12).
2. Open `firmware/obdojo/obdojo.ino`. Its folder must also contain `obdojo_fw.cpp` and `web_ui.h`.
3. **Tools** menu: Board **ESP32 Dev Module**, Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)** (required).
4. Upload. Hold **BOOT** if it sits on `Connecting...`.
5. Serial Monitor at 115200 should show `OBDojo 1.0.0`, the K-line pins, `Wi-Fi up`, `Bluetooth ELM327 ready`, then `Ready`.

Or skip the IDE and flash `obdojo-firmware-full.bin` at `0x0`.

---

## 3. First time on the bike

Key on, engine off, unit plugged in. On the web page or in the app:

1. **Tools > K-line test.** Four steps should go green: the wake-up pulse (should say 25.0 ms low), the transceiver echo, the ECU's answer, a register read. Each failing step says what to check.
2. **Tools > Register sweep.** Reads every register 00–FF (about 35 s) and lists the ones that answer. Tap **CSV** and keep the file.
3. **Tools > Register monitor.** Tap **Start**, then **Mark**, then do one thing: twist the throttle, start the engine and let it warm up, click through the gears. Bytes that moved turn green. That's how you find out which register is which on the EX300.
4. Compare the Dash with the bike's own gauges (rpm, coolant, gear, speed). If one is wrong, point it at the right register in *Settings > Live data > Live value registers* (e.g. `gear=0B`), or add a custom signal.
5. **Tools > Throttle calibration:** throttle shut → *Learn closed*, wide open → *Learn open*.
6. **Codes.** Compare with dealer mode on the dash (key on, odometer screen, hold the left button 2 s).
7. With the key off, measure KDS pin 4. If it's still 12 V, turn on deep sleep (*Settings > Power*) or unplug the unit when parked.

Please send the sweep CSV and anything you work out: it's the first EX300 data anyone will have.

---

## 4. Web page

1. Join Wi-Fi **OBDojo**, password **obdojo1234** (change it in Settings).
2. The page opens by itself. If not, go to **http://1.1.1.1** (type the `http://`).

If 1.1.1.1 gives you trouble it's usually the phone's **Private DNS** (set it to *Automatic* or *Off* while on OBDojo) or the phone sending the request over **mobile data**. Changing *Settings > Wi-Fi > Hotspot address* to `10.1.1.1` avoids both. Use **Add to Home Screen** to make it work like an app.

Tabs: **Dash** (0–14k tacho with shift light, gear from the ECU, throttle, speed, coolant, intake air, MAP, battery, throttle and sub-throttle volts, atmospheric pressure, custom signals, peaks), **Codes** (current and stored service codes, failure records, clear, ABS), **Tools** (K-line test, frame trace, ECU info, register sweep, register monitor, watch a request, throttle calibration, raw commands), **Log** (records to the unit's memory, CSV download), **Settings**.

---

## 5. Android app

Install the APK from *Download* (allow your browser to install unknown apps). After that it updates itself from this repo's releases. Requires Android 8 or newer.

- **Bluetooth (default):** pair with **OBDojo**, PIN **1234** (or tap *Pair new* on the Connect screen), pick it in the list and tap **Connect**. One Bluetooth connection at a time, so close Torque first.
- **Wi-Fi:** tap **Wi-Fi** on the Connect screen, check the hotspot name and password, tap **Connect**. The app only sends its own traffic through the hotspot, so mobile data keeps working.
- **Demo mode:** no unit handy? *Try demo mode* plays a pretend Ninja 300 ride with a stored code. Every screen works, including the sweep and monitor on pretend register bytes.

Same five tabs and features as the web page. Settings has the everyday ones up top and the rest (home Wi-Fi, K-line timing and addresses, register map, custom signals, wiring, sleep) under *Show advanced*.

To build it yourself instead: open the `android` folder in Android Studio and *Build > Build APK(s)*.

---

## 6. Torque Pro (Bluetooth)

OBDojo shows up as an ELM327 adapter that claims ISO 15765-4 CAN, so Torque doesn't try to drive a K-line itself. Underneath, the unit answers from the ECU over KDS.

1. Pair with **OBDojo**, PIN **1234**.
2. Torque: *Settings > OBD2 Adapter Settings*: **Bluetooth**, device **OBDojo**, protocol *Automatic*.
3. Key on. *Connected to ECU*.

Standard PIDs: RPM (0C), speed (0D), coolant (05), intake air (0F), throttle (11), MAP (0B), atmospheric pressure (33), battery (42). Battery comes from the ECU's register 0A, or from the sense pin if you wired one.

Codes: Kawasaki's two-digit service codes show as **P10xx**, e.g. service code 13 (intake air temp sensor) → `P1013`. Clear codes works.

**Extra PIDs** (*Settings > Manage extra PIDs/Sensors > Add custom PID*, header blank):

| Name | PID | Equation | Notes |
|---|---|---|---|
| Gear | `22F010` | `A` | 0 = neutral |
| RPM (full resolution) | `22F011` | `(A*256)+B` | |
| Sub-throttle | `22F012` | `((A*256)+B)/1000` | volts |
| Throttle sensor | `22F013` | `((A*256)+B)/1000` | volts |
| Throttle raw | `22F014` | `(A*256)+B` | 0–1023 |
| Custom signal 1–4 | `22F001`–`22F004` | `((SIGNED(A)*256)+B)/10` | whatever you set up |
| Any ECU register | `21xx` | from the monitor | Torque's A = the first data byte after `61 xx`, e.g. `2106` coolant = `(A-48)/1.6` |

Only read services (`21`, `1A`, `12`, `13`, `17`, `18`, `3E`) are passed through. Anything that could write to or reprogram the ECU is blocked.

---

## 7. Teltonika tracker

Same as OBDuke: in Teltonika Configurator, enable Bluetooth with PIN 1234, and in *OBD II (Bluetooth)* set the external name to `OBDojo` (or the MAC from *Settings > About*). Pick coolant, intake temp, throttle, MAP, speed, RPM and battery. On ESP32 core 3.x, Secure Simple Pairing can override the PIN; if pairing fails, set the tracker to *no PIN*.

---

## Settings

All settings are editable from the web page, the app, or serial (`set <key> <value>`), and survive power cycles. ↻ = needs a reboot.

| Group | Settings |
|---|---|
| Wi-Fi | Mode (hotspot / hotspot + home / off), hotspot name, password, address (1.1.1.1), channel, hidden, home Wi-Fi, `.local` name |
| Web page | Login user + password (blank = no login), auto-open page on join |
| Bluetooth ELM327 | Name, on/off, pairing PIN, extra Torque PIDs |
| K-line / ECU | TX / RX / CS pins, init MOSFET pin, tester address (auto F1 then F2), ECU address (11), P4 byte gap (5 ms), P3 gap after a reply (55 ms), allow raw commands |
| Live data | Register for each value (`rpm=09 tps=04 spd=0C gear=0B ect=06 iat=07 map=05 baro=08 vbat=0A sub=5B`; `=00` turns one off), RPM formula, throttle closed / open raw values |
| Display | Metric / imperial, refresh rate, coolant warning, shift light rpm (12,500) |
| Custom signals | Up to 4 values from any register: `REG:byte:length:b/l:scale:offset:name:unit`, e.g. `5E:0:1:b:0.01953:0:Tilt sensor:V` |
| Battery | Sense pin, divider ratio, low-voltage warning |
| Logging | Interval, start on power-up |
| System / Power | Status LED pin, deep sleep after N minutes with no ECU, wake-up check interval |

**Factory reset:** *Settings > Reset*, or `factory yes` over serial.

## Thanks

The Kawasaki KDS protocol details come from people who worked it out in the open: KDS2Bluetooth (and its Z750 / Z1000 register captures), the ECUHacking forum's Kawasaki threads, and the other DIY KDS readers listed in the research notes.
