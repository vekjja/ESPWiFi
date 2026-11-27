const MODULES_KEY = "espwifi.browserModules";
const LAYOUT_KEY = "espwifi.moduleLayout";

function readJson(key) {
  try {
    const parsed = JSON.parse(localStorage.getItem(key) || "null");
    return parsed && typeof parsed === "object" ? parsed : null;
  } catch {
    return null;
  }
}

function writeJson(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // The in-memory copy still applies for this session.
  }
}

function applyLegacyLayout(modules) {
  const store = readJson(LAYOUT_KEY);
  if (!store) return modules;
  return (modules || []).map((module) => {
    const saved = store[String(module?.key)];
    if (!saved) return module;
    return { ...module, ...saved };
  });
}

export function readBrowserModules() {
  const stored = readJson(MODULES_KEY);
  return stored && Array.isArray(stored.modules) ? stored.modules : null;
}

export function rememberBrowserModules(modules) {
  writeJson(MODULES_KEY, { modules: Array.isArray(modules) ? modules : [] });
}

// First visit keeps whatever the device already had, including an older
// layout saved only in this browser. After that, the browser copy wins.
export function adoptBrowserModules(deviceModules) {
  const stored = readBrowserModules();
  if (stored) return stored;
  const seeded = applyLegacyLayout(
    Array.isArray(deviceModules) ? deviceModules : []
  );
  rememberBrowserModules(seeded);
  return seeded;
}

export function withBrowserModules(config) {
  const modules = adoptBrowserModules(config?.modules);
  if (!config || config.modules === modules) return config;
  return { ...config, modules };
}

// Module list, placement, and card settings never go to the device.
export function configForDevice(config) {
  if (!config || typeof config !== "object" || !("modules" in config)) {
    return config;
  }
  const next = { ...config };
  delete next.modules;
  return next;
}
