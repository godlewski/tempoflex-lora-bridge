# TempoFlex ↔ Dongle radio protocol

Decoded from captures of one Mr. Steam TempoFlex remote (P104702BK, FCC ID 2A5N4-TEMPO) and its
Dongle, US 915 MHz. Everything here was observed on real hardware; anything not confirmed is marked
**unknown**. Implementation: [`esphome/steam_protocol.h`](../esphome/steam_protocol.h).

## Radio

| | |
|---|---|
| Frequency | 915.000 MHz |
| Modulation | **LoRa**, bandwidth 125 kHz, spreading factor 7, coding rate 4/5, explicit header |
| Sync word | `0x12` (SX127x notation), `[0x14, 0x24]` on an SX126x |
| Preamble | 8 symbols |
| LoRa CRC | on |
| Packet length | always **50 bytes** |
| Encryption / rolling code | **none** |

The FCC test report for this remote describes "ASK, 8.49 kHz bandwidth". **That's wrong**: the
signal is LoRa. An SDR shows the characteristic chirps.

## Frame layout

```
offset  0-1    a5 5a            start marker
        2-13   sender block     12 bytes
        14-25  receiver block   12 bytes
        26     N                number of bytes that follow, including the checksum
        27...  payload          first byte = command
        26+N   checksum         low byte of the sum of bytes 26 .. 25+N
        ...    padding          up to 50 bytes
```

- An **address block** is a type byte, a 7-character **ASCII serial number**, and 4 more bytes
  (meaning unknown, constant per device). The remote's and the Dongle's blocks swap places between
  requests and replies. Because they contain serial numbers, the examples below show them as
  `[REMOTE]` and `[DONGLE]`.
- The **checksum covers only N and the payload**, not the addresses.
- The remote **doesn't clear its buffer**: the padding after its checksum often holds leftovers from
  its previous packet. Ignore everything after the checksum.
- The bridge's "pairing" (`steam_pairing.h`) is simply the first 26 bytes of a remote frame:
  `a5 5a [REMOTE] [DONGLE]`.

Example: Steam ON from the remote:

```
a5 5a [REMOTE] [DONGLE] 08 07 00 01 00 14 03 70 97 00 00 …
                        N  └──── payload ─────┘ sum
08+07+00+01+00+14+03+70 = 0x97 ✓
```

## Temperatures

Every temperature is a 16-bit big-endian value: **(°F − 32) × 10**, i.e. tenths of a degree above
freezing.

| Raw | °F |
|---|---|
| `01 c2` (450) | 77.0 |
| `03 0c` (780) | 110.0 |
| `03 66` (870) | 119.0 |
| `03 70` (880) | 120.0 |

## Remote → Dongle commands

| Command | Payload | Meaning | When the remote sends it |
|---|---|---|---|
| `00` | `00 TT TT` | **Poll / keep-alive.** `TT TT` = room temperature measured by the remote | Every 0.3–2.6 s while awake or while steaming |
| `03` | `03 TT TT` | Set the temperature setpoint | Up/down arrows while temperature is selected |
| `04` | `04 00 MM` | Set the duration, minutes | Up/down arrows while the timer is selected; also after power |
| `05` | `05 00 0X` | Aroma on (`01`) / off (`00`) | Aroma button, **only while steaming** |
| `06` | `06 00 MM TT TT` | Load a saved profile (duration + setpoint) | Profile button |
| `07` | `07 00 SS 00 MM TT TT` | **Steam** on (`SS = 01`) / off (`00`), carrying duration and setpoint | Power button |
| `15` | `15 00 0X` | Chroma light on / off | Chroma button |
| `16` | `16 00 0X` | Chroma mode (`01`, `02` seen) | Chroma button while the light is on |

The **Timer** and **Temperature** buttons send nothing: they only choose what the arrows adjust.

## Dongle → remote status

The Dongle answers **every** remote frame, about 0.2–0.3 s later, with a status frame (N = `0x13`):

| Payload offset | Example | Meaning |
|---|---|---|
| 0–1 | `01 00` | constant (unknown) |
| 2 | `01` | **Steam on** |
| 3 | `00` | unknown |
| 4 | `00` | **Aroma on** |
| 5–6 | `03 70` | **Setpoint** (temperature encoding) |
| 7–8 | `01 f4` | unknown. 500 would be 82 °F in the temperature encoding; possibly the generator's own sensor |
| 9–10 | `00 14` | **Duration** setting, minutes |
| 11–12 | `00 14` | **Minutes remaining** (counts down once a minute; `00 00` when off) |
| 13–14 | `00 00` | unknown (possibly fault flags, such as low water) |
| 15 | `00` | **Chroma on** |
| 16 | `02` | **Chroma mode** |
| 17 | `5a` | unknown (90) |

**The reply to a command shows the state from just *before* that command.** That's why the
remote, and the bridge, send each command twice and then poll: the reply to the poll is the first
one that reflects the change.

## Behavior that matters

- **The Dongle has a fail-safe.** If the steam is on and the polls stop for a minute or two, it turns
  the steam off. Anything that starts the steam has to keep polling, and every poll carries a room
  temperature.
- **The remote takes over a session it didn't start.** Wake it while the steam is on: it reads the
  Dongle's status, shows ON, and keeps polling with real temperatures.
- **Remote and Dongle signal levels:** with the bridge in a neighboring room, the remote arrived at
  about −38 dBm and the Dongle at about −74 dBm, both with SNR around +12 dB. 3 dBm transmit power
  from the bridge was enough.
- Unrelated 915 MHz traffic (utility meters, other sensors) is common. The LoRa sync word and the
  `a5 5a` marker filter it out.
