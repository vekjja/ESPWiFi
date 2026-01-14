import React, { useEffect, useMemo, useState, useRef } from "react";
import { Stack, Paper, Box, Typography } from "@mui/material";
import {
  DndContext,
  DragOverlay,
  PointerSensor,
  useDroppable,
  useSensor,
  useSensors,
} from "@dnd-kit/core";
import {
  SortableContext,
  rectSortingStrategy,
  useSortable,
  arrayMove,
} from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import DeleteOutlineIcon from "@mui/icons-material/DeleteOutline";
import RSSIButton from "./RSSIButton";
import CameraButton from "./CameraButton";
import DeviceSettingsButton from "./DeviceSettingsButton";
import LogsButton from "./LogsButton";
import FileBrowserButton from "./FileBrowserButton";
import BluetoothButton from "./BluetoothButton";
import AddModuleButton from "./AddModuleButton";

// Storage keys for persisting button configuration
const STORAGE_KEY = "settingsButtonOrder.v1";
const ENABLED_KEY = "settingsButtonEnabled.v1";

// Button IDs that cannot be removed from the bar
const NON_REMOVABLE_IDS = new Set([
  // RSSI is a core status signal; keep it visible by default.
  "rssi",
  "deviceSettings",
  "addModule",
]);

// Default button order priority for initial setup
const DEFAULT_ORDER_PRIORITY = [
  "rssi",
  "deviceSettings",
  "bluetooth",
  "addModule",
  "files",
  "logs",
];

// Button label mapping for display
const BUTTON_LABELS = {
  deviceSettings: "Device Settings",
  rssi: "RSSI",
  bluetooth: "Bluetooth",
  camera: "Camera",
  logs: "Logs",
  files: "File Browser",
  addModule: "Add Module",
};

/**
 * Compute default button order based on priority list
 * @param {string[]} ids - Available button IDs
 * @returns {string[]} Ordered button IDs
 */
function computeDefaultOrder(ids) {
  const remaining = new Set(ids);
  const out = [];
  for (const id of DEFAULT_ORDER_PRIORITY) {
    if (remaining.has(id)) {
      out.push(id);
      remaining.delete(id);
    }
  }
  // Append any other buttons in their existing/available order
  for (const id of ids) {
    if (remaining.has(id)) {
      out.push(id);
      remaining.delete(id);
    }
  }
  return out;
}

/**
 * Insert button ID after another ID in the list
 * @param {string[]} list - Current button list
 * @param {string} id - ID to insert
 * @param {string} afterId - ID to insert after
 * @returns {string[]} Updated list
 */
function insertAfter(list, id, afterId) {
  if (list.includes(id)) return list;
  const idx = list.indexOf(afterId);
  if (idx === -1) return [...list, id];
  return [...list.slice(0, idx + 1), id, ...list.slice(idx + 1)];
}

/**
 * Ensure non-removable buttons are in preferred positions
 * @param {string[]} list - Current button list
 * @returns {string[]} List with non-removables positioned correctly
 */
function ensureNonRemovablesInPreferredPositions(list) {
  let next = Array.isArray(list) ? [...list] : [];

  // RSSI at the front when available
  if (!next.includes("rssi")) {
    next = ["rssi", ...next];
  }

  // Settings after RSSI when possible
  if (!next.includes("deviceSettings")) {
    next = next.includes("rssi")
      ? insertAfter(next, "deviceSettings", "rssi")
      : ["deviceSettings", ...next];
  }

  // Add after Settings when possible
  if (!next.includes("addModule")) {
    next = next.includes("deviceSettings")
      ? insertAfter(next, "addModule", "deviceSettings")
      : [...next, "addModule"];
  }

  return next;
}

/**
 * Drop zone component for removing buttons
 * Appears when dragging a removable button
 * @param {Object} props - Component props
 * @param {string|null} props.activeId - Currently dragged button ID
 */
function RemoveDropZone({ activeId }) {
  const { isOver, setNodeRef } = useDroppable({ id: "trash" });
  const canRemove = Boolean(activeId) && !NON_REMOVABLE_IDS.has(activeId);
  if (!canRemove) return null;

  return (
    <Box
      ref={setNodeRef}
      sx={{
        width: "100%",
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        gap: 1,
        mt: 1,
        py: 1,
        borderRadius: 1.5,
        border: "1px dashed",
        borderColor: isOver ? "error.main" : "divider",
        bgcolor: isOver ? "rgba(244, 67, 54, 0.12)" : "transparent",
        color: isOver ? "error.main" : "text.secondary",
        userSelect: "none",
      }}
    >
      <DeleteOutlineIcon fontSize="small" />
      <Typography variant="body2" sx={{ fontWeight: 600 }}>
        Drop here to remove
      </Typography>
    </Box>
  );
}

