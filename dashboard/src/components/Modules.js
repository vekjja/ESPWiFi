import React, { useState, useEffect } from "react";
import { Container } from "@mui/material";
import PinModule from "./PinModule";
import WebSocketModule from "./WebSocketModule";

export default function Modules({ config, saveConfig }) {
  const [modules, setModules] = useState({
    pins: {},
    webSockets: [],
  });

  // Update modules from config when it changes
  useEffect(() => {
    if (config) {
      setModules({
        pins: config.pins || {},
        webSockets: config.webSockets || [],
      });
    }
  }, [config]);

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

  if (!config) {
    return <div>Loading configuration...</div>;
  }

  return (
    <Container
      sx={{
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
        marginTop: 2,
      }}
    >
      {/* Render Pin Modules */}
      {Object.keys(modules.pins).map((pinNum) => (
        <PinModule
          key={`pin-${pinNum}`}
          pinNum={pinNum}
          initialProps={modules.pins[pinNum]}
          config={config}
          onUpdate={updatePin}
          onDelete={deletePin}
        />
      ))}

      {/* Render WebSocket Modules */}
      {modules.webSockets.map((webSocket, index) => (
        <WebSocketModule
          key={`websocket-${index}`}
          index={index}
          initialProps={webSocket}
          onUpdate={updateWebSocket}
          onDelete={deleteWebSocket}
        />
      ))}
    </Container>
  );
}
