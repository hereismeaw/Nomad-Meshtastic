# RadioMaster Nomad: installed firmware and validation

The main ESP32 on **COM3** now runs `radiomaster-nomad` firmware
`2.8.3.9bcf0f7`. The detected MCU is ESP32-PICO-D4 revision 1.1 with 4 MB flash.
The C3 backpack firmware was not written, and LR1121 firmware was not updated.

Both radios initialize successfully on the installed build, including a repeated
reset test: A is LONG_FAST; B is MEDIUM_FAST. Both report device 3, hardware 34,
firmware 1.3. USB Meshtastic API reads work. Windows detects the live BLE service
as **Meshtastic_4434**; the configured pairing PIN is **123456**.

**RF TX remains inhibited.** No measured PA calibration table is shipped. The
region remains UNSET pending the operating country. Actual simultaneous packet
reception, RF transmission/ACK exchanges, optical LED behavior and physical fan
operation are not claimed verified. This is experimental bring-up firmware and
does not fulfill every advanced feature in the original dual-radio specification.

## Controls

| Control | Behavior |
| --- | --- |
| Button GPIO14 | Standard Meshtastic user button |
| Button GPIO12, short press | Fan AUTO -> ON -> OFF; saved across reboots |
| Button GPIO12, hold 3 seconds | Toggle the local RF gate; calibration is still mandatory |
| RGB pixel 0 / pixel 1, GPIO22 | Radio A / Radio B |
| Green pulse | Valid received packet, 150 ms |
| Red | Transmitting, bad received frame or radio initialization fault |
| Blue blinking on both pixels | Bluetooth pairing status; 250 ms on/off |
| Pixels off | Idle |

Fan GPIO2 starts before a permitted transmission and runs for 60 seconds after
TX. Cooling overrides manual OFF. AUTO is otherwise off at idle. No temperature
or current measurement is provided by the verified board target, so thermal
protection from measured temperature/current is not claimed.

Both LR1121 reset lines are asserted early, before the power/configuration wait,
and released by each radio's initialization. Startup retry is bounded. An initial
bring-up build reported A init error -707 while B and BLE worked. The current
build has succeeded on both radios on two subsequent boots; the first failure's
precise cause has not been conclusively established.

## Evidence and limits

| Check | Result / evidence |
| --- | --- |
| Original main flash | 4,194,304-byte backup; checksum and app validation hash pass |
| Installed image | esptool write verification passes; `nomad-final-app-flash.log` |
| Both radio initialization | `nomad-final-boot.log`, `nomad-reboot-check.log` |
| Standard USB API | `nomad-usb-api.json`: firmware, node ID, config read successfully |
| Live BLE advertising | `nomad-ble-advertising.json`, `nomad-ble-after-reboot.json` |
| Existing radio regression suite | 55 cases pass under native ASan coverage env |
| Nomad policy suite | 1 Unity case passes; includes origin, ACK affinity, RX flood, wrap and RF gate checks |
| Calibration CSV generator | 6 Python tests pass |
| Fan and blue pairing light | Implemented; direct physical confirmation still pending |
| Actual two-network RX/TX, ACKs, PDR | Not performed; peers and RF bench setup required |
| Conducted 30 dBm, isolation, thermal tests | Not performed |

Native runs were filtered to `test_radio` and `test_nomad_policy`; they are not
an all-suite GREEN result. No OTA packet or conducted-power result is fabricated.
The reliable UART speed for this unit/session is 115200 baud. A 460800-baud read
failed the digest check and the installer stopped before writing; the earlier
verified backup remains intact.

## Builds and artifacts

| Environment | Result | Static RAM | App flash | Log |
| --- | --- | --- | --- | --- |
| `radiomaster-nomad` | PASS | 98,772 B | 1,809,755 B | `nomad-ship-build.log` |
| `radiomaster-nomad-rf-lab` | PASS | 98,772 B | 1,810,183 B | `nomad-lab-ship-build.log` |
| `tlora-v2-1-1_8` | PASS | 108,868 B | 2,019,571 B | `regression-esp32-build.log` |

RAM values are static allocations, not runtime peak heap/stack measurements.
Packaged images, ELF files, manifests and SHA256 files are under
`artifacts/<environment>/`. `nomad-build-report.json` records exact paths, hashes,
source revisions, modified files and the hardware evidence.

