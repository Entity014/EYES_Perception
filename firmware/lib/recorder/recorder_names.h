#pragma once
#include <cstddef>
#include <cstdint>

void formatVideoPath(uint32_t n, char* out, size_t outSize);
void formatCounter(uint32_t n, char* out, size_t outSize);
uint32_t parseCounter(const char* text, uint32_t fallback);
