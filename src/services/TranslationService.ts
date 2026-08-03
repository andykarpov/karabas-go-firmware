import { ref, computed } from 'vue';

export type Locale = 'en' | 'ru' | 'uk';

// Структура всех строк интерфейса
export interface TranslationSchema {
  title: string;
  connectedDevice: string;
  unknownDevice: string;
  btnConnect: string;
  btnDownload: string;
  btnCopy: string;
  mappingTitle: string;
  btnRecord: string;
  btnRetryRecord: string;
  btnRecording: string;
  rawReportTitle: string;
  waitingInput: string;
  previewTitle: string;
  manualByte: string;
  manualType: string;
  manualVal: string;
}

const translations: Record<Locale, TranslationSchema> = {
  en: {
    title: 'Karabas USB HID Gamepad Mapper',
    connectedDevice: 'Device: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Unknown HID Gamepad',
    btnConnect: 'Connect Gamepad',
    btnDownload: 'Download .INI Driver',
    btnCopy: 'Copy INI to Clipboard',
    mappingTitle: 'Button Mapping',
    btnRecord: 'Record',
    btnRetryRecord: 'Retry',
    btnRecording: 'Press button...',
    rawReportTitle: 'Raw USB Report (Hex):',
    waitingInput: 'Waiting for input...',
    previewTitle: 'Testing Preview (Sega Megadrive 2)',
    manualByte: 'Byte:',
    manualType: 'Type (0/1):',
    manualVal: 'Bit/Value:'
  },
  ru: {
    title: 'Karabas USB HID Геймпад Маппер',
    connectedDevice: 'Устройство: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Неизвестный HID геймпад',
    btnConnect: 'Подключить геймпад',
    btnDownload: 'Скачать .INI драйвер',
    btnCopy: 'Скопировать INI',
    mappingTitle: 'Назначение кнопок',
    btnRecord: 'Записать',
    btnRetryRecord: 'Перезаписать',
    btnRecording: 'Нажмите кнопку...',
    rawReportTitle: 'Сырой USB Report (Hex):',
    waitingInput: 'Ожидание ввода...',
    previewTitle: 'Экран проверки (Sega Megadrive 2)',
    manualByte: 'Байт:',
    manualType: 'Тип (0/1):',
    manualVal: 'Бит/Знач:'
  },
  uk: {
    title: 'Karabas USB HID Ґеймпад Маппер',
    connectedDevice: 'Пристрій: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Невідомий HID ґеймпад',
    btnConnect: 'Підключити ґеймпад',
    btnDownload: 'Завантажити .INI драйвер',
    btnCopy: 'Скопіювати INI',
    mappingTitle: 'Призначення кнопок',
    btnRecord: 'Записати',
    btnRetryRecord: 'Перезаписати',
    btnRecording: 'Натисніть кнопку...',
    rawReportTitle: 'Сирий USB Report (Hex):',
    waitingInput: 'Очікування введення...',
    previewTitle: 'Екран перевірки (Sega Megadrive 2)',
    manualByte: 'Байт:',
    manualType: 'Тип (0/1):',
    manualVal: 'Біт/Знач:'
  }
};

// Определяем начальный язык (из localStorage или язык браузера, по умолчанию 'en')
const getInitialLocale = (): Locale => {
  const saved = localStorage.getItem('gamepad_mapper_lang') as Locale;
  if (saved && ['en', 'ru', 'uk'].includes(saved)) return saved;
  
  const browserLang = navigator.language.slice(0, 2);
  if (browserLang === 'ru') return 'ru';
  if (browserLang === 'uk') return 'uk';
  return 'en';
};

const currentLocale = ref<Locale>(getInitialLocale());

export const TranslationService = {
  // Текущая выбранная локаль (реактивная)
  currentLocale,

  // Список доступных языков для селектора
  availableLocales: [
    { code: 'en', label: 'English' },
    { code: 'ru', label: 'Русский' },
    { code: 'uk', label: 'Українська' }
  ] as const,

  // Функция переключения языка
  setLocale(locale: Locale) {
    currentLocale.value = locale;
    localStorage.setItem('gamepad_mapper_lang', locale);
  },

  // Реактивный геттер для получения строк интерфейса
  t: computed(() => translations[currentLocale.value]),

  // Утилита для подстановки динамических параметров (например, имени девайса, VID, PID)
  format(text: string, params: Record<string, string>): string {
    let formatted = text;
    for (const [key, value] of Object.entries(params)) {
      formatted = formatted.replace(`{${key}}`, value);
    }
    return formatted;
  }
};