```powershell
.\.venv\Scripts\pio run -d firmware -e radiomaster-nomad
.\.venv\Scripts\pio run -d firmware -e radiomaster-nomad-rf-lab
.\.venv\Scripts\pio run -d firmware -e tlora-v2-1-1_8
.\.venv\Scripts\python -m unittest -v test_nomad_calibration
```

`collect-nomad-results.py` refuses to package missing/failed build or test evidence.
`nomad-changes.patch` contains the complete firmware change, including new files.
Clang-format 20.1.0 with `.trunk/configs/.clang-format` was used; `git diff --check`
passes. The local tool setup uses PlatformIO Core 6.2.1b2 because the pinned
ESP32 platform rejected the initially installed Core 6.1.19 and its auto-selected
6.2.0.post1 package. The final builds pass.

## Backup, installation and recovery

Verified original main-flash dump:

`backups/nomad-20261105-161954/main-esp32-original.bin`

SHA256: `5e561be875b716154eb595ace5dc1f7d2244a210a0a4d518289946919945dc1c`

The dump includes original main-firmware/settings. Keep it private and keep the
hash and `flash-id.log` with it. The device originally carried an ExpressLRS
hardware definition matching Nomad; its exact application release was not
identified. The backup app's own checksum and validation hash are valid.

The prepared installer explicitly selects `--chip esp32`, backs up the entire
flash before any write, rejects an undersized chip or missing/ambiguous image,
and stops on a failed backup. Without `-Install` it only backs up and resets.
With `-Install` it replaces main-ESP32 firmware/settings. It never selects a C3
image. In this session the actual writes used explicit esptool commands after
validating the original backup.

```powershell
.\install-nomad.ps1 -Port COM3 -Baud 115200
.\install-nomad.ps1 -Port COM3 -Baud 115200 -Install
```

First conversion uses the factory image at **0x0** and matching LittleFS at
**0x300000**. Later updates can write only the matching application at **0x10000**
to preserve settings and filesystem; this was used for the final installed build.

Recovery procedure (not physically exercised): verify the backup hash, enter
the **main ESP32** bootloader, confirm ESP32 identity and restore the full dump:

```powershell
.\.venv\Scripts\python -m esptool --chip esp32 --port COM3 --baud 115200 write-flash 0x0 backups/nomad-20261105-161954/main-esp32-original.bin
```

Do not use backpack passthrough or C3 boot/control pins. If automatic bootloader
entry fails, use the manufacturer's main-ESP32 BOOT/reset procedure rather than
guessing which physical controls belong to the backpack.

## Implementation and configuration

Each radio owns a RadioLib module, modem state, FIFO buffer, IRQ trampoline,
TX queue, frequency, preset and counters. Shared SPI transactions use the existing
lock. IRQ handlers defer work; SPI is not used from ISR context. The per-radio
FreeRTOS readout optimization is disabled for this target; RX work runs on the
main task. Receive frame loss under load therefore needs real PDR measurement.

Packet origin stays in bounded local metadata, without changing OTA protobufs.
Outgoing metadata has separate storage so RX flood does not evict it. Pending
retransmissions carry a radio field and restore their origin before retry.
Responses/ACKs from a different PHY are rejected; retransmission timing and
receive-airtime deferral select the matching radio. Missing origin fails closed.
This is not cryptographic replay protection.

`NomadRadioSettings` / `NomadRadioStatistics` are version-1 C++ board extensions.
Settings include enabled, preset, slot, target power, TX/RX permission and a stored
role value. The role value has no scheduling behavior yet. Radio settings use
NVS `nomad-a-v1` and `nomad-b-v1`; global mode/fan use `nomad-v1`. Fan is
`fan=0/1/2` for AUTO/ON/OFF. Operating mode supports `mode=0/1/2` for
SINGLE_A/SINGLE_B/DUAL. Startup fallback is available in the default dual mode.

There are no phone/serial endpoints for those board-specific C++ settings.
The app's single `lora.modem_preset` / `channel_num` does not configure both
contexts; region, encryption/channel keys and standard Meshtastic APIs are shared.
Standard TX and RX dispatch remain disabled while region is UNSET, although chip
initialization succeeds. C3 UART/control pins 18/5/23/19 remain reserved.

Unsupported/incomplete features: DUAL_DIVERSITY, experimental parallel TX,
optional cross-preset bridging, independent airtime ledgers, per-radio role
semantics, full configuration endpoints and complete hardware fault-injection
coverage. Airtime accounting/enforcement is still shared and conservative.

## RF frontend and lab calibration

