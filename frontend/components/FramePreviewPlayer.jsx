'use client';

import { useEffect, useRef, useState } from 'react';
import { computeHoldMs } from '../lib/playback';
import { aviUrl, exportUrl, frameUrl, getSessionFrames, videoPreviewUrl, videoUrl } from '../lib/api';

export default function FramePreviewPlayer({ sessionId }) {
  const [frames, setFrames] = useState([]);
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [error, setError] = useState('');
  const timerRef = useRef(null);

  useEffect(() => {
    setIndex(0); setPlaying(false); setError('');
    getSessionFrames(sessionId).then(setFrames).catch((reason) => setError(reason.message));
  }, [sessionId]);

  useEffect(() => {
    if (!playing || !frames.length) return undefined;
    timerRef.current = setTimeout(() => setIndex((current) => (current + 1) % frames.length), computeHoldMs(frames, index));
    return () => clearTimeout(timerRef.current);
  }, [playing, index, frames]);

  if (error) return <p className="error-message">{error}</p>;
  if (!frames.length) return <div className="preview-panel"><div className="preview-stage"><video controls preload="metadata" src={videoPreviewUrl(sessionId)}>Your browser does not support MP4 video.</video></div><p className="empty-message">Video-only session. <a className="export-link" href={videoUrl(sessionId)}>Download MP4</a>{' '}<a className="export-link" href={aviUrl(sessionId)}>Download AVI</a></p></div>;
  const frame = frames[index];
  return <div className="preview-panel">
    <div className="preview-stage"><img src={frameUrl(sessionId, frame.sequence)} alt={`Frame ${frame.sequence}`} /></div>
    <div className="player-controls"><button onClick={() => setPlaying((value) => !value)}>{playing ? 'Pause' : 'Play'}</button><input aria-label="Frame position" type="range" min="0" max={frames.length - 1} value={index} onChange={(event) => { setPlaying(false); setIndex(Number(event.target.value)); }} /><span>{index + 1} / {frames.length}</span></div>
    <a className="export-link" href={exportUrl(sessionId)}>Download ZIP + CSV</a>{' '}
    <a className="export-link" href={videoUrl(sessionId)}>Download MP4</a>{' '}
    <a className="export-link" href={aviUrl(sessionId)}>Download AVI</a>
  </div>;
}
