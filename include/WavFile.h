// WavFile.h - minimal PCM16 mono WAV writer
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

inline void wavWriteLe16(FILE* file, uint16_t value) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xff),
                            static_cast<uint8_t>((value >> 8) & 0xff)};
  fwrite(bytes, 1, sizeof(bytes), file);
}

inline void wavWriteLe32(FILE* file, uint32_t value) {
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value & 0xff),
      static_cast<uint8_t>((value >> 8) & 0xff),
      static_cast<uint8_t>((value >> 16) & 0xff),
      static_cast<uint8_t>((value >> 24) & 0xff),
  };
  fwrite(bytes, 1, sizeof(bytes), file);
}

inline bool wavWritePcm16Mono(FILE* file, const uint8_t* pcm, size_t pcmBytes,
                              int sampleRate) {
  if (file == nullptr || pcm == nullptr || pcmBytes == 0 || sampleRate <= 0) {
    return false;
  }

  const uint16_t channels = 1;
  const uint16_t bitsPerSample = 16;
  const uint32_t byteRate =
      static_cast<uint32_t>(sampleRate * channels * bitsPerSample / 8);
  const uint16_t blockAlign = channels * bitsPerSample / 8;
  const uint32_t dataSize = static_cast<uint32_t>(pcmBytes);
  const uint32_t riffSize = 36 + dataSize;

  fwrite("RIFF", 1, 4, file);
  wavWriteLe32(file, riffSize);
  fwrite("WAVE", 1, 4, file);
  fwrite("fmt ", 1, 4, file);
  wavWriteLe32(file, 16);
  wavWriteLe16(file, 1);
  wavWriteLe16(file, channels);
  wavWriteLe32(file, static_cast<uint32_t>(sampleRate));
  wavWriteLe32(file, byteRate);
  wavWriteLe16(file, blockAlign);
  wavWriteLe16(file, bitsPerSample);
  fwrite("data", 1, 4, file);
  wavWriteLe32(file, dataSize);
  return fwrite(pcm, 1, pcmBytes, file) == pcmBytes;
}

inline bool wavHasRiffHeader(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    return false;
  }

  uint8_t riff[12];
  const bool ok = fread(riff, 1, sizeof(riff), file) == sizeof(riff) &&
                  memcmp(riff, "RIFF", 4) == 0 &&
                  memcmp(riff + 8, "WAVE", 4) == 0;
  fclose(file);
  return ok;
}
