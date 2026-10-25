import { describe, it, expect } from 'vitest';
import {
  parseRules, formatRule, mapReport, rulePressed, generateIni, parseIni, validateIni, iniFileName,
  acceptsReport, JOY_BIT, JOY_HAT, JOY_RANGE, JOY_VALUE, type JoyConfig,
} from './JoyMap';

// the same cases as test/test_hid_joy_map in karabas-go-firmware
const cfg = (buttons: JoyConfig['buttons'], reportId: number | null = null): JoyConfig =>
  ({ vid: '081f', pid: 'e401', name: '', reportId, buttons });

describe('parseRules', () => {
  it('parses the v1 types', () => {
    expect(parseRules('5,1,6')).toEqual([{ pos: 5, type: JOY_BIT, val: 6, arg: 0 }]);
    expect(parseRules('1,0,255')).toEqual([{ pos: 1, type: JOY_VALUE, val: 255, arg: 0 }]);
    expect(parseRules(' 2 , 1 , 3 ')).toEqual([{ pos: 2, type: JOY_BIT, val: 3, arg: 0 }]);
    expect(parseRules('4')).toEqual([{ pos: 4, type: JOY_VALUE, val: 0, arg: 0 }]);
  });

  it('parses several sources', () => {
    expect(parseRules('1 , 3 , 0 , 63 | 5 , 2 , 0')).toEqual([
      { pos: 1, type: JOY_RANGE, val: 0, arg: 63 },
      { pos: 5, type: JOY_HAT, val: 0, arg: 0 },
    ]);
  });

  it('rejects what the firmware rejects', () => {
    for (const v of ['', 'a,b,c', '1,0,3,4', '1,1,2,3,4', '1,1,9', '1,4,8', '300,0,0', '1,2,8',
                     '1,2,0,5', '1,3,10', '1,3,10,5', '1,5,0', '1,0,0|', '1,0,0|x', '-1,0,0']) {
      expect(parseRules(v), v).toBeNull();
    }
  });

  it('formats back', () => {
    for (const v of ['5,1,6', '1,3,0,63', '0,2,2', '0,2,2,4', '2,4,0']) {
      expect(formatRule(parseRules(v)![0])).toBe(v);
    }
  });
});

describe('mapReport', () => {
  it('maps a report', () => {
    const c = cfg({ up: parseRules('1,0,0')!, a: parseRules('5,1,5')! });
    expect([...mapReport(c, new Uint8Array([0x7f, 0x7f, 0, 0, 0, 0x0f, 0, 0]))]).toEqual([]);
    expect([...mapReport(c, new Uint8Array([0x7f, 0x00, 0, 0, 0, 0x2f, 0, 0]))]).toEqual(['up', 'a']);
  });

  it('ignores bytes beyond a short report', () => {
    const c = cfg({ a: parseRules('5,1,0')! });
    expect(mapReport(c, new Uint8Array([0xff, 0xff, 0xff])).size).toBe(0);
  });

  it('maps a hat with diagonals', () => {
    const c = cfg({ up: parseRules('0,2,0,4')!, right: parseRules('0,2,2,4')!, down: parseRules('0,2,4,4')!, left: parseRules('0,2,6,4')! });
    const dirs = [['up'], ['up', 'right'], ['right'], ['down', 'right'], ['down'], ['down', 'left'], ['left'], ['up', 'left']];
    dirs.forEach((d, i) => {
      expect([...mapReport(c, new Uint8Array([(i << 4) | 0x0f]))].sort()).toEqual([...d].sort());
    });
    expect(mapReport(c, new Uint8Array([0x8f])).size).toBe(0);
    expect(mapReport(c, new Uint8Array([0xff])).size).toBe(0);
  });

  it('maps ranges, several sources and clear bits', () => {
    const left = parseRules('0,3,0,63|2,2,6')!;
    expect(left.some(r => rulePressed(r, new Uint8Array([0x80, 0, 0x0f])))).toBe(false);
    expect(left.some(r => rulePressed(r, new Uint8Array([63, 0, 0x0f])))).toBe(true);
    expect(left.some(r => rulePressed(r, new Uint8Array([64, 0, 0x0f])))).toBe(false);
    expect(left.some(r => rulePressed(r, new Uint8Array([0x80, 0, 0x06])))).toBe(true);
    const start = parseRules('0,4,3')![0];
    expect(rulePressed(start, new Uint8Array([0xff]))).toBe(false);
    expect(rulePressed(start, new Uint8Array([0xf7]))).toBe(true);
  });

  it('filters by report id', () => {
    expect(acceptsReport(cfg({}), 3)).toBe(true);
    expect(acceptsReport(cfg({}, 1), 1)).toBe(true);
    expect(acceptsReport(cfg({}, 1), 0)).toBe(false);
  });
});

describe('ini', () => {
  const retrolink = `[info]
vid = 081f
pid = e401
name = Retrolink USB Gamepad

[buttons]
up = 1,0,0
down = 1,0,255
a = 5,1,5
`;

  it('reads a v1 file', () => {
    const { cfg: c, bad } = parseIni(retrolink);
    expect(bad).toEqual([]);
    expect(c.vid).toBe('081f');
    expect(c.reportId).toBeNull();
    expect(c.buttons.a).toEqual([{ pos: 5, type: JOY_BIT, val: 5, arg: 0 }]);
  });

  it('round-trips through generateIni', () => {
    const c = cfg({ up: parseRules('1,3,0,63|5,2,0')!, start: parseRules('6,4,5')! }, 1);
    c.name = 'Pad – “fancy”';
    const text = generateIni(c);
    expect(text).toContain('report_id = 1\n');
    expect(text).toContain('name = Pad  fancy\n');
    expect(parseIni(text).cfg).toEqual({ ...c, name: 'Pad  fancy' });
    expect(validateIni(text)).toEqual([]);
    expect(iniFileName(c)).toBe('081f_e401.ini');
  });

  it('reports what the firmware would reject', () => {
    const long = retrolink + `x = ${Array(20).fill('5,1,4').join('|')}\n`;
    const kinds = validateIni(long + 'y = 1,9,9\n').map(p => p.kind);
    expect(kinds).toContain('long_line');
    expect(kinds).toContain('bad_value');
    expect(validateIni('[buttons]\nup = 1,0,0\n').map(p => p.kind)).toEqual(['no_vid_pid']);
    const many = '[info]\nvid = 0001\npid = 0002\n[buttons]\n' +
      ['up', 'down', 'left', 'right', 'a', 'b', 'c', 'x', 'y'].map(b => `${b} = 0,1,0|0,1,1|0,1,2|0,1,3`).join('\n');
    expect(validateIni(many).map(p => p.kind)).toEqual(['too_many_rules']);
  });
});
