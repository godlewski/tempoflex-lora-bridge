# Instructions for AI agents

You're helping someone set up this bridge. The human-facing guide is [README.md](README.md); this
file tells you how to run it safely.

## Ground rules

- **The person must physically do these themselves:** attach the antenna (before any power), plug
  in the board, press buttons on the remote and on the board, and confirm whether steam is actually
  flowing. Ask them, and wait for their answer.
- **Never invent values.** Wi-Fi name and password come from the person. Generate `api_key`
  (`openssl rand -base64 32`) and `ota_password` (`openssl rand -hex 16`) yourself.
  `steam_pairing.h` bytes come only from the bridge's `PAIRING:` log line.
- **Never commit or print** `esphome/secrets.yaml` or `esphome/steam_pairing.h`. Both are
  gitignored. Don't paste the Wi-Fi password or the pairing bytes into chat output, issues or PRs.
- **Ask before anything that turns on the steam** (a PRG press you request, or toggling
  `switch.steam_bridge_steam`), and make sure someone is there to watch it.
- **Don't raise `keepalive_max_min`** or `tx_power_dbm` unless the person explicitly asks, and
  explain the [safety tradeoff](README.md#safety) when they do.

## Steps

1. **Check compatibility.** Ask for the FCC ID on the back of the remote. Proceed if it's
   `2A5N4-TEMPO`. Otherwise explain that it's untested and point to
   [docs/REVERSE-ENGINEERING.md](docs/REVERSE-ENGINEERING.md).
2. **Tools.** Make sure `uv` is installed (`uvx esphome version` works). On Linux, make sure the
   user is in the serial group (`uucp` on Arch, `dialout` on Debian/Ubuntu); adding them needs sudo
   and a re-login, so tell them.
3. **Secrets.** In `esphome/`: `cp secrets.yaml.template secrets.yaml` and
   `cp steam_pairing.h.template steam_pairing.h`. Fill in `secrets.yaml`, with Wi-Fi from the person
   (it must be 2.4 GHz). Leave the pairing file as zeros (listen-only).
4. **Validate before flashing:** `uvx esphome config steam-bridge.yaml`.
5. **Antenna and USB.** Ask the person to attach the antenna, then plug in the board. Confirm a
   serial port appeared (`ls /dev/ttyUSB* /dev/cu.*`). If none appears but the board lights up, the
   cable is charge-only.
6. **Flash:** `uvx esphome run steam-bridge.yaml --device <port> --no-logs`. If it can't connect, ask
   them to hold PRG, tap RST, release PRG.
7. **Pair.** Run `uvx esphome logs steam-bridge.yaml --device <port>` in the background. Ask them to
   wake the remote and press **power** or an **arrow** (Timer and Temperature send nothing). Wait for
   `PAIRING: paste into steam_pairing.h: ...`, write those 26 bytes into `steam_pairing.h`, and flash
   again. Over Wi-Fi is fine now: `--device <bridge IP>`.
8. **Verify listening.** Within a couple of minutes the log should show Dongle `rx` frames (the
   bridge polls at boot and every 2 minutes while idle), and `Dongle connected` should turn on.
9. **Verify control (ask first).** With the person watching the shower, ask them to tap PRG, or
   toggle the switch from Home Assistant. Confirm from the log that the Dongle reports steam on, ask
   them whether steam is flowing, then turn it off the same way.
10. **Home Assistant.** Follow [README → Home Assistant](README.md#home-assistant). If HA doesn't
    discover the bridge, add it by IP, and suggest a DHCP reservation.

## Reading the log

| Line | Meaning |
|---|---|
| `rx rssi=… a55a…` | A received frame (debug). Byte 26 = N; `13` = Dongle status |
| `PAIRING: paste into steam_pairing.h: …` | First remote frame while unpaired |
| `Not paired yet` | A command was requested before pairing; nothing was sent |
| `Steam ON; keep-alive for N min` | The bridge started the steam and is polling |
| `Remote is active; handing off` | The real remote took over; the bridge stopped polling |
| `Keep-alive cap reached` | The bridge sent OFF at the cap |
| `Dongle reports steam OFF; keep-alive stopped` | Steam turned off elsewhere (remote or timer) |

Decode frames with [docs/PROTOCOL.md](docs/PROTOCOL.md). The Dongle's status bytes follow N at
offset 27.
