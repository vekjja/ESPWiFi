// Split a media-socket byte stream into pieces decodeAudioData can play,
// and schedule those pieces as they arrive.

const BITRATES_V1_L3 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0];
const BITRATES_V2_L3 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0];
const SAMPLE_RATES = {
  3: [44100, 48000, 32000],
  2: [22050, 24000, 16000],
  0: [11025, 12000, 8000],
};

export function estimateTrack(bytes, fileSize) {
  if (!bytes || bytes.length < 4 || !fileSize) return null;
  const id3 = id3Size(bytes);
  if (id3 > bytes.length) return null;
  let i = id3;
  while (i + 4 <= bytes.length && !isSync(bytes, i)) i += 1;
  if (i + 4 > bytes.length || !frameLength(bytes, i)) return null;
  const version = (bytes[i + 1] >> 3) & 3;
  const bitrateIdx = (bytes[i + 2] >> 4) & 0xf;
  const srIdx = (bytes[i + 2] >> 2) & 3;
  const rates = SAMPLE_RATES[version];
  const table = version === 3 ? BITRATES_V1_L3 : BITRATES_V2_L3;
  const bitrate = (table[bitrateIdx] || 0) * 1000;
  const sampleRate = rates ? rates[srIdx] || 0 : 0;
  if (!bitrate) return null;
  const audioBytes = Math.max(0, fileSize - i);
  return {
    durationMs: Math.round((audioBytes * 8 * 1000) / bitrate),
    bitrate,
    sampleRate,
  };
}

export function mimeForName(name) {
  const lower = String(name || "").toLowerCase();
  if (lower.endsWith(".wav")) return "audio/wav";
  if (lower.endsWith(".ogg")) return "audio/ogg";
  if (lower.endsWith(".m4a") || lower.endsWith(".aac")) return "audio/mp4";
  if (lower.endsWith(".flac")) return "audio/flac";
  return "audio/mpeg";
}

function isIos() {
  const ua = navigator.userAgent || "";
  if (/iPad|iPhone|iPod/.test(ua)) return true;
  return navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1;
}

export function canUseMse(mime) {
  // iOS will not play audio/mpeg through MediaSource. It decodes the bytes.
  if (isIos()) return false;
  const MS = window.MediaSource || window.ManagedMediaSource;
  return Boolean(
    MS &&
      typeof MS.isTypeSupported === "function" &&
      MS.isTypeSupported(mime)
  );
}

function concat(a, b) {
  if (!a || a.length === 0) return b || new Uint8Array(0);
  if (!b || b.length === 0) return a;
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0);
  out.set(b, a.length);
  return out;
}

function id3Size(buf) {
  if (buf.length < 10) return 0;
  if (buf[0] !== 0x49 || buf[1] !== 0x44 || buf[2] !== 0x33) return 0;
  const size =
    ((buf[6] & 0x7f) << 21) |
    ((buf[7] & 0x7f) << 14) |
    ((buf[8] & 0x7f) << 7) |
    (buf[9] & 0x7f);
  const footer = buf[5] & 0x10 ? 10 : 0;
  return 10 + size + footer;
}

function isSync(buf, i) {
  return buf[i] === 0xff && (buf[i + 1] & 0xe0) === 0xe0;
}

function frameLength(buf, i) {
  if (i + 4 > buf.length) return 0;
  const version = (buf[i + 1] >> 3) & 3;
  const layer = (buf[i + 1] >> 1) & 3;
  const bitrateIdx = (buf[i + 2] >> 4) & 0xf;
  const srIdx = (buf[i + 2] >> 2) & 3;
  const padding = (buf[i + 2] >> 1) & 1;
  if (version === 1 || layer !== 1 || bitrateIdx === 0 || bitrateIdx === 15 || srIdx === 3) {
    return 0;
  }
  const rates = SAMPLE_RATES[version];
  if (!rates) return 0;
  const sampleRate = rates[srIdx];
  const kbps = (version === 3 ? BITRATES_V1_L3 : BITRATES_V2_L3)[bitrateIdx];
  if (!kbps || !sampleRate) return 0;
  const coeff = version === 3 ? 144 : 72;
  return Math.floor((coeff * kbps * 1000) / sampleRate) + padding;
}

