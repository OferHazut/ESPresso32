# Espresso32

Firmware for an ESP32-S3-based PID temperature controller for an espresso machine boiler, with a built-in web dashboard for live monitoring and tuning.

<p align="center">
  <img src="images/final.jpeg" alt="Espresso machine with Espresso32 controller installed" width="500">
</p>

<p align="center">
  <img src="images/InitialWarmup.jpeg" alt="Dashboard: initial warmup, PID off" width="260">
  <img src="images/NoIntegratorStage.jpeg" alt="Dashboard: approaching setpoint, integrator still off" width="260">
  <img src="images/PID.jpeg" alt="Dashboard: full PID control holding setpoint" width="260">
</p>
<p align="center"><em>Warmup stages on the live dashboard: initial heating (PID off), approach to setpoint (integrator off), and full PID control once at temperature.</em></p>

## What it does

- Reads boiler temperature from a MAX31855 thermocouple amplifier.
- Runs a PID loop (with a linear-regression derivative filter for noise rejection and a duty-cycle cap with slope-based override for fast recovery after flushes) to drive an SSR controlling the heating element.
- Drives a "ready" LED once the temperature has settled near setpoint.
- Serves a live web dashboard (temperature/PID charts, history, CSV export) over WiFi, plus a JSON API.
- Reads config (WiFi credentials, pin assignments, PID gains, etc.) from `config.yaml` on the device's SPIFFS filesystem at boot, falling back to compiled-in defaults.
- Optional pressure sensing support (present in code, currently disabled — no sensor wired up).

## Code layout

- `src/main.cpp` — setup/loop, wiring the modules below together, plus a watchdog and a stuck-PID auto-restart safeguard.
- `include/config`, `src/config` — `AppConfig`: default settings and the `config.yaml` loader/saver.
- `include/control`, `src/control` — control logic: `PidController`, `LinearDerivativeFilter`, `SsrController` (PID + duty cycle → SSR pin), `DcCapOverride` (bypass the duty cycle cap on fast temperature drops), `ReadyHysteresis`/`ReadyIndicator` (ready-LED state machine).
- `include/sensing`, `src/sensing` — `TemperatureSensor` (MAX31855) and `PressureSensor` (analog transducer), both built on a shared `SensingBase` sampling/median-filter helper.
- `include/models` — plain telemetry structs (`TemperatureTelemetry`, `PressureTelemetry`).
- `include/server`, `src/server` — `TelemetryServer`: WiFi connection, HTTP server, JSON/CSV APIs, and in-memory history ring buffers.
- `data/` — the web dashboard (`index.html`, `app.js`, `styles.css`, using Chart.js from a CDN) and the runtime `config.yaml`, both flashed to the device's SPIFFS partition.
- `test/` — Unity tests for the control-logic modules, run on the `native` PlatformIO environment (no hardware needed).

## Building & flashing

Requires [PlatformIO](https://platformio.org/).

```sh
# Edit data/config.yaml with your WiFi credentials first (see below).
pio run -t upload          # build and flash firmware
pio run -t uploadfs        # upload data/ (dashboard + config.yaml) to SPIFFS
pio device monitor         # serial log at 115200 baud
```

Run the native unit tests (control logic only, no hardware):

```sh
pio test -e native
```

## Configuration

Before flashing, set your WiFi network in `data/config.yaml`:

```yaml
wifi:
  ssid: "YOUR_WIFI_SSID"
  password: "YOUR_WIFI_PASSWORD"
```

This file is uploaded to the device's filesystem via `pio run -t uploadfs` and is read at boot; it is not compiled into the firmware. `include/config/AppConfig.h` holds compiled-in fallback defaults (used only if `config.yaml` fails to load) along with default pin assignments and PID gains — adjust to match your wiring/hardware if it differs from the pins documented in that file.

## Hardware

Built for a `4d_systems_esp32s3_gen4_r8n16` board. Default pin mapping (see `include/config/AppConfig.h` and the sensing/control headers):

| Signal | Pin |
| --- | --- |
| MAX31855 CLK / CS / DO | 11 / 12 / 13 |
| SSR control | 10 |
| Ready LED | 2 |
| Pressure ADC (optional, disabled by default) | 4 |

Power to the machine itself is switched by a Switcher smart plug, so it can be turned on remotely from a phone before getting home — the controller then takes over boiler regulation once it's powered up.

<p align="center">
  <img src="images/switcher.jpeg" alt="Switcher smart plug used to power the machine on remotely" width="300">
</p>

## Bill of Materials

### Insulation & thermal materials

- [Noise insulation foam 50cm × 200cm × 5mm](https://he.aliexpress.com/item/1005007864118122.html) — **aluminum foil backing caused electrical shorts; had to peel it off. Look for an alternative product without foil.** Size was more than enough
- [Red silicone closed-cell heat insulation for boiler, 250mm × 250mm × 3mm, up to 250°C](https://he.aliexpress.com/item/1005007139747645.html) — **rated for boiler temperatures; 3mm thickness (could try 2mm for easier fitting in some areas)**
- [Silicone heat insulation mat, 200mm × 250mm × 5mm](https://he.aliexpress.com/item/1005009325127764.html)  **rated for boiler temperatures**
- [Vinyl film, 50cm × 152cm](https://he.aliexpress.com/item/1005010632259965.html) — to wrap the machine from the outside, was quite easy to apply, **size is more than enough**

### Electrical components & wiring

- [FDD 1.25-250 connectors, 10 pairs](https://he.aliexpress.com/item/1005008784415035.html) — plus electrical cables and 5V charger for the main switch and board power
- [Jumper wires, 10cm](https://he.aliexpress.com/item/4000203371860.html)
- [Capacitors](https://he.aliexpress.com/item/1005006418528392.html) — placed one on the temperature probe for noise filtering

### Sensors

- [M3 6×6.8mm bolt hex FTARB03 K-type thermocouple probe](https://he.aliexpress.com/item/1005004948080451.html) — fits the boiler opening
- [MAX31855 thermocouple amplifier board](https://he.aliexpress.com/item/1005006635665601.html) — **note: I also bought a pre-assembled version with probe before finding the individual M3 probe that fits the boiler**

### Main controller

- [ESP32-S3 N16R8 board](https://he.aliexpress.com/item/1005008201847680.html) — built for 4d_systems_esp32s3_gen4_r8n16

### SSR & boiler

- [SSR 40DA solid-state relay](https://he.aliexpress.com/item/1005005943909513.html) — drives the heating element
- Optional: [Red O-rings for Rancilio boiler, OD 82mm × 3.5mm, 5pcs](https://he.aliexpress.com/item/1005005409190788.html) — **these fit properly in case you open the boiler for cleaning and need to replace the original one** 

## Development

<p align="center">
  <img src="images/boiler_insulation.jpeg" alt="Boiler with thermocouple and SSR wiring installed, insulation being fitted" width="400">
  <img src="images/first_temperature_demo.jpeg" alt="Bench setup during early development, first live temperature chart" width="400">
</p>
<p align="center"><em>Fitting the thermocouple, SSR wiring, and boiler insulation; early bench testing with the first live temperature readout.</em></p>
