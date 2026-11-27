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
import { MODULE_GUTTER, MODULE_SIZES, moduleSlotWidth } from "./moduleConfig";

function estimateHeight(module) {
  let height = 280;
  if (module.type === "systemInfo") {
    height = module.info === "storage" ? 760 : 460;
  } else if (module.type === "camera") {
    height = 540;
  } else if (module.type === "musicPlayer") {
    height = 460;
  }
  return height + 20;
}

function raiseSkyline(skyline, x, width, y) {
  const right = x + width;
  const next = [];
  skyline.forEach((seg) => {
    const segRight = seg.x + seg.w;
    if (segRight <= x || seg.x >= right) {
      next.push(seg);
      return;
    }
    if (seg.x < x) next.push({ x: seg.x, y: seg.y, w: x - seg.x });
    if (segRight > right) {
      next.push({ x: right, y: seg.y, w: segRight - right });
    }
  });
  next.push({ x, y, w: width });
  next.sort((a, b) => a.x - b.x);
  const merged = [];
  next.forEach((seg) => {
    const last = merged[merged.length - 1];
    if (last && last.y === seg.y && last.x + last.w === seg.x) {
      last.w += seg.w;
    } else {
      merged.push({ ...seg });
    }
  });
  return merged;
}

function skylineTop(skyline, x, width) {
  const right = x + width;
  let y = 0;
  skyline.forEach((seg) => {
    if (seg.x < right && seg.x + seg.w > x) y = Math.max(y, seg.y);
  });
  return y;
}

// Place each module at its own width. Later modules sit beside earlier ones
// and drop into the gaps under shorter neighbors. Widths are never stretched.
function packModules(items, heights, boardWidth) {
  const board = Math.max(0, Math.round(boardWidth || 0));
  const boxes = new Map();
  if (!board) {
    return { boxes, height: 0 };
  }
  let skyline = [{ x: 0, y: 0, w: board }];
  items.forEach((item) => {
    const slot = Math.max(1, moduleSlotWidth(item, board));
    const measured = heights[String(item.key)];
    const customHeight =
      item.size === "custom" && Number(item.height) > 0
        ? Math.round(Number(item.height) + MODULE_GUTTER * 2)
        : 0;
    const presetHeight =
      !customHeight && item.size && MODULE_SIZES[item.size] && !item.wide
        ? MODULE_SIZES[item.size].height + MODULE_GUTTER * 2
        : 0;
    const h = customHeight || presetHeight || measured || estimateHeight(item);
    if (slot >= board) {
      const y = skyline.reduce((max, seg) => Math.max(max, seg.y), 0);
      boxes.set(item.key, { x: 0, y, w: board, h });
      skyline = [{ x: 0, y: y + h, w: board }];
      return;
    }
    const xs = new Set([0]);
    skyline.forEach((seg) => {
      xs.add(seg.x);
      const alignRight = seg.x + seg.w - slot;
      if (alignRight > 0) xs.add(alignRight);
    });
    let best = null;
    Array.from(xs)
      .filter((candidate) => candidate >= 0 && candidate + slot <= board)
      .sort((a, b) => a - b)
      .forEach((candidate) => {
        const top = skylineTop(skyline, candidate, slot);
        if (!best || top < best.y || (top === best.y && candidate < best.x)) {
          best = { x: candidate, y: top };
        }
      });
    const x = best ? best.x : 0;
    const y = best ? best.y : 0;
    boxes.set(item.key, { x, y, w: slot, h });
    skyline = raiseSkyline(skyline, x, slot, y + h);
  });
  const height = skyline.reduce((max, seg) => Math.max(max, seg.y), 0);
  return { boxes, height };
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

function SortableModule({ module, box, children }) {
  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id: `module-${module.key}` });

  const style = {
    position: "absolute",
    left: box.x,
    top: box.y,
    width: box.w,
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.5 : 1,
    zIndex: isDragging ? 1000 : 1,
    boxSizing: "border-box",
    display: "flow-root",
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
          if (entry.target.hasAttribute("data-resizing")) return;
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

  const layout = useMemo(
    () => packModules(moduleItems, heights, boardWidth),
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
    const box = layout.boxes.get(module.key) || {
      x: 0,
      y: 0,
      w: moduleSlotWidth(module, boardWidth) || boardWidth,
    };
    return (
      <SortableModule key={`${module.type}-${module.key}`} module={module} box={box}>
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
          <div
            ref={boardRef}
            style={{
              position: "relative",
              width: "100%",
              height: layout.height,
              minHeight: "100px",
            }}
          >
            {moduleItems.map((module) => renderModule(module))}
          </div>
        </SortableContext>
      </Container>
    </DndContext>
  );
}
