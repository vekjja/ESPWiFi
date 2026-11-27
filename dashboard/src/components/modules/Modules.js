import React, { useState, useEffect, useLayoutEffect, useMemo, useRef } from "react";
import { Container } from "@mui/material";
import {
  DndContext,
  closestCenter,
  PointerSensor,
  useSensor,
  useSensors,
} from "@dnd-kit/core";
import {
  arrayMove,
  SortableContext,
} from "@dnd-kit/sortable";
import { useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import PinModule from "./pin/PinModule";
import WebSocketModule from "./websocket/WebSocketModule";
import CameraModule from "./camera/CameraModule";
import MusicPlayerModule from "./music/MusicPlayerModule";
import SystemInfoModule from "./systemInfo/SystemInfoModule";
import { wrapperWidth } from "./moduleConfig";

// One column per this many pixels of board width. Two columns appear once the
// board can hold two cards, which is what lets a short card sit in the gap
// beside a taller one.
const COLUMN_MIN = 380;

function estimateHeight(module) {
  if (module.type === "systemInfo") {
    return module.info === "storage" ? 760 : 460;
  }
  if (module.type === "camera") return 540;
  if (module.type === "musicPlayer") return 460;
  return 280;
}

function isWide(module) {
  return module?.wide === true;
}

function columnBasis(column) {
  const requested = column.map((module) => {
    const width = Number(module.width);
    return module.size === "custom" && width > 0 ? width : COLUMN_MIN;
  });
  return Math.max(COLUMN_MIN, ...requested);
}

function packColumns(items, heights, columnCount) {
  const columns = Array.from({ length: columnCount }, () => []);
  const columnHeights = Array(columnCount).fill(0);
  items.forEach((item) => {
    let index = 0;
    for (let i = 1; i < columnCount; i += 1) {
      if (columnHeights[i] < columnHeights[index]) index = i;
    }
    columns[index].push(item);
    columnHeights[index] += heights[String(item.key)] || estimateHeight(item);
  });
  return columns;
}

function masonrySortingStrategy({ rects, activeIndex, overIndex, index }) {
  const newRects = arrayMove(rects, overIndex, activeIndex);
  const oldRect = rects[index];
  const newRect = newRects[index];
  if (!newRect || !oldRect) return null;
  return {
    x: newRect.left - oldRect.left,
    y: newRect.top - oldRect.top,
    scaleX: 1,
    scaleY: 1,
  };
}

function buildRows(items, heights, boardWidth) {
  const fitted = Math.max(1, Math.floor((boardWidth || COLUMN_MIN) / COLUMN_MIN));
  const rows = [];
  let batch = [];
  const flush = () => {
    if (!batch.length) return;
    const columnCount = Math.max(1, Math.min(fitted, batch.length));
    rows.push({
      kind: "columns",
      columns: packColumns(batch, heights, columnCount),
    });
    batch = [];
  };
  items.forEach((item) => {
    if (isWide(item)) {
      flush();
      rows.push({ kind: "full", item });
      return;
    }
    batch.push(item);
  });
  flush();
  return rows;
}

function SortableModule({ module, children }) {
  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id: `module-${module.key}` });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.5 : 1,
    zIndex: isDragging ? 1000 : 1,
    width: wrapperWidth(module),
    maxWidth: "100%",
    boxSizing: "border-box",
  };

  return (
    <div
      ref={setNodeRef}
      style={style}
      data-module-key={module.key}
      {...attributes}
      {...listeners}
    >
      {children}
    </div>
  );
}

