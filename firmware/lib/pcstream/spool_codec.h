#pragma once
#include <cstdint>
#include <cstddef>

// Each record in the SD spool file is: [8-byte header][JPEG payload].
// Header is little-endian: sequence number, then payload length.
constexpr size_t SPOOL_HEADER_LEN = 8;

inline void encodeSpoolHeader(uint32_t seq, uint32_t len, uint8_t out[SPOOL_HEADER_LEN]) {
  out[0] = (uint8_t)(seq);       out[1] = (uint8_t)(seq >> 8);
  out[2] = (uint8_t)(seq >> 16); out[3] = (uint8_t)(seq >> 24);
  out[4] = (uint8_t)(len);       out[5] = (uint8_t)(len >> 8);
  out[6] = (uint8_t)(len >> 16); out[7] = (uint8_t)(len >> 24);
}

inline void decodeSpoolHeader(const uint8_t in[SPOOL_HEADER_LEN], uint32_t& seq, uint32_t& len) {
  seq = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
  len = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
}
