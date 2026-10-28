// The default map the firmware builds for a pad without an ini, computed here from the parsed
// descriptor WebHID gives (device.collections). A port of the rules of
// lib/common/hid_auto_map.c in karabas-go-firmware: keep them in sync.
//
// Only top-level Joystick / Gamepad / Multi-axis collections (Generic Desktop) are used:
// the first X / Y axis and the hat switch -> directions, D-pad / Start / Select usages, and
// Button 1..10 -> X A B Y C Z C Z Mode Start.
//
// The firmware parses the raw descriptor; WebHID hands it over already parsed, grouped by
// collection. Bit offsets are summed per report id in the order WebHID lists the items, which
// matches the descriptor for the usual pads. Treat the result as a preview.

import type { Md2Button } from './JoyMap';

const PAGE_DESKTOP = 0x01;
const PAGE_BUTTON = 0x09;
const PAD_USAGES = [0x04, 0x05, 0x08];
const BUTTON_MAP: Md2Button[] = ['x', 'a', 'b', 'y', 'c', 'z', 'c', 'z', 'mode', 'start'];

export type AutoKind = 'button' | 'x' | 'y' | 'hat';

export interface AutoField {
  reportId: number;
  kind: AutoKind;
  btn?: Md2Button;
  offset: number; // bits, in the report data after the report id byte
  size: number;
  min: number;
  max: number;
}

// the parts of HIDReportItem / HIDCollectionInfo that are used (kept loose for tests)
export interface ReportItem {
  isConstant?: boolean;
  isArray?: boolean;
  isRange?: boolean;
  usages?: number[];
  usageMinimum?: number;
  usageMaximum?: number;
  reportSize?: number;
  reportCount?: number;
  logicalMinimum?: number;
  logicalMaximum?: number;
}
export interface Collection {
  usagePage?: number;
  usage?: number;
  children?: Collection[];
  inputReports?: { reportId?: number; items?: ReportItem[] }[];
}

function usageAt(item: ReportItem, i: number): number {
  if (item.isRange) {
    const u = (item.usageMinimum ?? 0) + i;
    return Math.min(u, item.usageMaximum ?? u);
  }
  const us = item.usages ?? [];
  return us.length ? us[Math.min(i, us.length - 1)] : 0;
}

export function buildAutoMap(collections: Collection[]): AutoField[] {
  const fields: AutoField[] = [];
  const offsets = new Map<number, number>();
  let hasX = false, hasY = false, hasHat = false;

  const walk = (c: Collection, pad: boolean) => {
    for (const rep of c.inputReports ?? []) {
      const id = rep.reportId ?? 0;
      for (const item of rep.items ?? []) {
        const size = item.reportSize ?? 0, count = item.reportCount ?? 0;
        const start = offsets.get(id) ?? 0;
        offsets.set(id, start + size * count);
        if (!pad || item.isConstant || item.isArray || size < 1 || size > 32) continue;
        const min = item.logicalMinimum ?? 0, max = item.logicalMaximum ?? 0;
        for (let i = 0; i < count; i++) {
          const u = usageAt(item, i);
          const page = u >>> 16, uid = u & 0xffff;
          const f = { reportId: id, offset: start + i * size, size, min, max };
          if (page === PAGE_BUTTON) {
            if (uid >= 1 && uid <= BUTTON_MAP.length) fields.push({ ...f, kind: 'button', btn: BUTTON_MAP[uid - 1] });
          } else if (page === PAGE_DESKTOP) {
            if (uid === 0x30 && !hasX) { hasX = true; fields.push({ ...f, kind: 'x' }); }
            else if (uid === 0x31 && !hasY) { hasY = true; fields.push({ ...f, kind: 'y' }); }
            else if (uid === 0x39 && !hasHat) { hasHat = true; fields.push({ ...f, kind: 'hat' }); }
            else if (uid === 0x90) fields.push({ ...f, kind: 'button', btn: 'up' });
            else if (uid === 0x91) fields.push({ ...f, kind: 'button', btn: 'down' });
            else if (uid === 0x92) fields.push({ ...f, kind: 'button', btn: 'right' });
            else if (uid === 0x93) fields.push({ ...f, kind: 'button', btn: 'left' });
            else if (uid === 0x3d) fields.push({ ...f, kind: 'button', btn: 'start' });
            else if (uid === 0x3e) fields.push({ ...f, kind: 'button', btn: 'mode' });
          }
        }
      }
    }
    for (const child of c.children ?? []) walk(child, pad);
  };

  for (const c of collections) {
    walk(c, c.usagePage === PAGE_DESKTOP && PAD_USAGES.includes(c.usage ?? -1));
  }
  return fields;
}

function readBits(data: Uint8Array, offset: number, size: number): number | null {
  if (Math.ceil((offset + size) / 8) > data.length) return null;
  let v = 0;
  for (let b = 0; b < size; b++) {
    const bit = offset + b;
    if ((data[bit >> 3] >> (bit & 7)) & 1) v += 2 ** b;
  }
  return v;
}

const HAT: Md2Button[][] = [['up'], ['up', 'right'], ['right'], ['down', 'right'], ['down'], ['down', 'left'], ['left'], ['up', 'left']];

export function autoMapReport(fields: AutoField[], reportId: number, data: Uint8Array): Set<Md2Button> {
  const res = new Set<Md2Button>();
  for (const f of fields) {
    if (f.reportId !== reportId) continue;
    const raw = readBits(data, f.offset, f.size);
    if (raw === null) continue;
    const v = f.min < 0 && raw >= 2 ** (f.size - 1) ? raw - 2 ** f.size : raw;
    if (f.kind === 'button') {
      if (v) res.add(f.btn!);
    } else if (f.kind === 'x' || f.kind === 'y') {
      const range = f.max - f.min;
      if (range <= 0) continue;
      const center2 = f.min + f.max, v2 = 2 * v, dead2 = Math.trunc(range / 2);
      if (v2 < center2 - dead2) res.add(f.kind === 'x' ? 'left' : 'up');
      else if (v2 > center2 + dead2) res.add(f.kind === 'x' ? 'right' : 'down');
    } else if (f.kind === 'hat') {
      if (v < f.min || v > f.max) continue;
      const n = f.max - f.min + 1, pos = v - f.min;
      const dirs = n === 8 ? HAT[pos] : n === 4 ? HAT[pos * 2] : [];
      dirs.forEach(d => res.add(d));
    }
  }
  return res;
}

export function autoMapHasReport(fields: AutoField[], reportId: number): boolean {
  return fields.some(f => f.reportId === reportId);
}
