import React, { useState, useEffect, useMemo } from "react";
import { Container } from "@mui/material";
import {
  DndContext,
  closestCenter,
  KeyboardSensor,
  PointerSensor,
  useSensor,
  useSensors,
} from "@dnd-kit/core";
import {
  arrayMove,
  SortableContext,
  sortableKeyboardCoordinates,
  verticalListSortingStrategy,
} from "@dnd-kit/sortable";
import { useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import PinModule from "./PinModule";
import WebSocketModule from "./WebSocketModule";

// Sortable wrapper component for Pin modules
function SortablePinModule({
  pinNum,
  initialProps,
  config,
  onUpdate,
  onDelete,
}) {
  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id: `pin-${pinNum}` });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.5 : 1,
    zIndex: isDragging ? 1000 : 1,
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <PinModule
        pinNum={pinNum}
        initialProps={initialProps}
        config={config}
        onUpdate={onUpdate}
        onDelete={onDelete}
      />
    </div>
  );
}

// Sortable wrapper component for WebSocket modules
function SortableWebSocketModule({ index, initialProps, onUpdate, onDelete }) {
  // Use a stable ID based on the WebSocket URL or name to prevent issues during reordering
  const stableId = `websocket-${
    initialProps.url || initialProps.name || index
  }`;

  const {
    attributes,
    listeners,
    setNodeRef,
    transform,
    transition,
    isDragging,
  } = useSortable({ id: stableId });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.5 : 1,
    zIndex: isDragging ? 1000 : 1,
  };

  return (
    <div ref={setNodeRef} style={style} {...attributes} {...listeners}>
      <WebSocketModule
        index={index}
        initialProps={initialProps}
        onUpdate={onUpdate}
        onDelete={onDelete}
      />
    </div>
  );
}

export default function Modules({ config, saveConfig }) {
  const [modules, setModules] = useState({
    pins: {},
    webSockets: [],
  });

  // Configure sensors for drag detection with activation constraints
  const sensors = useSensors(
    useSensor(PointerSensor, {
      activationConstraint: {
        distance: 8, // Require 8px movement before starting drag
      },
      // Exclude slider elements from drag activation
      shouldActivateOnEvent: (event) => {
        const target = event.target;
        // Don't activate drag if clicking on slider elements
        if (
          target.closest('[role="slider"]') ||
          target.closest(".MuiSlider-root") ||
          target.closest("[data-dnd-kit-sortable-item]") === null
        ) {
          return false;
        }
        return true;
      },
    }),
    useSensor(KeyboardSensor, {
      coordinateGetter: sortableKeyboardCoordinates,
    })
  );

  // Update modules from config when it changes
  useEffect(() => {
    if (config) {
      setModules({
        pins: config.pins || {},
        webSockets: config.webSockets || [],
      });
    }
  }, [config]);

  // Use useMemo to ensure stable references
  const pinKeys = useMemo(() => Object.keys(modules.pins), [modules.pins]);
  const webSocketItems = useMemo(
    () => modules.webSockets,
    [modules.webSockets]
  );

  const updatePin = (pinNum, pinState) => {
    const updatedPins = { ...modules.pins };
    updatedPins[pinNum] = pinState;

    const updatedModules = { ...modules, pins: updatedPins };
    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, pins: updatedPins };
    saveConfig(updatedConfig);
  };

  const deletePin = (pinNum) => {
    const updatedPins = { ...modules.pins };
    delete updatedPins[pinNum];

    const updatedModules = { ...modules, pins: updatedPins };
    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, pins: updatedPins };
    saveConfig(updatedConfig);
  };

  const updateWebSocket = (index, webSocketState) => {
    const updatedWebSockets = [...modules.webSockets];
    updatedWebSockets[index] = webSocketState;

    const updatedModules = { ...modules, webSockets: updatedWebSockets };
    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);
  };

  const deleteWebSocket = (index) => {
    const updatedWebSockets = modules.webSockets.filter((_, i) => i !== index);

    const updatedModules = { ...modules, webSockets: updatedWebSockets };
    setModules(updatedModules);

    // Update the global config
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);
  };

  const handleDragEnd = (event) => {
    const { active, over } = event;

    if (active.id !== over.id) {
      // Handle pin reordering
      if (active.id.toString().startsWith("pin-")) {
        const oldIndex = pinKeys.indexOf(
          active.id.toString().replace("pin-", "")
        );
        const newIndex = pinKeys.indexOf(
          over.id.toString().replace("pin-", "")
        );

        if (oldIndex !== -1 && newIndex !== -1) {
          const reorderedKeys = arrayMove(pinKeys, oldIndex, newIndex);
          const reorderedPins = {};
          reorderedKeys.forEach((pinKey) => {
            reorderedPins[pinKey] = modules.pins[pinKey];
          });

          const updatedModules = { ...modules, pins: reorderedPins };
          setModules(updatedModules);

          // Update the global config (local only)
          const updatedConfig = { ...config, pins: reorderedPins };
          saveConfig(updatedConfig);
        }
      }
      // Handle WebSocket reordering
      else if (active.id.toString().startsWith("websocket-")) {
        // Find the indices based on stable IDs
        const activeWebSocket = webSocketItems.find(
          (ws) =>
            `websocket-${ws.url || ws.name || webSocketItems.indexOf(ws)}` ===
            active.id
        );
        const overWebSocket = webSocketItems.find(
          (ws) =>
            `websocket-${ws.url || ws.name || webSocketItems.indexOf(ws)}` ===
            over.id
        );

        if (activeWebSocket && overWebSocket) {
          const oldIndex = webSocketItems.indexOf(activeWebSocket);
          const newIndex = webSocketItems.indexOf(overWebSocket);

          if (oldIndex !== -1 && newIndex !== -1) {
            const reorderedWebSockets = arrayMove(
              webSocketItems,
              oldIndex,
              newIndex
            );

            const updatedModules = {
              ...modules,
              webSockets: reorderedWebSockets,
            };
            setModules(updatedModules);

            // Update the global config (local only)
            const updatedConfig = {
              ...config,
              webSockets: reorderedWebSockets,
            };
            saveConfig(updatedConfig);
          }
        }
      }
    }
  };

  if (!config) {
    return <div>Loading configuration...</div>;
  }

  // Create stable IDs for WebSocket items
  const webSocketIds = webSocketItems.map(
    (ws, index) => `websocket-${ws.url || ws.name || index}`
  );

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
        {/* Render Pin Modules */}
        <SortableContext
          items={pinKeys.map((key) => `pin-${key}`)}
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
            {pinKeys.map((pinNum) => (
              <SortablePinModule
                key={`pin-${pinNum}`}
                pinNum={pinNum}
                initialProps={modules.pins[pinNum]}
                config={config}
                onUpdate={updatePin}
                onDelete={deletePin}
              />
            ))}
          </div>
        </SortableContext>

        {/* Render WebSocket Modules */}
        <SortableContext
          items={webSocketIds}
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
            {webSocketItems.map((webSocket, index) => (
              <SortableWebSocketModule
                key={`websocket-${webSocket.url || webSocket.name || index}`}
                index={index}
                initialProps={webSocket}
                onUpdate={updateWebSocket}
                onDelete={deleteWebSocket}
              />
            ))}
          </div>
        </SortableContext>
      </Container>
    </DndContext>
  );
}
