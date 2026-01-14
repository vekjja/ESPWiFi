#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// NimBLE GATT types
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"

/**
 * @brief Fixed-size helper for building NimBLE GATT service definitions.
 *
 * Why this exists:
 * - NimBLE expects service/characteristic definition arrays to remain valid for
 *   the lifetime of registration (no stack / no temporary compound literals).
 * - We want safe defaults (zero-init) and a simple override surface.
 *
 * Design:
 * - No heap allocations (embedded-friendly).
 * - Uses fixed-size arrays with a required 0-terminator element at the end.
 */
template <size_t MAX_CHARACTERISTICS> class GattServiceDef {
public:
  using AccessCb = int (*)(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

  GattServiceDef() { reset(); }

  GattServiceDef(const ble_uuid_t *svc_uuid,
                 uint8_t svc_type = BLE_GATT_SVC_TYPE_PRIMARY) {
    reset();
    setServiceUuid(svc_uuid);
    setServiceType(svc_type);
  }

  void reset() {
    std::memset(chrs_, 0, sizeof(chrs_));
    std::memset(svcs_, 0, sizeof(svcs_));
    chr_count_ = 0;

    // svc[0] is our service; svc[1] stays as the required terminator.
    svcs_[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svcs_[0].uuid = nullptr;
    svcs_[0].includes = nullptr;
    svcs_[0].characteristics = chrs_;
  }

  void setServiceType(uint8_t svc_type) { svcs_[0].type = svc_type; }
  void setServiceUuid(const ble_uuid_t *svc_uuid) { svcs_[0].uuid = svc_uuid; }

  /**
   * @brief Add a characteristic definition. Must be called before services() is
   * used.
   *
   * flags: BLE_GATT_CHR_F_READ / WRITE / NOTIFY / etc.
   */
  bool addCharacteristic(const ble_uuid_t *chr_uuid, uint16_t flags,
                         AccessCb access_cb, void *arg = nullptr,
                         uint8_t min_key_size = 0) {
    if (chr_count_ >= MAX_CHARACTERISTICS) {
      return false;
    }

    ble_gatt_chr_def &chr = chrs_[chr_count_++];
    // Ensure a clean starting point for forward compatibility.
    std::memset(&chr, 0, sizeof(chr));

    chr.uuid = chr_uuid;
    chr.access_cb = access_cb;
    chr.arg = arg;
    chr.descriptors = nullptr;
    chr.flags = flags;
    chr.min_key_size = min_key_size;
    chr.val_handle = nullptr;
    chr.cpfd = nullptr;

    // Maintain required terminator element.
    std::memset(&chrs_[chr_count_], 0, sizeof(chrs_[chr_count_]));
    return true;
  }

  /**
   * @brief Returns a pointer to a 0-terminated array of service definitions.
   */
  const ble_gatt_svc_def *services() const { return svcs_; }

private:
  ble_gatt_chr_def chrs_[MAX_CHARACTERISTICS + 1]; // +1 for terminator
  ble_gatt_svc_def svcs_[2];                       // service + terminator
  size_t chr_count_{0};
};
