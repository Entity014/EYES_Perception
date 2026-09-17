'use client';

import { useEffect, useState } from 'react';
import { getSessions } from '../lib/api';

export default function SessionList({ selectedId, onSelect }) {
  const [sessions, setSessions] = useState([]);
  const [error, setError] = useState('');
  useEffect(() => { getSessions().then(setSessions).catch((reason) => setError(reason.message)); }, []);
  if (error) return <p className="error-message">{error}</p>;
  if (!sessions.length) return <p className="empty-message">No sessions recorded yet.</p>;
  return <div className="session-list">{sessions.map((session) => <button className={`session-row ${session.id === selectedId ? 'selected' : ''}`} key={session.id} onClick={() => onSelect(session.id)}>
    <span className="session-number">#{String(session.id).padStart(3, '0')}</span><span>{new Date(session.started_at).toLocaleString()}</span><span className={session.upload_state === 'complete' ? 'complete' : 'active'}>{session.upload_state === 'uploading' ? 'uploading' : session.upload_state === 'recording' ? 'recording' : 'complete'}</span>
  </button>)}</div>;
}
