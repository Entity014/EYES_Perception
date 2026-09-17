'use client';

import { useEffect, useRef, useState } from 'react';
import { liveWsUrl } from '../lib/api';

export default function LiveView() {
  const imageRef = useRef(null);
  const [status, setStatus] = useState('connecting');
  const [frames, setFrames] = useState(0);

  useEffect(() => {
    let activeUrl;
    let pendingBlob;
    let loading = false;
    let disposed = false;
    let displayedFrames = 0;
    let lastCountUpdate = 0;

    const showLatest = (blob) => {
      if (disposed) return;
      if (loading) {
        // Keep only the newest JPEG while the browser decodes the current one.
        pendingBlob = blob;
        return;
      }

      const image = imageRef.current;
      if (!image) return;
      loading = true;
      const nextUrl = URL.createObjectURL(blob);
      const finish = () => {
        if (disposed) {
          URL.revokeObjectURL(nextUrl);
          return;
        }
        const previousUrl = activeUrl;
        activeUrl = nextUrl;
        if (previousUrl) URL.revokeObjectURL(previousUrl);
        loading = false;
        displayedFrames += 1;
        const now = Date.now();
        if (now - lastCountUpdate >= 500) {
          lastCountUpdate = now;
          setFrames(displayedFrames);
        }
        const newestBlob = pendingBlob;
        pendingBlob = undefined;
        if (newestBlob) showLatest(newestBlob);
      };
      image.onload = finish;
      image.onerror = () => {
        URL.revokeObjectURL(nextUrl);
        loading = false;
        const newestBlob = pendingBlob;
        pendingBlob = undefined;
        if (newestBlob) showLatest(newestBlob);
      };
      image.src = nextUrl;
    };

    const socket = new WebSocket(liveWsUrl());
    socket.binaryType = 'blob';
    socket.onopen = () => setStatus('connected');
    socket.onclose = () => setStatus('disconnected');
    socket.onerror = () => setStatus('error');
    socket.onmessage = ({ data }) => showLatest(data);
    return () => {
      disposed = true;
      socket.close();
      if (activeUrl) URL.revokeObjectURL(activeUrl);
    };
  }, []);

  return <section className="viewfinder" aria-label="Live camera view">
    <div className="viewfinder-topline"><span className={`status-dot ${status}`} /> {status}<span className="frame-count">{frames.toLocaleString()} frames</span></div>
    <div className="image-stage"><img ref={imageRef} alt="Live camera feed" /></div>
  </section>;
}