The verified manufacturer reference defines APC2 DAC values
`[120,120,120,120,120,120,95]`, single drive
`[-17,-16,-14,-11,-7,-3,5]`, dual drive
`[-18,-14,-8,-6,-2,3,5]`, and LNA gain 12. They are not measurements of this unit.
Startup uses DAC120; this is **not** asserted to electrically shut down the PA.
Reset control and rejection of transmit requests inhibit the LoRa transmitter.

The RF-switch masks match the verified ExpressLRS defaults: enable 0x0f,
standby 0, RX 4, TX/HP 8, HF TX 2, HF RX 1. DC-DC is configured by the real
LR1121 driver. No TCXO drive voltage is invented; the target omits it and the
installed radios initialize at tcxoVoltage=0.

The lab profile defaults both power targets to 30 dBm. Its effective software
budget becomes 100% only with the local gate open; public region tables remain
unchanged. CAD, collision backoff and serialized scheduling remain active.
There is no continuous-carrier mode. The lab image was built, not installed.

`nomad_calibration.py` converts real conducted measurements to a header. CSV
columns: `unit_serial,meter_serial,measured_at,radio,frequency_khz,target_dbm,
measured_dbm,drive_dbm,dac`. Radio is 0/1. Missing provenance, duplicate entries,
drive outside -17..5, DAC outside 95..120 and target mismatch over 0.5 dB are
rejected. Firmware requires a matching radio, center frequency and target. Supply
the generated header through `NOMAD_CALIBRATION_HEADER`; no valid production
measurement entries are shipped. Test-only synthetic values are not calibration.

Bench validation still required:

1. Select the real operating country/region, then verify each center frequency,
   SF/BW/CR, band edges and occupied-bandwidth overlap. Use separate slots where
   the region and stock peers permit; automatic overlap correction is not implemented.
2. Receive numbered messages from stock LONG_FAST and MEDIUM_FAST peers at the
   same time. Record origin, PDR, RSSI/SNR, duplicate behavior and queue depths.
3. Confirm fan-button persistence and blue pairing blink on the actual device.
4. With suitable rated loads, attenuators and a calibrated analyzer/power meter,
   measure each radio/band/DAC/drive step from low power upward. Record losses,
   instrument provenance, voltage, current and temperature. Never connect 1 W
   directly to an unprotected receiver/analyzer input.
5. Only integrate validated measurements, then use the local gate to test packet
   TX, ACK/retry correctness, RX restoration and queue backpressure on each PHY.
6. Measure receiver desensitization, isolation, sustained-traffic thermal behavior
   and recovery from individual BUSY/IRQ faults. Parallel TX remains unavailable.

## Source revisions and changed files

- Meshtastic: `9bcf0f776edabb3b2447db323323cf2c49835452`
- ExpressLRS: `caf1ccd8199f22585dca727d75ad4f5e3c4c698d`
- Targets: `7154fd86b704d4b521b9ad748815ec6dce8efecb`
- RadioLib: `5878f990bdb4c95c75494a21562c0c5a49659c6f`

References: [Nomad hardware target](https://github.com/ExpressLRS/targets/blob/7154fd86b704d4b521b9ad748815ec6dce8efecb/TX/Radiomaster%20Nomad.json),
[LR1121 driver](https://github.com/ExpressLRS/ExpressLRS/blob/caf1ccd8199f22585dca727d75ad4f5e3c4c698d/src/lib/LR1121Driver/LR1121.cpp).

- `src/main.cpp`
- `src/mesh/Channels.cpp`
- `src/mesh/Channels.h`
- `src/mesh/LR11x0Interface.cpp`
- `src/mesh/MeshModule.h`
- `src/mesh/NextHopRouter.cpp`
- `src/mesh/NextHopRouter.h`
- `src/mesh/NomadDualRadio.cpp`
- `src/mesh/NomadDualRadio.h`
- `src/mesh/NomadRadioPolicy.h`
- `src/mesh/NomadRfFrontend.h`
- `src/mesh/RadioInterface.cpp`
- `src/mesh/RadioInterface.h`
- `src/mesh/RadioLibInterface.cpp`
- `src/mesh/RadioLibInterface.h`
- `src/mesh/ReliableRouter.cpp`
- `src/mesh/Router.cpp`
- `test/test_nomad_policy/test_main.cpp`
- `variants/esp32/radiomaster_nomad/platformio.ini`
- `variants/esp32/radiomaster_nomad/rfswitch.h`
- `variants/esp32/radiomaster_nomad/variant.h`
