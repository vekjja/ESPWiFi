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
import MoviePlayerModule from "./movie/MoviePlayerModule";
import SystemInfoModule from "./systemInfo/SystemInfoModule";
import LogsModule from "./logs/LogsModule";
import { ModuleLayoutContext } from "./Module";
import {
  MODULE_GUTTER,
  MODULE_SIZES,
  moduleSlotWidth,
  viewableCardHeight,
} from "./moduleConfig";

// A scroll or a control inside a module must not start a drag. dnd-kit's
// PointerSensor otherwise treats the first 8px of a finger scroll as a move.
class ModulePointerSensor extends PointerSensor {
  static activators = [
    {
      eventName: "onPointerDown",
      handler: ({ nativeEvent: event }) => {
        if (!event.isPrimary || event.button !== 0) return false;
        const target = event.target;
        if (!(target instanceof Element)) return false;
        if (
          target.closest(
            "[data-no-dnd], input, textarea, select, button, a, label, .MuiSlider-root, .MuiIconButton-root, .MuiSwitch-root, .MuiCheckbox-root"
          )
        ) {
          return false;
        }
        return true;
      },
    },
  ];
}

function estimateHeight(module) {
  let height = 280;
  if (module.type === "systemInfo") {
    height = module.info === "storage" ? 760 : 460;
  } else if (module.type === "camera") {
    height = 540;
  } else if (module.type === "musicPlayer") {
    height = viewableCardHeight();
  } else if (module.type === "moviePlayer") {
    height = 580;
  } else if (module.type === "logs") {
    height = 520;
  }
  return height + 20;
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
  const preferred = customHeight || presetHeight || 0;
  return Math.max(preferred, measured || 0) || estimateHeight(item);
}

function hasAnchor(item) {
  return Number.isFinite(item?.x) && Number.isFinite(item?.y);
}

