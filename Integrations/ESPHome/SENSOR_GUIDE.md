# Apollo TEMP_PRO-1 Sensor, Operations Guide

A field reference for deploying, configuring, and troubleshooting the
TEMP_PRO-1 MQTT firmware (Ethernet and WiFi variants).

This revision matches firmware version 26.7.12.1. Earlier revisions of this
guide described features that have since been removed (the drain detector,
automatic power-mode flips, MQTT last-will, the retained-command scrub) and
used an incorrect form for the data topics. If you have a printed copy dated
May 2026 or earlier, discard it.

---

## Table of Contents

1. [What It Is](#what-it-is)
2. [Hardware Specs](#hardware-specs)
3. [Power Modes (Battery vs Wired)](#power-modes-battery-vs-wired)
4. [Deployment Checklist](#deployment-checklist)
5. [Boot Button Cheat Sheet](#boot-button-cheat-sheet)
6. [Web UI Reference](#web-ui-reference)
7. [MQTT Topic Reference](#mqtt-topic-reference)
8. [Safety Features](#safety-features)
9. [Common Operator Workflows](#common-operator-workflows)
10. [Troubleshooting](#troubleshooting)
11. [Reference: Compile and Flash](#reference-compile-and-flash)
12. [Appendix: Topic Examples](#appendix-topic-examples)

---

## What It Is

A self-contained air-quality sensor that wakes on a schedule (default every
30 minutes), samples particulate matter, temperature, and humidity, publishes
one batch of readings to MQTT, and goes back to sleep. Plugged into PoE or
USB it runs continuously and republishes every 10 seconds.

Built around an ESP32-S3 with deep sleep, a Sensirion SEN55 particulate
sensor, an SHT3x temperature and humidity sensor, and a MAX17048 battery
fuel gauge.

Battery life depends on the hardware revision and the power path more than
on firmware settings. Measured results: units powered from the standard
external battery pack run two to three months (the pack's own idle draw
dominates); units powered from a battery wired directly to the battery
terminals run several times longer. The five month figure in earlier guides
did not hold up under measurement.

---

## Hardware Specs

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3, pinned at 160 MHz (down from 240 for power) |
| Particulates | Sensirion SEN55, mass concentration only (VOC/NOx disabled for short wakes) |
| Temp/Humidity | One SHT3x (probe-mountable) |
| Battery gauge | MAX17048 (always-on) |
| Network | W5500 Ethernet (PoE-capable) or WiFi (separate firmware file) |
| Connectivity | MQTT only. No Home Assistant API, no MQTT discovery |
| Power options | USB-C, PoE (Ethernet variant), battery |
| Sleep current | About 100 µA on Rev 12 boards; about 4.9 mA on earlier revisions |
| Active current | Roughly 150 to 250 mA depending on workload |

---

## Power Modes (Battery vs Wired)

The single most important setting. Set by `Power Source` in the web UI, by
the MQTT command topic `apollo/temp-pro-1/command/power_source`, or by a
triple press of the boot button.

**The device never changes this setting by itself.** There is no drain
detector and no automatic flip in either direction. If a unit is in the
wrong mode, an operator put it there or a command did. This is deliberate:
earlier firmware auto-flipped modes on sensor glitches and stale broker
messages, and units disappeared into deep sleep or drained their packs.

### `battery` mode (factory default)

- Wakes on a timer (`Sleep Duration`, default 30 min)
- Connects to MQTT, publishes one batch, sleeps again
- Forced asleep at `Max Wake Time` (default 45 s) no matter what, except
  while a firmware upload is in progress
- Below the critical battery threshold (default 5 percent) the next sleep is
  extended to `Critical Battery Sleep Duration` (default 60 min). The mode
  does not change; readings still publish every wake.

### `wired` mode

- Stays awake continuously
- Republishes every `Publish Interval` seconds (default 10)
- Never sleeps, regardless of battery readings or boot loops

---

## Deployment Checklist

### Per unit, at install time

1. Flash the correct variant (`TEMP_PRO-1_MQTT_ETH.yaml` for PoE units,
   `TEMP_PRO-1_MQTT.yaml` for WiFi units).
2. Set `Power Source` for the installation type (wired for PoE/USB, battery
   for battery packs).
3. Set the `Column ID` (dropdown) or `Custom Column ID` (free text). This
   becomes part of the device's data topics. Avoid `+`, `#`, `/`, and
   spaces; the firmware replaces them with `_` and logs a warning.
4. Optionally set `Deployment Notes` (published as metadata, useful for
   "installed by, date, location" records).
5. Confirm data arrives at `Apollo/temp-pro-1-<column>/sensor/temperature`.
6. Check the boxed summary in the device log: power source, column ID,
   battery percent, and `Health: OK`.

### Per fleet, before deploying

1. Confirm the broker address in `secrets.yaml` (or set the MQTT Broker
   override per device afterward).
2. If devices sit on a different subnet than the broker, test one battery
   unit in place before installing the rest. Stateful firewalls that drop
   new cross-subnet connections break battery-mode publishing while wired
   units keep working; that is a network fix, not a firmware setting.
3. Decide web UI credentials at commissioning and set per-device overrides
   if the site requires unique logins.

---

## Boot Button Cheat Sheet

| Gesture | Effect |
|---------|--------|
| Single press | Status flash. Blue = MQTT connected. Green = WiFi up, no MQTT (WiFi variant only). Yellow = not connected. |
| Triple press | Toggle power source. Solid red 3 s = now wired (stays awake). Solid green 3 s = now battery (will sleep). |
| Hold 10 s | Factory reset. Clears WiFi credentials, column ID, all overrides, all stored settings. |
| Any press while asleep | Wakes the device for one cycle. |

A 20 ms debounce filter is applied in firmware, so a single scratchy press
cannot register as a triple press.

---

## Web UI Reference

Reach the web UI while the device is awake: `http://<device-ip>/` or via
mDNS (`apollo-tmp-pro-1-mqtte-<mac>.local` for Ethernet units,
`apollo-temp-pro-1-mqtt-<mac>.local` for WiFi units). Battery units are only
reachable during a wake window; press the boot button to wake one, then
triple-press into wired mode if you need time to work.

The UI is served from the device itself. It does not fetch anything from the
internet, so it works on isolated networks. Sign-in uses the site's standard
credentials (set at build time from `secrets.yaml`); per-device overrides
are available under MQTT and Network.

Entities are grouped on the page as follows.

### Live Readings

Temperature (F), Relative Humidity, PM buckets (0.3-1, 1-2.5, 2.5-4, and
4-10 µm plus total, in µg/m³), Battery level, Battery voltage.

### Calibration

| Entity | Default | Notes |
|--------|---------|-------|
| SHT Temperature Offset | 0.0 F | Added after the C-to-F conversion. Range -10 to +10. |
| SHT Humidity Offset | 0.0 % | Result clamped to 0-100. Range -20 to +20. |
| Warm-up Delay | 20 s | SEN55 stabilization time before sampling. 20 s is the datasheet minimum. |

### Sleep and Power

| Entity | Default | Notes |
|--------|---------|-------|
| Power Source | battery | The master mode select. Never changes by itself. |
| Sleep Duration | 30 min | Applies from the next sleep. Range 1-1440. |
| Max Wake Time | 45 s | Hard cap per wake on battery. Below 45 risks losing publishes on slow networks. |
| Critical Battery Threshold | 5 % | 0 disables. |
| Critical Battery Sleep Duration | 60 min | Used while below the threshold. |
| Publish Interval | 10 s | Wired mode republish cadence. |
| Battery LED on Wake | off | Battery-level color flash on each wake. Costs battery. |
| Force Sleep (button) | | Flips to battery mode and sleeps immediately. |

### MQTT and Network

| Entity | Default | Notes |
|--------|---------|-------|
| MQTT Broker / MQTT Port | empty / 0 | Override the compile-time broker. Applies on next reboot. If an override cannot reach a broker for five consecutive boots, alternate boots retry the compile-time default so the device stays recoverable. The firmware never erases the override. |
| MQTT Username / Password | empty | Override compile-time credentials. Reboot to apply. |
| MQTT Topic Prefix | empty | Empty means `Apollo/temp-pro-1`. Changing it moves the whole data tree on the next publish, and the firmware clears the retained values at the old prefix automatically. |
| MQTT QoS | 1 | 0, 1, or 2 for all data publishes. Leave at 1 unless you know why. |
| Web Auth Username / Password | empty | Override the built-in web login. Applies on next reboot. A forgotten password means USB reflash or the MQTT web_auth commands, so verify before rebooting. |
| Use Static IP + Static IP/Gateway/Subnet/DNS 1/DNS 2 | off, DNS empty | Applied at boot on both variants. If you set a static IP and your broker is a hostname, you must set the site's DNS servers; public DNS (8.8.8.8) is usually blocked on plant networks and cannot resolve internal names. Simplest safe choice: set the MQTT Broker override to the broker's IP address. |

### Identification

Column ID (dropdown), Custom Column ID (free text, wins over the dropdown),
Deployment Notes. All three are also settable per device over MQTT (see the
topic reference).

### Maintenance

ESP Reboot, Publish Now (wired diagnostics), Clean SEN55 Fan (about 10 s of
fan boost, safe anytime), Buzzer Enabled (master switch), Buzzer Volume,
Test Buzzer, Stop Buzzer, Restart SEN55 (recovers a wedged sensor without a
reboot), Test LED, Verbose Logging (DEBUG until toggled off; resets to on at
reboot).

### Diagnostics (read-only)

Health Status (OK / DEGRADED / CRITICAL), Wake Reason (timer / button /
cold_boot), Uptime (this wake), Device ID (MAC), IP Address, ESP
Temperature, per-sensor fail counters (consecutive; reset on a good read),
lifetime battery min/max, Critical Battery (this wake), RGB Light, Accessory
Power (leave this on; it powers the sensors).

---

## MQTT Topic Reference

Two different topic shapes exist, and the difference matters:

- **Data topics** (device publishes): `Apollo/temp-pro-1-<id>/...`
  Capital A. The device id is joined with a hyphen, so the whole thing is a
  single topic level: `Apollo/temp-pro-1-J8/sensor/temperature`.
- **Command topics** (device subscribes): `apollo/temp-pro-1/command/...`
  Lowercase a. Commands are fleet-wide broadcast except the per-device and
  class-scoped forms listed below.

`<id>` is the Custom Column ID if set, else the Column ID, else the last six
hex digits of the MAC in lowercase (for example `352bc8`).

All data topics are retained and published at the configured QoS (default
1): battery units once per wake, wired units every publish interval.

### Sensor data: `Apollo/temp-pro-1-<id>/sensor/`

| Topic | Payload |
|-------|---------|
| `temperature` | Degrees F, one decimal. `-1.0` when the SHT3x read failed; check `status/sensors_skipped` to distinguish a failure from a real below-zero reading. |
| `humidity` | Percent, one decimal. `-1.0` on failure. |
| `battery_level` | Integer percent. `-1` on failure. |
| `pm_0_3_to_1`, `pm_1_to_2_5`, `pm_2_5_to_4`, `pm_4_to_10`, `pm_total` | µg/m³, one decimal. The PM block is skipped as a whole when the SEN55 read failed; the old retained values remain, and `status/sensors_skipped` says `pm`. |

### Metadata: `Apollo/temp-pro-1-<id>/meta/`

`device_id` (MAC), `device_name`, `ip_address`, `firmware_version`,
`boot_count`, `wake_reason`, `wake_duration_s`, `battery_voltage`,
`free_heap_kb`, `deployment_notes`, `sen55_serial`, `sen55_firmware`,
`power_source` (battery/wired), `sleep_mode` (legacy ON/OFF mirror of
power_source), `sen55_fail_count`, `sht_fail_count`, `max17048_fail_count`,
`lifetime_min_battery_pct`, `lifetime_max_battery_pct`,
`lifetime_min_battery_v`, `lifetime_max_battery_v`, units under `unit_pm`,
`unit_battery`, `unit_temperature`, `unit_humidity`, and mirrored
configuration under `config_sleep_duration_min`, `config_max_wake_time_s`,
`config_critical_battery_threshold_pct`,
`config_critical_battery_sleep_min`, `config_warmup_delay_s`,
`config_publish_interval_s`.

### Status: `Apollo/temp-pro-1-<id>/status/`

| Topic | Payload |
|-------|---------|
| `health` | `OK`, `DEGRADED` (a sensor failing 1-4 reads in a row or recent unexpected reboots), or `CRITICAL` (critical battery, a sensor failing 5+ in a row, or boot-loop cooldown). Recovered devices return to OK on their own. |
| `sensors_skipped` | Empty when the last publish was complete, else a CSV from `pm`, `sht`, `batt`. |
| `critical_battery` | `OK` or `TRIGGERED`. |
| `sen55_errors` | Hex status register; `0x00000000` is healthy. Not republished when the read fails, so it can go stale; cross-check the fail counters. |

**There is no availability or last-will topic.** A device that stops
publishing keeps its last retained values at the broker. To detect a dead or
unreachable device, alarm on the age of `meta/boot_count` or
`meta/wake_duration_s` changes in your SCADA tags (for a 30 min sleep, alarm
at 90 min of silence).

### Commands (all devices subscribe): `apollo/temp-pro-1/command/`

| Topic | Payload | Notes |
|-------|---------|-------|
| `power_source` | `wired` or `battery` (legacy `ON`=wired, `OFF`=battery) | See the retained-message guard below. |
| `sleep_duration` | minutes, 1-1440 | |
| `max_wake_time` | seconds, 30-180 | |
| `warmup_delay` | seconds, 0-60 | |
| `publish_interval` | seconds, 1-3600 | |
| `critical_battery_threshold` | percent, 0-50 | |
| `critical_battery_sleep` | minutes, 5-1440 | |
| `publish_now` | any non-empty payload | Wired devices only; battery devices ignore it (they publish every wake anyway). |
| `restart` | any non-empty payload | Guarded: rejected and cleared if it arrives within 60 s of the device connecting (a retained replay). Send non-retained to wired devices. |
| `battery_led_on_wake` | `ON`/`OFF` | Empty payloads ignored. |
| `light/on`, `light/off`, `light/red`, `light/green`, `light/blue` | ignored | |
| `light/rgb` | `r,g,b` 0-255 | |
| `light/effect` | `Slow Pulse`, `Fast Pulse`, `none` | |
| `buzzer` | preset (`beep`, `siren`, `success`, `error`) or RTTTL string | Silent when Buzzer Enabled is off. |
| `buzzer/stop` | any | |
| `column_id` | a column from the dropdown list | Broadcast form sets EVERY device; prefer the per-device form below. |
| `custom_column_id` | free text | Same warning. |
| `deployment_notes` | free text | Same warning. |
| `mqtt_broker`, `mqtt_port`, `mqtt_username`, `mqtt_password` | value | Reboot to apply. Broadcast by design; the fleet shares broker credentials. |
| `web_auth_username`, `web_auth_password` | value | Reboot to apply. Mind the lockout risk. |

Class-scoped forms exist for `power_source` and `sleep_duration`:
`apollo/temp-pro-1/battery/command/...` reaches only devices currently in
battery mode, `apollo/temp-pro-1/wired/command/...` only wired ones.

### Per-device commands: `apollo/temp-pro-1/<mac6>/command/`

`column_id`, `custom_column_id`, and `deployment_notes` also listen on a
per-device topic where `<mac6>` is the lowercase 6-hex MAC suffix (as shown
in `meta/device_id`, without colons). Always use these for identity changes.
Broadcasting an identity command collapses every device onto one topic
prefix.

### Numeric payload validation

All numeric commands reject non-numeric payloads and values outside the
listed ranges, and log what they rejected. Values set by command persist
across reboots and power cuts.

### The retained-message guard (read this before scripting commands)

The broker replays retained command payloads to every device on every
connect. Two guards protect the fleet:

- A `battery` payload on any power_source topic arriving within 60 seconds
  of that device's MQTT connect is rejected AND the retained message is
  deleted at the broker. Battery units are only connected during brief
  wakes, so anything they receive right after connecting is almost always a
  stale replay, and a stale `battery` used to put wired devices to sleep.
  To switch a connected wired device to battery: publish non-retained after
  it has been connected for over a minute.
- A `restart` arriving within 60 seconds of connect is likewise rejected and
  cleared, so a leftover retained restart cannot reboot-loop the fleet.

A retained `wired` IS accepted immediately (it is the safe direction and it
is how you catch sleeping battery units; see the workflows below). Clean it
up afterward: leaving it retained will flip every future battery unit to
wired as it connects, and nothing will ever flip it back.

---

## Safety Features

1. **Wake-time cap (battery mode).** A watchdog interval forces sleep at
   `Max Wake Time` seconds regardless of MQTT state, with a fresh sensor
   read and a best-effort publish first. It fires once per wake, pauses
   while a firmware upload is in progress (with a 10 minute upper failsafe),
   and cannot be defeated by anything except switching to wired mode.
2. **Critical battery.** Below the threshold, battery units extend their
   next sleep. Wired units only publish the flag. No mode changes.
3. **Boot-loop cooldown.** Three consecutive unexpected resets (crashes,
   watchdogs, brownouts; commanded reboots do not count) put a battery unit
   into a 30 minute cooldown sleep. Before sleeping it publishes
   `status/health = CRITICAL` and its wake reason so you can see why it went
   quiet. Wired units stay awake and show CRITICAL health instead. The
   counter clears after one healthy cycle.
4. **Retained-command guards.** See the topic reference above.
5. **Firmware update self-check.** After an update, the new image must
   connect to the broker and publish once before it is accepted. If it
   cannot, the device automatically rolls back to the previous firmware on
   the next boot. A botched update cannot brick a remote unit; it also means
   you should confirm one successful publish after each OTA before calling
   it done.
6. **Broker override fallback.** A broker/port override that has never
   produced a connection for five consecutive boots makes the device retry
   the compile-time broker on alternate boots. Recovery without a truck
   roll; the override itself is kept.
7. **Topic sanitization.** Illegal MQTT characters in the column ID or
   topic prefix are replaced with `_` at publish time (a `#` in a topic
   would otherwise make the broker drop the connection on every publish).

---

## Common Operator Workflows

### Deploy a new battery unit

1. Flash, power on. The unit is in battery mode with a 45 s wake cap, so do
   the initial config promptly, or triple-press into wired mode while you
   work and triple-press back when done.
2. Set Column ID, confirm data on the broker, done.

### Deploy a wired (PoE/USB) unit

1. Flash, power on, triple-press (or set Power Source) to wired. Solid red
   confirms.
2. Set Column ID. It now publishes every 10 seconds.

### Wake up all battery devices for maintenance

    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/command/power_source' -m 'wired' -r

Retained, so each battery unit flips to wired as it wakes over the next
sleep interval and then stays awake. When finished, clean up in this order:

    # 1. remove the retained landmine
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/command/power_source' -n -r
    # 2. wait until the devices have been connected over 60 s, then
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/command/power_source' -m 'battery'

(Non-retained battery command, and only after 60 s connected, or the guard
rejects it. Repeat once if a unit was mid-reconnect.)

### Update firmware on a battery unit

1. Flip the fleet (or the unit) to wired as above.
2. `esphome upload TEMP_PRO-1_MQTT.yaml` or upload the .bin in the web UI.
   The wake cap pauses during the transfer.
3. Watch for one successful publish from the new firmware. That publish is
   what accepts the update (safety feature 5); without it the unit reverts.
4. Flip back to battery and clear the retained command.

### Find unhealthy units

    # Health rollup for the whole fleet. Note the wildcard position: the
    # device id lives INSIDE the first topic level.
    mosquitto_sub -h BROKER -v -t 'Apollo/+/status/health'

    # Which sensors are being skipped fleet-wide
    mosquitto_sub -h BROKER -v -t 'Apollo/+/status/sensors_skipped'

### Detect silent or dead devices

There is no last-will. Watch `Apollo/+/meta/boot_count` freshness in your
SCADA tags and alarm when a device has not changed it for three sleep
intervals.

### Audit fleet configuration

    mosquitto_sub -h BROKER -v -t 'Apollo/+/meta/config_sleep_duration_min'
    mosquitto_sub -h BROKER -v -t 'Apollo/+/meta/firmware_version'

### Rename a device (change its column)

Publish to the per-device topic:

    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/352bc8/command/column_id' -m 'J8'

The device moves its data tree on the next publish and automatically clears
the retained values at the old prefix, so no ghost device remains.

### Fully reset a single unit

Hold the boot button 10 seconds, or use the Factory Reset control. This
wipes WiFi credentials, column ID, all overrides, and lifetime statistics.

---

## Troubleshooting

### Device not publishing to MQTT

1. Wired or battery? A battery unit publishes for a few seconds every sleep
   interval; watch `Apollo/+/meta/boot_count` rather than expecting live
   data.
2. Check the device log (web UI or `esphome logs`). A red `REJECTED` line
   means the retained-command guard fired; that is the guard working.
3. `X of Y MQTT publishes DROPPED` in the log means the broker connection
   was down at publish time. Check broker reachability from that subnet.
   Battery units failing on a different subnet than the broker while wired
   units work is the known cross-subnet firewall issue; that fix belongs to
   the network team, not to device settings.
4. If a broker override is set (MQTT and Network group), confirm it is
   right. After five failed boots the device alternates with the
   compile-time broker and logs it loudly.

### Battery dies faster than expected

- Check `meta/wake_duration_s`: healthy wakes run 30 to 40 s. Wakes pinned
  at Max Wake Time mean the device struggles to connect and every wake
  costs full price; fix the network path first.
- Check for a retained `wired` on the power_source command topics. A unit
  left in wired mode stays awake until the pack is flat; nothing flips it
  back automatically.
- Lifetime min/max battery and the fail counters reveal failing hardware
  that lengthens wakes.

### Health: DEGRADED right after a reboot

Normal if a sensor glitched once; the counters are consecutive and clear on
the next good read (a battery unit clears within one wake, a wired unit
within about 5 minutes). Persistent DEGRADED with a climbing fail counter is
a hardware signal.

### Health: CRITICAL right after a firmware update

Commanded reboots do not count toward boot-loop detection, but a crash
during the first boot of new firmware does, and rollback protection may have
reverted the image. Check `meta/firmware_version` against what you flashed.
If it reverted, the new image never managed a publish; investigate
connectivity before flashing again.

### Lifetime min battery shows a skewed value

It is a lifetime minimum and never normalizes on its own. Factory reset
clears it (along with everything else), or ignore it and watch
`battery_voltage` instead.

### Boot button does not wake the device

The button wakes via GPIO0. If the device also does not respond on the
network in wired mode, power-cycle it. Repeated `cold_boot` wake reasons
afterward point at the boot-loop handling above.

### MQTT broker IP changed

Fleet-wide (applies at each device's next boot):

    mosquitto_pub -h OLD_BROKER -t 'apollo/temp-pro-1/command/mqtt_broker' -m '10.0.0.42' -r
    mosquitto_pub -h OLD_BROKER -t 'apollo/temp-pro-1/command/restart' -m '1'

Battery units pick the retained broker value up on their next wake and use
it from the boot after that. Clear the retained messages once
`meta/ip_address` shows everyone on the new broker. If the old broker is
already dead, wired units need the web UI; battery units need a button wake
plus the web UI.

### Device stuck in wired mode but should be battery

An operator or a retained command put it there. Check for retained payloads
on all three power_source command topics, clear them, then send a
non-retained `battery` while the unit has been connected over 60 s (or
triple-press the button on the device).

### Device in battery mode but should be wired

Same causes in reverse. Verify with `meta/power_source`. Send `wired`
(retained is fine; it is accepted immediately) or triple-press.

---

## Reference: Compile and Flash

    # Compile only (no flash)
    esphome compile TEMP_PRO-1_MQTT_ETH.yaml      # PoE/Ethernet units
    esphome compile TEMP_PRO-1_MQTT.yaml          # WiFi units

    # Flash via USB
    esphome upload TEMP_PRO-1_MQTT_ETH.yaml --device /dev/cu.usbmodem*

    # Flash over network (device must be awake; use wired mode)
    esphome upload TEMP_PRO-1_MQTT_ETH.yaml --device <device-ip>

    # Stream live logs
    esphome logs TEMP_PRO-1_MQTT_ETH.yaml --device <device-ip>

    # Erase all flash (full clean slate including stored settings)
    esptool.py --chip esp32s3 --port /dev/cu.usbmodem* erase_flash

Build with ESPHome 2026.3.1 (the version the fleet is flashed with) unless a
newer version has been verified on a bench unit first. A `secrets.yaml` is
required next to the YAML files; copy `secrets.yaml.example` and fill it in.

---

## Appendix: Topic Examples

    # Everything from one device (the device id is inside the first level)
    mosquitto_sub -h BROKER -v -t 'Apollo/temp-pro-1-J8/#'

    # One value from all devices
    mosquitto_sub -h BROKER -v -t 'Apollo/+/sensor/battery_level'
    mosquitto_sub -h BROKER -v -t 'Apollo/+/status/health'

    # Wake all battery devices into wired mode (remember the cleanup!)
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/command/power_source' -m 'wired' -r

    # Restart all WIRED devices (non-retained; battery units reboot each wake anyway)
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/command/restart' -m '1'

    # Set sleep duration to 1 hour for battery devices only
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/battery/command/sleep_duration' -m '60'

    # Set one device's column id (per-device topic, mac suffix from meta/device_id)
    mosquitto_pub -h BROKER -t 'apollo/temp-pro-1/352bc8/command/column_id' -m 'H05'
