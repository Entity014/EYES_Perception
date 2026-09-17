const API_BASE = (process.env.NEXT_PUBLIC_API_BASE ?? 'http://localhost:8080').replace(/\/$/, '');

async function jsonRequest(path, options) {
  const response = await fetch(`${API_BASE}${path}`, options);
  if (!response.ok) throw new Error(`Request failed (${response.status})`);
  return response.json();
}

export function startSession() { return jsonRequest('/session/start', { method: 'POST' }); }
export function stopSession() { return jsonRequest('/session/stop', { method: 'POST' }); }
export function getSessions() { return jsonRequest('/sessions'); }
export function getSessionFrames(sessionId) { return jsonRequest(`/session/${sessionId}/frames`); }
export function frameUrl(sessionId, sequence) { return `${API_BASE}/session/${sessionId}/frame/${sequence}.jpg`; }
export function exportUrl(sessionId) { return `${API_BASE}/session/${sessionId}/export`; }
export function videoUrl(sessionId) { return `${API_BASE}/session/${sessionId}/video.mp4`; }
export function videoPreviewUrl(sessionId) { return `${API_BASE}/session/${sessionId}/preview.mp4`; }
export function aviUrl(sessionId) { return `${API_BASE}/session/${sessionId}/video.avi`; }

export async function setResolution(size) {
  try {
    const response = await fetch(`${API_BASE}/resolution`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: size,
    });
    return response.ok;
  } catch (error) {
    console.error('setResolution failed:', error);
    return false;
  }
}

export async function setColorMode(mode) {
  try {
    const response = await fetch(`${API_BASE}/colormode`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: mode,
    });
    return response.ok;
  } catch (error) {
    console.error('setColorMode failed:', error);
    return false;
  }
}

export async function setBrightness(value) {
  try {
    const response = await fetch(`${API_BASE}/brightness`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: String(value),
    });
    return response.ok;
  } catch (error) {
    console.error('setBrightness failed:', error);
    return false;
  }
}

export function liveWsUrl() { return API_BASE.replace(/^http/, 'ws') + '/live'; }
