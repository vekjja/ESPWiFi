#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "GattServiceDef.h"

// NimBLE bits used by access callbacks
#include "host/ble_att.h"
#include "host/util/util.h" // os_mbuf_append

class ESPWiFi;

/**
 * @brief Encapsulates ESPWiFi's NimBLE GATT service definitions + callbacks.
 *
 * Goals:
 * - Keep all GATT-service-related UUIDs, defaults, and access callbacks out of
 *   `BLE.cpp`.
 * - No heap allocations (safe for embedded).
 * - Stable storage for NimBLE service/characteristic definitions.
 */
class GattServices {
public:
  // ---- Capacity limits (no heap).
  static constexpr size_t maxServices = 6;
  static constexpr size_t maxCharacteristicsPerService = 8;
  static constexpr size_t maxAdvertisedUuids16 =
      3; // ADV payload space is tight

  using ServiceDef = GattServiceDef<maxCharacteristicsPerService>;
  using AccessCb = typename ServiceDef::AccessCb;

  // Use a standard service for Web Bluetooth "auto discovery" (UI requests it).
  // Device Information Service (0x180A).
  inline static const ble_uuid16_t deviceInfoServiceUUID =
      BLE_UUID16_INIT(0x180A);

  // Default "control" characteristic UUID (still custom).
  inline static const ble_uuid16_t controlCharUUID = BLE_UUID16_INIT(0xFFF1);

  // ---- Registry API (like registerRoute()).

  // Register (or replace) a 16-bit UUID service.
  // NOTE: changes require BLE restart to take effect.
  bool registerService16(uint16_t svcUuid16,
                         uint8_t svcType = BLE_GATT_SVC_TYPE_PRIMARY) {
    ServiceEntry *entry = findServiceEntry(svcUuid16);
    if (!entry) {
      entry = allocServiceEntry(svcUuid16);
      if (!entry) {
        return false;
      }
    }

    entry->inUse = true;
    entry->svcType = svcType;
    entry->svcUuid.u.type = BLE_UUID_TYPE_16;
    entry->svcUuid.value = svcUuid16;
    entry->chrCount = 0;

    entry->def.reset();
    entry->def.setServiceType(svcType);
    entry->def.setServiceUuid((const ble_uuid_t *)&entry->svcUuid);

    markDirty();
    return true;
  }

  // Ensure a service exists without resetting its characteristics.
  // - If the service already exists, returns true and leaves it unchanged.
  // - If not, registers it.
  bool ensureService16(uint16_t svcUuid16,
                       uint8_t svcType = BLE_GATT_SVC_TYPE_PRIMARY) {
    ServiceEntry *entry = findServiceEntry(svcUuid16);
    if (entry && entry->inUse) {
      return true;
    }
    return registerService16(svcUuid16, svcType);
  }

  // Remove a service by UUID.
  // NOTE: changes require BLE restart to take effect.
  bool unregisterService16(uint16_t svcUuid16) {
    ServiceEntry *entry = findServiceEntry(svcUuid16);
    if (!entry) {
      return false;
    }
    entry->inUse = false;
    markDirty();
    return true;
  }

  // Add a characteristic to an existing service.
  // NOTE: changes require BLE restart to take effect.
  bool addCharacteristic16(uint16_t svcUuid16, uint16_t chrUuid16,
                           uint16_t flags, AccessCb accessCb,
                           void *arg = nullptr, uint8_t minKeySize = 0) {
    ServiceEntry *entry = findServiceEntry(svcUuid16);
    if (!entry || !entry->inUse) {
      return false;
    }
    if (entry->chrCount >= maxCharacteristicsPerService) {
      return false;
    }

    entry->chrUuids[entry->chrCount].u.type = BLE_UUID_TYPE_16;
    entry->chrUuids[entry->chrCount].value = chrUuid16;

    bool ok = entry->def.addCharacteristic(
        (const ble_uuid_t *)&entry->chrUuids[entry->chrCount], flags, accessCb,
        arg, minKeySize);
    if (!ok) {
      return false;
    }

    entry->chrCount++;
    markDirty();
    return true;
  }

  // Clear all registered services.
  // NOTE: changes require BLE restart to take effect.
  void clear() {
    for (size_t i = 0; i < maxServices; i++) {
      entries_[i] = ServiceEntry{};
    }
    markDirty();
  }

  // ---- Compiled outputs for BLE.cpp

