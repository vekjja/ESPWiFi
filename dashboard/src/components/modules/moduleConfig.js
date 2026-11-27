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

export const CONNECTION_CHROME = {
  connected: { label: "Connected" },
  connecting: { label: "Connecting…" },
  reconnecting: { label: "Reconnecting…" },
  offline: { label: "Offline" },
  error: { label: "Connection unavailable" },
  auth: { label: "Sign in required" },
  disconnected: { label: "Disconnected" },
};

export const MODULE_GUTTER = 10;

// Card border-box width. Wide cards fill the board. A custom size is the
// pixels from the resize handle and is not stretched to a column.
export function moduleCardWidth(module) {
  if (!module || module.wide) return null;
  const width = Number(module.width);
  if (module.size === "custom" && width > 0) return Math.round(width);
  if (module.size && MODULE_SIZES[module.size]) return MODULE_SIZES[module.size].width;
  if (module.type === "pin") return 260;
  if (module.type === "webSocket") return 300;
  if (module.type === "camera" || module.type === "musicPlayer") return 440;
  if (module.type === "systemInfo") return 360;
  return 320;
}

// Outer slot, including the card's horizontal margin, capped to the board.
export function moduleSlotWidth(module, boardWidth) {
  const board = Math.max(0, Math.round(boardWidth || 0));
  if (!board) return 0;
  if (module?.wide) return board;
  const card = moduleCardWidth(module);
  return Math.min(board, card + MODULE_GUTTER * 2);
}

export function connectionLabel(status) {
  return (
    CONNECTION_CHROME[status]?.label || CONNECTION_CHROME.disconnected.label
  );
}

function squareFrame(width, height) {
  return {
    width,
    minWidth: width,
    maxWidth: width,
    height,
    minHeight: height,
    maxHeight: height,
  };
}

function fluidFrame() {
  return {
    width: "calc(100% - 20px)",
    maxWidth: "calc(100% - 20px)",
    minWidth: 0,
    minHeight: "auto",
  };
}

/**
 * Frame for a module record from config.modules.
 * Custom width/height win, then a named size, then the type layout, then medium.
 */
export function frameSxForModule(moduleConfig) {
  if (!moduleConfig) return fluidFrame();

  const customHeight =
    moduleConfig.size === "custom" && Number(moduleConfig.height) > 0
      ? Number(moduleConfig.height)
      : null;

  if (moduleConfig.wide) {
    return {
      ...fluidFrame(),
      ...(customHeight
        ? {
            height: customHeight,
            minHeight: customHeight,
            maxHeight: customHeight,
          }
        : null),
    };
  }

  if (moduleConfig.size === "custom") {
    const width = moduleCardWidth(moduleConfig) || MODULE_SIZES.medium.width;
    const height = customHeight || MODULE_SIZES.medium.height;
    return {
      width,
      maxWidth: `calc(100% - ${MODULE_GUTTER * 2}px)`,
      minWidth: 0,
      height,
      minHeight: height,
      maxHeight: height,
    };
  }

  if (moduleConfig.size && MODULE_SIZES[moduleConfig.size]) {
    const preset = MODULE_SIZES[moduleConfig.size];
    return squareFrame(preset.width, preset.height);
  }

  return fluidFrame();
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
