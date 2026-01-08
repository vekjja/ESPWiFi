/**
 * @file storageUtils.js
 * @brief Safe wrappers around Web Storage for browsers that can throw
 * (notably iOS Safari in some privacy modes).
 */

export function safeGetItem(key) {
  try {
    return window?.localStorage?.getItem(key) ?? null;
  } catch {
    return null;
  }
}

export function safeSetItem(key, value) {
  try {
    window?.localStorage?.setItem(key, value);
    return true;
  } catch {
    return false;
  }
}

export function safeRemoveItem(key) {
  try {
    window?.localStorage?.removeItem(key);
    return true;
  } catch {
    return false;
  }
}

