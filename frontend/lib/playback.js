export function computeHoldMs(frames, index, { minMs = 16, maxMs = 500 } = {}) {
  if (index >= frames.length - 1) return maxMs;
  const delta = frames[index + 1].timestamp - frames[index].timestamp;
  if (!Number.isFinite(delta) || delta <= 0) return minMs;
  return Math.min(Math.max(delta, minMs), maxMs);
}