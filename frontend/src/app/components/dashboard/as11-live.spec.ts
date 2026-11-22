// The AirSense 11's live card: which figures show and when to ask again.
// `npm test` runs it with Node's own test runner.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { as11LiveFigures, as11LiveNextPollMs, AS11_LIVE_IDLE_MS, AS11_LIVE_STREAMING_MS } from './as11-live.ts';

test('a stream that has not started shows nothing', () => {
  assert.deepEqual(as11LiveFigures({ enabled: true, streaming: false }), []);
  assert.deepEqual(as11LiveFigures(null), []);
});

test('the figures the machine sent show, in the card order, rounded', () => {
  const f = as11LiveFigures({
    enabled: true, streaming: true,
    leak_lpm: 3.04, pressure_cmh2o: 9.06, resp_rate: 14.5, tidal_ml: 449.6,
    minute_vent_lpm: 6.5, snore: 0.2, flow_limitation: 0.05,
  });
  assert.deepEqual(f.map((x) => `${x.label}=${x.value}${x.unit ? ' ' + x.unit : ''}`), [
    'leak=3.0 L/min', 'pressure=9.1 cmH₂O', 'respRate=14.5 /min', 'tidalVolume=450 mL',
    'minuteVent=6.5 L/min', 'snore=0.20', 'flowLimitation=0.05',
  ]);
});

test('a figure the machine lacks is left out, not shown as zero', () => {
  const f = as11LiveFigures({ enabled: true, streaming: true, leak_lpm: 3, spo2: null, heart_rate: null });
  assert.deepEqual(f.map((x) => x.label), ['leak']);
});

test('a real zero still shows', () => {
  const f = as11LiveFigures({ enabled: true, streaming: true, leak_lpm: 0 });
  assert.equal(f[0].value, '0.0');
});

test('SpO2 and heart rate show when the machine sends them', () => {
  const f = as11LiveFigures({ enabled: true, streaming: true, spo2: 95.5, heart_rate: 62 });
  assert.deepEqual(f.map((x) => `${x.label}=${x.value}`), ['spo2=95.5', 'heartRate=62']);
});

test('with the option off nothing is asked again', () => {
  assert.equal(as11LiveNextPollMs({ enabled: false, streaming: false }), null);
});

test('it asks quickly while streaming and slowly while the machine waits', () => {
  assert.equal(as11LiveNextPollMs({ enabled: true, streaming: true }), AS11_LIVE_STREAMING_MS);
  assert.equal(as11LiveNextPollMs({ enabled: true, streaming: false }), AS11_LIVE_IDLE_MS);
});

test('a failed request is tried again later', () => {
  assert.equal(as11LiveNextPollMs(null), AS11_LIVE_IDLE_MS);
});
