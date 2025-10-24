import React, { useState, useEffect, useMemo } from "react";
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
  verticalListSortingStrategy,
} from "@dnd-kit/sortable";
import { useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import PinModule from "./PinModule";
import WebSocketModule from "./WebSocketModule";
import CameraModule from "./CameraModule";

// Sortable wrapper component for Pin modules
function SortablePinModule({ module, config, onUpdate, onDelete }) {
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
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <PinModule
        pinNum={module.number}
        initialProps={module}
        config={config}
        onUpdate={onUpdate}
        onDelete={onDelete}
      />
    </div>
  );
}

// Sortable wrapper component for WebSocket modules
function SortableWebSocketModule({ module, onUpdate, onDelete }) {
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
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <WebSocketModule
        index={module.key}
        initialProps={module}
        onUpdate={onUpdate}
        onDelete={onDelete}
      />
    </div>
  );
}

// Sortable wrapper component for Camera modules
function SortableCameraModule({
  module,
  config,
  onUpdate,
  onDelete,
  deviceOnline,
}) {
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
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <CameraModule
        config={module}
        globalConfig={config}
        onUpdate={onUpdate}
        onDelete={onDelete}
        deviceOnline={deviceOnline}
      />
    </div>
  );
}

export default function Modules({
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline = true,
}) {
  const [modules, setModules] = useState([]);

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

    if (active.id !== over.id) {
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
    console.log("Deleting module:", moduleKey);
    const updatedModules = modules.filter((module) => module.key !== moduleKey);

    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, modules: updatedModules };
    saveConfig(updatedConfig); // Update local config
    saveConfigToDevice(updatedConfig); // Save to device
  };

  if (!config) {
    return <div>Loading configuration...</div>;
  }

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
        <SortableContext
          items={moduleIds}
          strategy={verticalListSortingStrategy}
        >
          <div
            style={{
              display: "flex",
              flexWrap: "wrap",
              justifyContent: "center",
              width: "100%",
              minHeight: "100px",
            }}
          >
            {moduleItems.map((module) => {
              if (module.type === "pin") {
                return (
                  <SortablePinModule
                    key={`pin-${module.key}`}
                    module={module}
                    config={config}
                    onUpdate={updateModule}
                    onDelete={deleteModule}
                  />
                );
              } else if (module.type === "webSocket") {
                return (
                  <SortableWebSocketModule
                    key={`websocket-${module.key}`}
                    module={module}
                    onUpdate={updateModule}
                    onDelete={deleteModule}
                  />
                );
              } else if (module.type === "camera") {
                return (
                  <SortableCameraModule
                    key={`camera-${module.key}`}
                    module={module}
                    config={config}
                    onUpdate={updateModule}
                    onDelete={deleteModule}
                    deviceOnline={deviceOnline}
                  />
                );
              }
              return null;
            })}
          </div>
        </SortableContext>
      </Container>
    </DndContext>
  );
}