export function splitMp3(buf) {
  if (!buf || buf.length < 4) return { ready: null, rest: buf || new Uint8Array(0) };
  const id3 = id3Size(buf);
  if (id3 > buf.length) return { ready: null, rest: buf };

  let i = id3;
  while (i + 4 <= buf.length && !isSync(buf, i)) i += 1;
  if (i + 4 > buf.length) {
    const keepFrom = id3 > 0 ? 0 : Math.max(0, buf.length - 3);
    return { ready: null, rest: buf.subarray(keepFrom) };
  }

  let first = i;
  const firstLen = frameLength(buf, first);
  if (firstLen && first + firstLen <= buf.length) {
    const frame = buf.subarray(first, first + Math.min(firstLen, 160));
    const xing = frame.findIndex(
      (_, idx) =>
        idx + 4 <= frame.length &&
        frame[idx] === 0x58 &&
        frame[idx + 1] === 0x69 &&
        frame[idx + 2] === 0x6e &&
        frame[idx + 3] === 0x67
    );
    const info =
      xing < 0
        ? frame.findIndex(
            (_, idx) =>
              idx + 4 <= frame.length &&
              frame[idx] === 0x49 &&
              frame[idx + 1] === 0x6e &&
              frame[idx + 2] === 0x66 &&
              frame[idx + 3] === 0x6f
          )
        : xing;
    if (info >= 0) first += firstLen;
  }

  i = first;
  let end = first;
  while (i + 4 <= buf.length && isSync(buf, i)) {
    const len = frameLength(buf, i);
    if (!len || i + len > buf.length) break;
    i += len;
    end = i;
  }
  if (end <= first) return { ready: null, rest: buf.subarray(Math.max(0, first)) };
  return { ready: buf.subarray(first, end), rest: buf.subarray(end) };
}

function readLe16(buf, off) {
  return buf[off] | (buf[off + 1] << 8);
}

function readLe32(buf, off) {
  return (
    buf[off] |
    (buf[off + 1] << 8) |
    (buf[off + 2] << 16) |
    (buf[off + 3] << 24)
  ) >>> 0;
}

function wrapWav(pcm, fmt) {
  const block = fmt.channels * (fmt.bits >> 3);
  const dataSize = pcm.length - (pcm.length % block);
  if (dataSize <= 0) return null;
  const out = new Uint8Array(44 + dataSize);
  const view = new DataView(out.buffer);
  out.set([0x52, 0x49, 0x46, 0x46, 0, 0, 0, 0, 0x57, 0x41, 0x56, 0x45], 0);
  out.set([0x66, 0x6d, 0x74, 0x20], 12);
  view.setUint32(4, 36 + dataSize, true);
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, fmt.channels, true);
  view.setUint32(24, fmt.sampleRate, true);
  view.setUint32(28, fmt.sampleRate * block, true);
  view.setUint16(32, block, true);
  view.setUint16(34, fmt.bits, true);
  out.set([0x64, 0x61, 0x74, 0x61], 36);
  view.setUint32(40, dataSize, true);
  out.set(pcm.subarray(0, dataSize), 44);
  return { ready: out, rest: pcm.subarray(dataSize), fmt };
}

