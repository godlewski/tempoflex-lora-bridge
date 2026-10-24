# TempoFlex LoRa Bridge

**Control a Mr. Steam steam shower from Home Assistant** by speaking the TempoFlex remote's radio
protocol with a $25 ESP32 LoRa board running ESPHome. No cloud, no app, and no wiring into the
steam generator.

- **Preheat from Home Assistant.** Turn the steam on from your phone, a dashboard or an automation.
- **Hand off to the real remote.** Walk in, wake the remote, and it takes over with its own
  thermostat and timer.
- **See what's happening.** Steam on/off, minutes remaining, setpoint and duration, plus the room
  temperature the remote measures, even in sessions you started at the remote.

> [!NOTE]
> **Built with AI.** I worked out this protocol and wrote the firmware and these docs together with
> an AI coding agent (Claude Code), and verified each step on real hardware. The
> [step-by-step story](docs/REVERSE-ENGINEERING.md) shows how. Review it before you trust it with
> your shower, like any code you find online.

> [!WARNING]
> **This drives a steam generator.** Hot steam and hot surfaces can burn. This is an unofficial
> project, not affiliated with or endorsed by Mr. Steam. Using it may void your warranty. Read
> [Safety](#safety) before you use it, and don't run it unattended.

---

## Does it work with my shower?

**Tested with exactly one setup:**

| | |
|---|---|
| Remote | **Mr. Steam TempoFlex**, model **P104702BK**, FCC ID **2A5N4-TEMPO** |
| Generator side | The **"Dongle"** the TempoFlex pairs with, plugged into a Mr. Steam steam generator |
| Region | United States, **915 MHz** |

If your remote has that FCC ID (it's on the label on the back), this should work. Other Mr. Steam
remotes (iSteam, SteamLinx and so on), other regions (868 MHz) or newer TempoFlex firmware are
**untested**. [docs/REVERSE-ENGINEERING.md](docs/REVERSE-ENGINEERING.md) explains how to check a
different remote with a $40 SDR.

## What you need

| Part | Notes |
|---|---|
| **Heltec WiFi LoRa 32 V3, 915 MHz** ([Amazon](https://www.amazon.com/Heltec-LoRa-32-V3-Meshtastic/dp/B0D1H1FN9Y)) | ESP32-S3 with an SX1262 LoRa radio. Get the **915 MHz** version; it comes with an antenna |
| **USB-C cable that carries data** | Many charging cables don't, and the board just won't show up |
| **USB power adapter** | Any 5 V phone charger. The bridge stays plugged in |
| **3D-printed case** (optional) | I used [Heltec LoRa 32 case for Meshtastic](https://www.printables.com/model/118750-heltec-lora-32-case-for-meshtastic). PETG holds up better than PLA |
| **A computer** | Linux, macOS or Windows, for the first flash over USB |
| **Home Assistant** | With the ESPHome integration (built in) |

Optional, only to investigate a *different* remote: an RTL-SDR such as the **NooElec NESDR Smart
v5**. You don't need it for the TempoFlex, because the protocol is already decoded.

## How it works

```
TempoFlex remote  <-- LoRa 915 MHz -->  Dongle (on the steam generator)
                         ^
                         | listens to both, and can talk like the remote
                  Heltec V3 bridge  <-- Wi-Fi -->  Home Assistant
```

The remote and the Dongle exchange 50-byte LoRa packets with **no encryption and no rolling code**:
a fixed header holding both devices' addresses, a command, and a simple sum checksum. The bridge
learns your remote's header once (the pairing step below) and can then send the same commands the
remote does. While the steam is on, the remote polls the Dongle every few seconds and reports the
room temperature in each poll, and the **Dongle shuts the steam off if the polls stop**. The bridge
handles this; see [Safety](#safety). Byte-level details are in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Setup

You'll need about 30 minutes. Every command runs from the `esphome/` folder.

### 1. Attach the antenna first

The antenna comes loose in the bag, with a tiny gold **u.FL** snap connector. Line it up over the
gold socket next to the metal shield on the board and press straight down with a fingernail until
it **clicks**. **Never power the board without the antenna**: transmitting into nothing can damage
the radio.

### 2. Install the tools

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), which runs ESPHome without a
separate install:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh     # macOS / Linux
uvx esphome version                                 # downloads ESPHome on first run
```

On **Linux**, your user needs access to serial ports: the `uucp` group on Arch, `dialout` on
Debian/Ubuntu (`sudo usermod -aG dialout $USER`, then log out and back in).

### 3. Get the code

```bash
git clone https://github.com/<you>/tempoflex-lora-bridge.git
cd tempoflex-lora-bridge/esphome
```

### 4. Fill in your secrets

```bash
cp secrets.yaml.template secrets.yaml
cp steam_pairing.h.template steam_pairing.h
openssl rand -base64 32     # paste as api_key
openssl rand -hex 16        # paste as ota_password
```

Edit `secrets.yaml` with your **2.4 GHz** Wi-Fi name and password and the two values above. Leave
`steam_pairing.h` as all zeros for now: that's **listen-only mode**, and the bridge won't transmit.

### 5. First flash (USB)

Plug the board into your computer and flash it:

```bash
uvx esphome run steam-bridge.yaml
```

Choose the USB serial port when asked (`/dev/ttyUSB0` on Linux; on macOS, a `cu.usbserial` or
`cu.SLAB` device). The first build takes a few minutes. If the upload can't connect: hold **PRG**,
tap **RST**, release **PRG**, and try again.

### 6. Pair with your remote

Keep the log running (`uvx esphome logs steam-bridge.yaml`), wake the TempoFlex remote and press
any button. The bridge prints a line like:

```
[W][steam]: PAIRING: paste into steam_pairing.h: 0xA5, 0x5A, 0x.., 0x.., ...
```

Copy those **26 bytes** over the zeros in `steam_pairing.h`, then flash again:

```bash
uvx esphome run steam-bridge.yaml     # can go over Wi-Fi now
```

These bytes are your remote's and Dongle's addresses. They contain the devices' serial numbers, so
`steam_pairing.h` is gitignored; don't publish it.

### 7. Test at the bridge

The board's **PRG** button (one of the two tiny buttons next to USB-C; the other is **RST**,
which reboots) toggles the steam. Tap it, and within a few seconds the generator should start; the
remote shows ON when you wake it. Tap again to turn it off. The log shows each command and the
Dongle's reply.

### 8. Add it to Home Assistant

See [Home Assistant](#home-assistant) below.

## Home Assistant

**Add the device:** *Settings → Devices & services*. The **Steam Bridge** usually appears as
discovered; click *Configure* and paste your `api_key`. If it doesn't appear (Home Assistant on a
different subnet or VLAN, so mDNS can't reach it), use *Add integration → ESPHome* and enter the
bridge's IP address, port `6053`. Give the bridge a **DHCP reservation** in your router so that IP
never changes.

**Entities** (with the default name `steam-bridge`):

| Entity | What it is |
|---|---|
| `switch.steam_bridge_steam` | **Steam on/off.** Shows the real state, including changes made at the remote |
| `number.steam_bridge_temperature_setpoint` | Setpoint, °F |
| `number.steam_bridge_duration` | Session length, minutes |
| `sensor.steam_bridge_time_remaining` | Minutes left in the current session |
| `sensor.steam_bridge_room_temperature` | Room temperature measured by the remote (updates only while the remote is awake) |
| `binary_sensor.steam_bridge_remote_active` | Someone is using the remote |
| `binary_sensor.steam_bridge_dongle_connected` | The Dongle answered in the last 5 minutes (diagnostic) |
| `sensor.steam_bridge_dongle_signal` | Dongle signal strength (diagnostic) |

**Dashboard:** [homeassistant/dashboard-card.yaml](homeassistant/dashboard-card.yaml) is a card
built only from standard Home Assistant cards. Add a card, choose *Manual*, and paste it in.

**Automations:** [homeassistant/automations.yaml](homeassistant/automations.yaml) has a timed
preheat and a "preheat done, wake the remote" phone notification.

**How a typical session goes:**

1. Home Assistant turns **Steam** on (from the dashboard, a schedule or a voice assistant).
2. The bridge keeps the steam alive for up to `keepalive_max_min` (default **10 min**), then turns
   it off by itself.
3. You walk in and wake the remote. It shows ON and starts polling, and the bridge **hands off
   immediately**: the remote's thermostat and timer run the rest of the session, and Home Assistant
   now shows the room temperature.

## Safety

- **While the bridge is in charge, there's no thermostat.** The remote is the thermostat: its polls
  carry the room temperature. The bridge has no sensor, so its keep-alive polls report a fixed
  77 °F, and the generator runs flat out without cycling at the setpoint. That's why the bridge's
  own run time is capped (`keepalive_max_min`, default 10 minutes) and it sends OFF at the cap.
  Raise the cap carefully.
- **Waking the remote gives control back to it.** From then on, the real thermostat and the
  remote's timer apply.
- The generator's own safety features (its timer, high-limit cutoff and low-water protection) are
  unaffected. The bridge only sends what the remote would send.
- Don't run steam unattended or with anyone inside who can't easily get out.

## Settings

Change these at the top of [`esphome/steam-bridge.yaml`](esphome/steam-bridge.yaml), then flash
again:

| Setting | Default | Meaning |
|---|---|---|
| `keepalive_max_min` | `10` | Longest the bridge alone keeps the steam on |
| `setpoint_min_f` / `setpoint_max_f` | `80` / `120` | Setpoint range shown in Home Assistant; match your remote's |
| `tx_power_dbm` | `3` | Transmit power. 3 dBm was plenty from another room; the maximum is 22 |
| `name` / `friendly_name` | `steam-bridge` / `Steam Bridge` | Device name; also changes the entity IDs |

## Troubleshooting

| Symptom | Fix |
|---|---|
| Board lights up but no serial port appears | Charge-only USB cable. Use one that carries data |
| Flash works but the log shows nothing after the boot banner | The logger must be on `UART0` (already set). On a V3 the USB-C port is a CP2102 serial chip, not the ESP32-S3's native USB |
| `Not paired yet` warning | Do [step 6](#6-pair-with-your-remote) |
| No `PAIRING` line when you press remote buttons | Check the antenna. Some buttons (Timer, Temperature) only change the remote's screen and send nothing; press **power** or the up/down arrows. If still nothing, see [docs/REVERSE-ENGINEERING.md](docs/REVERSE-ENGINEERING.md) |
| Steam turns on, then stops after a minute or two | That's the Dongle's fail-safe when polls stop. Make sure you're on this firmware; the bridge keeps polling up to the cap |
| `Dongle connected` is off | The bridge is out of range or the generator has no power. Move the bridge closer or raise `tx_power_dbm` |
| Pressing the board's buttons does nothing | You pressed **RST** (reboots). **PRG** is the other one. PRG is also ignored for 10 s after boot |

## For AI agents

If you're an AI agent helping someone set this up, start with [AGENTS.md](AGENTS.md). It lists the
exact steps, which values must come from the person, and which actions need their confirmation.

## Repo layout

```
esphome/steam-bridge.yaml          ESPHome firmware config (no personal values)
esphome/steam_protocol.h           frame building and parsing
esphome/steam_pairing.h.template   -> steam_pairing.h, your remote's 26-byte header
esphome/secrets.yaml.template      -> secrets.yaml, Wi-Fi and keys
homeassistant/                     dashboard card and example automations
docs/PROTOCOL.md                   the radio protocol, byte by byte
docs/REVERSE-ENGINEERING.md        how it was figured out, and how to check a different remote
tools/                             RTL-SDR analysis scripts used for the discovery
```

## License

[MIT](LICENSE). Mr. Steam, TempoFlex, iSteam and SteamLinx are trademarks of their owners; they are
named here only to describe compatibility.
