import React, { useState, useEffect } from "react";
import { Container, Typography } from "@mui/material";

export default function WebSockets({ config }) {
  const [webSockets, setWebSockets] = useState([]);
  const [imageURLs, setImageURLs] = useState([]);

  useEffect(() => {
    if (config && config.webSockets) {
      setWebSockets(config.webSockets);
    }
  }, [config]);

  useEffect(() => {
    const sockets = webSockets.map((webSocket, index) => {
      const ws = new WebSocket(webSocket.url);
      ws.binaryType = "arraybuffer";

      ws.onmessage = (event) => {
        const blob = new Blob([event.data], { type: "image/jpeg" });
        const objectURL = URL.createObjectURL(blob);
        setImageURLs((prev) => {
          const updated = [...prev];
          updated[index] = objectURL;
          return updated;
        });
      };

      ws.onerror = (error) => {
        console.error("WebSocket error:", error);
      };

      return ws;
    });

    return () => {
      sockets.forEach((ws) => ws.close());
    };
  }, [webSockets]);

  return (
    <Container sx={{ marginTop: 2 }}>
      <Typography variant="h6">Camera Streams</Typography>
      {webSockets.length > 0 ? (
        webSockets.map((webSocket, index) => (
          <div key={index}>
            <Typography variant="body1">
              WebSocket URL: {webSocket.url}
            </Typography>
            <img
              src={imageURLs[index]}
              alt="Camera Stream"
              style={{ width: "100%", marginTop: "10px" }}
            />
          </div>
        ))
      ) : (
        <Typography variant="body2">No camera streams configured.</Typography>
      )}
    </Container>
  );
}
