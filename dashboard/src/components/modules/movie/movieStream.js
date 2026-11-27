// Random-access reads of a movie file over the media socket. The device
// keeps the file open and returns the requested byte range.

const CHUNK = 32 * 1024;

export function openMovieFile(url, path) {
  const ws = new WebSocket(url);
  ws.binaryType = "arraybuffer";
  let closed = false;
  const waiters = [];

  function failAll(error) {
    while (waiters.length) {
      waiters.shift().reject(error);
    }
  }

  ws.onmessage = (event) => {
    if (closed) return;
    const waiter = waiters[0];
    if (!waiter) return;
    if (typeof event.data === "string") {
      let msg;
      try {
        msg = JSON.parse(event.data);
      } catch {
        return;
      }
      if (msg?.ok === false) {
        waiters.shift();
        const reason =
          msg.error === "unknown_cmd"
            ? "Update the device firmware to play movies."
            : msg.error || "media read failed";
        waiter.reject(new Error(reason));
        return;
      }
      if (msg?.type === "media_start") {
        waiters.shift();
        waiter.resolve(msg);
        return;
      }
      if (msg?.type === "media_chunk") {
        const len = Number(msg.len) || 0;
        if (!len) {
          waiters.shift();
          waiter.resolve(new Uint8Array(0));
        } else {
          waiter.len = len;
        }
        return;
      }
      waiters.shift();
      waiter.resolve(msg);
      return;
    }
    if (!(event.data instanceof ArrayBuffer) || !waiter.len) return;
    waiters.shift();
    waiter.resolve(new Uint8Array(event.data).subarray(0, waiter.len));
  };

  ws.onerror = () => {
    if (!closed) failAll(new Error("media socket error"));
  };
  ws.onclose = () => {
    if (!closed) failAll(new Error("media socket closed"));
  };

  function request(cmd) {
    return new Promise((resolve, reject) => {
      if (closed || ws.readyState !== WebSocket.OPEN) {
        reject(new Error("stopped"));
        return;
      }
      waiters.push({ resolve, reject, len: 0 });
      try {
        ws.send(JSON.stringify(cmd));
      } catch (error) {
        waiters.pop();
        reject(error);
      }
    });
  }

  const ready = new Promise((resolve, reject) => {
    const start = () => {
      request({
        cmd: "media_start",
        fs: "sd",
        path,
        mime: "video/mp4",
        chunkSize: CHUNK,
      }).then((msg) => {
        const size = Number(msg?.size) || 0;
        if (!size) {
          reject(new Error("movie file has no size"));
          return;
        }
        resolve(size);
      }, reject);
    };
    if (ws.readyState === WebSocket.OPEN) start();
    else ws.onopen = start;
  });

  let chain = Promise.resolve();
  const read = (start, end) => {
    const run = chain.then(async () => {
      await ready;
      const out = new Uint8Array(end - start);
      let filled = 0;
      while (filled < out.length) {
        const want = Math.min(CHUNK, out.length - filled);
        const bytes = await request({
          cmd: "media_read",
          offset: start + filled,
          maxBytes: want,
        });
        if (!bytes || bytes.length === 0) break;
        const n = Math.min(bytes.length, out.length - filled);
        out.set(bytes.subarray(0, n), filled);
        filled += n;
      }
      if (filled !== out.length) {
        throw new Error("short movie read");
      }
      return out;
    });
    chain = run.then(
      () => {},
      () => {}
    );
    return run;
  };

  return {
    ready,
    read,
    close() {
      if (closed) return;
      closed = true;
      // Drop in-flight reads before the stop acknowledgement arrives, or a
      // read waiting for bytes treats that acknowledgement as file data.
      failAll(new Error("stopped"));
      const stop = () => {
        try {
          if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ cmd: "media_stop" }));
          }
        } catch {
          // ignore
        }
        try {
          ws.close(1000, "stop");
        } catch {
          // ignore
        }
      };
      if (ws.readyState === WebSocket.CONNECTING) ws.onopen = stop;
      else stop();
    },
  };
}
