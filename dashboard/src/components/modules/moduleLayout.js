const STORAGE_KEY = "espwifi.moduleLayout";
const LAYOUT_KEYS = ["x", "y", "width", "height", "size", "wide"];

export function isLayoutPatch(patch) {
  const keys = Object.keys(patch || {});
  return keys.length > 0 && keys.every((key) => LAYOUT_KEYS.includes(key));
}

function readStore() {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}");
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}

function writeStore(store) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(store));
  } catch {
    // The arrangement still applies for this session.
  }
}

export function rememberModuleLayout(modules) {
  const store = {};
  (modules || []).forEach((module) => {
    if (module?.key == null) return;
    const entry = {};
    LAYOUT_KEYS.forEach((key) => {
      if (module[key] != null) entry[key] = module[key];
    });
    store[String(module.key)] = entry;
  });
  writeStore(store);
}

export function applyModuleLayout(modules) {
  const store = readStore();
  return (modules || []).map((module) => {
    const saved = store[String(module?.key)];
    if (!saved) return module;
    return { ...module, ...saved };
  });
}
