import React, { useState, useEffect, useLayoutEffect, useMemo, useRef } from "react";
import {
  DndContext,
  DragOverlay,
  closestCenter,
  PointerSensor,
  useSensor,
  useSensors,
} from "@dnd-kit/core";
import {
  SortableContext,
} from "@dnd-kit/sortable";
import { useSortable } from "@dnd-kit/sortable";
import PinModule from "./pin/PinModule";
import WebSocketModule from "./websocket/WebSocketModule";
import CameraModule from "./camera/CameraModule";
import MusicPlayerModule from "./music/MusicPlayerModule";
import SystemInfoModule from "./systemInfo/SystemInfoModule";
import LogsModule from "./logs/LogsModule";
import { ModuleLayoutContext } from "./Module";
import { MODULE_GUTTER, MODULE_SIZES, moduleSlotWidth } from "./moduleConfig";

function estimateHeight(module) {
  let height = 280;
  if (module.type === "systemInfo") {
    height = module.info === "storage" ? 760 : 460;
  } else if (module.type === "camera") {
    height = 540;
  } else if (module.type === "musicPlayer") {
    height = 460;
  } else if (module.type === "logs") {
    height = 520;
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

function moduleHeight(item, heights) {
  const measured = heights[String(item.key)];
  const customHeight =
    item.size === "custom" && Number(item.height) > 0
      ? Math.round(Number(item.height) + MODULE_GUTTER * 2)
      : 0;
  const presetHeight =
    !customHeight && item.size && MODULE_SIZES[item.size] && !item.wide
      ? MODULE_SIZES[item.size].height + MODULE_GUTTER * 2
      : 0;
  return customHeight || presetHeight || measured || estimateHeight(item);
}

function placeItem(skyline, boxes, item, heights, board) {
  const slot = Math.max(1, moduleSlotWidth(item, board));
  const h = moduleHeight(item, heights);
  if (slot >= board) {
    const y = skyline.reduce((max, seg) => Math.max(max, seg.y), 0);
    boxes.set(item.key, { x: 0, y, w: board, h });
    return [{ x: 0, y: y + h, w: board }];
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
  return raiseSkyline(skyline, x, slot, y + h);
}

// Place each module at its own width and height. Later modules sit beside
// earlier ones and drop into the gaps under shorter neighbors. A resize pin
// keeps the card under the pointer; the others keep their own sizes.
function packModules(items, heights, boardWidth, pin) {
  const board = Math.max(0, Math.round(boardWidth || 0));
  const boxes = new Map();
  if (!board) {
    return { boxes, height: 0 };
  }
  const above = [];
  const rest = [];
  items.forEach((item) => {
    if (pin && item.key === pin.key) return;
    const origin = pin?.origins?.get(item.key);
    if (pin && origin && origin.y + origin.h <= pin.y + 1) above.push(item);
    else rest.push(item);
  });
  let skyline = [{ x: 0, y: 0, w: board }];
  above.forEach((item) => {
    skyline = placeItem(skyline, boxes, item, heights, board);
  });
  if (pin) {
    const pinned = items.find((item) => item.key === pin.key);
    const wide = !!pinned?.wide;
    const x = wide ? 0 : Math.max(0, Math.min(pin.x, board - 1));
    const slot = wide
      ? board
      : Math.max(1, Math.min(board - x, Math.round(pin.width) + MODULE_GUTTER * 2));
    const h = Math.max(1, Math.round(pin.height) + MODULE_GUTTER * 2);
    const box = { x, y: pin.y, w: slot, h };
    boxes.set(pin.key, box);
    skyline = raiseSkyline(skyline, box.x, box.w, box.y + box.h);
  }
  rest.forEach((item) => {
    skyline = placeItem(skyline, boxes, item, heights, board);
  });
  const height = skyline.reduce((max, seg) => Math.max(max, seg.y), 0);
  return { boxes, height };
}

// Reading order: the pointer inserts before the first card it is above or
// to the left of, so a drop in open space lands in that gap.
function insertIndex(others, boxes, x, y) {
  let index = others.length;
  for (let i = 0; i < others.length; i += 1) {
    const box = boxes.get(others[i].key);
    if (!box) continue;
    if (y >= box.y + box.h) continue;
    if (y < box.y) return i;
    if (x < box.x + box.w / 2) return i;
    index = i + 1;
  }
  return index;
}

function SortableModule({ module, box, hidden, sliding, children }) {
  const { attributes, listeners, setNodeRef } = useSortable({
    id: `module-${module.key}`,
    animateLayoutChanges: () => false,
  });

  const style = {
    position: "absolute",
    left: box.x,
    top: box.y,
    width: box.w,
    zIndex: hidden ? 0 : 1,
    opacity: hidden ? 0 : 1,
    boxSizing: "border-box",
    display: "flow-root",
    transition: sliding ? "left 140ms ease, top 140ms ease" : undefined,
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
  const [resizePreview, setResizePreview] = useState(null);
  const [dragKey, setDragKey] = useState(null);
  const [dropIndex, setDropIndex] = useState(null);
  const [dragSize, setDragSize] = useState(null);
  const dragKeyRef = useRef(null);
  const dropIndexRef = useRef(null);
  const dragOriginsRef = useRef(null);
  const previewRef = useRef(null);
  const layoutRef = useRef({ boxes: new Map() });
  const metricsRef = useRef({ items: [], heights: {}, boardWidth: 0 });

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
  metricsRef.current = { items: moduleItems, heights, boardWidth };
  previewRef.current = resizePreview;

  const layoutApi = useMemo(
    () => ({
      maxCardWidth(key) {
        const board = metricsRef.current.boardWidth;
        if (!board) return 0;
        const preview = previewRef.current;
        const x =
          preview && preview.key === key
            ? preview.x
            : layoutRef.current.boxes.get(key)?.x || 0;
        return Math.max(1, board - x - MODULE_GUTTER * 2);
      },
      previewResize(key, size) {
        setResizePreview((prev) => {
          if (!size) return null;
          if (
            prev &&
            prev.key === key &&
            prev.width === size.width &&
            prev.height === size.height
          ) {
            return prev;
          }
          if (prev && prev.key === key) {
            return { ...prev, width: size.width, height: size.height };
          }
          const origins = new Map(layoutRef.current.boxes);
          const box = origins.get(key) || { x: 0, y: 0 };
          return {
            key,
            x: box.x,
            y: box.y,
            width: size.width,
            height: size.height,
            origins,
          };
        });
      },
    }),
    []
  );

  const clearDrag = () => {
    dragKeyRef.current = null;
    dropIndexRef.current = null;
    dragOriginsRef.current = null;
    setDragKey(null);
    setDropIndex(null);
    setDragSize(null);
  };

  const handleDragStart = (event) => {
    const key = moduleItems.find(
      (module) => `module-${module.key}` === String(event.active?.id)
    )?.key;
    if (key == null) return;
    const index = moduleItems.findIndex((module) => module.key === key);
    dragKeyRef.current = key;
    dropIndexRef.current = index;
    dragOriginsRef.current = new Map(layoutRef.current.boxes);
    setDragKey(key);
    setDropIndex(index);
    const initial = event.active?.rect?.current?.initial;
    if (initial) setDragSize({ width: initial.width, height: initial.height });
  };

  const handleDragMove = (event) => {
    const key = dragKeyRef.current;
    if (key == null || !boardRef.current) return;
    const rect =
      event.active?.rect?.current?.translated ||
      event.active?.rect?.current?.initial;
    if (!rect) return;
    const board = boardRef.current.getBoundingClientRect();
    const x = rect.left + rect.width / 2 - board.left;
    const y = rect.top + rect.height / 2 - board.top;
    const { items } = metricsRef.current;
    const others = items.filter((item) => item.key !== key);
    const index = insertIndex(others, dragOriginsRef.current || new Map(), x, y);
    if (dropIndexRef.current !== index) {
      dropIndexRef.current = index;
      setDropIndex(index);
    }
  };

  const handleDragEnd = () => {
    const key = dragKeyRef.current;
    const index = dropIndexRef.current;
    const items = metricsRef.current.items;
    clearDrag();
    if (key == null || index == null) return;
    const active = items.find((item) => item.key === key);
    if (!active) return;
    const others = items.filter((item) => item.key !== key);
    const next = others.slice();
    next.splice(Math.max(0, Math.min(index, others.length)), 0, active);
    const unchanged =
      next.length === items.length &&
      next.every((item, i) => item.key === items[i].key);
    if (unchanged) return;
    setModules(next);
    const updatedConfig = { ...config, modules: next };
    saveConfig(updatedConfig);
    saveConfigToDevice(updatedConfig);
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
          if (
            dragKeyRef.current != null &&
            String(key) === String(dragKeyRef.current)
          ) {
            return;
          }
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

  const orderedItems = useMemo(() => {
    if (dragKey == null || dropIndex == null) return moduleItems;
    const active = moduleItems.find((item) => item.key === dragKey);
    if (!active) return moduleItems;
    const others = moduleItems.filter((item) => item.key !== dragKey);
    const next = others.slice();
    next.splice(Math.max(0, Math.min(dropIndex, others.length)), 0, active);
    return next;
  }, [moduleItems, dragKey, dropIndex]);

  const layout = useMemo(
    () => packModules(orderedItems, heights, boardWidth, resizePreview),
    [orderedItems, heights, boardWidth, resizePreview]
  );
  layoutRef.current = layout;

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
    } else if (module.type === "logs") {
      child = (
        <LogsModule
          module={module}
          config={config}
          deviceOnline={deviceOnline}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
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
      <SortableModule
        key={`${module.type}-${module.key}`}
        module={module}
        box={box}
        hidden={module.key === dragKey}
        sliding={dragKey != null}
      >
        {child}
      </SortableModule>
    );
  };

  const moduleIds = orderedItems.map((module) => `module-${module.key}`);
  const dropBox = dragKey != null ? layout.boxes.get(dragKey) : null;

  return (
    <ModuleLayoutContext.Provider value={layoutApi}>
      <DndContext
        sensors={sensors}
        collisionDetection={closestCenter}
        onDragStart={handleDragStart}
        onDragMove={handleDragMove}
        onDragEnd={handleDragEnd}
        onDragCancel={clearDrag}
      >
        <div style={{ width: "100%", marginTop: 16 }}>
          <SortableContext items={moduleIds} strategy={() => null}>
            <div
              ref={boardRef}
              style={{
                position: "relative",
                width: "100%",
                height: layout.height,
                minHeight: "100px",
              }}
            >
              {dropBox ? (
                <div
                  style={{
                    position: "absolute",
                    left: dropBox.x + MODULE_GUTTER,
                    top: dropBox.y + MODULE_GUTTER,
                    width: Math.max(0, dropBox.w - MODULE_GUTTER * 2),
                    height: Math.max(0, dropBox.h - MODULE_GUTTER * 2),
                    border: "1px dashed rgba(71, 255, 240, 0.75)",
                    borderRadius: 5,
                    background: "rgba(71, 255, 240, 0.06)",
                    pointerEvents: "none",
                    zIndex: 0,
                    transition: "left 140ms ease, top 140ms ease",
                  }}
                />
              ) : null}
              {orderedItems.map((module) => renderModule(module))}
            </div>
          </SortableContext>
          <DragOverlay dropAnimation={null}>
            {dragKey != null && dragSize ? (
              <div
                style={{
                  width: dragSize.width,
                  height: dragSize.height,
                  border: "1px solid rgba(71, 255, 240, 0.9)",
                  borderRadius: 5,
                  background: "rgba(18, 18, 18, 0.55)",
                  boxSizing: "border-box",
                }}
              />
            ) : null}
          </DragOverlay>
        </div>
      </DndContext>
    </ModuleLayoutContext.Provider>
  );
}
