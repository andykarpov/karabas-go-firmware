import { describe, it, expect } from 'vitest';
import { buildAutoMap, autoMapReport, autoMapHasReport, type Collection } from './AutoMap';

// the pads of test/test_hid_auto_map in karabas-go-firmware, as WebHID would list them
const desktop = (u: number) => (0x01 << 16) | u;
const button = (u: number) => (0x09 << 16) | u;

// DragonRise generic USB pad (0079:0006): X Y X X Y bytes, a 4-bit null field, 10 buttons,
// 10 vendor bits, all in a logical collection inside the joystick one
const dragonrise: Collection[] = [{
  usagePage: 0x01, usage: 0x04,
  children: [{
    inputReports: [{ reportId: 0, items: [
      { reportSize: 8, reportCount: 5, logicalMinimum: 0, logicalMaximum: 255, usages: [desktop(0x30), desktop(0x31), desktop(0x30), desktop(0x30), desktop(0x31)] },
      { reportSize: 4, reportCount: 1, logicalMinimum: 0, logicalMaximum: 7, usages: [desktop(0)] },
      { reportSize: 1, reportCount: 10, logicalMinimum: 0, logicalMaximum: 1, isRange: true, usageMinimum: button(1), usageMaximum: button(10) },
      { reportSize: 1, reportCount: 10, logicalMinimum: 0, logicalMaximum: 1, usages: [(0xff00 << 16) | 1] },
    ] }],
  }],
}];

// report id 1: 16 buttons, hat 0..7, padding, signed X / Y; report id 2: one button
const hatPad: Collection[] = [{
  usagePage: 0x01, usage: 0x05,
  inputReports: [
    { reportId: 1, items: [
      { reportSize: 1, reportCount: 16, logicalMinimum: 0, logicalMaximum: 1, isRange: true, usageMinimum: button(1), usageMaximum: button(16) },
      { reportSize: 4, reportCount: 1, logicalMinimum: 0, logicalMaximum: 7, usages: [desktop(0x39)] },
      { reportSize: 4, reportCount: 1, isConstant: true },
      { reportSize: 8, reportCount: 2, logicalMinimum: -127, logicalMaximum: 127, usages: [desktop(0x30), desktop(0x31)] },
    ] },
    { reportId: 2, items: [
      { reportSize: 1, reportCount: 1, logicalMinimum: 0, logicalMaximum: 1, usages: [button(1)] },
      { reportSize: 7, reportCount: 1, isConstant: true },
    ] },
  ],
}];

const keyboard: Collection[] = [{
  usagePage: 0x01, usage: 0x06,
  inputReports: [{ reportId: 0, items: [
    { reportSize: 1, reportCount: 8, logicalMinimum: 0, logicalMaximum: 1, isRange: true, usageMinimum: (0x07 << 16) | 0xe0, usageMaximum: (0x07 << 16) | 0xe7 },
  ] }],
}];

const sorted = (s: Set<string>) => [...s].sort();

describe('AutoMap', () => {
  it('maps the DragonRise layout like the Retrolink driver ini', () => {
    const m = buildAutoMap(dragonrise);
    const idle = [0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00];
    expect(autoMapReport(m, 0, new Uint8Array(idle)).size).toBe(0);
    const cases: [number, number, string][] = [[1, 0x00, 'up'], [1, 0xff, 'down'], [0, 0x00, 'left'], [0, 0xff, 'right']];
    for (const [i, v, btn] of cases) {
      const p = [...idle]; p[i] = v;
      expect(sorted(autoMapReport(m, 0, new Uint8Array(p)))).toEqual([btn]);
    }
    const bits: [number, number, string][] = [[5, 5, 'a'], [5, 6, 'b'], [5, 4, 'x'], [5, 7, 'y'], [6, 5, 'start'], [6, 4, 'mode'], [6, 0, 'c'], [6, 1, 'z']];
    for (const [i, bit, btn] of bits) {
      const p = [...idle]; p[i] |= 1 << bit;
      expect(sorted(autoMapReport(m, 0, new Uint8Array(p)))).toEqual([btn]);
    }
  });

  it('maps a hat, signed axes and report ids', () => {
    const m = buildAutoMap(hatPad);
    expect(autoMapReport(m, 1, new Uint8Array([0, 0, 0x08, 0, 0])).size).toBe(0);
    expect(sorted(autoMapReport(m, 1, new Uint8Array([0, 0, 0xf1, 0, 0])))).toEqual(['right', 'up']);
    expect(sorted(autoMapReport(m, 1, new Uint8Array([0, 0, 0x08, 0x80, 0x7f])))).toEqual(['down', 'left']);
    expect(sorted(autoMapReport(m, 1, new Uint8Array([0x02, 0x02, 0x08, 0, 0])))).toEqual(['a', 'start']);
    expect(sorted(autoMapReport(m, 2, new Uint8Array([0x01])))).toEqual(['x']);
    expect(autoMapHasReport(m, 1)).toBe(true);
    expect(autoMapHasReport(m, 3)).toBe(false);
  });

  it('does not map a keyboard', () => {
    expect(buildAutoMap(keyboard)).toEqual([]);
  });
});
