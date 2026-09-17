import Database from 'better-sqlite3';

export function openDb(filePath) {
  const database = new Database(filePath);
  database.pragma('journal_mode = WAL');
  database.exec(`
    CREATE TABLE IF NOT EXISTS sessions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      started_at INTEGER NOT NULL,
      ended_at INTEGER,
      recorded_ended_at INTEGER,
      upload_state TEXT NOT NULL DEFAULT 'complete'
    );
    CREATE TABLE IF NOT EXISTS frames (
      session_id INTEGER NOT NULL,
      sequence INTEGER NOT NULL,
      timestamp INTEGER NOT NULL,
      source TEXT NOT NULL,
      path TEXT NOT NULL,
      PRIMARY KEY (session_id, sequence),
      FOREIGN KEY (session_id) REFERENCES sessions(id)
    );
  `);
  const columns = database.prepare('PRAGMA table_info(sessions)').all();
  if (!columns.some((column) => column.name === 'upload_state')) {
    database.exec("ALTER TABLE sessions ADD COLUMN upload_state TEXT NOT NULL DEFAULT 'complete'");
  }
  if (!columns.some((column) => column.name === 'recorded_ended_at')) {
    database.exec('ALTER TABLE sessions ADD COLUMN recorded_ended_at INTEGER');
  }

  return {
    createSession() {
      const startedAt = Date.now();
      const result = database.prepare(
        "INSERT INTO sessions (started_at, ended_at, upload_state) VALUES (?, NULL, 'recording')"
      ).run(startedAt);
      return { id: Number(result.lastInsertRowid), started_at: startedAt };
    },

    beginUpload(id) {
      database.prepare("UPDATE sessions SET upload_state = 'uploading', recorded_ended_at = ? WHERE id = ?").run(Date.now(), id);
    },

    completeUpload(id) {
      database.prepare("UPDATE sessions SET ended_at = ?, upload_state = 'complete' WHERE id = ?").run(Date.now(), id);
    },

    // Kept for callers that do not use deferred upload.
    endSession(id) {
      database.prepare("UPDATE sessions SET ended_at = ?, upload_state = 'complete' WHERE id = ?").run(Date.now(), id);
    },

    getOpenSession() {
      return database.prepare(
        "SELECT * FROM sessions WHERE upload_state = 'recording' ORDER BY id DESC LIMIT 1"
      ).get() ?? null;
    },

    getUploadingSession() {
      return database.prepare(
        "SELECT * FROM sessions WHERE upload_state = 'uploading' ORDER BY id ASC LIMIT 1"
      ).get() ?? null;
    },

    getSession(id) {
      return database.prepare('SELECT * FROM sessions WHERE id = ?').get(id) ?? null;
    },

    insertFrame({ sessionId, sequence, timestamp, source, path }) {
      database.prepare(
        'INSERT OR IGNORE INTO frames (session_id, sequence, timestamp, source, path) VALUES (?, ?, ?, ?, ?)'
      ).run(sessionId, sequence, timestamp, source, path);
    },

    getSessions() {
      return database.prepare('SELECT * FROM sessions ORDER BY id DESC').all();
    },

    getFramesForSession(sessionId) {
      return database.prepare(
        'SELECT * FROM frames WHERE session_id = ? ORDER BY sequence ASC'
      ).all(sessionId);
    },

    getSequenceGaps(sessionId) {
      const rows = database.prepare(
        'SELECT sequence FROM frames WHERE session_id = ? ORDER BY sequence ASC'
      ).all(sessionId);
      const gaps = [];
      for (let index = 1; index < rows.length; index++) {
        for (let sequence = rows[index - 1].sequence + 1; sequence < rows[index].sequence; sequence++) {
          gaps.push(sequence);
        }
      }
      return gaps;
    },

    close() {
      database.close();
    },
  };
}
