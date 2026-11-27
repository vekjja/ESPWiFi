import { createSocketSession } from "../../../utils/socketSession";

// The camera socket lives outside the card. Moving or resizing the module
// remounts the React tree, and that must not send camera_stop / camera_start.
const streams = new Map();

function ensureStream() {
  if (streams.has("camera")) return streams.get("camera");
  const state = {
    started: false,
    status: "disconnected",
    frameUrl: "",
    getUrl: () => "",
    onStatus: null,
    onFrame: null,
  };
  const session = createSocketSession({
    getUrl: () => state.getUrl?.() || "",
    binary: true,
    onStatus: (status) => {
      state.status = status;
      state.onStatus?.(status);
    },
    onOpen: (ws) => {
      if (!state.started) return;
      try {
        ws.send(JSON.stringify({ cmd: "camera_start" }));
      } catch {
        // ignore
      }
    },
    onMessage: (event) => {
      if (!(event.data instanceof ArrayBuffer)) return;
      const nextUrl = URL.createObjectURL(
        new Blob([event.data], { type: "image/jpeg" })
      );
      if (state.frameUrl) URL.revokeObjectURL(state.frameUrl);
      state.frameUrl = nextUrl;
      state.onFrame?.(nextUrl);
    },
  });
  const stream = { session, state };
  streams.set("camera", stream);
  return stream;
}

export function cameraStreamRunning() {
  return Boolean(streams.get("camera")?.state.started);
}

export function attachCameraStream(handlers) {
  const stream = ensureStream();
  stream.state.getUrl = handlers.getUrl;
  stream.state.onStatus = handlers.onStatus;
  stream.state.onFrame = handlers.onFrame;
  if (stream.state.status) handlers.onStatus?.(stream.state.status);
  if (stream.state.frameUrl) handlers.onFrame?.(stream.state.frameUrl);
  return {
    start() {
      stream.state.started = true;
      if (stream.session.getSocket()) {
        try {
          stream.session
            .getSocket()
            .send(JSON.stringify({ cmd: "camera_start" }));
        } catch {
          // ignore
        }
        return;
      }
      stream.session.start();
    },
    stop() {
      stream.state.started = false;
      const ws = stream.session.getSocket();
      try {
        ws?.send(JSON.stringify({ cmd: "camera_stop" }));
      } catch {
        // ignore
      }
      if (stream.state.frameUrl) {
        URL.revokeObjectURL(stream.state.frameUrl);
        stream.state.frameUrl = "";
      }
      stream.session.stop();
    },
    detach() {
      if (stream.state.onStatus === handlers.onStatus) stream.state.onStatus = null;
      if (stream.state.onFrame === handlers.onFrame) stream.state.onFrame = null;
    },
  };
}
