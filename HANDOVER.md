# Handover - Nomad Meshtastic

Owner: hereismeaw  
Repo: https://github.com/hereismeaw/Nomad-Meshtastic (`main`, public)  
Device: RadioMaster Nomad, ESP32-PICO-D4 4 MB, dual LR1121  
Installed firmware: `radiomaster-nomad` / Meshtastic `2.8.3.9bcf0f7`  
BLE name on this unit: `Meshtastic_4434`  
Pairing PIN: `123456` (fixed, no screen)  
Phone app: connected. Node renamed **Nomad Test**, region **Thailand**, modem shown as Long Range - Fast.

This is bring-up firmware. Dual radios initialize. USB and BLE work. Dual-channel UX in the stock app does not exist yet. RF TX is still inhibited until a measured PA table exists.

---

## What is done

- Nomad target under `firmware/variants/esp32/radiomaster_nomad/`
- Dual-radio path: `firmware/src/mesh/NomadDualRadio.*`, policy in `NomadRadioPolicy.h`
- Defaults in this build: radio A `LONG_FAST`, radio B `MEDIUM_FAST`
- Windows installer `install-nomad.ps1` - full 4 MB backup before write, ESP32 only, never flashes the C3 backpack
- Flash artifacts in `artifacts/radiomaster-nomad/` (no ELF in git)
- Live photo: `docs/nomad-ble-connected.png`
- Original factory dump stays local in `backups/` (gitignored - do not publish)

## Controls

| Input | Intended behavior |
| --- | --- |
| GPIO14 | Stock Meshtastic user button |
| GPIO12 short | Fan AUTO -> ON -> OFF, persisted |
| GPIO12 hold 3 s | Toggle local RF gate (calibration still required before real TX) |
| GPIO2 | Fan |
| GPIO22 | Two WS2812-style pixels (code assumes radio A = pixel 0, radio B = pixel 1) |

GPIO12 does not enter pairing. Pairing starts when the phone connects and asks for the PIN.

---

## LED status - incomplete

RGB LEDs are **coded but not fully working on the hardware**. Treat this as an open bug, not a finished feature.

Firmware (`NomadDualRadio.cpp`) drives `Adafruit_NeoPixel` on `NOMAD_RGB_PIN` (GPIO22), 2 pixels, `NEO_GRB + NEO_KHZ800`:

| Intended pattern | Meaning |
| --- | --- |
| Green pulse ~150 ms | Valid RX on that radio |
| Red | TX, bad frame, or radio init fault |
| Blue blink both pixels, 250 ms | BLE pairing |
| Off | Idle |

What is actually true today:

- `rgb_pairing_visual_verified` is **false** in `nomad-hardware-report.json`
- Optical LED behavior was never signed off on the bench
- On the live unit, LED behavior is incomplete - not every state shows, and pairing / RX / TX colors cannot be trusted yet
- Idle is programmed as **pixels off**, so a quiet node looks dead even if the driver is fine
- TX is inhibited, so the red TX path almost never runs
- Without mesh traffic, the green RX pulse never runs
- Blue pairing blink only while `BluetoothStatus::PAIRING` - after the phone is bonded, that state ends

Likely next debug (in order):

1. Confirm GPIO22 is the Nomad status strip, pixel count, and wiring (one vs two LEDs, which end is pixel 0)
2. Confirm color order (`NEO_GRB` vs `NEO_RGB`) and timing (`NEO_KHZ800` vs `NEO_KHZ400`)
3. Raise brightness - current colors use `0x20` per channel and can look off in daylight
4. Add a boot self-test: both pixels R then G then B then off, so hardware is proven before radio/BLE logic
5. Keep a low idle heartbeat (dim) so "working but quiet" is visible
6. Hold blue for the whole pairing attempt, then a solid color for "BLE connected"

Do not claim RGB status in docs or posts until a physical pass exists.

Fan GPIO is in the same bucket: implemented, not fully physically confirmed.

---

## What is not done (roadmap)

1. **Dual-channel Meshtastic app** - stock Android/iOS/Web still show one modem. Need A/B views, TX radio picker, inbox tagged by radio, config per radio. Example end state: LongFast + ShortTurbo on one handheld.
2. **Dual-channel firmware** - persist two presets, TX selection, ACK affinity, region/duty cycle for two live modems.
3. **RF** - PA/APC2 calibration, unlock TX, conducted power, isolation, thermal. Region was UNSET in bring-up; the phone now shows Thailand - confirm that write stuck and that TX is still gated.
4. **Other** - RX IRQ latency on two LR1121s, fan/power, image size on 4 MB, upstream Nomad target.
5. **C3 backpack** - present, not flashed, not a Meshtastic serial transport.

## Build / flash

```powershell
.\.venv\Scripts\pio run -d firmware -e radiomaster-nomad
.\install-nomad.ps1 -Port <COMx> -Baud 115200
.\install-nomad.ps1 -Port <COMx> -Baud 115200 -Install
```

Use 115200 on this unit. 460800 failed a backup digest. First conversion: factory image at `0x0`, LittleFS at `0x300000`. Later app-only updates can use `0x10000`.

## Evidence files

| File | Use |
| --- | --- |
| `NOMAD-IMPLEMENTATION.md` | Original bring-up notes |
| `nomad-build-report.json` | Build hashes and limits |
| `nomad-usb-api.json` | USB API read |
| `nomad-ble-advertising.json` | BLE advertise |

Do not commit `backups/`, `.venv/`, `firmware/.pio/`, or ELF files.

## Safety

Unofficial firmware on a radio that was not sold as Meshtastic. No 30 dBm TX until calibration and a legal allocation. RadioMaster / ExpressLRS / Meshtastic names belong to their owners.