export function splitWav(buf, fmtState) {
  if (!buf || buf.length === 0) return { ready: null, rest: buf || new Uint8Array(0), fmt: fmtState };
  if (!fmtState) {
    if (buf.length < 12) return { ready: null, rest: buf, fmt: null };
    const riff =
      buf[0] === 0x52 && buf[1] === 0x49 && buf[2] === 0x46 && buf[3] === 0x46;
    if (!riff) return { ready: null, rest: new Uint8Array(0), fmt: null };
    let off = 12;
    let fmt = null;
    while (off + 8 <= buf.length) {
      const id = String.fromCharCode(buf[off], buf[off + 1], buf[off + 2], buf[off + 3]);
      const size = readLe32(buf, off + 4);
      if (id === "fmt " && off + 8 + 16 <= buf.length) {
        fmt = {
          channels: readLe16(buf, off + 10),
          sampleRate: readLe32(buf, off + 12),
          bits: readLe16(buf, off + 22),
        };
      }
      if (id === "data") {
        const data = buf.subarray(off + 8);
        if (!fmt || !fmt.channels || !fmt.bits) {
          return { ready: null, rest: buf, fmt: null };
        }
        const wrapped = wrapWav(data, fmt);
        if (!wrapped) return { ready: null, rest: buf, fmt };
        return { ready: wrapped.ready, rest: wrapped.rest, fmt };
      }
      off += 8 + size + (size & 1);
    }
    return { ready: null, rest: buf, fmt };
  }
  const wrapped = wrapWav(buf, fmtState);
  if (!wrapped) return { ready: null, rest: buf, fmt: fmtState };
  return wrapped;
}

function decodeBuffer(ctx, bytes) {
  const copy = bytes.slice().buffer;
  return new Promise((resolve, reject) => {
    let settled = false;
    const ok = (value) => {
      if (settled) return;
      settled = true;
      resolve(value);
    };
    const bad = (error) => {
      if (settled) return;
      settled = true;
      reject(error || new Error("decode failed"));
    };
    try {
      const pending = ctx.decodeAudioData(copy, ok, bad);
      if (pending && typeof pending.then === "function") pending.then(ok, bad);
    } catch (error) {
      bad(error);
    }
  });
}

function plausibleDuration(byteLength, audioBuffer) {
  if (!audioBuffer || !(audioBuffer.duration > 0.05)) return false;
  const bps = (byteLength * 8) / audioBuffer.duration;
  return bps >= 8000 && bps <= 400000;
}

