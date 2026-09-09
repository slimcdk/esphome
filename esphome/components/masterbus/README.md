# Mastervolt MasterBus

Read and control Mastervolt equipment — battery blocks, chargers, inverters, displays — from
ESPHome over a CAN transceiver.

MasterBus is a closed protocol running at 250 kbit/s on CAN. The wire format used here was
recovered by watching a live installation and checking every step against the vendor's own
library, and `masterbus_protocol.h` records how far each part is actually known. Nothing in this
component guesses at a frame it has not seen.

> [!WARNING]
> This component can write to the bus, and on a real installation those fields open battery
> contactors and switch chargers. Read [Writing to the bus](#writing-to-the-bus) before you
> configure a `switch`, `button` or `number`.

### Table of contents

- [Setup](#setup)
  - [CAN bus](#configuration-of-the-can-bus)
  - [Hub](#configuration-of-the-hub)
  - [Devices](#configuration-of-devices)
- [Finding your equipment](#finding-your-equipment)
- [Entities](#entities)
  - [Common options](#common-options)
  - [`sensor`](#sensor)
  - [`binary_sensor`](#binary_sensor)
  - [`switch`](#switch)
  - [`button`](#button)
  - [`number`](#number)
  - [`select`](#select)
  - [`text_sensor`](#text_sensor)
- [Polling and `update_interval`](#polling-and-update_interval)
- [Writing to the bus](#writing-to-the-bus)
- [Automations](#automations)
- [Example](#example)
- [Wiring](#wiring)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)

<br>

## Setup

The component sits on top of the standard [`canbus`][canbus-component] component, so any CAN
platform ESPHome supports will do.

---

### Configuration of the CAN bus

```yaml
canbus:
  - platform: esp32_can
    id: mb_can
    tx_pin: REPLACEME
    rx_pin: REPLACEME
    bit_rate: 250kbps
    mode: NORMAL
    can_id: 0
    rx_queue_len: 32
```

* `bit_rate` (**Required**): MasterBus runs at **250 kbit/s**. No other rate will work.

* `mode` (*Optional*): `NORMAL` lets the node transmit, which is what polling and writing need. Use
  `LISTENONLY` for a first look at a bus you would rather not touch; entities still update from
  what other devices ask for.

* `rx_queue_len` (*Optional*): a busy MasterBus carries about 200 frames a second. A queue of 32 or
  more keeps frames from being dropped during a slow loop.

> [!NOTE]
> The `canbus` component logs one line per frame at DEBUG. At 200 frames a second that is far more
> than a 115200 baud serial log can carry. Add `logs: {canbus: WARN}` to your `logger:` unless you
> are specifically debugging the bus.

---

### Configuration of the hub

```yaml
masterbus:
  id: mb
  canbus_id: mb_can
```

* `id` (*Optional*, [ID][config-id]): the hub's ID, so entities can reference it.

* `canbus_id` (**Required**, [ID][config-id]): the [`canbus`][canbus-component] component to listen
  on.

* `scan` (*Optional*, boolean, default `false`): ask every device on the bus to announce itself at
  boot and print a pasteable configuration. See [Finding your
  equipment](#finding-your-equipment).

* `log_all_frames` (*Optional*, boolean, default `false`): log every MasterBus frame at DEBUG, with
  the message type and address already decoded. For protocol work, not for normal use.

* `devices` (*Optional*, list): the equipment you want to talk to. See below.

Several hubs may be configured, one per CAN bus.

---

### Configuration of devices

Every entity belongs to a device, and a device is one physical unit addressed by the identifier it
announces on the bus.

```yaml
masterbus:
  canbus_id: mb_can
  devices:
    - id: mb_battery
      device: 0x6D56EA
      timeout: 60s
```

* `id` (**Required**, [ID][config-id]): referenced by entities through `masterbus_device_id`.

* `device` (**Required**, hex int): the device's MasterBus address, `0x0` to `0x7FFFFF`. The
  extended CAN identifier gives the address 23 bits; the rest carries the message type.

* `timeout` (*Optional*, [time][config-time], default `60s`): how long without a frame before the
  device counts as offline and its entities go unavailable.

* `on_online` / `on_offline` (*Optional*, [Automation][automation]): see
  [Automations](#automations).

<br>

## Finding your equipment

You do not need to know your addresses in advance. Set `scan: true`, flash, and read the log:

```yaml
masterbus:
  canbus_id: mb_can
  scan: true
```

The scan sends a node request, then walks every group of every device that answers, reading each
field's number, display type, name, unit and range. It prints the result as configuration you can
paste:

```
[I][masterbus.scan]: Scan found device 0x6D56EA
[I][masterbus.scan]: Walking 7 devices for their fields.
[I][masterbus.scan]: sensor:
[I][masterbus.scan]:   # device 0x6D56EA, group 1: Battery
[I][masterbus.scan]:   - platform: masterbus
[I][masterbus.scan]:     masterbus_device_id: mb_device_6D56EA
[I][masterbus.scan]:     param: 139
[I][masterbus.scan]:     name: "Battery"
[I][masterbus.scan]:     update_interval: 10s
[I][masterbus.scan]:     unit_of_measurement: "V"
[I][masterbus.scan]:     # range 0 to 600 step 0.01
```

The group name is printed as a comment above each group's fields, and it matters: field names on
this equipment are short and repeat. A battery block reports three separate fields all called
`Battery` — voltage, current and temperature — and it is the group that tells them apart.

> [!TIP]
> Scanning transmits, so it needs `mode: NORMAL`. It also walks every group of every device and
> takes minutes on a full installation. Turn it off again once you have your configuration.

<br>

## Entities

### Common options

Every platform below accepts these, in addition to its own base schema:

* `masterbus_device_id` (**Required**, [ID][config-id]): which device this field belongs to.

* `param` (**Required**, hex int, `0x0`–`0xFFFF`): the field number.

* `tab` (*Optional*, default `monitoring`): `monitoring`, `alarm`, `history` or `configuration`.
  Only `monitoring` has a known frame format; the others are accepted so a configuration can be
  written now and work when the format is decoded.

* `value_type` (*Optional*): how to read the 32 bits the device sends. Each platform accepts only
  the types it can render, and defaults sensibly, so you rarely need to set this.

* `update_interval` (*Optional*, [time][config-time]): see [Polling and
  `update_interval`](#polling-and-update_interval). **Left unset the entity never transmits.**

* `timeout` (*Optional*, [time][config-time]): how long without a value before this one entity goes
  unavailable. Unset, it follows its device.

---

### `sensor`

Numeric readings. Accepts `value_type` of `float` (default), `boolean` or `list_option`.

```yaml
sensor:
  - platform: masterbus
    masterbus_device_id: mb_battery
    param: 139
    name: Block voltage
    update_interval: 10s
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2
```

A field that reports "not a number" publishes as unavailable rather than as zero.

---

### `binary_sensor`

A boolean field, read only.

```yaml
binary_sensor:
  - platform: masterbus
    masterbus_device_id: mb_charger
    param: 21
    name: Charger running
    update_interval: 1min
```

---

### `switch`

A boolean field you can also set.

```yaml
switch:
  - platform: masterbus
    masterbus_device_id: mb_charger
    param: 21
    name: Charger on
```

The switch publishes nothing when you operate it. It follows what the device reports afterwards,
so it never shows a state the equipment did not confirm. Give it an `update_interval`, or make
sure something else on the bus polls the field, or it will never show a state at all.

---

### `button`

Fires an event field once. Buttons take no `update_interval` — there is nothing to read back.

```yaml
button:
  - platform: masterbus
    masterbus_device_id: mb_battery
    param: 117
    name: Close relay
```

---

### `number`

A numeric field you can set. `min_value`, `max_value` and `step` are required; a scan prints the
device's own range as a comment.

```yaml
number:
  - platform: masterbus
    masterbus_device_id: mb_charger
    param: 20
    name: Charger max current
    min_value: 0
    max_value: 100
    step: 1
    unit_of_measurement: "%"
```

---

### `select`

A list field. The options are the device's own, listed in the order it numbers them, because the
reported index picks the entry.

```yaml
select:
  - platform: masterbus
    masterbus_device_id: mb_charger
    param: 18
    name: Charger stage
    options: ["Bulk", "Absorption", "Float"]
```

> [!NOTE]
> Get the order from a scan, not from guesswork. An index the list does not cover publishes as
> unavailable and logs a warning.

---

### `text_sensor`

Accepts `value_type` of `text` (default), `time` or `date`.

> [!WARNING]
> **This platform does not receive values yet.** A monitoring answer carries 32 bits, and the hub
> currently renders only `float`, `boolean` and `list_option` from them — anything else publishes
> as unavailable. Time and date do fit (they arrive as a raw 32-bit value, not a float) and text
> needs a second read from the device's string table. Neither path is wired up.

<br>

## Polling and `update_interval`

A MasterBus device answers a field **only when asked**. Nothing arrives unprompted, so an entity
with no `update_interval` never transmits and stays silent — which is deliberate, so that a first
configuration on someone's boat or battery bank does nothing until asked.

The unusual part: **an answer meant for someone else counts.** MasterBus answers are broadcast, so
a display panel or another bridge asking the same device for the same field updates your entity
too. When that has already happened within the interval, the request is skipped.

So `update_interval` is an upper bound on how stale a value may get, and an upper bound on your own
traffic — not a fixed transmit rate. On a bus where a display panel already polls at 1 Hz, a node
configured with 35 entities at `10s` was measured transmitting **nothing at all** for the fields the
panel covered, while every entity kept updating. For a field nobody else wants, the same node polls
at exactly the configured interval.

<br>

## Writing to the bus

Writing works, and on real equipment it does real things. Two properties of the protocol are worth
knowing before you wire a button into an automation.

**There is often no state to read back.** Fields like `Close relay` and `Open relay` are *events*:
firing one sends the command, and reading the field afterwards always returns false. It reports the
last command, not the contactor's position. The only evidence a command took effect is some other
field — the block's own current, say.

**A write that reaches the bus is not a command that took effect.** Equipment can be shared between
devices, and then only one of them accepts the command for it. On the installation this component
was developed against, two battery blocks share one contactor: either one opens it, but only one of
the two closes it again. Correctly formed writes to the other block changed nothing at all, with no
error anywhere.

This component therefore does not retry, does not publish optimistically, and does not decide which
device to address on your behalf. Which device owns what is a property of your installation, not of
the protocol. Read the result back and build the retry logic you need on top.

<br>

## Automations

### `on_online`

Fires the first time a device answers after being offline.

```yaml
masterbus:
  canbus_id: mb_can
  devices:
    - id: mb_battery
      device: 0x6D56EA
      on_online:
        - logger.log: "Battery bank answered"
      on_offline:
        - logger.log: "Battery bank stopped answering"
```

### `on_offline`

Fires when nothing has been heard from the device for its `timeout`.

<br>

## Example

A battery bank of six blocks, reading the pack totals from one block's cluster group and each
block's own readings from its battery group.

```yaml
canbus:
  - platform: esp32_can
    id: mb_can
    tx_pin: GPIO20
    rx_pin: GPIO21
    bit_rate: 250kbps
    mode: NORMAL
    can_id: 0
    rx_queue_len: 32

logger:
  logs:
    canbus: WARN

masterbus:
  id: mb
  canbus_id: mb_can
  devices:
    - id: mb_bat1
      device: 0x6D56EA
      on_offline:
        - logger.log: "BAT 1 stopped answering"
    - id: mb_bat2
      device: 0x6D4ECC

sensor:
  # Group 0 "Cluster" on any one block reports the pack as a whole.
  - platform: masterbus
    masterbus_device_id: mb_bat1
    param: 0
    name: Bank state of charge
    update_interval: 10s
    unit_of_measurement: "%"
    device_class: battery
    state_class: measurement
    accuracy_decimals: 1
  - platform: masterbus
    masterbus_device_id: mb_bat1
    param: 1
    name: Bank voltage
    update_interval: 10s
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2

  # Group 1 "Battery" is each block's own readings.
  - platform: masterbus
    masterbus_device_id: mb_bat1
    param: 139
    name: BAT 1 voltage
    update_interval: 10s
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2
  - platform: masterbus
    masterbus_device_id: mb_bat2
    param: 139
    name: BAT 2 voltage
    update_interval: 10s
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2

button:
  - platform: masterbus
    masterbus_device_id: mb_bat1
    param: 119
    name: BAT 1 open relay
  - platform: masterbus
    masterbus_device_id: mb_bat1
    param: 117
    name: BAT 1 close relay
```

<br>

## Wiring

MasterBus uses RJ45 connectors carrying CAN plus power. Only CAN_H and CAN_L are needed here; on
the cables seen so far they are the **orange** and **orange/white** pair.

| | |
|---|---|
| CAN_H, CAN_L | to the transceiver's bus side |
| Transceiver CTX | to the ESP's `tx_pin` |
| Transceiver CRX | to the ESP's `rx_pin` |
| Ground | common with the MasterBus ground |

**Termination is 120 Ω at each end of the bus, and no more.** Measure across CAN_H and CAN_L with
everything powered down: 60 Ω means exactly two terminators, which is correct. 40 Ω means three,
and the bus will be unreliable. Many cheap transceiver modules carry a 120 Ω resistor on board that
has to come off if the bus is already terminated at both ends.

> [!TIP]
> On an SN65HVD230 module, check the `Rs` pin. It must sit near ground for high-speed mode. Held
> high it puts the transceiver in standby, where the receiver still works but the driver does not —
> so the node reads the bus perfectly and every transmit fails. That failure looks exactly like a
> dead chip.

<br>

## Troubleshooting

**Nothing at all is received.** Check the bit rate is `250kbps`, then the termination, then the
wiring. A scan that hears nothing says so in the log.

**Values are received but nothing can be transmitted** (`ERROR_ALLTXBUSY` on every attempt). The
receiver works and the driver does not. Check the transceiver's `Rs`/standby pin, then swap the
transceiver — this exact symptom has turned out to be a module fault.

**Entities stay unavailable.** Either the device is not answering — check the address against a
scan — or nothing is asking. An entity with no `update_interval` transmits nothing and only updates
when something else on the bus asks for the same field.

**The log is missing lines or full of half-lines.** The serial log cannot keep up with a busy bus.
Mute the frame log with `logs: {canbus: WARN}`, raise the baud rate, or read the log over the API
instead. Two readers on the same serial port also produce interleaved half-lines that look like
corruption but are not.

**A write is accepted but nothing happens.** See [Writing to the bus](#writing-to-the-bus). Check
whether another device shares the equipment you are commanding.

<br>

## Limitations

* Only the `monitoring` tab has a known frame format. The other tabs are accepted in configuration
  but cannot be read or written yet.
* `text_sensor` cannot receive values yet — see [`text_sensor`](#text_sensor).
* Firmware update over MasterBus is out of scope, and the bootloader tab is deliberately absent.
* The component carries no table of device types, article numbers or field layouts. What a field
  means is a property of your equipment; use a scan to find out.

<br>

[canbus-component]: <https://esphome.io/components/canbus.html> "ESPHome CAN bus Component"
[config-id]: <https://esphome.io/guides/configuration-types#config-id> "ESPHome ID Config Schema"
[config-time]: <https://esphome.io/guides/configuration-types#config-time> "ESPHome Time Config Schema"
[automation]: <https://esphome.io/automations/actions.html> "ESPHome Automation"
