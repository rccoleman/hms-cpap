// The AirSense 11's live card: what to show and when to ask again. Pure, so
// `npm test` (Node's own runner) reads it without Angular.

/** GET /api/as11/live: the AirSense 11's figures while it streams therapy. */
export interface As11Live {
  enabled: boolean;
  streaming: boolean;
  age_s?: number;
  leak_lpm?: number | null;
  pressure_cmh2o?: number | null;
  therapy_pressure_cmh2o?: number | null;
  expiratory_pressure_cmh2o?: number | null;
  resp_rate?: number | null;
  tidal_ml?: number | null;
  minute_vent_lpm?: number | null;
  snore?: number | null;
  flow_limitation?: number | null;
  spo2?: number | null;
  heart_rate?: number | null;
}

/** How often to ask: quickly while the machine streams, slowly while it waits. */
export const AS11_LIVE_STREAMING_MS = 2000;
export const AS11_LIVE_IDLE_MS = 15000;
/** A figure this old (seconds) is shown as stalled, not live. */
export const AS11_LIVE_STALE_S = 10;

/** One figure of the card, or null when the machine did not send it. */
export interface As11LiveFigure {
  key: string;
  label: string;
  value: string;
  unit: string;
}

const FIGURES: { key: keyof As11Live; label: string; unit: string; places: number; icon: string }[] = [
  { key: 'leak_lpm',        label: 'leak',           unit: 'L/min',  places: 1, icon: 'fa-wind' },
  { key: 'pressure_cmh2o',  label: 'pressure',       unit: 'cmH₂O',  places: 1, icon: 'fa-gauge' },
  { key: 'therapy_pressure_cmh2o', label: 'therapyPressure', unit: 'cmH₂O', places: 1, icon: 'fa-gauge-high' },
  { key: 'expiratory_pressure_cmh2o', label: 'expiratoryPressure', unit: 'cmH₂O', places: 1, icon: 'fa-gauge' },
  { key: 'resp_rate',       label: 'respRate',       unit: '/min',   places: 1, icon: 'fa-lungs' },
  { key: 'tidal_ml',        label: 'tidalVolume',    unit: 'mL',     places: 0, icon: 'fa-lungs' },
  { key: 'minute_vent_lpm', label: 'minuteVent',     unit: 'L/min',  places: 1, icon: 'fa-lungs' },
  { key: 'snore',           label: 'snore',          unit: '',       places: 2, icon: 'fa-volume-high' },
  { key: 'flow_limitation', label: 'flowLimitation', unit: '',       places: 2, icon: 'fa-chart-line' },
  { key: 'spo2',            label: 'spo2',           unit: '%',      places: 1, icon: 'fa-droplet' },
  { key: 'heart_rate',      label: 'heartRate',      unit: 'bpm',    places: 0, icon: 'fa-heart-pulse' },
];

/** The figures the machine sent, in the card's order; the rest are left out. */
export function as11LiveFigures(d: As11Live | null): (As11LiveFigure & { icon: string })[] {
  if (!d || !d.streaming) return [];
  const out: (As11LiveFigure & { icon: string })[] = [];
  for (const f of FIGURES) {
    const v = d[f.key];
    if (typeof v !== 'number' || !isFinite(v)) continue;
    out.push({ key: String(f.key), label: f.label, value: v.toFixed(f.places), unit: f.unit, icon: f.icon });
  }
  return out;
}

/** When to ask again: not at all while the option is off, soon while it streams. */
export function as11LiveNextPollMs(d: As11Live | null): number | null {
  if (!d) return AS11_LIVE_IDLE_MS;             // the request failed: try again later
  if (!d.enabled) return null;                  // the option is off: nothing to watch
  return d.streaming ? AS11_LIVE_STREAMING_MS : AS11_LIVE_IDLE_MS;
}
