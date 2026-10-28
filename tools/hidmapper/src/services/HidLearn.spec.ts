import { describe, it, expect } from 'vitest';
import { RestState, detect, isRest, MAX_REPORT } from './HidLearn';
import { formatRule, parseRules, rulePressed, JOY_BIT, JOY_BIT_CLEAR, JOY_HAT, JOY_RANGE } from './JoyMap';

// the same cases as test/test_hid_learn in karabas-go-firmware
const u8 = (a: number[]) => new Uint8Array(a);
const rule = (pos: number, type: number, val: number, arg = 0) => ({ pos, type, val, arg });

describe('HidLearn', () => {
  it('DragonRise / Retrolink', () => {
    const idle = [0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00];
    const rest = new RestState();
    for (let i = 0; i < 3; i++) rest.add(0, u8(idle));
    expect(detect(rest, 0, u8(idle))).toBeNull();
    expect(isRest(rest, 0, u8(idle))).toBe(true);

    const press = (i: number, v: number) => { const p = [...idle]; p[i] = v; return u8(p); };
    expect(detect(rest, 0, press(1, 0x00))).toEqual(rule(1, JOY_RANGE, 0, 63));
    expect(isRest(rest, 0, press(1, 0x00))).toBe(false);
    expect(detect(rest, 0, press(0, 0xff))).toEqual(rule(0, JOY_RANGE, 191, 255));
    expect(detect(rest, 0, press(5, 0x2f))).toEqual(rule(5, JOY_BIT, 5));
    expect(detect(rest, 0, press(6, 0x20))).toEqual(rule(6, JOY_BIT, 5));
  });

  it('hat and jitter', () => {
    const rest = new RestState();
    rest.add(1, u8([0x08, 0x7e, 0x81, 0x00]));
    rest.add(1, u8([0x08, 0x81, 0x7e, 0x00]));
    expect(detect(rest, 1, u8([0x08, 0x7e, 0x81, 0x00]))).toBeNull();
    expect(isRest(rest, 1, u8([0x08, 0x85, 0x79, 0x00]))).toBe(true);
    expect(detect(rest, 1, u8([0x08, 0x85, 0x79, 0x00]))).toBeNull();
    expect(detect(rest, 1, u8([0x02, 0x81, 0x7e, 0x00]))).toEqual(rule(0, JOY_HAT, 2));
    expect(detect(rest, 1, u8([0x18, 0x81, 0x7e, 0x00]))).toEqual(rule(0, JOY_BIT, 4));
    expect(detect(rest, 1, u8([0x08, 0x00, 0x7e, 0x01]))).toEqual(rule(3, JOY_BIT, 0));
    expect(detect(rest, 1, u8([0x08, 0x10, 0x7e, 0x00]))).toEqual(rule(1, JOY_RANGE, 0, 0x47));
    expect(detect(rest, 2, u8([0x02, 0x81, 0x7e, 0x00]))).toBeNull();
    expect(isRest(rest, 2, u8([0x08, 0x7e, 0x81, 0x00]))).toBe(false);
  });

  it('hat in the high nibble', () => {
    const rest = new RestState();
    rest.add(0, u8([0xf0]));
    expect(detect(rest, 0, u8([0x40]))).toEqual(rule(0, JOY_HAT, 4, 4));
  });

  it('signed axis', () => {
    const rest = new RestState();
    for (const v of [0xff, 0x01, 0x00]) rest.add(0, u8([v]));
    expect(isRest(rest, 0, u8([0xff]))).toBe(true);
    expect(detect(rest, 0, u8([0x80]))).toEqual(rule(0, JOY_RANGE, 0x80, 0xc0));
    expect(detect(rest, 0, u8([0x7f]))).toEqual(rule(0, JOY_RANGE, 64, 127));
  });

  it('active-low button', () => {
    const rest = new RestState();
    rest.add(0, u8([0xff]));
    expect(detect(rest, 0, u8([0xfb]))).toEqual(rule(0, JOY_BIT_CLEAR, 2));
  });

  it('a learned rule maps back', () => {
    const rest = new RestState();
    const idle = u8([0x80, 0x0f, 0x00]);
    rest.add(0, idle);
    for (const p of [[0x00, 0x0f, 0x00], [0xff, 0x0f, 0x00], [0x80, 0x06, 0x00], [0x80, 0x0f, 0x80]]) {
      const r = detect(rest, 0, u8(p))!;
      const back = parseRules(formatRule(r))![0];
      expect(rulePressed(back, u8(p))).toBe(true);
      expect(rulePressed(back, idle)).toBe(false);
    }
  });

  it('a press while resting', () => {
    const idle = [0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00];
    const rest = new RestState();
    rest.add(0, u8(idle));
    const press = (i: number, v: number) => { const p = [...idle]; p[i] = v; return u8(p); };
    expect(rest.moved(0, press(1, 0x00))).toBe(true);
    expect(rest.moved(0, press(1, 0x84))).toBe(false);
    expect(rest.moved(1, press(1, 0x84))).toBe(false);
    const zero = new RestState();
    zero.add(0, u8([0x00, 0x00]));
    expect(zero.moved(0, u8([0xff, 0x01]))).toBe(false);
    expect(zero.moved(0, u8([0x80, 0x00]))).toBe(true);
  });

  it('clamps long reports', () => {
    const rest = new RestState();
    rest.add(0, new Uint8Array(100));
    expect(rest.len).toBe(MAX_REPORT);
    expect(isRest(rest, 0, new Uint8Array(100))).toBe(true);
  });
});
