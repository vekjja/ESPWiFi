import React, { useState, useEffect } from "react";
import Pin from "./Pin";
import { Container } from "@mui/material";

export default function Pins({ config, saveConfig }) {
  const [pins, setPins] = useState({});

  useEffect(() => {
    if (config && config.pins) {
      setPins(config.pins);
    } else if (config && !config.pins) {
      setPins({}); // Initialize pins as an empty object if undefined
    }
  }, [config]);

  const pinElements = Object.keys(pins).map((key) => {
    const pin = pins[key];
    return (
      <Pin
        key={key}
        config={config}
        pinNum={key}
        props={pin}
        updatePins={(pinState, deletePin) => {
          const updatedPins = { ...pins };
          if (deletePin) {
            delete updatedPins[key];
          } else {
            updatedPins[key] = pinState;
          }
          setPins(updatedPins);
          saveConfig({ ...config, pins: updatedPins });
        }}
      />
    );
  });

  if (!config || !config.pins) {
    return <div>Loading configuration...</div>;
  }

  return (
    <Container
      sx={{ display: "flex", flexWrap: "wrap", justifyContent: "center" }}
    >
      {pinElements}
    </Container>
  );
}