/**
 * Sortable wrapper for button bar items
 * Provides drag-and-drop functionality
 * @param {Object} props - Component props
 * @param {string} props.id - Button ID
 * @param {React.ReactNode} props.children - Button component
 */
function SortableBarItem({ id, children }) {
  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.6 : 1,
  };

  return (
    <Box
      ref={setNodeRef}
      style={style}
      {...attributes}
      {...listeners}
      sx={{
        // Allow vertical scrolling while still supporting long-press drag
        touchAction: "pan-y",
        cursor: "grab",
      }}
    >
      {children}
    </Box>
  );
}

/**
 * Settings button bar component with drag-and-drop reordering
 * Features:
 * - Drag-and-drop button reordering
 * - Show/hide buttons (except non-removable ones)
 * - Persistent storage of button order and visibility
 * - Conditional button rendering based on device capabilities
 *
 * @param {Object} props - Component props
 * @param {Object} props.config - Global configuration object
 * @param {boolean} props.deviceOnline - Device connectivity status
 * @param {Function} props.saveConfig - Function to update local config
 * @param {Function} props.saveConfigToDevice - Function to save config to device
 * @param {Function} props.getRSSIColor - Function to get RSSI color
 * @param {Function} props.getRSSIIcon - Function to get RSSI icon name
 * @param {boolean} props.cameraEnabled - Camera enabled status
 * @param {Function} props.getCameraColor - Function to get camera button color
 */
