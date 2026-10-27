// Learning a mapping from raw reports. A port of lib/KarabasCommon/src/hid_learn.c in
// karabas-go-firmware (the OSD gamepad mapper does the same): keep them in sync.
//
// First the pad rests while RestState.add() collects reports: the last one and the range each
// byte jitters in. Then for each button detect() compares a report with the rest state and
// returns the source that changed:
//   - a hat nibble that left its centered value 8..15 (the other nibble unchanged) -> type 2;
//   - one bit of a byte that does not jitter -> type 1 (set) or 4 (clear); a byte that rests
//     centered (0x60..0x9f) is an axis even if one bit moved;
//   - otherwise the byte that moved most, at least AXIS_MIN beyond its rest range -> type 3,
//     from halfway between the rest and the pressed value to the end. A byte that jitters
//     around 0 (0xff..0x01) is read as signed.

import { JOY_BIT, JOY_BIT_CLEAR, JOY_HAT, JOY_RANGE, type JoyRule } from './JoyMap';

export const MAX_REPORT = 64;
export const AXIS_MIN = 0x30;
const REST_MARGIN = 0x18;

const s8 = (u: number) => (u << 24) >> 24;

export class RestState {
  len = 0;
  reportId = 0;
  last = new Uint8Array(0);
  umin: number[] = [];
  umax: number[] = [];
  smin: number[] = [];
  smax: number[] = [];

  // reports with another id or length than the first are ignored
  add(reportId: number, data: Uint8Array) {
    data = data.subarray(0, MAX_REPORT);
    if (!data.length) return;
    if (!this.len) {
      this.len = data.length;
      this.reportId = reportId;
      this.umin = Array.from(data);
      this.umax = Array.from(data);
      this.smin = Array.from(data, s8);
      this.smax = Array.from(data, s8);
    }
    if (reportId !== this.reportId || data.length !== this.len) return;
    data.forEach((u, i) => {
      const s = s8(u);
      this.umin[i] = Math.min(this.umin[i], u);
      this.umax[i] = Math.max(this.umax[i], u);
      this.smin[i] = Math.min(this.smin[i], s);
      this.smax[i] = Math.max(this.smax[i], s);
    });
    this.last = data.slice();
  }

  sameReport(reportId: number, data: Uint8Array): boolean {
    return this.len > 0 && reportId === this.reportId && Math.min(data.length, MAX_REPORT) === this.len;
  }

  isSigned(i: number) { return this.smax[i] - this.smin[i] < this.umax[i] - this.umin[i]; }
  isStable(i: number) { return this.umin[i] === this.umax[i]; }

  range(i: number): [number, number, boolean] {
    const sgn = this.isSigned(i);
    return sgn ? [this.smin[i], this.smax[i], true] : [this.umin[i], this.umax[i], false];
  }
}

function axisMove(pos: number, lo: number, hi: number, v: number, sgn: boolean): { dist: number; rule: JoyRule } | null {
  const dmin = sgn ? -128 : 0, dmax = sgn ? 127 : 255;
  let from: number, to: number, dist: number;
  if (v < lo - AXIS_MIN) {
    let t = Math.trunc((lo + v) / 2);
    if (sgn && lo >= 0 && t >= 0) t = -1;
    from = dmin; to = t; dist = lo - v;
  } else if (v > hi + AXIS_MIN) {
    let t = Math.trunc((hi + v + 1) / 2);
    if (sgn && hi <= 0 && t <= 0) t = 1;
    from = t; to = dmax; dist = v - hi;
  } else {
    return null;
  }
  return { dist, rule: { pos, type: JOY_RANGE, val: from & 0xff, arg: to & 0xff } };
}

export function detect(rest: RestState, reportId: number, data: Uint8Array): JoyRule | null {
  if (!rest.sameReport(reportId, data)) return null;
  const len = rest.len;

  for (let pass = 0; pass < 2; pass++) {
    for (let i = 0; i < len; i++) {
      if (!rest.isStable(i)) continue;
      const b = rest.last[i], c = data[i];
      if (b === c) continue;
      if (pass === 0) {
        for (const shift of [0, 4]) {
          const bn = (b >> shift) & 0x0f, cn = (c >> shift) & 0x0f;
          const otherSame = ((b ^ c) & (0xf0 >> shift)) === 0;
          if (bn >= 8 && cn <= 7 && otherSame) return { pos: i, type: JOY_HAT, val: cn, arg: shift };
        }
      } else {
        const d = b ^ c;
        const centered = b >= 0x60 && b <= 0x9f;
        if (!centered && (d & (d - 1)) === 0) {
          const bit = 31 - Math.clz32(d);
          return { pos: i, type: (c & d) ? JOY_BIT : JOY_BIT_CLEAR, val: bit, arg: 0 };
        }
      }
    }
  }

  let best: { dist: number; rule: JoyRule } | null = null;
  for (let i = 0; i < len; i++) {
    const [lo, hi, sgn] = rest.range(i);
    const v = sgn ? s8(data[i]) : data[i];
    const m = axisMove(i, lo, hi, v, sgn);
    if (m && (!best || m.dist > best.dist)) best = m;
  }
  return best ? best.rule : null;
}

export function isRest(rest: RestState, reportId: number, data: Uint8Array): boolean {
  if (!rest.sameReport(reportId, data)) return false;
  for (let i = 0; i < rest.len; i++) {
    if (rest.isStable(i)) {
      if (data[i] !== rest.last[i]) return false;
      continue;
    }
    const [lo, hi, sgn] = rest.range(i);
    const v = sgn ? s8(data[i]) : data[i];
    if (v < lo - REST_MARGIN || v > hi + REST_MARGIN) return false;
  }
  return true;
}
