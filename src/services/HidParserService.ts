export interface HidMapping {
  byteIndex: number;
  isBitmask: number; // 1 = кнопка (бит), 0 = ось/D-pad
  valueOrBit: number; // Номер бита (0-7) или значение оси (0-255)
}

export interface GamepadConfig {
  vid: string;
  pid: string;
  name: string;
  buttons: Record<string, HidMapping>;
}

export class HidParserService {
  /**
   * Проверяет, нажата ли конкретная кнопка на основе текущего USB-репорта и карты маппинга
   */
/**
   * Проверяет, нажата ли кнопка, сравнивая текущее состояние с состоянием покоя (baseline)
   */
  public static isButtonPressed(
    dataView: DataView, 
    mapping: HidMapping, 
    baselineBytes: number[] = []
  ): boolean {
    if (mapping.byteIndex >= dataView.byteLength) return false;
    
    const byteValue = dataView.getUint8(mapping.byteIndex);

    if (mapping.isBitmask === 1) {
      // КНОПКА (БИТ)
      const bitMask = 1 << mapping.valueOrBit;
      const currentBit = (byteValue & bitMask) !== 0;
      
      // Если у нас есть слепок покоя, берем бит оттуда. Иначе считаем покой нулем (0)
      const baseByte = baselineBytes[mapping.byteIndex] ?? 0;
      const baseBit = (baseByte & bitMask) !== 0;

      // Кнопка нажата, если текущее состояние бита ИЗМЕНИЛОСЬ по сравнению с покоем
      return currentBit !== baseBit;
    } else {
      // ОСЬ / D-PAD
      // Здесь по-прежнему проверяем на точное совпадение со значением (0 или 255)
      return byteValue === mapping.valueOrBit;
    }
  }

  /**
   * Генерирует финальный текст INI-файла для Karabas
   */
  public static generateIni(config: GamepadConfig): string {
    let ini = `[info]\n`;
    ini += `vid = ${config.vid}\n`;
    ini += `pid = ${config.pid}\n`;
    ini += `name = ${config.name}\n\n`;
    ini += `[buttons]\n`;

    for (const [btnName, map] of Object.entries(config.buttons)) {
      ini += `${btnName} = ${map.byteIndex},${map.isBitmask},${map.valueOrBit}\n`;
    }
    return ini;
  }
}
