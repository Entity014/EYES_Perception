'use client';

import { useState } from 'react';
import { setBrightness, setColorMode, setResolution, startSession, stopSession } from '../lib/api';

export default function ControlsPanel() {
  const [recording, setRecording] = useState(false);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('Ready');
  const [brightness, setBrightnessValue] = useState(1);

  async function toggleRecord() {
    setBusy(true);
    try {
      const session = recording ? await stopSession() : await startSession();
      setRecording(!recording);
      setMessage(recording ? `Session ${session.id} uploading` : `Session ${session.id} recording`);
    } catch (error) { setMessage(error.message); }
    finally { setBusy(false); }
  }

  async function changeResolution(event) {
    try {
      const ok = await setResolution(event.target.value);
      setMessage(ok ? 'Resolution updated' : 'Resolution update failed');
    } catch (error) {
      setMessage(`Resolution update failed: ${error.message}`);
    }
  }

  async function changeColor(event) {
    try {
      const ok = await setColorMode(event.target.checked ? 'gray' : 'color');
      setMessage(ok ? 'Color mode updated' : 'Color mode update failed');
    } catch (error) {
      setMessage(`Color mode update failed: ${error.message}`);
    }
  }

  async function changeBrightness(event) {
    const value = Number(event.target.value);
    setBrightnessValue(value);
    const ok = await setBrightness(value);
    setMessage(ok ? 'Brightness updated' : 'Brightness update failed');
  }

  return <section className="control-panel" aria-label="Capture controls">
    <button className={`record-button ${recording ? 'recording' : ''}`} onClick={toggleRecord} disabled={busy}>
      <span className="record-glyph" /> {recording ? 'Stop recording' : 'Record session'}
    </button>
    <label>Resolution<select defaultValue="svga" onChange={changeResolution}><option value="vga">VGA · 640 x 480</option><option value="svga">SVGA · 800 x 600</option><option value="uxga">UXGA · 1600 x 1200</option></select></label>
    <label>Brightness<input type="range" min="-2" max="2" step="1" value={brightness} onChange={changeBrightness} /><span>{brightness > 0 ? `+${brightness}` : brightness}</span></label>
    <label className="toggle-label"><input type="checkbox" onChange={changeColor} /> <span>Grayscale</span></label>
    <span className="control-message">{message}</span>
  </section>;
}