export function createPcmPlayer({ ctx, gain, onTime, onStart, onEnded, onFallback }) {
  let pieces = [];
  let nodes = [];
  let nextWhen = 0;
  let origin = 0;
  let playhead = 0;
  let base = 0;
  let segmentStart = 0;
  let scheduled = 0;
  let latest = null;
  let raw = new Uint8Array(0);
  let carry = new Uint8Array(0);
  let wavFmt = null;
  let mime = "audio/mpeg";
  let chain = Promise.resolve();
  let epoch = 0;
  let eof = false;
  let paused = false;
  let stopped = false;
  let started = false;
  let ended = false;
  let warned = false;
  let decodedAt = 0;
  let timer = 0;

  const position = () => {
    if (paused || nodes.length === 0) return playhead;
    const value = playhead + (ctx.currentTime - origin);
    return value < 0 ? 0 : value;
  };

  const stopTimer = () => {
    if (timer) cancelAnimationFrame(timer);
    timer = 0;
  };

  const tick = () => {
    stopTimer();
    if (stopped || paused) return;
    timer = requestAnimationFrame(() => {
      if (stopped || paused) return;
      onTime(position());
      if (nodes.length) tick();
    });
  };

  const stopNodes = () => {
    const current = nodes;
    nodes = [];
    current.forEach((node) => {
      node.onended = null;
      try {
        node.stop();
      } catch {
        // already stopped
      }
    });
  };

  const finishIfDrained = () => {
    if (stopped || paused || ended || !eof || nodes.length > 0) return;
    ended = true;
    stopTimer();
    onEnded();
  };

  const schedule = (buffer, trackStart, skip = 0) => {
    if (stopped || paused) return;
    const playable = buffer.duration - skip;
    if (!(playable > 0.02)) return;
    if (ctx.state !== "running") {
      const pending = ctx.resume();
      if (pending && typeof pending.catch === "function") pending.catch(() => {});
    }
    const src = ctx.createBufferSource();
    src.buffer = buffer;
    src.connect(gain);
    const now = ctx.currentTime;
    if (nodes.length === 0) {
      playhead = trackStart + skip;
      origin = now;
      nextWhen = now + 0.03;
      if (!started) {
        started = true;
        onStart();
      }
      tick();
    }
    const when = Math.max(nextWhen, now + 0.01);
    try {
      src.start(when, skip);
    } catch {
      return;
    }
    nextWhen = when + playable;
    src.onended = () => {
      nodes = nodes.filter((node) => node !== src);
      finishIfDrained();
    };
    nodes.push(src);
  };

  const addBuffer = (audioBuffer) => {
    const start = base;
    base += audioBuffer.duration;
    pieces.push({ start, buffer: audioBuffer });
    schedule(audioBuffer, start, 0);
  };

  // Phones reject a bare MP3 frame. Decode the original bytes gathered so
  // far, and only schedule the part we have not already queued.
  const adopt = (audioBuffer) => {
    latest = audioBuffer;
    if (paused || stopped) {
      scheduled = audioBuffer.duration;
      return;
    }
    const fresh = audioBuffer.duration - scheduled;
    if (fresh < 0.05) return;
    const skip = scheduled;
    scheduled = audioBuffer.duration;
    schedule(audioBuffer, segmentStart, skip);
  };

  const resumeCtx = async () => {
    if (ctx.state === "running") return;
    try {
      await ctx.resume();
    } catch {
      // The tap already resumed this context. A later resume can fail.
    }
  };

  return {
    push(bytes, nextMime) {
      if (stopped || !bytes || bytes.length === 0) return;
      if (nextMime) mime = nextMime;
      const copy = bytes.slice();
      const ticket = epoch;
      chain = chain
        .then(async () => {
          if (stopped || ticket !== epoch) return;
          if (mime === "audio/wav") {
            carry = concat(carry, copy);
            const split = splitWav(carry, wavFmt);
            wavFmt = split.fmt || wavFmt;
            carry = split.rest || new Uint8Array(0);
            if (!split.ready || split.ready.length < 64) return;
            const audioBuffer = await decodeBuffer(ctx, split.ready);
            if (stopped || ticket !== epoch) return;
            addBuffer(audioBuffer);
            return;
          }
          raw = concat(raw, copy);
          if (!eof && raw.length < 2048) return;
          if (!eof && scheduled > 0 && raw.length - decodedAt < 32768) return;
          decodedAt = raw.length;
          await resumeCtx();
          let audioBuffer = null;
          try {
            audioBuffer = await decodeBuffer(ctx, raw);
          } catch {
            audioBuffer = null;
          }
          if (stopped || ticket !== epoch) return;
          if (!audioBuffer || (!eof && !plausibleDuration(raw.length, audioBuffer))) {
            if (!warned) {
              warned = true;
              console.warn("🎵 Waiting for more audio before decode");
            }
            return;
          }
          adopt(audioBuffer);
        })
        .catch((error) => {
          console.error("🎵 Stream decode failed:", error);
        });
    },
    markEof() {
      chain = chain.then(async () => {
        if (stopped) return;
        eof = true;
        if (mime !== "audio/wav" && raw.length > 0) {
          await resumeCtx();
          try {
            const audioBuffer = await decodeBuffer(ctx, raw);
            if (!stopped && audioBuffer && audioBuffer.duration > 0.05) {
              adopt(audioBuffer);
            } else if (!audioBuffer) {
              console.error("🎵 Could not decode audio");
            }
          } catch (error) {
            console.error("🎵 Could not decode audio:", error);
          }
        }
        if (
          raw.length > 0 &&
          typeof onFallback === "function" &&
          (!started || ctx.state !== "running")
        ) {
          stopNodes();
          onFallback(raw, mime);
          return;
        }
        finishIfDrained();
      });
    },
    pause() {
      if (paused || stopped) return;
      playhead = position();
      paused = true;
      stopTimer();
      stopNodes();
    },
    resume() {
      if (stopped || !paused) return;
      paused = false;
      ended = false;
      nextWhen = 0;
      const pending = ctx.resume();
      if (pending && typeof pending.catch === "function") pending.catch(() => {});
      let startedPiece = false;
      if (latest) {
        const skip = Math.max(0, playhead - segmentStart);
        if (skip < latest.duration - 0.02) {
          schedule(latest, segmentStart, skip);
          startedPiece = true;
        }
      } else {
        pieces.forEach((piece) => {
          const end = piece.start + piece.buffer.duration;
          if (end <= playhead + 0.01) return;
          const skip = Math.max(0, playhead - piece.start);
          schedule(piece.buffer, piece.start, skip);
          startedPiece = true;
        });
      }
      if (!startedPiece) tick();
    },
    seek(seconds) {
      const wasPaused = paused;
      epoch += 1;
      stopNodes();
      stopTimer();
      pieces = [];
      latest = null;
      scheduled = 0;
      segmentStart = seconds;
      raw = new Uint8Array(0);
      carry = new Uint8Array(0);
      wavFmt = null;
      base = seconds;
      playhead = seconds;
      origin = ctx.currentTime;
      nextWhen = 0;
      eof = false;
      ended = false;
      warned = false;
      decodedAt = 0;
      paused = wasPaused;
      onTime(seconds);
    },
    stop() {
      stopped = true;
      paused = true;
      stopTimer();
      stopNodes();
      pieces = [];
      latest = null;
      raw = new Uint8Array(0);
      carry = new Uint8Array(0);
    },
    setVolume(value) {
      gain.gain.value = value;
    },
  };
}