  // Stable, 0-terminated array of service defs.
  // If empty, a default DIS(0x180A)+control characteristic is created.
  const ble_gatt_svc_def *serviceDefs(ESPWiFi *espwifi) {
    ensureDefaultIfEmpty(espwifi);
    rebuildIfDirty(espwifi);
    return compiledSvcs_;
  }

  // Best-effort list of 16-bit service UUIDs to advertise (truncated).
  const ble_uuid16_t *advertisedUuids16(size_t *outCount, ESPWiFi *espwifi) {
    ensureDefaultIfEmpty(espwifi);
    rebuildIfDirty(espwifi);
    if (outCount) {
      *outCount = advertisedUuidCount_;
    }
    return advertisedUuids16_;
  }

private:
  struct ServiceEntry {
    bool inUse = false;
    uint8_t svcType = BLE_GATT_SVC_TYPE_PRIMARY;
    ble_uuid16_t svcUuid{};
    size_t chrCount = 0;
    ble_uuid16_t chrUuids[maxCharacteristicsPerService]{};
    ServiceDef def{};
  };

  // Default characteristic handler: responds "ok" to reads; accepts writes.
  static int defaultControlCharAccessCb(uint16_t conn_handle,
                                        uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt,
                                        void *arg) {
    (void)conn_handle;
    (void)attr_handle;

    // `arg` is the ESPWiFi instance passed in during service construction.
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      static const char kResp[] = "ok";
      return os_mbuf_append(ctxt->om, kResp, sizeof(kResp) - 1) == 0
                 ? 0
                 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      // Accept writes; provisioning payload handling can be added later.
      return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
  }

  ServiceEntry *findServiceEntry(uint16_t svcUuid16) {
    for (size_t i = 0; i < maxServices; i++) {
      if (entries_[i].inUse && entries_[i].svcUuid.value == svcUuid16) {
        return &entries_[i];
      }
    }
    return nullptr;
  }

  ServiceEntry *allocServiceEntry(uint16_t svcUuid16) {
    for (size_t i = 0; i < maxServices; i++) {
      if (!entries_[i].inUse) {
        entries_[i] = ServiceEntry{};
        entries_[i].inUse = true;
        entries_[i].svcUuid.u.type = BLE_UUID_TYPE_16;
        entries_[i].svcUuid.value = svcUuid16;
        return &entries_[i];
      }
    }
    return nullptr;
  }

  bool hasAnyService() const {
    for (size_t i = 0; i < maxServices; i++) {
      if (entries_[i].inUse) {
        return true;
      }
    }
    return false;
  }

  void ensureDefaultIfEmpty(ESPWiFi *espwifi) {
    if (hasAnyService()) {
      return;
    }
    (void)registerService16(deviceInfoServiceUUID.value);
    (void)addCharacteristic16(
        deviceInfoServiceUUID.value, controlCharUUID.value,
        BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE, defaultControlCharAccessCb,
        /*arg=*/espwifi);
  }

  void markDirty() { dirty_ = true; }

  void rebuildIfDirty(ESPWiFi *espwifi) {
    (void)espwifi;
    if (!dirty_) {
      return;
    }

    std::memset(compiledSvcs_, 0, sizeof(compiledSvcs_));
    compiledSvcCount_ = 0;

    std::memset(advertisedUuids16_, 0, sizeof(advertisedUuids16_));
    advertisedUuidCount_ = 0;

    for (size_t i = 0; i < maxServices; i++) {
      if (!entries_[i].inUse) {
        continue;
      }
      if (compiledSvcCount_ >= maxServices) {
        break;
      }

      // Copy the service def by value; it contains pointers into the entry's
      // internal characteristic array (stable).
      compiledSvcs_[compiledSvcCount_] = entries_[i].def.services()[0];
      compiledSvcCount_++;

      if (advertisedUuidCount_ < maxAdvertisedUuids16) {
        advertisedUuids16_[advertisedUuidCount_] = entries_[i].svcUuid;
        advertisedUuidCount_++;
      }
    }

    // Terminator already zeroed by memset.
    dirty_ = false;
  }

  bool dirty_ = true;
  ServiceEntry entries_[maxServices]{};

  ble_gatt_svc_def compiledSvcs_[maxServices + 1]{};
  size_t compiledSvcCount_ = 0;

  ble_uuid16_t advertisedUuids16_[maxAdvertisedUuids16]{};
  size_t advertisedUuidCount_ = 0;
};
