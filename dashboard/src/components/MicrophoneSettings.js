import React, { useState, useEffect } from "react";
import {
  Box,
  Button,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  FormControl,
  FormControlLabel,
  InputLabel,
  MenuItem,
  Select,
  Switch,
  Typography,
  Slider,
} from "@mui/material";

const MicrophoneSettings = ({ open, onClose, config, saveConfig }) => {
  const [enabled, setEnabled] = useState(true);
  const [sampleRate, setSampleRate] = useState(16000);
  const [gain, setGain] = useState(1.0);
  const [autoGain, setAutoGain] = useState(true);
  const [noiseReduction, setNoiseReduction] = useState(true);

  useEffect(() => {
    if (config && config.microphone) {
      setEnabled(config.microphone.enabled || true);
      setSampleRate(config.microphone.sampleRate || 16000);
      setGain(config.microphone.gain || 1.0);
      setAutoGain(config.microphone.autoGain !== false);
      setNoiseReduction(config.microphone.noiseReduction !== false);
    }
  }, [config]);

  const handleSave = () => {
    const configToSave = {
      ...config,
      microphone: {
        enabled: enabled,
        sampleRate: sampleRate,
        gain: gain,
        autoGain: autoGain,
        noiseReduction: noiseReduction,
      },
    };

    saveConfig(configToSave);
    onClose();
  };

  const handleCloseModal = () => {
    // Reset to original values
    if (config && config.microphone) {
      setEnabled(config.microphone.enabled || true);
      setSampleRate(config.microphone.sampleRate || 16000);
      setGain(config.microphone.gain || 1.0);
      setAutoGain(config.microphone.autoGain !== false);
      setNoiseReduction(config.microphone.noiseReduction !== false);
    }
    onClose();
  };

  return (
    <Dialog open={open} onClose={handleCloseModal} maxWidth="md" fullWidth>
      <DialogTitle>
        <Typography variant="h6" component="div">
          🎤 XIAO ESP32S3 Sense Microphone Settings
        </Typography>
      </DialogTitle>
      <DialogContent>
        <Box sx={{ display: "flex", flexDirection: "column", gap: 3, mt: 2 }}>
          {/* Microphone Info */}
          <Box sx={{ p: 2, bgcolor: "info.light", borderRadius: 1 }}>
            <Typography variant="body2" color="info.dark">
              <strong>XIAO ESP32S3 Sense PDM Microphone</strong>
              <br />
              • Built-in PDM microphone on GPIO 41 (data) and GPIO 42 (clock)
              <br />
              • Supports real-time audio processing and streaming
              <br />• Configurable gain, auto-gain, and noise reduction
            </Typography>
          </Box>

          {/* Enable/Disable */}
          <FormControlLabel
            control={
              <Switch
                checked={enabled}
                onChange={(e) => setEnabled(e.target.checked)}
              />
            }
            label="Enable Microphone"
          />

          {/* Sample Rate */}
          <FormControl fullWidth>
            <InputLabel>Sample Rate</InputLabel>
            <Select
              value={sampleRate}
              label="Sample Rate"
              onChange={(e) => setSampleRate(e.target.value)}
              disabled={!enabled}
            >
              <MenuItem value={8000}>8 kHz (Voice)</MenuItem>
              <MenuItem value={16000}>16 kHz (Speech)</MenuItem>
              <MenuItem value={44100}>44.1 kHz (Music)</MenuItem>
            </Select>
          </FormControl>

          {/* Gain Control */}
          <Box>
            <Typography gutterBottom>Audio Gain: {gain.toFixed(1)}x</Typography>
            <Slider
              value={gain}
              onChange={(e, newValue) => setGain(newValue)}
              min={0.1}
              max={10.0}
              step={0.1}
              disabled={!enabled}
              marks={[
                { value: 0.1, label: "0.1x" },
                { value: 1.0, label: "1.0x" },
                { value: 5.0, label: "5.0x" },
                { value: 10.0, label: "10.0x" },
              ]}
            />
          </Box>

          {/* Auto Gain */}
          <FormControlLabel
            control={
              <Switch
                checked={autoGain}
                onChange={(e) => setAutoGain(e.target.checked)}
                disabled={!enabled}
              />
            }
            label="Auto Gain Control (automatically adjusts gain based on audio levels)"
          />

          {/* Noise Reduction */}
          <FormControlLabel
            control={
              <Switch
                checked={noiseReduction}
                onChange={(e) => setNoiseReduction(e.target.checked)}
                disabled={!enabled}
              />
            }
            label="Noise Reduction (applies high-pass filter to reduce background noise)"
          />
        </Box>
      </DialogContent>
      <DialogActions>
        <Button onClick={handleCloseModal} color="secondary">
          Cancel
        </Button>
        <Button onClick={handleSave} variant="contained" color="primary">
          Save Settings
        </Button>
      </DialogActions>
    </Dialog>
  );
};

export default MicrophoneSettings;
