# How the protocol was figured out

The short version of the build log, for anyone who wants to repeat this with a different remote or
check whether a newer TempoFlex still behaves the same way. I did this with an AI coding agent
(Claude Code): the agent proposed each experiment and wrote the analysis scripts, I pressed buttons,
and every conclusion below comes from a real capture.

## 1. Look up the FCC filing

The remote's FCC ID is on its label: **2A5N4-TEMPO**. The filing at
<https://fccid.io/2A5N4-TEMPO> has a test report, the user manual and internal photos. It gave the
band (902–928 MHz), but its "ASK, 8.49 kHz" description turned out to be wrong.

(fccid.io blocks some automated fetches. `curl -A "Mozilla/5.0" https://fccid.io/2A5N4-TEMPO/<exhibit>.pdf`
works.)

**Dead end:** a CC1101 radio module, the usual choice for sub-GHz remotes, can't receive this at
all. It has no LoRa modem.

## 2. Listen with an RTL-SDR

A NooElec NESDR Smart v5 and the `rtl-sdr` / `rtl_433` packages (in most Linux distributions'
repos):

```bash
rtl_test -t                                    # is the dongle detected?
rtl_sdr -f 915.05M -s 1024k -g 20 capture.cu8  # record raw IQ while pressing buttons
```

**Use fixed gain** (`-g 20`). Auto gain clipped the strong remote signal next to the antenna.
`rtl_433` decoded nothing useful, because it doesn't do LoRa, but its pulse view showed regular
~96 ms bursts.

## 3. Identify the modulation

[`tools/bursts.py`](../tools/bursts.py) finds bursts in a `.cu8` recording and measures each one:
level, clipping, carrier offset, FM deviation and AM depth. The remote's bursts were wide
(~125 kHz) with a frequency sweep inside each symbol: **LoRa chirps**, not ASK.

[`tools/lora_symbols.py`](../tools/lora_symbols.py) dechirps a burst and prints its symbols:

```bash
uv run tools/lora_symbols.py capture.cu8 53.135 --sf 7 --bw 125e3 --tuned 915.05e6
```

It found spreading factor 7 (1.024 ms symbols), a preamble of at least 8 symbols, and **sync word
0x12**. Comparing captures showed:

- **Two-way traffic.** Every remote packet is answered by the Dongle about 0.25 s later, at a lower
  level.
- **No rolling code.** Polls repeat byte for byte.
- **Commands differ** from polls in only one region, plus the checksum.

## 4. Decode the bytes with a real LoRa radio

An SDR can show LoRa symbols, but turning them into bytes needs dewhitening, Hamming decoding and
so on. It's far easier to let a real LoRa chip do it. A Heltec WiFi LoRa 32 V3 running ESPHome's
`sx126x` component, with the settings above and an `on_packet` handler that logs hex, received
every frame with valid CRC: **50 bytes, both directions**.

Pressing buttons one at a time (5 s apart, writing down the order) and diffing the frames gave the
command table in [PROTOCOL.md](PROTOCOL.md). The checksum fell out of comparing ON and OFF: they
differ by one byte, and so do their last payload bytes.

## 5. Replay, and the fail-safe

- **Sending a captured ON frame once:** the Dongle replied, but its reply still said "off". Its
  replies describe the state *before* the command.
- **Sending ON twice and then a poll,** as the remote does: **the steam came on.**
- **A minute or two later it stopped by itself.** The Dongle turns the steam off when the polls
  stop. The poll turned out to carry the room temperature (it climbed 77.0 → 77.4 °F as the room
  warmed), so the remote *is* the thermostat.

The firmware therefore keeps polling (with a fixed 77 °F) only up to a cap, and **hands off** the
moment it hears the real remote, which keeps the session going with real readings.

## Gotchas collected along the way

- **Charge-only USB cables.** The board powers up and runs its demo, but no serial port appears.
- **Heltec V3 logs go to UART0** (its CP2102), not the ESP32-S3's native USB. Set
  `logger: hardware_uart: UART0`.
- **PRG vs RST.** They're identical tiny buttons. RST reboots the board.
- **ESPHome template switches default to `restore_mode: ALWAYS_OFF`,** which runs the turn-off
  action at every boot, so every reboot sent Steam OFF. Use `restore_mode: DISABLED`.
- **A USB serial reset can register as a PRG press** (GPIO0 is a boot-strap pin). Ignore button
  presses for a few seconds after boot.
- **Home Assistant on another subnet** won't discover the bridge over mDNS. Add it by IP and
  reserve that IP in DHCP.

## Checking a different remote

1. Find its FCC ID and band.
2. Record with an RTL-SDR at fixed gain while pressing one button, and run `tools/bursts.py`. Wide
   bursts with sweeping frequency mean LoRa.
3. Run `tools/lora_symbols.py` to get the spreading factor and sync word.
4. Put those values in `esphome/steam-bridge.yaml` (`spreading_factor`, `bandwidth`, `sync_value`,
   `frequency`), flash, and watch the `rx` log lines. If the frames look like the ones in
   PROTOCOL.md, the rest may just work. If not, the same button-by-button diffing applies.