export default function SettingsButtonBar({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
  onRefreshConfig,
  getRSSIColor,
  getRSSIIcon,
  controlRssi = null,
  logsText = "",
  logsError = "",
  onRequestLogs = null,
  cloudMode = false,
  controlConnected = false,
  deviceInfoOverride = null,
  controlWs = null,
  musicPlaybackState = { isPlaying: false, isPaused: false },
}) {
  // Track previous visible IDs to detect newly available buttons
  const prevVisibleIdsRef = useRef(new Set());
  /**
   * Build list of available buttons based on device capabilities
   * Some buttons (camera, bluetooth) only appear if device supports them
   *
   * When control socket is disconnected (deviceOnline=false), all buttons
   * except devicePicker should be disabled to prevent operations that require
   * the WebSocket connection (e.g., logs, settings changes).
   */
  const availableButtons = useMemo(() => {
    const items = [];
    const wifiMode = config?.wifi?.mode || "client";
    // RSSI button is available when config exists and not in access point mode
    const rssiAvailable = Boolean(config) && wifiMode !== "accessPoint";
    // In paired/tunnel mode, use the control socket to support settings/logs.
    // Keep other HTTP-backed features (files/modules) disabled for now.
    const canCloudConfigure = cloudMode && controlConnected;
    const httpCapable = deviceOnline && !cloudMode;
    const logsCapable = (cloudMode && controlConnected) || deviceOnline;

    items.push({
      id: "deviceSettings",
      render: () => (
        <DeviceSettingsButton
          config={config}
          deviceOnline={
            (httpCapable || canCloudConfigure) && !musicPlaybackState.isPlaying
          }
          saveConfigToDevice={saveConfigToDevice}
          onRefreshConfig={onRefreshConfig}
          cloudMode={cloudMode}
          controlConnected={controlConnected}
          deviceInfoOverride={deviceInfoOverride}
        />
      ),
    });

    if (rssiAvailable) {
      items.push({
        id: "rssi",
        render: () => (
          <RSSIButton
            config={config}
            deviceOnline={deviceOnline}
            saveConfig={saveConfig}
            saveConfigToDevice={saveConfigToDevice}
            getRSSIColor={getRSSIColor}
            getRSSIIcon={getRSSIIcon}
            controlRssi={controlRssi}
            musicPlaybackState={musicPlaybackState}
          />
        ),
      });
    }

    // Always show Bluetooth button - never disabled (keep enabled even during music playback)
    items.push({
      id: "bluetooth",
      render: () => <BluetoothButton config={config} />,
    });

    // Show camera button if camera is installed and there's at least one camera module
    const hasCameraModule = config?.modules?.some((m) => m.type === "camera");
    if (config && config?.camera?.installed !== false && hasCameraModule) {
      items.push({
        id: "camera",
        render: () => (
          <CameraButton
            config={config}
            deviceOnline={deviceOnline && !musicPlaybackState.isPlaying}
            saveConfigToDevice={saveConfigToDevice}
          />
        ),
      });
    }

    items.push({
      id: "logs",
      render: () => (
        <LogsButton
          config={config}
          deviceOnline={logsCapable && !musicPlaybackState.isPlaying}
          saveConfigToDevice={saveConfigToDevice}
          controlConnected={controlConnected}
          logsText={logsText}
          logsError={logsError}
          onRequestLogs={onRequestLogs}
        />
      ),
    });

    items.push({
      id: "files",
      render: () => (
        <FileBrowserButton
          config={config}
          deviceOnline={deviceOnline && !musicPlaybackState.isPlaying}
          controlWs={controlWs}
        />
      ),
    });

    items.push({
      id: "addModule",
      render: () => (
        <AddModuleButton
          config={config}
          deviceOnline={deviceOnline && !musicPlaybackState.isPlaying}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          missingSettingsButtons={[]}
          onAddSettingsButton={() => {}}
        />
      ),
    });

    return items;
  }, [
    config,
    deviceOnline,
    saveConfig,
    saveConfigToDevice,
    onRefreshConfig,
    getRSSIColor,
    getRSSIIcon,
    controlRssi,
    logsText,
    logsError,
    onRequestLogs,
    cloudMode,
    controlConnected,
    deviceInfoOverride,
    controlWs,
    musicPlaybackState.isPlaying,
  ]);

  const visibleIds = useMemo(
    () => availableButtons.map((b) => b.id),
    [availableButtons]
  );

  /**
   * Load enabled button IDs from localStorage
   * Defaults to all visible buttons
   */
  const [enabledIds, setEnabledIds] = useState(() => {
    try {
      const raw = window.localStorage.getItem(ENABLED_KEY);
      const parsed = raw ? JSON.parse(raw) : null;
      return Array.isArray(parsed) ? parsed : visibleIds;
    } catch {
      return visibleIds;
    }
  });

  /**
   * Load button order from localStorage
   * Defaults to computed default order
   */
  const [order, setOrder] = useState(() => {
    try {
      const raw = window.localStorage.getItem(STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : null;
      return Array.isArray(parsed) ? parsed : computeDefaultOrder(visibleIds);
    } catch {
      return computeDefaultOrder(visibleIds);
    }
  });

  // Currently dragged button ID
  const [activeId, setActiveId] = useState(null);

  /**
   * Reconcile enabled buttons when available buttons change
   * (e.g., camera/bluetooth appears/disappears based on device capabilities)
   */
  useEffect(() => {
    setEnabledIds((prev) => {
      const prevList = Array.isArray(prev) ? prev : [];
      const prevVisibleIds = prevVisibleIdsRef.current;
      const currentVisibleIds = new Set(visibleIds);

      // Filter out buttons that are no longer available
      const next = prevList.filter((id) => currentVisibleIds.has(id));

      // Auto-enable newly available buttons (like camera when module is added)
      for (const id of visibleIds) {
        if (!prevVisibleIds.has(id) && !next.includes(id)) {
          // This is a newly available button, auto-enable it
          next.push(id);
        }
      }

      // Always ensure non-removable buttons are enabled
      for (const id of NON_REMOVABLE_IDS) {
        if (currentVisibleIds.has(id) && !next.includes(id)) {
          next.push(id);
        }
      }

      // Update the ref for next comparison
      prevVisibleIdsRef.current = currentVisibleIds;

      // First run: if nothing loaded, default to all available.
      if (next.length === 0) return visibleIds;
      return next;
    });
  }, [visibleIds]);

  /**
   * Keep button order in sync with enabled and available buttons
   */
  useEffect(() => {
    setOrder((prev) => {
      const prevList = Array.isArray(prev) ? prev : [];
      const enabledSet = new Set(
        enabledIds.filter((id) => visibleIds.includes(id))
      );
      const next = prevList.filter((id) => enabledSet.has(id));
      for (const id of enabledIds) {
        if (enabledSet.has(id) && !next.includes(id)) next.push(id);
      }
      // Ensure required ids are present without forcing them to the front.
      return ensureNonRemovablesInPreferredPositions(next);
    });
  }, [enabledIds, visibleIds]);

  /**
   * Persist button order to localStorage
   */
  useEffect(() => {
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(order));
    } catch {
      // ignore
    }
  }, [order]);

  /**
   * Persist enabled button list to localStorage
   */
  useEffect(() => {
    try {
      window.localStorage.setItem(ENABLED_KEY, JSON.stringify(enabledIds));
    } catch {
      // ignore
    }
  }, [enabledIds]);

  /**
   * Configure drag sensors
   * Long-press prevents accidental drags while allowing button clicks
   */
  const sensors = useSensors(
    useSensor(PointerSensor, {
      // 250ms delay prevents accidental drags, 6px tolerance allows small movements
      activationConstraint: { delay: 250, tolerance: 6 },
    })
  );

  /**
   * Get buttons in current order
   */
  const orderedButtons = useMemo(() => {
    const map = new Map(availableButtons.map((b) => [b.id, b]));
    return order.map((id) => map.get(id)).filter(Boolean);
  }, [availableButtons, order]);

  /**
   * Get list of buttons that can be added back
   */
  const missingSettingsButtons = useMemo(() => {
    const enabledSet = new Set(enabledIds);
    return visibleIds
      .filter((id) => !enabledSet.has(id) && !NON_REMOVABLE_IDS.has(id))
      .map((id) => ({ id, label: BUTTON_LABELS[id] || id }));
  }, [enabledIds, visibleIds]);

  /**
   * Add a button back to the bar
   * @param {string} id - Button ID to add
   */
  const handleAddSettingsButton = (id) => {
    if (!id) return;
    if (NON_REMOVABLE_IDS.has(id)) return;
    if (!visibleIds.includes(id)) return;
    setEnabledIds((prev) => {
      const next = Array.isArray(prev) ? [...prev] : [];
      if (!next.includes(id)) next.push(id);
      return next;
    });
    setOrder((prev) => {
      const next = Array.isArray(prev) ? [...prev] : [];
      if (!next.includes(id)) next.push(id);
      return next;
    });
  };

  /**
   * Render the button bar
   * Same layout for mobile and desktop - horizontal row below header
   */
  return (
    <Paper
      elevation={2}
      sx={{
        position: "sticky",
        top: "var(--app-header-height, 9vh)", // Below the header (mobile-safe)
        zIndex: 1000,
        backgroundColor: "background.paper",
        borderRadius: 0,
        p: 1,
        mx: -1, // Extend to edges
      }}
    >
      <DndContext
        sensors={sensors}
        onDragStart={({ active }) => {
          setActiveId(active?.id ?? null);
        }}
        onDragCancel={() => setActiveId(null)}
        onDragEnd={({ active, over }) => {
          setActiveId(null);
          if (!over || active.id === over.id) return;

          // Drop-to-remove
          if (over.id === "trash") {
            const id = String(active.id);
            if (NON_REMOVABLE_IDS.has(id)) return;
            setEnabledIds((prev) =>
              Array.isArray(prev) ? prev.filter((x) => x !== id) : []
            );
            setOrder((prev) =>
              Array.isArray(prev) ? prev.filter((x) => x !== id) : []
            );
            return;
          }

          setOrder((items) => {
            const oldIndex = items.indexOf(active.id);
            const newIndex = items.indexOf(over.id);
            if (oldIndex === -1 || newIndex === -1) return items;
            return arrayMove(items, oldIndex, newIndex);
          });
        }}
      >
        <Stack
          direction="row"
          spacing={0.5}
          justifyContent="center"
          alignItems="center"
          sx={{
            flexWrap: "wrap",
            gap: 0.5,
            minHeight: "48px", // Ensure consistent height
          }}
        >
          <SortableContext items={order} strategy={rectSortingStrategy}>
            {orderedButtons.map((btn) => (
              <SortableBarItem key={btn.id} id={btn.id}>
                {btn.id === "addModule"
                  ? (() => (
                      <AddModuleButton
                        config={config}
                        deviceOnline={deviceOnline}
                        saveConfig={saveConfig}
                        saveConfigToDevice={saveConfigToDevice}
                        missingSettingsButtons={missingSettingsButtons}
                        onAddSettingsButton={handleAddSettingsButton}
                      />
                    ))()
                  : btn.render()}
              </SortableBarItem>
            ))}
          </SortableContext>

          <DragOverlay />
        </Stack>

        <RemoveDropZone activeId={activeId} />
      </DndContext>
    </Paper>
  );
}
