#include "ESPWiFi.h"

#include "esp_random.h"

namespace {

// Crockford-ish Base32 without ambiguous chars: no I, L, O, 0, 1
static const char *kClaimAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

static void makeClaimCode(char *out, size_t outLen) {
  if (!out || outLen == 0) {
    return;
  }
  // 8 chars + NUL
  if (outLen < 9) {
    out[0] = '\0';
    return;
  }
  const size_t alphaLen = 32;
  uint32_t r = esp_random();
  for (int i = 0; i < 8; i++) {
    // Mix in fresh entropy occasionally
    if ((i & 3) == 0) {
      r ^= esp_random();
    }
    const uint8_t idx = (uint8_t)(r & 31U);
    out[i] = kClaimAlphabet[idx % alphaLen];
    // xorshift
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
  }
  out[8] = '\0';
}

} // namespace

// NOTE: For now this is process-lifetime state (per boot). If you want the
// claim code to survive reboots, persist it to config or a small file.
static char s_claimCode[9] = {0};
static uint32_t s_claimIssuedAtMs = 0;
static constexpr uint32_t kClaimTtlMs = 10U * 60U * 1000U; // 10 minutes

std::string ESPWiFi::getClaimCode(bool rotate) {
  const uint32_t now = millis();
  const bool expired = (s_claimIssuedAtMs == 0) ||
                       ((uint32_t)(now - s_claimIssuedAtMs) > kClaimTtlMs);
  if (rotate || s_claimCode[0] == '\0' || expired) {
    makeClaimCode(s_claimCode, sizeof(s_claimCode));
    s_claimIssuedAtMs = now;
  }
  return std::string(s_claimCode);
}

uint32_t ESPWiFi::claimExpiresInMs() {
  if (s_claimIssuedAtMs == 0) {
    return kClaimTtlMs;
  }
  const uint32_t now = millis();
  const uint32_t age = (uint32_t)(now - s_claimIssuedAtMs);
  if (age >= kClaimTtlMs) {
    return 0;
  }
  return kClaimTtlMs - age;
}
