// @ts-nocheck
import { describe, it, expect } from 'vitest';
import { HidParserService } from './HidParserService';
import type { HidMapping } from './HidParserService'; 

describe('HidParserService', () => {
  it('должен корректно определять нажатие цифровой кнопки (активный высокий)', () => {
    const buffer = new Uint8Array([0, 0, 0, 0, 0, 16]); // 5-й байт = 16 (бит 4 равен 1)
    const dataView = new DataView(buffer.buffer);
    const mapping: HidMapping = { byteIndex: 5, isBitmask: 1, valueOrBit: 4 };
    
    // Передаем покой, где все нули
    expect(HidParserService.isButtonPressed(dataView, mapping, [0, 0, 0, 0, 0, 0])).toBe(true);
  });

  it('должен корректно определять нажатие инвертированной кнопки (активный низкий)', () => {
    const buffer = new Uint8Array([0, 0, 0, 0, 0, 0]); // 5-й байт стал 0 (бит 4 упал в 0)
    const dataView = new DataView(buffer.buffer);
    const mapping: HidMapping = { byteIndex: 5, isBitmask: 1, valueOrBit: 4 };
    
    // В покое этот бит был равен 1 (1 << 4 = 16)
    expect(HidParserService.isButtonPressed(dataView, mapping, [0, 0, 0, 0, 0, 16])).toBe(true);
  });

});

