#pragma once
// Mr. Steam TempoFlex <-> Dongle LoRa frames. Full description: docs/PROTOCOL.md
//
//   [0-1]   a5 5a           start marker
//   [2-13]  sender block    12 bytes, identifies the sending device (type byte + ASCII serial + 4 bytes)
//   [14-25] receiver block  12 bytes, identifies the receiving device
//   [26]    N               bytes that follow, up to and including the checksum
//   [27..]  payload         first byte is the command
//   [26+N]  checksum        low byte of the sum of bytes 26 .. 25+N
//   rest    padding to 50 bytes
#include <cstdint>
#include <vector>

#include "steam_pairing.h"  // STEAM_PAIRING: your remote's first 26 bytes (see steam_pairing.h.template)

namespace steam {

static const size_t FRAME_LEN = 50;
static const size_t HEADER_LEN = 26;
static const uint8_t STATUS_N = 0x13;  // only the Dongle sends 19-byte status frames

// Remote commands (first payload byte).
static const uint8_t CMD_POLL = 0x00;        // 00 TT TT: keep-alive poll carrying the room temperature
static const uint8_t CMD_TEMP = 0x03;        // 03 TT TT: setpoint
static const uint8_t CMD_DURATION = 0x04;    // 04 00 MM: minutes
static const uint8_t CMD_AROMA = 0x05;       // 05 00 0X: Aroma on/off (remote sends it only while steaming)
static const uint8_t CMD_PROFILE = 0x06;     // 06 00 MM TT TT: load a saved profile
static const uint8_t CMD_STEAM = 0x07;       // 07 00 SS 00 MM TT TT: steam on/off with duration + setpoint
static const uint8_t CMD_LIGHT = 0x15;       // 15 00 0X: Chroma light on/off
static const uint8_t CMD_LIGHT_MODE = 0x16;  // 16 00 0X: Chroma mode

// Temperatures are (F - 32) * 10, i.e. tenths of a degree above freezing.
inline uint16_t f_to_raw(float f) { return (uint16_t) ((f - 32.0f) * 10.0f + 0.5f); }
inline float raw_to_f(uint16_t raw) { return raw / 10.0f + 32.0f; }

// steam_pairing.h.template ships all zeros, which means "not paired yet: listen only".
inline bool paired() { return STEAM_PAIRING[0] == 0xA5 && STEAM_PAIRING[1] == 0x5A; }

// Length and checksum check, for frames from either side.
inline bool valid(const std::vector<uint8_t> &x) {
  if (x.size() != FRAME_LEN || x[0] != 0xA5 || x[1] != 0x5A)
    return false;
  uint8_t n = x[26];
  if (n < 2 || 26 + n >= FRAME_LEN)
    return false;
  uint8_t sum = 0;
  for (size_t i = 26; i < 26u + n; i++)
    sum += x[i];
  return sum == x[26 + n];
}

inline bool same(const std::vector<uint8_t> &x, size_t at, const uint8_t *ref, size_t from, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (x[at + i] != ref[from + i])
      return false;
  return true;
}

// A Dongle status frame. Once paired, only from *our* Dongle (its block is our receiver block).
inline bool is_dongle(const std::vector<uint8_t> &x) {
  return valid(x) && x[26] == STATUS_N && (!paired() || same(x, 2, STEAM_PAIRING, 14, 12));
}

// A frame from a remote. Once paired, only from *our* remote addressed to *our* Dongle. This bridge
// never hears its own transmissions, so one of these means a person is using the remote.
inline bool is_remote(const std::vector<uint8_t> &x) {
  return valid(x) && x[26] != STATUS_N && (!paired() || same(x, 0, STEAM_PAIRING, 0, HEADER_LEN));
}

// The remote's poll (00 TT TT) carries its room temperature reading.
inline bool remote_room_temp(const std::vector<uint8_t> &x, float &f) {
  if (!is_remote(x) || x[26] != 4 || x[27] != CMD_POLL)
    return false;
  f = raw_to_f((x[28] << 8) | x[29]);
  return true;
}

// Build a frame as our remote would send it.
inline std::vector<uint8_t> frame(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> b(STEAM_PAIRING, STEAM_PAIRING + HEADER_LEN);
  uint8_t n = payload.size() + 1;
  uint8_t sum = n;
  b.push_back(n);
  for (uint8_t v : payload) {
    b.push_back(v);
    sum += v;
  }
  b.push_back(sum);
  b.resize(FRAME_LEN, 0x00);
  return b;
}

// Dongle status payload, byte by byte after N (see docs/PROTOCOL.md).
struct Status {
  bool steam, aroma, light;
  uint8_t light_mode;
  uint16_t setpoint_raw, duration, remaining;
};

inline bool parse_status(const std::vector<uint8_t> &x, Status &s) {
  if (!is_dongle(x))
    return false;
  s.steam = x[29] != 0;
  s.aroma = x[31] != 0;
  s.setpoint_raw = (x[32] << 8) | x[33];
  s.duration = (x[36] << 8) | x[37];
  s.remaining = (x[38] << 8) | x[39];
  s.light = x[42] != 0;
  s.light_mode = x[43];
  return true;
}

}  // namespace steam
