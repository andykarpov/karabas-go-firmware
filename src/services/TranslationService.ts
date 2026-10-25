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
  btnImport: string;
  mappingTitle: string;
  btnRecord: string;
  btnAdd: string;
  btnClear: string;
  btnRecordAll: string;
  btnSkip: string;
  btnStop: string;
  btnRecalibrate: string;
  btnRecording: string;
  calibrating: string;
  calibrated: string;
  pressButton: string;
  releaseButton: string;
  defaultMapYes: string;
  defaultMapNo: string;
  previewSource: string;
  previewIni: string;
  previewDefault: string;
  problemsTitle: string;
  problemLongLine: string;
  problemTooMany: string;
  problemBadValue: string;
  problemNoVidPid: string;
  importMismatch: string;
  rawReportTitle: string;
  waitingInput: string;
  previewTitle: string;
}

const translations: Record<Locale, TranslationSchema> = {
  en: {
    title: 'Karabas USB HID Gamepad Mapper',
    connectedDevice: 'Device: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Unknown HID Gamepad',
    btnConnect: 'Connect Gamepad',
    btnDownload: 'Download .INI Driver',
    btnCopy: 'Copy INI to Clipboard',
    btnImport: 'Open .INI',
    mappingTitle: 'Button Mapping',
    btnRecord: 'Record',
    btnAdd: '+',
    btnClear: '×',
    btnRecordAll: 'Record all',
    btnSkip: 'Skip',
    btnStop: 'Stop',
    btnRecalibrate: 'Recalibrate',
    btnRecording: 'Press button...',
    calibrating: "Don't touch the gamepad: learning its idle state...",
    calibrated: 'Ready. Record the buttons, or test the default map.',
    pressButton: 'Press {btn}',
    releaseButton: 'Release {btn}',
    defaultMapYes: 'This pad works without an INI: the firmware maps it from its HID descriptor. An INI is only needed to change the layout.',
    defaultMapNo: 'The firmware has no default map for this pad, it needs an INI.',
    previewSource: 'Test with:',
    previewIni: 'this INI',
    previewDefault: 'the default map',
    problemsTitle: 'The firmware would not accept this INI:',
    problemLongLine: 'line {line} is longer than 79 characters',
    problemTooMany: '{count} sources, the firmware takes up to 32',
    problemBadValue: 'invalid value for {key}: {value}',
    problemNoVidPid: 'no vid / pid',
    importMismatch: 'This INI is for {vid}:{pid}, the connected pad is {dvid}:{dpid}.',
    rawReportTitle: 'Raw USB Report (Hex):',
    waitingInput: 'Waiting for input...',
    previewTitle: 'Testing Preview (Sega Megadrive 2)'
  },
  ru: {
    title: 'Karabas USB HID Геймпад Маппер',
    connectedDevice: 'Устройство: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Неизвестный HID геймпад',
    btnConnect: 'Подключить геймпад',
    btnDownload: 'Скачать .INI драйвер',
    btnCopy: 'Скопировать INI',
    btnImport: 'Открыть .INI',
    mappingTitle: 'Назначение кнопок',
    btnRecord: 'Записать',
    btnAdd: '+',
    btnClear: '×',
    btnRecordAll: 'Записать все',
    btnSkip: 'Пропустить',
    btnStop: 'Стоп',
    btnRecalibrate: 'Откалибровать',
    btnRecording: 'Нажмите кнопку...',
    calibrating: 'Не трогайте геймпад: запоминаем состояние покоя...',
    calibrated: 'Готово. Запишите кнопки или проверьте раскладку по умолчанию.',
    pressButton: 'Нажмите {btn}',
    releaseButton: 'Отпустите {btn}',
    defaultMapYes: 'Этот геймпад работает без INI: прошивка строит раскладку по его HID-дескриптору. INI нужен, только чтобы её поменять.',
    defaultMapNo: 'Раскладки по умолчанию для этого геймпада у прошивки нет, нужен INI.',
    previewSource: 'Проверять:',
    previewIni: 'этот INI',
    previewDefault: 'раскладку по умолчанию',
    problemsTitle: 'Прошивка не примет этот INI:',
    problemLongLine: 'строка {line} длиннее 79 символов',
    problemTooMany: 'источников {count}, прошивка берёт до 32',
    problemBadValue: 'неверное значение {key}: {value}',
    problemNoVidPid: 'нет vid / pid',
    importMismatch: 'Этот INI для {vid}:{pid}, а подключён {dvid}:{dpid}.',
    rawReportTitle: 'Сырой USB Report (Hex):',
    waitingInput: 'Ожидание ввода...',
    previewTitle: 'Экран проверки (Sega Megadrive 2)'
  },
  uk: {
    title: 'Karabas USB HID Ґеймпад Маппер',
    connectedDevice: 'Пристрій: {name} (VID: {vid}, PID: {pid})',
    unknownDevice: 'Невідомий HID ґеймпад',
    btnConnect: 'Підключити ґеймпад',
    btnDownload: 'Завантажити .INI драйвер',
    btnCopy: 'Скопіювати INI',
    btnImport: 'Відкрити .INI',
    mappingTitle: 'Призначення кнопок',
    btnRecord: 'Записати',
    btnAdd: '+',
    btnClear: '×',
    btnRecordAll: 'Записати всі',
    btnSkip: 'Пропустити',
    btnStop: 'Стоп',
    btnRecalibrate: 'Відкалібрувати',
    btnRecording: 'Натисніть кнопку...',
    calibrating: 'Не торкайтеся ґеймпада: запам’ятовуємо стан спокою...',
    calibrated: 'Готово. Запишіть кнопки або перевірте розкладку за замовчуванням.',
    pressButton: 'Натисніть {btn}',
    releaseButton: 'Відпустіть {btn}',
    defaultMapYes: 'Цей ґеймпад працює без INI: прошивка будує розкладку за його HID-дескриптором. INI потрібен, лише щоб її змінити.',
    defaultMapNo: 'Розкладки за замовчуванням для цього ґеймпада у прошивки немає, потрібен INI.',
    previewSource: 'Перевіряти:',
    previewIni: 'цей INI',
    previewDefault: 'розкладку за замовчуванням',
    problemsTitle: 'Прошивка не прийме цей INI:',
    problemLongLine: 'рядок {line} довший за 79 символів',
    problemTooMany: 'джерел {count}, прошивка бере до 32',
    problemBadValue: 'невірне значення {key}: {value}',
    problemNoVidPid: 'немає vid / pid',
    importMismatch: 'Цей INI для {vid}:{pid}, а підключено {dvid}:{dpid}.',
    rawReportTitle: 'Сирий USB Report (Hex):',
    waitingInput: 'Очікування введення...',
    previewTitle: 'Екран перевірки (Sega Megadrive 2)'
  }
};

// localStorage can be missing or throw (private mode, tests)
const loadLocale = (): string | null => {
  try { return localStorage.getItem('gamepad_mapper_lang'); } catch { return null; }
};

const getInitialLocale = (): Locale => {
  const saved = loadLocale() as Locale;
  if (saved && ['en', 'ru', 'uk'].includes(saved)) return saved;

  const browserLang = typeof navigator !== 'undefined' ? navigator.language.slice(0, 2) : 'en';
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
    try { localStorage.setItem('gamepad_mapper_lang', locale); } catch { /* not saved */ }
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
