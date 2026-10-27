// The Karabas drivers/*.ini format (v2) and its mapping of a report to MD2 buttons.
// A port of lib/KarabasCommon/src/hid_joy_map.c in karabas-go-firmware: keep them in sync.
//
// A button is mapped by one or more sources separated by '|'. A source is
// "pos,type,value[,arg]", pos being the byte in the report (after the report id byte):
//   0  byte == value
//   1  bit value (0..7) is set
//   2  hat switch: the low nibble of the byte, shifted right by arg (0..4), is value (0..7)
//      or a diagonal next to it; 8..15 is centered
//   3  value <= byte <= arg (an axis range)
//   4  bit value (0..7) is clear

export const MD2_BUTTONS = ['up', 'down', 'left', 'right', 'a', 'b', 'c', 'x', 'y', 'z', 'start', 'mode'] as const;
export type Md2Button = typeof MD2_BUTTONS[number];

export const JOY_VALUE = 0;
export const JOY_BIT = 1;
export const JOY_HAT = 2;
export const JOY_RANGE = 3;
export const JOY_BIT_CLEAR = 4;

export const MAX_RULES = 32;    // per driver, as in the firmware
export const MAX_LINE = 79;     // the firmware reads ini lines into an 80-byte buffer

export interface JoyRule {
  pos: number;
  type: number;
  val: number;
  arg: number;
}

export interface JoyConfig {
  vid: string;          // 4 hex digits, lowercase
  pid: string;
  name: string;
  reportId: number | null; // null: any report
  buttons: Partial<Record<Md2Button, JoyRule[]>>;
}

function parseRule(src: string): JoyRule | null {
  const parts = src.split(',').map(s => s.trim());
  if (parts.length < 1 || parts.length > 4) return null;
  const f = [0, 0, 0, 0];
  for (let i = 0; i < parts.length; i++) {
    if (!/^\d+$/.test(parts[i])) return null;
    f[i] = Number(parts[i]);
    if (f[i] > 255) return null;
  }
  const n = parts.length;
  switch (f[1]) {
    case JOY_VALUE:
      if (n > 3) return null;
      break;
    case JOY_BIT:
    case JOY_BIT_CLEAR:
      if (n > 3 || f[2] > 7) return null;
      break;
    case JOY_HAT:
      if (f[2] > 7 || f[3] > 4) return null;
      break;
    case JOY_RANGE:
      if (n < 4 || f[2] > f[3]) return null;
      break;
    default:
      return null;
  }
  return { pos: f[0], type: f[1], val: f[2], arg: f[3] };
}

// "pos,type,value|..." -> rules, null if any source is malformed
export function parseRules(value: string): JoyRule[] | null {
  const rules: JoyRule[] = [];
  for (const src of value.split('|')) {
    const r = parseRule(src);
    if (!r) return null;
    rules.push(r);
  }
  return rules;
}

export function formatRule(r: JoyRule): string {
  if (r.type === JOY_RANGE || (r.type === JOY_HAT && r.arg)) return `${r.pos},${r.type},${r.val},${r.arg}`;
  return `${r.pos},${r.type},${r.val}`;
}

export function rulePressed(r: JoyRule, report: Uint8Array): boolean {
  if (r.pos >= report.length) return false;
  const b = report[r.pos];
  switch (r.type) {
    case JOY_VALUE: return b === r.val;
    case JOY_BIT: return ((b >> r.val) & 1) === 1;
    case JOY_BIT_CLEAR: return ((b >> r.val) & 1) === 0;
    case JOY_RANGE: return b >= r.val && b <= r.arg;
    case JOY_HAT: {
      const hat = (b >> r.arg) & 0x0f;
      if (hat > 7) return false;
      const d = (hat - r.val) & 7;
      return d === 0 || d === 1 || d === 7;
    }
  }
  return false;
}

export function acceptsReport(cfg: JoyConfig, reportId: number): boolean {
  return cfg.reportId === null || cfg.reportId === reportId;
}

