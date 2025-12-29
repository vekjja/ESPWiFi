import React, { useEffect, useMemo, useState } from "react";
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
import { DeleteOutline as DeleteOutlineIcon } from "@mui/icons-material";
import RSSIButton from "./RSSIButton";
import CameraButton from "./CameraButton";
import DeviceSettingsButton from "./DeviceSettingsButton";
import LogsButton from "./LogsButton";
import FileBrowserButton from "./FileBrowserButton";
import BluetoothButton from "./BluetoothButton";
import AddModuleButton from "./AddModuleButton";

const STORAGE_KEY = "settingsButtonOrder.v1";
const ENABLED_KEY = "settingsButtonEnabled.v1";

const NON_REMOVABLE_IDS = new Set(["deviceSettings", "addModule"]);

const BUTTON_LABELS = {
  deviceSettings: "Device Settings",
  rssi: "RSSI",
  bluetooth: "Bluetooth",
  camera: "Camera",
  logs: "Logs",
  files: "File Browser",
  addModule: "Add Module",
};

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

export default function SettingsButtonBar({
  config,
  deviceOnline,
  onNetworkSettings,
  onCameraSettings,
  onRSSISettings,
  onFileBrowser,
  onAddModule,
  saveConfig,
  saveConfigToDevice,
  onRSSIDataChange,
  getRSSIColor,
  getRSSIIcon,
  // Camera specific props
  cameraEnabled,
  getCameraColor,
}) {
  const availableButtons = useMemo(() => {
    const items = [];

    items.push({
      id: "deviceSettings",
      render: () => (
        <DeviceSettingsButton
          config={config}
          deviceOnline={deviceOnline}
          saveConfigToDevice={saveConfigToDevice}
        />
      ),
    });

    items.push({
      id: "rssi",
      render: () => (
        <RSSIButton
          config={config}
          deviceOnline={deviceOnline}
          onRSSISettings={onRSSISettings}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          onRSSIDataChange={onRSSIDataChange}
          getRSSIColor={getRSSIColor}
          getRSSIIcon={getRSSIIcon}
        />
      ),
    });

    if (config && config?.bluetooth?.installed !== false) {
      items.push({
        id: "bluetooth",
        render: () => (
          <BluetoothButton
            config={config}
            deviceOnline={deviceOnline}
            saveConfig={saveConfig}
            saveConfigToDevice={saveConfigToDevice}
          />
        ),
      });
    }

    if (config && config?.camera?.installed !== false) {
      items.push({
        id: "camera",
        render: () => (
          <CameraButton
            config={config}
            deviceOnline={deviceOnline}
            saveConfigToDevice={saveConfigToDevice}
            cameraEnabled={cameraEnabled}
            getCameraColor={getCameraColor}
          />
        ),
      });
    }

    items.push({
      id: "logs",
      render: () => (
        <LogsButton
          config={config}
          deviceOnline={deviceOnline}
          saveConfigToDevice={saveConfigToDevice}
        />
      ),
    });

    items.push({
      id: "files",
      render: () => (
        <FileBrowserButton
          config={config}
          deviceOnline={deviceOnline}
          onFileBrowser={onFileBrowser}
        />
      ),
    });

    items.push({
      id: "addModule",
      render: () => (
        <AddModuleButton
          config={config}
          deviceOnline={deviceOnline}
          onAddModule={onAddModule}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          missingSettingsButtons={[]} // replaced below after enabledIds exists
          onAddSettingsButton={() => {}}
        />
      ),
    });

    return items;
  }, [
    config,
    deviceOnline,
    onRSSISettings,
    saveConfig,
    saveConfigToDevice,
    onRSSIDataChange,
    getRSSIColor,
    getRSSIIcon,
    cameraEnabled,
    getCameraColor,
    onFileBrowser,
    onAddModule,
  ]);

  const visibleIds = useMemo(
    () => availableButtons.map((b) => b.id),
    [availableButtons]
  );

  const [enabledIds, setEnabledIds] = useState(visibleIds);
  const [order, setOrder] = useState(visibleIds);
  const [activeId, setActiveId] = useState(null);

  // Load saved order once
  useEffect(() => {
    try {
      const raw = window.localStorage.getItem(STORAGE_KEY);
      if (!raw) return;
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) return;
      setOrder(parsed);
    } catch {
      // ignore
    }
  }, []);

  // Load enabled buttons once
  useEffect(() => {
    try {
      const raw = window.localStorage.getItem(ENABLED_KEY);
      if (!raw) return;
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) return;
      setEnabledIds(parsed);
    } catch {
      // ignore
    }
  }, []);

  // Reconcile when visible buttons change (e.g. camera/bluetooth appears/disappears)
  useEffect(() => {
    // Enabled buttons are a subset of what's available; always keep required buttons.
    setEnabledIds((prev) => {
      const prevList = Array.isArray(prev) ? prev : [];
      const next = prevList.filter((id) => visibleIds.includes(id));
      for (const id of NON_REMOVABLE_IDS) {
        if (visibleIds.includes(id) && !next.includes(id)) next.push(id);
      }
      // First run: if nothing loaded, default to all available.
      if (next.length === 0) return visibleIds;
      return next;
    });
  }, [visibleIds]);

  // Keep order in sync with enabled + available.
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
      // Ensure required ids are present if available
      for (const id of NON_REMOVABLE_IDS) {
        if (visibleIds.includes(id) && !next.includes(id)) next.unshift(id);
      }
      return next;
    });
  }, [enabledIds, visibleIds]);

  // Persist order
  useEffect(() => {
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(order));
    } catch {
      // ignore
    }
  }, [order]);

  // Persist enabled
  useEffect(() => {
    try {
      window.localStorage.setItem(ENABLED_KEY, JSON.stringify(enabledIds));
    } catch {
      // ignore
    }
  }, [enabledIds]);

  const sensors = useSensors(
    // Long-press (or click-and-hold) to drag; taps/clicks still activate the buttons
    useSensor(PointerSensor, {
      activationConstraint: { delay: 250, tolerance: 6 },
    })
  );

  const orderedButtons = useMemo(() => {
    const map = new Map(availableButtons.map((b) => [b.id, b]));
    return order.map((id) => map.get(id)).filter(Boolean);
  }, [availableButtons, order]);

  const missingSettingsButtons = useMemo(() => {
    const enabledSet = new Set(enabledIds);
    return visibleIds
      .filter((id) => !enabledSet.has(id) && !NON_REMOVABLE_IDS.has(id))
      .map((id) => ({ id, label: BUTTON_LABELS[id] || id }));
  }, [enabledIds, visibleIds]);

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

  // Both mobile and desktop use the same layout: horizontal row below header
  return (
    <Paper
      elevation={2}
      sx={{
        position: "sticky",
        top: "9vh", // Below the header
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
                        onAddModule={onAddModule}
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