export default function Modules({
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline = true,
  deviceInfo = null,
  controlWs,
  onMusicPlaybackChange,
}) {
  const [modules, setModules] = useState([]);
  const boardRef = useRef(null);
  const [boardWidth, setBoardWidth] = useState(0);
  const [heights, setHeights] = useState({});

  // Configure sensors for drag detection with activation constraints
  const sensors = useSensors(
    useSensor(PointerSensor, {
      activationConstraint: {
        distance: 8, // Require 8px movement before starting drag
      },
      // Exclude input elements from drag activation
      shouldActivateOnEvent: (event) => {
        const target = event.target;

        // More comprehensive check for interactive elements
        const interactiveSelectors = [
          "input",
          "textarea",
          "select",
          "button",
          "label",
          "a",
          '[role="slider"]',
          '[role="button"]',
          '[role="checkbox"]',
          '[role="switch"]',
          ".MuiSlider-root",
          ".MuiSlider-thumb",
          ".MuiSlider-track",
          ".MuiSlider-rail",
          ".MuiSlider-mark",
          ".MuiSlider-markLabel",
          ".MuiSlider-valueLabel",
          ".MuiTextField-root",
          ".MuiSelect-root",
          ".MuiSwitch-root",
          ".MuiCheckbox-root",
          ".MuiButton-root",
          ".MuiIconButton-root",
          ".MuiFab-root",
          ".IButton",
          "[aria-valuenow]",
          "[aria-valuemin]",
          "[aria-valuemax]",
          '[data-testid*="slider"]',
          '[class*="slider"]',
          '[class*="Slider"]',
          "[tabindex]",
          "[onclick]",
          "[onmousedown]",
          "[onmouseup]",
          // Additional Material-UI specific selectors
          ".MuiSlider-marked",
          ".MuiSlider-colorPrimary",
          ".MuiSlider-colorSecondary",
          ".MuiSlider-sizeSmall",
          ".MuiSlider-sizeMedium",
          ".MuiSlider-sizeLarge",
          ".MuiSlider-disabled",
          ".MuiSlider-readOnly",
        ];

        // Check if the target or any of its parents match any interactive selector
        for (const selector of interactiveSelectors) {
          if (target.closest(selector)) {
            return false;
          }
        }

        // Additional check: if the element has any event handlers or is part of a form
        if (
          target.onclick ||
          target.onmousedown ||
          target.onmouseup ||
          target.closest("form") ||
          target.closest("[data-no-dnd]")
        ) {
          return false;
        }

        return true;
      },
    })
  );

  // Update modules from config when it changes
  useEffect(() => {
    if (config) {
      let modulesArray = [];

      // Handle new unified modules format
      if (config.modules && Array.isArray(config.modules)) {
        modulesArray = config.modules.map((mod, idx) => ({
          ...mod,
          key: typeof mod.key === "number" ? mod.key : idx,
        }));
      } else {
        // Convert old separate format to new unified format
        const pins = config.pins || [];
        const webSockets = config.webSockets || [];

        // Convert pins
        if (Array.isArray(pins)) {
          pins.forEach((pin, idx) => {
            modulesArray.push({
              ...pin,
              type: "pin",
              key: pin.key ?? pin.number ?? `pin-${idx}-${Date.now()}`,
            });
          });
        } else if (typeof pins === "object") {
          Object.entries(pins).forEach(([pinNum, pinData], idx) => {
            modulesArray.push({
              ...pinData,
              type: "pin",
              number: parseInt(pinNum, 10),
              key: pinData.key ?? pinNum ?? `pin-${idx}-${Date.now()}`,
            });
          });
        }

        // Convert webSockets
        webSockets.forEach((webSocket, idx) => {
          modulesArray.push({
            ...webSocket,
            type: "webSocket",
            key: webSocket.key ?? webSocket.id ?? `ws-${idx}-${Date.now()}`,
          });
        });
      }

      setModules(modulesArray);
    }
  }, [config]);

  // Use useMemo to ensure stable references
  const moduleItems = useMemo(() => modules, [modules]);

  const handleDragEnd = (event) => {
    const { active, over } = event;

    if (!over || active.id === over.id) return;

    {
      const oldIndex = moduleItems.findIndex(
        (module) => `module-${module.key}` === active.id.toString()
      );
      const newIndex = moduleItems.findIndex(
        (module) => `module-${module.key}` === over.id.toString()
      );

      if (oldIndex !== -1 && newIndex !== -1) {
        const reorderedModules = arrayMove(moduleItems, oldIndex, newIndex);

        // Update local state first
        setModules(reorderedModules);

        // Update the global config with the reordered modules
        const updatedConfig = { ...config, modules: reorderedModules };
        saveConfig(updatedConfig); // Update local config
        saveConfigToDevice(updatedConfig); // Save to device
      }
    }
  };

  const updateModule = (moduleKey, moduleState) => {
    // Update local state immediately
    const updatedModules = modules.map((module) => {
      if (module.key === moduleKey) {
        return { ...module, ...moduleState };
      }
      return module;
    });

    setModules(updatedModules);

    // Update the global config with the updated modules
    const updatedConfig = { ...config, modules: updatedModules };
    saveConfig(updatedConfig); // Update local config
    saveConfigToDevice(updatedConfig); // Save to device
  };

  const deleteModule = (moduleKey) => {
    console.log("🗑️ Modules.deleteModule called with moduleKey:", moduleKey);
    console.log("🗑️ Current modules:", modules);
    const updatedModules = modules.filter((module) => module.key !== moduleKey);
    console.log("🗑️ Updated modules after filter:", updatedModules);

    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, modules: updatedModules };
    console.log("🗑️ Saving updated config:", updatedConfig);
    saveConfig(updatedConfig); // Update local config
    saveConfigToDevice(updatedConfig); // Save to device
  };

  useLayoutEffect(() => {
    const board = boardRef.current;
    if (!board) return undefined;
    const updateWidth = () => setBoardWidth(board.clientWidth);
    updateWidth();
    const ro = new ResizeObserver(updateWidth);
    ro.observe(board);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    const board = boardRef.current;
    if (!board) return undefined;
    const ro = new ResizeObserver((entries) => {
      setHeights((prev) => {
        let changed = false;
        const next = { ...prev };
        entries.forEach((entry) => {
          const key = entry.target.getAttribute("data-module-key");
          if (key == null) return;
          const height = Math.round(entry.target.offsetHeight);
          if (Math.abs((next[key] || 0) - height) > 4) {
            next[key] = height;
            changed = true;
          }
        });
        return changed ? next : prev;
      });
    });
    const watch = () => {
      board.querySelectorAll("[data-module-key]").forEach((node) => ro.observe(node));
    };
    watch();
    const mo = new MutationObserver(watch);
    mo.observe(board, { childList: true, subtree: true });
    return () => {
      ro.disconnect();
      mo.disconnect();
    };
  }, [moduleItems]);

  const rows = useMemo(
    () => buildRows(moduleItems, heights, boardWidth),
    [moduleItems, heights, boardWidth]
  );

  const renderModule = (module) => {
    let child = null;
    if (module.type === "pin") {
      child = (
        <PinModule
          pinNum={module.number}
          initialProps={module}
          config={config}
          onUpdate={updateModule}
          onDelete={deleteModule}
          controlWs={controlWs}
          deviceOnline={deviceOnline}
        />
      );
    } else if (module.type === "webSocket") {
      child = (
        <WebSocketModule
          index={module.key}
          initialProps={module}
          onUpdate={updateModule}
          onDelete={deleteModule}
        />
      );
    } else if (module.type === "camera") {
      child = (
        <CameraModule
          config={module}
          globalConfig={config}
          onUpdate={updateModule}
          onDelete={deleteModule}
          deviceOnline={deviceOnline}
          saveConfigToDevice={saveConfigToDevice}
          controlWs={controlWs}
        />
      );
    } else if (module.type === "musicPlayer") {
      child = (
        <MusicPlayerModule
          config={module}
          globalConfig={config}
          onUpdate={updateModule}
          onDelete={deleteModule}
          deviceOnline={deviceOnline}
          saveConfigToDevice={saveConfigToDevice}
          controlWs={controlWs}
          onMusicPlaybackChange={onMusicPlaybackChange}
        />
      );
    } else if (module.type === "systemInfo") {
      child = (
        <SystemInfoModule
          module={module}
          config={config}
          deviceInfo={deviceInfo}
          deviceOnline={deviceOnline}
          onUpdate={updateModule}
          onDelete={deleteModule}
        />
      );
    }
    if (!child) return null;
    return (
      <SortableModule key={`${module.type}-${module.key}`} module={module}>
        {child}
      </SortableModule>
    );
  };

  // Create IDs for module items
  const moduleIds = moduleItems.map((module) => `module-${module.key}`);

  return (
    <DndContext
      sensors={sensors}
      collisionDetection={closestCenter}
      onDragEnd={handleDragEnd}
    >
      <Container
        sx={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          marginTop: 2,
        }}
      >
        {/* Render all modules together */}
        <SortableContext items={moduleIds} strategy={masonrySortingStrategy}>
          <div ref={boardRef} style={{ width: "100%", minHeight: "100px" }}>
            {rows.map((row, rowIndex) =>
              row.kind === "full" ? (
                <div key={`full-${row.item.key}`} style={{ width: "100%" }}>
                  {renderModule(row.item)}
                </div>
              ) : (
                <div
                  key={`cols-${rowIndex}`}
                  style={{
                    display: "flex",
                    alignItems: "flex-start",
                    width: "100%",
                  }}
                >
                  {row.columns.map((column, columnIndex) => (
                    <div
                      key={columnIndex}
                      style={{
                        flex: `${columnBasis(column)} 1 0`,
                        minWidth: 0,
                        maxWidth: "100%",
                        display: "flex",
                        flexDirection: "column",
                        alignItems: "stretch",
                      }}
                    >
                      {column.map((module) => renderModule(module))}
                    </div>
                  ))}
                </div>
              )
            )}
          </div>
        </SortableContext>
      </Container>
    </DndContext>
  );
}
