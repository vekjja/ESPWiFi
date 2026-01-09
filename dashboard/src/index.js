import React from "react";
import ReactDOM from "react-dom/client";
import "./index.css";
import App from "./App";
import reportWebVitals from "./reportWebVitals";

function escapeHtml(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function isChunkLoadError(err) {
  const msg = String(err?.message || err || "");
  return (
    err?.name === "ChunkLoadError" ||
    msg.includes("ChunkLoadError") ||
    msg.includes("Loading chunk") ||
    msg.includes("Failed to fetch dynamically imported module")
  );
}

function hasReloadMarker() {
  try {
    return new URLSearchParams(window.location.search).has("__reload");
  } catch {
    return false;
  }
}

function markAndReload() {
  try {
    const url = new URL(window.location.href);
    url.searchParams.set("__reload", String(Date.now()));
    window.location.replace(url.toString());
  } catch {
    window.location.reload();
  }
}

function renderFatalOverlay(err, extra = "") {
  // IMPORTANT: never mutate #root directly; React may already be rendering,
  // and replacing its DOM can trigger "removeChild" errors.
  let overlay = document.getElementById("fatal-overlay");
  if (!overlay) {
    overlay = document.createElement("div");
    overlay.id = "fatal-overlay";
    overlay.style.position = "fixed";
    overlay.style.inset = "0";
    overlay.style.zIndex = "99999";
    overlay.style.background = "#111";
    overlay.style.color = "#fff";
    overlay.style.padding = "16px";
    overlay.style.fontFamily =
      "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace";
    overlay.style.overflow = "auto";
    document.body.appendChild(overlay);
  }
  const details = escapeHtml(
    `${extra ? `${extra}\n\n` : ""}${err?.stack || err?.message || err || ""}`
  );
  overlay.innerHTML = `
    <div style="
      min-height: 100vh;
      background: #111;
      color: #fff;
      padding: 16px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    ">
      <div style="font-size: 18px; font-weight: 700; margin-bottom: 8px;">ESPWiFi failed to load</div>
      <div style="opacity: 0.85; margin-bottom: 12px;">
        If this persists, try a hard refresh or clear the browser cache.
      </div>
      <pre style="
        white-space: pre-wrap;
        word-break: break-word;
        background: rgba(255,255,255,0.06);
        padding: 12px;
        border-radius: 8px;
      ">${details}</pre>
    </div>
  `;
}

// Global handlers so mobile users don't just see a blank screen.
window.addEventListener("error", (e) => {
  const err = e?.error || e;
  if (isChunkLoadError(err) && !hasReloadMarker()) {
    markAndReload();
    return;
  }
  renderFatalOverlay(err, "window.error");
});
window.addEventListener("unhandledrejection", (e) => {
  const err = e?.reason || e;
  if (isChunkLoadError(err) && !hasReloadMarker()) {
    markAndReload();
    return;
  }
  renderFatalOverlay(err, "unhandledrejection");
});

class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { hasError: false, error: null };
  }
  static getDerivedStateFromError(error) {
    return { hasError: true, error };
  }
  componentDidCatch(error) {
    // eslint-disable-next-line no-console
    console.error("App error boundary caught:", error);
  }
  render() {
    if (this.state.hasError) {
      const details =
        this.state.error?.stack ||
        this.state.error?.message ||
        String(this.state.error || "");
      return (
        <div
          style={{
            minHeight: "100vh",
            background: "#111",
            color: "#fff",
            padding: 16,
            fontFamily:
              "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
          }}
        >
          <div style={{ fontSize: 18, fontWeight: 700, marginBottom: 8 }}>
            ESPWiFi failed to load
          </div>
          <div style={{ opacity: 0.85, marginBottom: 12 }}>
            If this persists, try a hard refresh or clear the browser cache.
          </div>
          <pre
            style={{
              whiteSpace: "pre-wrap",
              wordBreak: "break-word",
              background: "rgba(255,255,255,0.06)",
              padding: 12,
              borderRadius: 8,
            }}
          >
            {details}
          </pre>
        </div>
      );
    }
    return this.props.children;
  }
}

const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>
);

// If you want to start measuring performance in your app, pass a function
// to log results (for example: reportWebVitals(console.log))
// or send to an analytics endpoint. Learn more: https://bit.ly/CRA-vitals
reportWebVitals();
