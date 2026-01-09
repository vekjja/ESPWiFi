import { safeGetItem, safeRemoveItem, safeSetItem } from "./storageUtils";

const DEVICES_KEY = "espwifi.devices.v1";
const SELECTED_KEY = "espwifi.devices.selectedId.v1";

/**
 * @typedef {Object} EspwifiDevice
 * @property {string} id - Stable identifier (prefer hostname/deviceId)
 * @property {string} name - Friendly name
 * @property {string=} hostname - Optional LAN hostname (espwifi.local, etc)
 * @property {string=} deviceId - Optional deviceId used by cloud tunnel
 * @property {string=} authToken - Device auth token (used for tunnel/UI auth)
 * @property {string=} cloudBaseUrl - e.g. "https://tnl.espwifi.io"
 * @property {number=} lastSeenAtMs
 * @property {number=} lastSelectedAtMs
 */

function nowMs() {
  return Date.now();
}

function safeJsonParse(raw, fallback) {
  try {
    return JSON.parse(raw);
  } catch {
    return fallback;
  }
}

export function loadDevices() {
  const raw = safeGetItem(DEVICES_KEY);
  if (!raw) return [];
  const parsed = safeJsonParse(raw, []);
  return Array.isArray(parsed) ? parsed : [];
}

export function saveDevices(devices) {
  return safeSetItem(
    DEVICES_KEY,
    JSON.stringify(Array.isArray(devices) ? devices : [])
  );
}

export function loadSelectedDeviceId() {
  return safeGetItem(SELECTED_KEY);
}

export function saveSelectedDeviceId(id) {
  if (!id) return safeRemoveItem(SELECTED_KEY);
  return safeSetItem(SELECTED_KEY, String(id));
}

export function upsertDevice(devices, devicePatch) {
  if (!devicePatch || !devicePatch.id) return devices;
  const id = String(devicePatch.id);
  const next = Array.isArray(devices) ? [...devices] : [];
  const idx = next.findIndex((d) => String(d?.id) === id);
  const merged = {
    ...(idx >= 0 ? next[idx] : {}),
    ...devicePatch,
    id,
    lastSeenAtMs:
      devicePatch.lastSeenAtMs ??
      (idx >= 0 ? next[idx]?.lastSeenAtMs : undefined),
  };
  if (idx >= 0) {
    next[idx] = merged;
  } else {
    next.unshift(merged);
  }
  return next;
}

export function removeDevice(devices, id) {
  const needle = String(id);
  return (Array.isArray(devices) ? devices : []).filter(
    (d) => String(d?.id) !== needle
  );
}

export function touchSelected(devices, id) {
  const needle = String(id);
  const next = (Array.isArray(devices) ? devices : []).map((d) => {
    if (String(d?.id) !== needle) return d;
    return { ...d, lastSelectedAtMs: nowMs() };
  });
  return next;
}