export function mapReport(cfg: JoyConfig, report: Uint8Array): Set<Md2Button> {
  const pressed = new Set<Md2Button>();
  for (const btn of MD2_BUTTONS) {
    if ((cfg.buttons[btn] ?? []).some(r => rulePressed(r, report))) pressed.add(btn);
  }
  return pressed;
}

export function iniFileName(cfg: JoyConfig): string {
  return `${cfg.vid}_${cfg.pid}.ini`;
}

// ini text; the firmware has no comments to skip, so there are none
export function generateIni(cfg: JoyConfig): string {
  const lines = ['[info]', `vid = ${cfg.vid}`, `pid = ${cfg.pid}`];
  // the name is informational: keep it printable ascii and within a line
  const name = cfg.name.replace(/[^\x20-\x7e]/g, '').trim().slice(0, MAX_LINE - 7);
  if (name) lines.push(`name = ${name}`);
  if (cfg.reportId !== null) lines.push(`report_id = ${cfg.reportId}`);
  lines.push('', '[buttons]');
  for (const btn of MD2_BUTTONS) {
    const rules = cfg.buttons[btn];
    if (rules && rules.length) lines.push(`${btn} = ${rules.map(formatRule).join('|')}`);
  }
  return lines.join('\n') + '\n';
}

export type IniProblem =
  | { kind: 'long_line'; line: number }
  | { kind: 'too_many_rules'; count: number }
  | { kind: 'bad_value'; key: string; value: string }
  | { kind: 'no_vid_pid' };

// what the firmware would reject or ignore
export function validateIni(text: string): IniProblem[] {
  const problems: IniProblem[] = [];
  text.split(/\r?\n/).forEach((l, i) => {
    if (l.length > MAX_LINE) problems.push({ kind: 'long_line', line: i + 1 });
  });
  const { cfg, bad } = parseIni(text);
  problems.push(...bad.map(b => ({ kind: 'bad_value' as const, ...b })));
  const count = MD2_BUTTONS.reduce((n, b) => n + (cfg.buttons[b]?.length ?? 0), 0);
  if (count > MAX_RULES) problems.push({ kind: 'too_many_rules', count });
  if (!/^[0-9a-f]{4}$/.test(cfg.vid) || !/^[0-9a-f]{4}$/.test(cfg.pid)) problems.push({ kind: 'no_vid_pid' });
  return problems;
}

export function parseIni(text: string): { cfg: JoyConfig; bad: { key: string; value: string }[] } {
  const cfg: JoyConfig = { vid: '', pid: '', name: '', reportId: null, buttons: {} };
  const bad: { key: string; value: string }[] = [];
  let section = '';
  for (const raw of text.split(/\r?\n/)) {
    const l = raw.trim();
    if (!l || l.startsWith(';') || l.startsWith('#')) continue;
    const sec = /^\[(.+)\]$/.exec(l);
    if (sec) { section = sec[1].trim().toLowerCase(); continue; }
    const eq = l.indexOf('=');
    if (eq < 0) continue;
    const key = l.slice(0, eq).trim().toLowerCase();
    const value = l.slice(eq + 1).trim();
    if (section === 'info') {
      if (key === 'vid') cfg.vid = value.toLowerCase().padStart(4, '0');
      else if (key === 'pid') cfg.pid = value.toLowerCase().padStart(4, '0');
      else if (key === 'name') cfg.name = value;
      else if (key === 'report_id') {
        const id = Number(value);
        if (Number.isInteger(id) && id >= 0 && id <= 255) cfg.reportId = id;
        else bad.push({ key, value });
      }
    } else if (section === 'buttons' && (MD2_BUTTONS as readonly string[]).includes(key)) {
      const rules = parseRules(value);
      if (rules) cfg.buttons[key as Md2Button] = rules;
      else bad.push({ key, value });
    }
  }
  return { cfg, bad };
}