let silentWavUrl = "";

function silentWav() {
  if (silentWavUrl) return silentWavUrl;
  const dataSize = 16;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);
  const write = (offset, text) => {
    for (let i = 0; i < text.length; i += 1) {
      view.setUint8(offset + i, text.charCodeAt(i));
    }
  };
  write(0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  write(8, "WAVE");
  write(12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, 8000, true);
  view.setUint32(28, 8000, true);
  view.setUint16(32, 1, true);
  view.setUint16(34, 8, true);
  write(36, "data");
  view.setUint32(40, dataSize, true);
  silentWavUrl = URL.createObjectURL(new Blob([buffer], { type: "audio/wav" }));
  return silentWavUrl;
}

// iOS ignores Web Audio until this tap has started an actual media element
// and a buffer is running on the context. Both have to happen before any await.
export function primeAudioElement(audio) {
  if (!audio) return;
  try {
    audio.src = silentWav();
    const pending = audio.play();
    if (pending && typeof pending.catch === "function") pending.catch(() => {});
  } catch {
    // ignore
  }
}

export function unlockContext(ctx, gain) {
  if (!ctx) return;
  const pending = ctx.resume();
  if (pending && typeof pending.catch === "function") pending.catch(() => {});
  if (gain) {
    try {
      gain.disconnect();
    } catch {
      // already disconnected
    }
    gain.connect(ctx.destination);
  }
  if (ctx.__espwifiKeeper) return;
  try {
    const rate = ctx.sampleRate || 44100;
    const buffer = ctx.createBuffer(1, rate, rate);
    const channel = buffer.getChannelData(0);
    for (let i = 0; i < channel.length; i += 128) channel[i] = 0.00002;
    const src = ctx.createBufferSource();
    src.buffer = buffer;
    src.loop = true;
    src.connect(ctx.destination);
    src.start();
    ctx.__espwifiKeeper = src;
  } catch {
    // ignore
  }
}
