// Visual source of truth for dashboard cards.
// Size, connection chrome, and playback outline live here so every module
// reads the same numbers instead of copying them into each component.

export const MODULE_SIZES = {
  small: { width: 180, height: 180 },
  medium: { width: 240, height: 240 },
  large: { width: 320, height: 320 },
};

export const MODULE_SIZE_OPTIONS = [
  { value: "small", label: "Small" },
  { value: "medium", label: "Medium" },
  { value: "large", label: "Large" },
  { value: "custom", label: "Custom" },
];

// Cards that are not a square preset. Keyed by module.type.
const MODULE_TYPE_FRAMES = {
  camera: {
    minWidth: 400,
    maxWidth: 500,
    minHeight: "auto",
    maxHeight: 500,
  },
  musicPlayer: {
    width: { xs: "100%", sm: "auto" },
    minWidth: { xs: "320px", sm: "520px" },
    maxWidth: { xs: "100%", sm: "820px" },
    minHeight: "auto",
    maxHeight: { xs: "90vh", sm: "720px" },
  },
  systemInfo: {
    minWidth: 320,
    maxWidth: 460,
    minHeight: "auto",
  },
};

export const CONNECTION_CHROME = {
  connected: { label: "Connected" },
  connecting: { label: "Connecting…" },
  reconnecting: { label: "Reconnecting…" },
  offline: { label: "Offline" },
  error: { label: "Connection unavailable" },
  auth: { label: "Sign in required" },
  disconnected: { label: "Disconnected" },
};

export function connectionLabel(status) {
  return (
    CONNECTION_CHROME[status]?.label || CONNECTION_CHROME.disconnected.label
  );
}

function squareFrame(width, height) {
  return {
    minWidth: width,
    maxWidth: width,
    minHeight: height,
    maxHeight: height,
  };
}

/**
 * Frame for a module record from config.modules.
 * Custom width/height win, then a named size, then the type layout, then medium.
 */
export function frameSxForModule(moduleConfig) {
  if (!moduleConfig) return squareFrame(MODULE_SIZES.medium.width, MODULE_SIZES.medium.height);

  if (moduleConfig.size === "custom") {
    const width = Number(moduleConfig.width) || MODULE_SIZES.medium.width;
    const height = Number(moduleConfig.height) || MODULE_SIZES.medium.height;
    return squareFrame(width, height);
  }

  if (moduleConfig.size && MODULE_SIZES[moduleConfig.size]) {
    const preset = MODULE_SIZES[moduleConfig.size];
    return squareFrame(preset.width, preset.height);
  }

  if (moduleConfig.type && MODULE_TYPE_FRAMES[moduleConfig.type]) {
    return { ...MODULE_TYPE_FRAMES[moduleConfig.type] };
  }

  return squareFrame(MODULE_SIZES.medium.width, MODULE_SIZES.medium.height);
}

/**
 * Border and background for a card. Returns null when nothing should override
 * the default: cyan primary outline and secondary.light fill.
 *
 * - playback: music outline only, fill stays the default
 * - connectionStatus: socket cards. Connected is the default look; anything
 *   else is the dim secondary outline and darker fill
 * - active: pin on/off. Passed only when the caller sets it
 * - online === false: error outline, fill stays the default
 */
export function chromeForModule({
  connectionStatus,
  online,
  active,
  playback,
} = {}) {
  if (playback === "paused") {
    return { borderColor: "musicPlayer.paused" };
  }
  if (playback === "playing") {
    return { borderColor: "musicPlayer.playing" };
  }
  if (connectionStatus) {
    const connected = connectionStatus === "connected";
    return {
      borderColor: connected ? "primary.main" : "secondary.main",
      backgroundColor: connected ? "secondary.light" : "secondary.dark",
    };
  }
  if (active === true || active === false) {
    return {
      borderColor:
        online === false
          ? "secondary.main"
          : active
          ? "primary.main"
          : "secondary.main",
      backgroundColor: active ? "secondary.light" : "secondary.dark",
    };
  }
  if (online === false) {
    return { borderColor: "error.main" };
  }
  return null;
}
