// What a prediction market came to, for the app's own Markets page (doc/unified-app.md §24): from what the chain's node
// says through the adapter (host chain.markets: listmarkets, the decisions they are about, gethivemindinfo), each
// market's stage (trading; voted on; partly decided; paid out), its outcomes with the chance of each, the last price of
// its trading once it closed, and once it paid out what a share of each paid, the outcome that won, whether the market
// had called it, and when it paid. Hivemind's own pages work it out alike (its hm/model.ts): a market that paid out has
// no price from its node any more, only its shares, so its last prices are the market maker's prices of those (nothing
// trades once trading closes). Pure functions: test/markets.test.ts tries them.
import type { ChainDecision, ChainMarket, ChainMarketsInfo } from './chains.svelte';

export type MarketStage = 'trading' | 'voting' | 'revealing' | 'counting' | 'partly' | 'paid';
export type MarketKind = 'binary' | 'scalar' | 'categorical';

/** An outcome: a word of the app (Yes, No, High, Low, None of them) or the options of several decisions answered yes (text of the chain). */
export type OutcomeLabel = { word: 'Yes' | 'No' | 'High' | 'Low' | 'None of them' } | { options: string[] };

export interface OutcomeRow {
  state: number;
  label: OutcomeLabel;
  /** Its chance now, or the last of the market's trading once trading closed. */
  price: number;
  /** What a share of it paid, once the market paid out; else null. */
  pays: number | null;
  won: boolean;
  /** Paid out at nothing. */
  lost: boolean;
  /** Ruled out by a resolved decision while the others are voted on. */
  out: boolean;
  /** One of the outcomes the market is about (a categorical market's other combinations are not). */
  exclusive: boolean;
}

export interface MarketResultView {
  stage: MarketStage;
  kind: MarketKind;
  /** The outcomes, the likeliest first; once paid out, the one that won first. */
  rows: OutcomeRow[];
  /** Once paid out: the outcome that won (null for a tie or an answer inside a scale). */
  winner: OutcomeRow | null;
  tie: boolean;
  /** A market on a scale: its ends, the answer of the vote once resolved, the market's estimate (last, or now). */
  scale: { min: number; max: number; answer: number | null; estimate: number } | null;
  /** The state the market's last prices favoured (null when two led together). */
  favourite: number | null;
  /** When it paid out: the period, and the block (periods counted in blocks) or the time its period started. */
  paid: { period: number; height?: number; time?: number } | null;
  /** The last period its decisions are voted in (the vote's end), when known. */
  closes: number | null;
}

const num = (v: unknown): number | null => {
  const n = typeof v === 'number' ? v : typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN;
  return Number.isFinite(n) ? n : null;
};

/** The market maker's prices of shares (LMSR, b the liquidity): the chance of each state. */
export function lmsrPrices(shares: readonly number[], b: number): number[] {
  if (!(b > 0) || shares.length === 0) return shares.map(() => 0);
  const top = Math.max(...shares.map((q) => q / b));
  const e = shares.map((q) => Math.exp(q / b - top));
  const sum = e.reduce((a, x) => a + x, 0);
  return e.map((x) => x / sum);
}

/** The node's prices while it gives them; else (a market that paid out) the maker's prices of its shares. Never NaN. */
export function lastPrices(m: Pick<ChainMarket, 'states' | 'liquidity'>): number[] {
  const states = m.states ?? [];
  const given = states.map((s) => num(s.price));
  if (states.length && given.every((p) => p !== null)) return given as number[];
  return lmsrPrices(
    states.map((s) => num(s.shares) ?? 0),
    num(m.liquidity) ?? 0,
  );
}

/** What a share of a state pays given the outcomes of the decisions (the chain's StatePayout). */
export function statePayout(outcomes: readonly number[], state: number): number {
  let pay = 1;
  outcomes.forEach((o, d) => (pay *= ((state >> d) & 1) === 1 ? o : 1 - o));
  return pay;
}

/** A decision's outcome once resolved (0 no, 1 yes, 0.5 a tie, the place on a scale), else null. */
export function decisionOutcome(d: ChainDecision | undefined): number | null {
  if (!d || d.status !== 'resolved') return null;
  const o = num(d.outcome);
  return o !== null && o >= 0 && o <= 1 ? o : null;
}

/** What a categorical market's decisions have in common, left out of their options ("Who wins? — Brazil": "Brazil"). */
export function optionTexts(texts: readonly string[]): string[] {
  if (texts.length < 2) return texts.slice();
  let prefix = texts[0];
  for (const x of texts) {
    let i = 0;
    while (i < prefix.length && i < x.length && prefix[i] === x[i]) i++;
    prefix = prefix.slice(0, i);
  }
  const cut = Math.max(prefix.lastIndexOf(' — '), prefix.lastIndexOf(': '), prefix.lastIndexOf(' - '));
  if (cut <= 0) return texts.slice();
  const sep = prefix.slice(cut).startsWith(' — ') || prefix.slice(cut).startsWith(' - ') ? 3 : 2;
  const options = texts.map((x) => x.slice(cut + sep).trim());
  return options.every((o) => o.length > 0) ? options : texts.slice();
}

