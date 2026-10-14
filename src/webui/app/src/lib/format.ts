// Times, durations and sizes, as chains-qt writes them.

function plural(n: number, word: string): string {
  return `${n} ${word}${n === 1 ? '' : 's'}`;
}

/** How long ago, in words (GUIUtil::formatNiceTimeOffset). */
export function niceTimeOffset(secs: number): string {
  const HOUR = 3600;
  const DAY = 24 * HOUR;
  const WEEK = 7 * DAY;
  const YEAR = 31556952;
  const s = Math.max(0, Math.floor(secs));
  if (s < 60) return plural(s, 'second');
  if (s < 2 * HOUR) return plural(Math.floor(s / 60), 'minute');
  if (s < 2 * DAY) return plural(Math.floor(s / HOUR), 'hour');
  if (s < 2 * WEEK) return plural(Math.floor(s / DAY), 'day');
  if (s < YEAR) return plural(Math.floor(s / WEEK), 'week');
  return `${plural(Math.floor(s / YEAR), 'year')} and ${plural(Math.floor((s % YEAR) / WEEK), 'week')}`;
}

/** A duration, short: "3 d 4 h", "5 m 2 s" (GUIUtil::formatDurationStr). */
export function shortDuration(secs: number): string {
  let s = Math.max(0, Math.floor(secs));
  const parts: string[] = [];
  const days = Math.floor(s / 86400);
  s %= 86400;
  const hours = Math.floor(s / 3600);
  s %= 3600;
  const minutes = Math.floor(s / 60);
  s %= 60;
  if (days) parts.push(`${days} d`);
  if (hours) parts.push(`${hours} h`);
  if (minutes) parts.push(`${minutes} m`);
  if (s || parts.length === 0) parts.push(`${s} s`);
  return parts.slice(0, 2).join(' ');
}

/** Bytes as kB/MB/GB (GUIUtil::formatBytes: decimal units). */
export function formatBytes(bytes: number): string {
  if (bytes < 1_000) return `${bytes} B`;
  if (bytes < 1_000_000) return `${Math.floor(bytes / 1_000)} kB`;
  if (bytes < 1_000_000_000) return `${Math.floor(bytes / 1_000_000)} MB`;
  return `${Math.floor(bytes / 1_000_000_000)} GB`;
}

/** A date and time of the chain (seconds since 1970), in the local time. */
export function formatTime(secs: number, short = false): string {
  if (!secs) return '–';
  const d = new Date(secs * 1000);
  const pad = (n: number) => String(n).padStart(2, '0');
  // Short: the year left out (a narrow window).
  if (short) return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

/** An integer with thin spaces between groups of three digits. */
export function formatCount(n: number): string {
  return String(Math.trunc(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
}

/** The middle of a long hash, left out: "0000a1b2…e3f4". */
export function shortHash(hash: string, keep = 8): string {
  return hash.length <= keep * 2 + 1 ? hash : `${hash.slice(0, keep)}…${hash.slice(-keep)}`;
}
