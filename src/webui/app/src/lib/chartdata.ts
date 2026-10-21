// What a chart of a balance draws (doc/unified-app.md §20.6): never more points than it has pixels. A wallet of 20 000
// transactions drew a path of 40 000 segments, made again and painted again (by software GL under WSLg: on every core)
// at each new transaction. Kept per column of pixels: the first point, the lowest, the highest and the last, in their
// order of time, so that a step line looks exactly as it did.

export interface BalancePoint {
  time: number;
  balance: bigint;
}

/** At most about 4 points per column of `columns` between t0 and t1; the points as they are when there are few. */
export function decimate<T extends BalancePoint>(points: readonly T[], t0: number, t1: number, columns: number): T[] {
  const cols = Math.max(1, Math.floor(columns));
  if (points.length <= cols * 4) return points as T[];
  const span = Math.max(1, t1 - t0);
  const out: T[] = [];
  let col = -1;
  let first: T | null = null;
  let low: T | null = null;
  let high: T | null = null;
  let last: T | null = null;
  const flush = () => {
    if (!first) return;
    const kept = [first, low!, high!, last!].filter((p, i, all) => all.indexOf(p) === i).sort((a, b) => a.time - b.time);
    out.push(...kept);
  };
  for (const p of points) {
    const c = Math.min(cols - 1, Math.max(0, Math.floor(((p.time - t0) / span) * cols)));
    if (c !== col) {
      flush();
      col = c;
      first = low = high = last = p;
      continue;
    }
    if (p.balance < low!.balance) low = p;
    if (p.balance > high!.balance) high = p;
    last = p;
  }
  flush();
  return out;
}

/**
 * A cheap signature of a list of records for what a balance history depends on (how many, their amounts and times):
 * a block that only changes their confirmations changes nothing here, and the history is not made again.
 */
export function historyKey(records: readonly { amount: bigint; time: number; status: string }[]): string {
  let sum = 0n;
  let times = 0;
  let out = 0;
  for (const r of records) {
    sum += r.amount;
    times = (times + r.time) % 2147483647;
    if (r.status === 'conflicted' || r.status === 'abandoned' || r.status === 'notaccepted' || r.status === 'dropped') out++;
  }
  return `${records.length}:${sum}:${times}:${out}`;
}
