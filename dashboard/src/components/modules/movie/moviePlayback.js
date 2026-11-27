// Plays MP4, MOV, WebM, and MKV by demuxing in the browser. Video uses
// WebCodecs. AC-3 and E-AC-3 use the Dolby decoder registered above.
// The file itself stays on the device and is read by range.

import { registerAc3Decoder } from "@mediabunny/ac3";
import {
  AudioBufferSink,
  CanvasSink,
  CustomSource,
  Input,
  MatroskaInputFormat,
  Mp4InputFormat,
  QuickTimeInputFormat,
  WebMInputFormat,
} from "mediabunny";

// Chrome has no Dolby decoder. This registers AC-3 and E-AC-3 for Mediabunny.
registerAc3Decoder();

const FORMATS = [
  new Mp4InputFormat(),
  new QuickTimeInputFormat(),
  new MatroskaInputFormat(),
  new WebMInputFormat(),
];

function drawFrame(canvas, ctx, source) {
  if (!source) return;
  if (canvas.width !== source.width || canvas.height !== source.height) {
    canvas.width = source.width;
    canvas.height = source.height;
  }
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(source, 0, 0);
}

export function createMoviePlayback(canvas, hooks = {}) {
  const ctx = canvas.getContext("2d");
  let loadId = 0;
  let disposed = true;
  let input = null;
  let file = null;
  let audioContext = null;
  let gain = null;
  let videoSink = null;
  let audioSink = null;
  let videoIter = null;
  let audioIter = null;
  let nextFrame = null;
  let playing = false;
  let playbackTimeAtStart = 0;
  let audioStart = 0;
  let firstTimestamp = 0;
  let endTimestamp = 0;
  let asyncId = 0;
  let raf = 0;
  let volume = 0.7;
  const queued = new Set();

  function getTime() {
    if (playing && audioContext) {
      return audioContext.currentTime - audioStart + playbackTimeAtStart;
    }
    return playbackTimeAtStart;
  }

  function stopNodes() {
    for (const node of queued) {
      try {
        node.stop();
      } catch {
        // already stopped
      }
    }
    queued.clear();
  }

  function disposePlayback() {
    disposed = true;
    playing = false;
    asyncId += 1;
    if (raf) cancelAnimationFrame(raf);
    raf = 0;
    const quiet = (result) => {
      if (result && typeof result.catch === "function") result.catch(() => {});
    };
    quiet(videoIter?.return?.());
    quiet(audioIter?.return?.());
    videoIter = null;
    audioIter = null;
    nextFrame = null;
    stopNodes();
    const current = input;
    input = null;
    videoSink = null;
    audioSink = null;
    try {
      current?.dispose();
    } catch {
      // ignore
    }
    try {
      audioContext?.close();
    } catch {
      // ignore
    }
    audioContext = null;
    gain = null;
    const open = file;
    file = null;
    try {
      open?.close();
    } catch {
      // ignore
    }
    if (ctx && canvas.width) ctx.clearRect(0, 0, canvas.width, canvas.height);
  }

  function stopping(error) {
    return (
      disposed ||
      error?.message === "stopped" ||
      error?.name === "InputDisposedError"
    );
  }

  async function updateNext() {
    const id = asyncId;
    try {
      while (videoIter && !disposed) {
        const frame = (await videoIter.next()).value ?? null;
        if (id !== asyncId || disposed) return;
        if (!frame) return;
        if (frame.timestamp <= getTime()) drawFrame(canvas, ctx, frame.canvas);
        else {
          nextFrame = frame;
          return;
        }
      }
    } catch (error) {
      if (stopping(error) || id !== asyncId) return;
      hooks.onError?.(error);
    }
  }

  async function startVideo() {
    if (!videoSink) return;
    const id = ++asyncId;
    try {
      if (videoIter) await videoIter.return?.();
    } catch {
      // The previous frame iterator is being replaced.
    }
    if (id !== asyncId || disposed) return;
    try {
      videoIter = videoSink.canvases(getTime());
      const first = (await videoIter.next()).value ?? null;
      if (id !== asyncId || disposed) return;
      const second = (await videoIter.next()).value ?? null;
      if (id !== asyncId || disposed) return;
      nextFrame = second;
      if (first) drawFrame(canvas, ctx, first.canvas);
    } catch (error) {
      if (stopping(error) || id !== asyncId) return;
      hooks.onError?.(error);
    }
  }

  function render() {
    if (disposed) return;
    const time = getTime();
    if (playing && endTimestamp > 0 && time >= endTimestamp) {
      playing = false;
      playbackTimeAtStart = endTimestamp;
      if (audioIter) audioIter.return?.();
      audioIter = null;
      stopNodes();
      hooks.onEnded?.();
    }
    if (nextFrame && nextFrame.timestamp <= getTime()) {
      drawFrame(canvas, ctx, nextFrame.canvas);
      nextFrame = null;
      void updateNext();
    }
    hooks.onTime?.(getTime(), Math.max(0, endTimestamp - firstTimestamp));
    raf = requestAnimationFrame(render);
  }

  function waitForHeadroom(timestamp, id) {
    return new Promise((resolve) => {
      const timer = setInterval(() => {
        if (id !== asyncId || !playing || timestamp - getTime() < 1) {
          clearInterval(timer);
          resolve();
        }
      }, 100);
    });
  }

  async function runAudio(id) {
    if (!audioSink || !audioIter || !audioContext || !gain) return;
    try {
      for await (const { buffer, timestamp } of audioIter) {
        if (id !== asyncId || !playing || disposed) return;
        const node = audioContext.createBufferSource();
        node.buffer = buffer;
        node.connect(gain);
        let when = audioStart + timestamp - playbackTimeAtStart;
        when = Math.round(audioContext.sampleRate * when) / audioContext.sampleRate;
        if (when >= audioContext.currentTime) node.start(when);
        else node.start(audioContext.currentTime, Math.max(0, audioContext.currentTime - when));
        queued.add(node);
        node.onended = () => queued.delete(node);
        if (timestamp - getTime() >= 1) await waitForHeadroom(timestamp, id);
      }
    } catch (error) {
      if (id === asyncId && !stopping(error)) hooks.onError?.(error);
    }
  }

  return {
    async load(fileHandle) {
      const id = ++loadId;
      disposePlayback();
      if (id !== loadId) return;
      disposed = false;
      file = fileHandle;
      try {
        if (typeof VideoDecoder === "undefined") {
          throw new Error("This browser cannot decode video.");
        }
        const size = await fileHandle.ready;
        if (id !== loadId) return;
        input = new Input({
          source: new CustomSource({
            getSize: () => size,
            read: (start, end) => fileHandle.read(start, end),
            prefetchProfile: "network",
          }),
          formats: FORMATS,
        });
        let videoTrack = await input.getPrimaryVideoTrack();
        let audioTrack = await input.getPrimaryAudioTrack();
        if (id !== loadId) return;
        const tracks = [videoTrack, audioTrack].filter(Boolean);
        if (!tracks.length) throw new Error("No audio or video track found.");
        firstTimestamp = Math.max(await input.getFirstTimestamp(tracks), 0);
        endTimestamp =
          (await input.getDurationFromMetadata(tracks, { skipLiveWait: true })) ??
          (await input.computeDuration(tracks));
        endTimestamp = Math.max(firstTimestamp, endTimestamp || 0);
        playbackTimeAtStart = firstTimestamp;
        if (id !== loadId) return;

        const warnings = [];
        if (videoTrack && !(await videoTrack.canDecode())) {
          warnings.push("This video codec cannot be played in this browser.");
          videoTrack = null;
        }
        if (audioTrack && !(await audioTrack.canDecode())) {
          warnings.push("Audio codec is not supported, so this movie plays without sound.");
          audioTrack = null;
        }
        if (!videoTrack && !audioTrack) {
          throw new Error(warnings.join(" ") || "Nothing in this file can be played.");
        }
        if (id !== loadId) return;

        const rate = audioTrack ? await audioTrack.getSampleRate() : 0;
        const Ctx = window.AudioContext || window.webkitAudioContext;
        try {
          audioContext = rate ? new Ctx({ sampleRate: rate }) : new Ctx();
        } catch {
          audioContext = new Ctx();
        }
        gain = audioContext.createGain();
        gain.gain.value = volume;
        gain.connect(audioContext.destination);
        if (videoTrack) {
          videoSink = new CanvasSink(videoTrack, { poolSize: 2, fit: "contain" });
        }
        if (audioTrack) audioSink = new AudioBufferSink(audioTrack);
        await startVideo();
        if (id !== loadId) return;
        if (!raf) raf = requestAnimationFrame(render);
        hooks.onReady?.({
          duration: Math.max(0, endTimestamp - firstTimestamp),
          warning: warnings.join(" "),
        });
      } catch (error) {
        if (id !== loadId) return;
        disposePlayback();
        throw error;
      }
    },

    async play() {
      if (!audioContext || disposed) return;
      if (audioContext.state === "suspended") await audioContext.resume();
      if (endTimestamp > 0 && getTime() >= endTimestamp - 0.05) {
        playbackTimeAtStart = firstTimestamp;
        await startVideo();
      }
      audioStart = audioContext.currentTime;
      playing = true;
      if (audioSink) {
        if (audioIter) await audioIter.return?.();
        audioIter = audioSink.buffers(getTime());
        void runAudio(asyncId);
      }
      if (videoSink && !nextFrame) void updateNext();
    },

    pause() {
      playbackTimeAtStart = getTime();
      playing = false;
      if (audioIter) audioIter.return?.();
      audioIter = null;
      stopNodes();
    },

    async seek(seconds) {
      const wasPlaying = playing;
      playbackTimeAtStart = Math.min(
        Math.max(Number(seconds) || 0, firstTimestamp),
        endTimestamp || firstTimestamp
      );
      playing = false;
      if (audioIter) audioIter.return?.();
      audioIter = null;
      stopNodes();
      await startVideo();
      if (wasPlaying) await this.play();
    },

    setVolume(value) {
      volume = Math.max(0, Math.min(1, Number(value) || 0));
      if (gain) gain.gain.value = volume;
    },

    isPlaying() {
      return playing;
    },

    stop() {
      loadId += 1;
      disposePlayback();
    },
  };
}
