'use client';

import Link from 'next/link';
import { useState } from 'react';
import FramePreviewPlayer from '../../components/FramePreviewPlayer';
import SessionList from '../../components/SessionList';

export default function LogPage() {
  const [selectedId, setSelectedId] = useState(null);
  return <main className="shell"><header className="topbar"><div><p className="eyebrow">EYES / PERCEPTION LAB</p><h1>Session <span>Archive</span></h1></div><Link href="/" className="nav-link">← Live view</Link></header><div className="archive-layout"><section><p className="section-label">RECORDED SESSIONS</p><SessionList selectedId={selectedId} onSelect={setSelectedId} /></section><section><p className="section-label">PREVIEW {selectedId ? ` / SESSION #${String(selectedId).padStart(3, '0')}` : ''}</p>{selectedId ? <FramePreviewPlayer sessionId={selectedId} /> : <div className="selection-prompt">Select a session<br />to inspect its frames.</div>}</section></div></main>;
}