/** The category of a market: a word in brackets at the start of its title ("[Sports] …"), as Hivemind's wizard writes it. */
export function categoryOf(title: string): string | null {
  const bracket = /^\s*\[([^\]]{1,24})\]/.exec(title);
  return bracket ? bracket[1].trim() : null;
}

/** The title without its category in brackets. */
export function titleWithoutCategory(title: string): string {
  return title.replace(/^\s*\[[^\]]{1,24}\]\s*/, '');
}

/** The stage of a decision's vote now (Hivemind's voteStage). */
function voteStage(d: ChainDecision, info: ChainMarketsInfo): 'resolved' | 'upcoming' | 'seal' | 'reveal' | 'waiting' {
  if (d.status === 'resolved') return 'resolved';
  const period = info.period ?? 0;
  if (period <= (d.eventperiod ?? 0)) return 'upcoming';
  if (period > (d.closeperiod ?? 0)) return 'waiting';
  if (period === d.closeperiod && info.phase === 'reveal') return 'reveal';
  return 'seal';
}

export function marketView(m: ChainMarket, decisions: ReadonlyMap<string, ChainDecision>, info?: ChainMarketsInfo | null): MarketResultView {
  const ids = (m.decisions ?? []).filter((d) => /^[0-9a-f]{64}$/.test(d));
  const ds = ids.map((id) => decisions.get(id));
  const states = (m.states ?? []).filter((s) => typeof s.state === 'number' && s.state >= 0 && s.state < 256);
  const kind: MarketKind = ids.length > 1 ? 'categorical' : ds[0]?.scaled ? 'scalar' : 'binary';
  const prices = lastPrices({ ...m, states });
  const outcomes = ds.map(decisionOutcome);
  const resolved = outcomes.filter((o) => o !== null).length;
  const complete = ids.length > 0 && resolved === ids.length;
  const settled = m.status === 'settled';
  const options = optionTexts(ds.map((d, i) => d?.text ?? `${ids[i].slice(0, 12)}…`));

  const rows: OutcomeRow[] = states.map((s, i) => {
    const state = s.state!;
    let label: OutcomeLabel;
    let exclusive = true;
    if (ids.length <= 1) {
      const yes = (state & 1) === 1;
      label = { word: kind === 'scalar' ? (yes ? 'High' : 'Low') : yes ? 'Yes' : 'No' };
    } else {
      const chosen = options.filter((_, d) => ((state >> d) & 1) === 1);
      exclusive = chosen.length === 1;
      label = chosen.length ? { options: chosen } : { word: 'None of them' };
    }
    const pays = settled && complete ? statePayout(outcomes as number[], state) : null;
    const out = !settled && outcomes.some((o, d) => (o === 0 || o === 1) && ((state >> d) & 1) !== o);
    return { state, label, price: prices[i] ?? 0, pays, won: pays !== null && Math.abs(pays - 1) < 1e-9, lost: pays === 0, out, exclusive };
  });
  const winner = rows.find((r) => r.won) ?? null;
  const paidOut = settled && complete;
  rows.sort((a, b) => (paidOut ? (b.pays ?? 0) - (a.pays ?? 0) : 0) || b.price - a.price || b.state - a.state);

  let favourite: number | null = null;
  let tied = false;
  for (const r of rows) {
    const best = favourite === null ? undefined : rows.find((x) => x.state === favourite);
    if (!best || r.price > best.price + 1e-9) {
      favourite = r.state;
      tied = false;
    } else if (Math.abs(r.price - best.price) <= 1e-9) tied = true;
  }
  if (tied) favourite = null;

  let scale: MarketResultView['scale'] = null;
  const d0 = ds[0];
  const lo = num(d0?.min);
  const hi = num(d0?.max);
  if (kind === 'scalar' && lo !== null && hi !== null) {
    const high = rows.find((r) => r.state === 1)?.price ?? 0;
    const said = num(d0?.answer);
    const o = outcomes[0];
    scale = { min: lo, max: hi, answer: paidOut && o !== null ? (said ?? lo + (hi - lo) * o) : null, estimate: lo + (hi - lo) * high };
  }

  let stage: MarketStage = 'trading';
  if (settled) stage = 'paid';
  else if (m.status !== 'trading') {
    if (resolved > 0) stage = 'partly';
    else if (!info) stage = 'voting';
    else {
      const st = ds.map((d) => (d ? voteStage(d, info) : 'seal'));
      stage = st.some((x) => x === 'seal' || x === 'upcoming') ? 'voting' : st.some((x) => x === 'reveal') ? 'revealing' : 'counting';
    }
  }

  const closesAll = ds.map((d) => num(d?.closeperiod));
  const closes = closesAll.length && closesAll.every((c) => c !== null) ? Math.max(...(closesAll as number[])) : null;
  let paid: MarketResultView['paid'] = null;
  if (settled && closes !== null) {
    const period = closes + 1;
    const pb = num(info?.periodblocks);
    const ps = num(info?.periodseconds);
    paid = pb ? { period, height: period * pb } : ps ? { period, time: (num(info?.starttime) ?? 0) + period * ps } : { period };
  }

  return { stage, kind, rows, winner, tie: paidOut && !winner && kind !== 'scalar', scale, favourite, paid, closes };
}
