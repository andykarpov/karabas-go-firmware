// @ts-nocheck
import { describe, it, expect } from 'vitest';
import { TranslationService } from './TranslationService';

describe('TranslationService', () => {
  it('должен корректно форматировать строки с параметрами', () => {
    const template = 'Device: {name} (VID: {vid})';
    const result = TranslationService.format(template, { name: 'Sega Pad', vid: '081f' });
    expect(result).toBe('Device: Sega Pad (VID: 081f)');
  });

  it('должен изменять активный перевод при смене локали', () => {
    TranslationService.setLocale('en');
    expect(TranslationService.t.value.btnConnect).toBe('Connect Gamepad');

    TranslationService.setLocale('ru');
    expect(TranslationService.t.value.btnConnect).toBe('Подключить геймпад');
  });
});
