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

// Sortable wrapper component for Pin modules
function SortablePinModule({ module, config, onUpdate, onDelete }) {
  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id: `module-${module.id}` });

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
  } = useSortable({ id: `module-${module.id}` });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.5 : 1,
    zIndex: isDragging ? 1000 : 1,
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <WebSocketModule
        index={module.id}
        initialProps={module}
        onUpdate={onUpdate}
        onDelete={onDelete}
      />
    </div>
  );
}

export default function Modules({ config, saveConfig }) {
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
        modulesArray = config.modules.map((module, index) => ({
          ...module,
          id: module.id !== undefined ? module.id : index, // Preserve existing IDs or use index
        }));
      } else {
        // Convert old separate format to new unified format
        const pins = config.pins || [];
        const webSockets = config.webSockets || [];

        let moduleId = 0;

        // Convert pins
        if (Array.isArray(pins)) {
          pins.forEach((pin) => {
            modulesArray.push({
              ...pin,
              type: "pin",
              id: moduleId++,
            });
          });
        } else if (typeof pins === "object") {
          // Convert from old object format
          Object.entries(pins).forEach(([pinNum, pinData]) => {
            modulesArray.push({
              ...pinData,
              type: "pin",
              number: parseInt(pinNum, 10),
              id: moduleId++,
            });
          });
        }

        // Convert webSockets
        webSockets.forEach((webSocket) => {
          modulesArray.push({
            ...webSocket,
            type: "webSocket",
            id: moduleId++,
          });
        });
      }

      // Ensure all modules have unique IDs
      const existingIds = new Set();
      modulesArray = modulesArray.map((module, index) => {
        let id = module.id;
        if (id === undefined || existingIds.has(id)) {
          // Generate a new unique ID
          let newId = index;
          while (existingIds.has(newId)) {
            newId++;
          }
          id = newId;
        }
        existingIds.add(id);
        return { ...module, id };
      });

      setModules(modulesArray);
    }
  }, [config]);

  // Use useMemo to ensure stable references
  const moduleItems = useMemo(() => modules, [modules]);

  const updateModule = (moduleId, moduleState) => {
    console.log("Updating module:", moduleId, moduleState);
    const updatedModules = modules.map((module) => {
      if (module.id === moduleId) {
        // Update the target module
        return { ...module, ...moduleState };
      } else if (module.type === "webSocket") {
        // For other WebSocket modules, preserve their connectionState from the previous config
        const prevConfigModule = (config.modules || []).find(
          (m) => m.id === module.id
        );
        if (
          prevConfigModule &&
          prevConfigModule.connectionState !== undefined
        ) {
          return {
            ...module,
            connectionState: prevConfigModule.connectionState,
          };
        }
      }
      return module;
    });

    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, modules: updatedModules };
    saveConfig(updatedConfig);
  };

  const deleteModule = (moduleId) => {
    console.log("Deleting module:", moduleId);
    const updatedModules = modules.filter((module) => module.id !== moduleId);

    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, modules: updatedModules };
    saveConfig(updatedConfig);
  };

  const handleDragEnd = (event) => {
    const { active, over } = event;

    if (active.id !== over.id) {
      console.log("Drag end:", active.id, "->", over.id);
      const oldIndex = moduleItems.findIndex(
        (module) => `module-${module.id}` === active.id.toString()
      );
      const newIndex = moduleItems.findIndex(
        (module) => `module-${module.id}` === over.id.toString()
      );

      console.log("Indices:", oldIndex, "->", newIndex);

      if (oldIndex !== -1 && newIndex !== -1) {
        const reorderedModules = arrayMove(moduleItems, oldIndex, newIndex);
        console.log(
          "Reordered modules:",
          reorderedModules.map((m) => ({
            id: m.id,
            name: m.name,
            type: m.type,
          }))
        );

        // Update local state first
        setModules(reorderedModules);

        // Update the global config with the reordered modules
        const updatedConfig = { ...config, modules: reorderedModules };
        saveConfig(updatedConfig);
      }
    }
  };

  if (!config) {
    return <div>Loading configuration...</div>;
  }

  // Create IDs for module items
  const moduleIds = moduleItems.map((module) => `module-${module.id}`);

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
                    key={`pin-${module.id}`}
                    module={module}
                    config={config}
                    onUpdate={updateModule}
                    onDelete={deleteModule}
                  />
                );
              } else if (module.type === "webSocket") {
                return (
                  <SortableWebSocketModule
                    key={`websocket-${module.id}`}
                    module={module}
                    onUpdate={updateModule}
                    onDelete={deleteModule}
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