function overlaps(a, b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

function clampSlot(x, y, w, h, board) {
  const width = Math.max(1, Math.min(w, Math.max(1, board || w)));
  const maxX = Math.max(0, (board || width) - width);
  return {
    x: Math.min(maxX, Math.max(0, Math.round(x))),
    y: Math.max(0, Math.round(y)),
    w: width,
    h: Math.max(1, h),
  };
}

function slotSize(item, heights, board, size) {
  if (size) {
    const w = item?.wide
      ? board
      : Math.min(board, Math.max(1, Math.round(size.width)) + MODULE_GUTTER * 2);
    return {
      w: Math.max(1, w),
      h: Math.max(1, Math.round(size.height) + MODULE_GUTTER * 2),
    };
  }
  return {
    w: Math.max(1, moduleSlotWidth(item, board) || board || 1),
    h: moduleHeight(item, heights),
  };
}

// A dropped module stays at the coordinates that were saved. Modules that
// have never been moved take the first open spot and stay there until dragged.
function openBox(item, heights, board, taken) {
  const { w, h } = slotSize(item, heights, board);
  const step = MODULE_GUTTER * 2;
  let y = 0;
  for (let guard = 0; guard < 80; guard += 1) {
    const maxX = Math.max(0, board - w);
    for (let x = 0; x <= maxX; x += step) {
      const rect = { x, y, w, h };
      if (!taken.some((box) => overlaps(rect, box))) return rect;
    }
    y += step;
  }
  const bottom = taken.reduce((max, box) => Math.max(max, box.y + box.h), 0);
  return clampSlot(0, bottom, w, h, board);
}

function layoutModules(items, heights, boardWidth, preview) {
  const board = Math.max(0, Math.round(boardWidth || 0));
  const boxes = new Map();
  if (!board) return { boxes, height: 0 };
  const taken = () => Array.from(boxes.values());
  items.forEach((item) => {
    if (preview && item.key === preview.key) return;
    if (hasAnchor(item)) return;
    boxes.set(item.key, openBox(item, heights, board, taken()));
  });
  items.forEach((item) => {
    if (preview && item.key === preview.key) return;
    if (!hasAnchor(item)) return;
    const { w, h } = slotSize(item, heights, board);
    boxes.set(item.key, clampSlot(item.x, item.y, w, h, board));
  });
  if (preview) {
    const item = items.find((entry) => entry.key === preview.key) || {};
    const { w, h } = slotSize(item, heights, board, {
      width: preview.width,
      height: preview.height,
    });
    boxes.set(preview.key, clampSlot(preview.x, preview.y, w, h, board));
  }
  let height = 0;
  boxes.forEach((box) => {
    height = Math.max(height, box.y + box.h);
  });
  return { boxes, height };
}

function SortableModule({ module, box, hidden, front, onSelect, children }) {
  const { attributes, listeners, setNodeRef } = useSortable({
    id: `module-${module.key}`,
    animateLayoutChanges: () => false,
  });
  const { onPointerDown, ...restListeners } = listeners || {};

  const style = {
    position: "absolute",
    left: box.x,
    top: box.y,
    width: box.w,
    zIndex: hidden ? 0 : front ? 4 : 1,
    opacity: hidden ? 0 : 1,
    boxSizing: "border-box",
    display: "flow-root",
  };

  return (
    <div
      ref={setNodeRef}
      style={style}
      data-module-key={module.key}
      {...attributes}
      {...restListeners}
      onPointerDown={(event) => {
        onSelect?.(module.key);
        onPointerDown?.(event);
      }}
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
  const [viewportHeight, setViewportHeight] = useState(() =>
    typeof window === "undefined" ? 0 : window.innerHeight
  );
  const [heights, setHeights] = useState({});
  const [resizePreview, setResizePreview] = useState(null);
  const [dragKey, setDragKey] = useState(null);
  const [frontKey, setFrontKey] = useState(null);
  const [dropSlot, setDropSlot] = useState(null);
  const [dragSize, setDragSize] = useState(null);
  const dragKeyRef = useRef(null);
  const dropSlotRef = useRef(null);
  const seenModuleKeysRef = useRef(null);
  const previewRef = useRef(null);
  const layoutRef = useRef({ boxes: new Map() });
  const metricsRef = useRef({ items: [], heights: {}, boardWidth: 0 });

  // Configure sensors for drag detection with activation constraints
  const sensors = useSensors(
    useSensor(ModulePointerSensor, {
      activationConstraint: {
        distance: 8,
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

  // A module that was just added has no saved position, so the board would
  // drop it on top of an existing card. Park it in the first open spot and
  // keep that spot. Modules already on screen are left where they are.
  useEffect(() => {
    if (!modules.length) return;
    const keys = modules.map((module) => module.key);
    if (seenModuleKeysRef.current == null) {
      seenModuleKeysRef.current = new Set(keys);
      return;
    }
    const newcomers = modules.filter(
      (module) =>
        !seenModuleKeysRef.current.has(module.key) && !hasAnchor(module)
    );
    if (!newcomers.length || !boardWidth || dragKeyRef.current != null) {
      if (!newcomers.length) seenModuleKeysRef.current = new Set(keys);
      return;
    }
    const others = modules.filter(
      (module) => !newcomers.some((item) => item.key === module.key)
    );
    const placed = layoutModules(others, heights, boardWidth, null);
    const taken = Array.from(placed.boxes.values());
    const next = modules.map((module) => {
      if (!newcomers.some((item) => item.key === module.key)) return module;
      const box = openBox(module, heights, boardWidth, taken);
      taken.push(box);
      return { ...module, x: box.x, y: box.y };
    });
    seenModuleKeysRef.current = new Set(keys);
    setModules(next);
    saveConfig({ modules: next });
  }, [modules, boardWidth, heights, saveConfig]);

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
          const box = layoutRef.current.boxes.get(key) || { x: 0, y: 0 };
          return {
            key,
            x: box.x,
            y: box.y,
            width: size.width,
            height: size.height,
          };
        });
      },
    }),
    []
  );

  const clearDrag = () => {
    dragKeyRef.current = null;
    dropSlotRef.current = null;
    setDragKey(null);
    setDropSlot(null);
    setDragSize(null);
  };

  const handleDragStart = (event) => {
    const key = moduleItems.find(
      (module) => `module-${module.key}` === String(event.active?.id)
    )?.key;
    if (key == null) return;
    const current = layoutRef.current.boxes.get(key) || null;
    dragKeyRef.current = key;
    dropSlotRef.current = current;
    setDragKey(key);
    setDropSlot(current);
    const initial = event.active?.rect?.current?.initial;
    if (initial) setDragSize({ width: initial.width, height: initial.height });
  };

  const handleDragMove = (event) => {
    const key = dragKeyRef.current;
    const boardNode = boardRef.current;
    if (key == null || !boardNode) return;
    const rect =
      event.active?.rect?.current?.translated ||
      event.active?.rect?.current?.initial;
    if (!rect) return;
    const { items, heights: packedHeights, boardWidth: width } = metricsRef.current;
    const active = items.find((item) => item.key === key);
    if (!active) return;
    const origin = boardNode.getBoundingClientRect();
    const { w, h } = slotSize(active, packedHeights, width);
    const slot = clampSlot(
      rect.left - origin.left,
      rect.top - origin.top,
      w,
      h,
      width
    );
    const prev = dropSlotRef.current;
    if (
      prev &&
      prev.x === slot.x &&
      prev.y === slot.y &&
      prev.w === slot.w &&
      prev.h === slot.h
    ) {
      return;
    }
    dropSlotRef.current = slot;
    setDropSlot(slot);
  };

  const handleDragEnd = () => {
    const key = dragKeyRef.current;
    const slot = dropSlotRef.current;
    const items = metricsRef.current.items;
    clearDrag();
    if (key == null || !slot) return;
    const active = items.find((item) => item.key === key);
    if (!active) return;
    if (active.x === slot.x && active.y === slot.y) return;
    const next = items.map((item) =>
      item.key === key ? { ...item, x: slot.x, y: slot.y } : item
    );
    setModules(next);
    saveConfig({ modules: next });
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

    saveConfig({ modules: updatedModules });
  };

  const deleteModule = (moduleKey) => {
    console.log("🗑️ Modules.deleteModule called with moduleKey:", moduleKey);
    console.log("🗑️ Current modules:", modules);
    const updatedModules = modules.filter((module) => module.key !== moduleKey);
    console.log("🗑️ Updated modules after filter:", updatedModules);

    setModules(updatedModules);
    saveConfig({ modules: updatedModules });
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
    const onResize = () => setViewportHeight(window.innerHeight);
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
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

  const layout = useMemo(
    () => layoutModules(moduleItems, heights, boardWidth, resizePreview),
    [moduleItems, heights, boardWidth, resizePreview, viewportHeight]
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
          deviceOnline={deviceOnline}
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
    } else if (module.type === "moviePlayer") {
      child = (
        <MoviePlayerModule
          config={module}
          globalConfig={config}
          onUpdate={updateModule}
          onDelete={deleteModule}
          deviceOnline={deviceOnline}
          controlWs={controlWs}
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
        front={module.key === frontKey || module.key === dragKey}
        onSelect={setFrontKey}
      >
        {child}
      </SortableModule>
    );
  };

  const moduleIds = moduleItems.map((module) => `module-${module.key}`);
  const dropBox = dragKey != null ? dropSlot : null;
  const boardHeight = Math.max(
    layout.height,
    dropBox ? dropBox.y + dropBox.h : 0
  );

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
        <div
          style={{
            width: "100%",
            marginTop: 16,
            boxSizing: "border-box",
            paddingLeft: MODULE_GUTTER,
            paddingRight: MODULE_GUTTER,
          }}
        >
          <SortableContext items={moduleIds} strategy={() => null}>
            <div
              ref={boardRef}
              style={{
                position: "relative",
                width: "100%",
                height: boardHeight,
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
                    zIndex: 2,
                  }}
                />
              ) : null}
              {moduleItems.map((module) => renderModule(module))}
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
