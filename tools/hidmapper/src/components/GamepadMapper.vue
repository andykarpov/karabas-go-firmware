<script setup lang="ts">
import { ref, reactive, computed } from 'vue';
import { TranslationService } from '../services/TranslationService';
import {
  MD2_BUTTONS, type Md2Button, type JoyConfig, formatRule, mapReport, acceptsReport,
  generateIni, parseIni, validateIni, iniFileName,
} from '../services/JoyMap';
import { RestState, detect, isRest } from '../services/HidLearn';
import { buildAutoMap, autoMapReport, autoMapHasReport, type AutoField, type Collection } from '../services/AutoMap';

// Используем реактивный объект переводов
const t = TranslationService.t;

const CALIBRATE_MS = 1000;

const device = ref<HIDDevice | null>(null);
const rawReportHex = ref<string>('');
const lastReportBytes = ref<number[]>([]);
const activeSegaButtons = ref<Record<string, boolean>>({});

// Конфигурация драйвера (формат ini v2 прошивки)
const config = reactive<JoyConfig>({ vid: '', pid: '', name: '', reportId: null, buttons: {} });

// Раскладка, которую прошивка построит сама по HID-дескриптору
const autoMap = ref<AutoField[]>([]);
const previewSource = ref<'ini' | 'default'>('ini');

// Состояние покоя геймпада (калибровка) и запись кнопок
let rest = new RestState();
const calibrating = ref(false);
const recording = ref<Md2Button | null>(null);
const appendMode = ref(false);      // добавить источник к кнопке, а не заменить
const recordAll = ref(false);       // записать все кнопки по очереди
const waitRelease = ref(false);     // кнопка записана, ждём отпускания
const importNote = ref('');

const iniText = computed(() => generateIni(config));
const problems = computed(() => validateIni(iniText.value).map(p => {
  switch (p.kind) {
    case 'long_line': return TranslationService.format(t.value.problemLongLine, { line: String(p.line) });
    case 'too_many_rules': return TranslationService.format(t.value.problemTooMany, { count: String(p.count) });
    case 'bad_value': return TranslationService.format(t.value.problemBadValue, { key: p.key, value: p.value });
    case 'no_vid_pid': return t.value.problemNoVidPid;
  }
}));
const hasRules = computed(() => MD2_BUTTONS.some(b => (config.buttons[b] ?? []).length > 0));

const status = computed(() => {
  if (!device.value) return '';
  if (calibrating.value) return t.value.calibrating;
  if (recording.value) {
    const fmt = waitRelease.value ? t.value.releaseButton : t.value.pressButton;
    return TranslationService.format(fmt, { btn: recording.value.toUpperCase() });
  }
  return t.value.calibrated;
});

const hex = (n: number) => n.toString(16).padStart(4, '0');

const calibrate = () => {
  rest = new RestState();
  calibrating.value = true;
  setTimeout(() => {
    calibrating.value = false;
    // a pad may only report on changes: the last report is the idle one then
    if (!rest.len && lastReportBytes.value.length) rest.add(lastReportId, new Uint8Array(lastReportBytes.value));
    config.reportId = rest.reportId ? rest.reportId : null;
  }, CALIBRATE_MS);
};

const connectDevice = async () => {
  try {
    const devices = await navigator.hid.requestDevice({ filters: [] });
    if (devices.length === 0) return;
    const d = devices[0];
    await d.open();
    device.value = d;
    config.vid = hex(d.vendorId);
    config.pid = hex(d.productId);
    config.name = d.productName || t.value.unknownDevice;
    autoMap.value = buildAutoMap(d.collections as unknown as Collection[]);
    previewSource.value = hasRules.value || !autoMap.value.length ? 'ini' : 'default';
    d.oninputreport = handleInputReport;
    calibrate();
  } catch (err) {
    console.error('Ошибка подключения HID:', err);
  }
};

let lastReportId = 0;

const handleInputReport = (event: HIDInputReportEvent) => {
  const bytes = new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength);
  const reportId = event.reportId;
  lastReportId = reportId;
  lastReportBytes.value = Array.from(bytes);
  rawReportHex.value = (reportId ? `[${reportId}] ` : '') +
    Array.from(bytes, b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');

  if (calibrating.value) {
    // touched while resting: start the rest again (as the OSD mapper of the firmware)
    if (rest.moved(reportId, bytes)) rest = new RestState();
    else rest.add(reportId, bytes);
    return;
  }
  if (recording.value) record(reportId, bytes);

  // Подсветка: так, как это увидит прошивка
  let pressed: Set<Md2Button> | null = null;
  if (previewSource.value === 'default') {
    if (autoMapHasReport(autoMap.value, reportId)) pressed = autoMapReport(autoMap.value, reportId, bytes);
  } else if (acceptsReport(config, reportId)) {
    pressed = mapReport(config, bytes);
  }
  if (pressed) {
    const active: Record<string, boolean> = {};
    pressed.forEach(b => { active[b] = true; });
    activeSegaButtons.value = active;
  }
};

const record = (reportId: number, bytes: Uint8Array) => {
  const btn = recording.value!;
  if (waitRelease.value) {
    if (isRest(rest, reportId, bytes)) {
      waitRelease.value = false;
      nextButton();
    }
    return;
  }
  const r = detect(rest, reportId, bytes);
  if (!r) return;
  const rules = appendMode.value ? [...(config.buttons[btn] ?? [])] : [];
  if (!rules.some(x => formatRule(x) === formatRule(r))) rules.push(r);
  config.buttons[btn] = rules;
  previewSource.value = 'ini';
  waitRelease.value = true;
};

const nextButton = () => {
  if (recordAll.value && recording.value) {
    const i = MD2_BUTTONS.indexOf(recording.value) + 1;
    recording.value = i < MD2_BUTTONS.length ? MD2_BUTTONS[i] : null;
  } else {
    recording.value = null;
  }
  if (!recording.value) recordAll.value = false;
};

const startRecording = (btn: Md2Button, append = false) => {
  if (calibrating.value) return;
  recordAll.value = false;
  appendMode.value = append;
  waitRelease.value = false;
  recording.value = btn;
};

const startRecordAll = () => {
  if (calibrating.value) return;
  recordAll.value = true;
  appendMode.value = false;
  waitRelease.value = false;
  recording.value = MD2_BUTTONS[0];
};

const skipButton = () => { waitRelease.value = false; nextButton(); };
const stopRecording = () => { recordAll.value = false; waitRelease.value = false; recording.value = null; };
const clearButton = (btn: Md2Button) => { delete config.buttons[btn]; };

const importIni = async (event: Event) => {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  input.value = '';
  if (!file) return;
  const { cfg } = parseIni(await file.text());
  importNote.value = '';
  if (device.value && cfg.vid && (cfg.vid !== config.vid || cfg.pid !== config.pid)) {
    importNote.value = TranslationService.format(t.value.importMismatch, { vid: cfg.vid, pid: cfg.pid, dvid: config.vid, dpid: config.pid });
  }
  if (!device.value) {
    config.vid = cfg.vid;
    config.pid = cfg.pid;
  }
  config.name = cfg.name || config.name;
  config.reportId = cfg.reportId;
  config.buttons = cfg.buttons;
  previewSource.value = 'ini';
};

const downloadIni = () => {
  const blob = new Blob([iniText.value], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = iniFileName(config);
  a.click();
  URL.revokeObjectURL(url);
};

const copyToClipboard = async () => {
  try {
    await navigator.clipboard.writeText(iniText.value);
  } catch (err) {
    console.error('Failed to copy:', err);
  }
};
</script>

<template>
  <div class="p-6 max-w-5xl mx-auto space-y-6 text-slate-800">
    <header class="flex flex-col sm:flex-row justify-between items-start sm:items-center bg-white p-4 rounded-xl shadow gap-4">
      <div>
        <h1 class="text-xl font-bold">{{ t.title }}</h1>
        <p class="text-sm text-slate-500" v-if="device">
          {{ TranslationService.format(t.connectedDevice, { name: config.name, vid: config.vid, pid: config.pid }) }}
        </p>
      </div>
      
      <div class="flex flex-wrap items-center gap-2 self-end sm:self-auto">
        <!-- Выпадающий список языков -->
        <select 
          :value="TranslationService.currentLocale.value" 
          @change="TranslationService.setLocale(($event.target as HTMLSelectElement).value as any)"
          class="bg-slate-100 border border-slate-300 rounded-lg px-2 py-1.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-indigo-500"
        >
          <option v-for="lang in TranslationService.availableLocales" :key="lang.code" :value="lang.code">
            {{ lang.label }}
          </option>
        </select>

        <label class="bg-slate-100 hover:bg-slate-200 border border-slate-300 px-3 py-2 rounded-lg font-medium transition text-sm cursor-pointer">
          {{ t.btnImport }}
          <input type="file" accept=".ini,.txt" class="hidden" @change="importIni" />
        </label>
        <button v-if="!device" @click="connectDevice" class="bg-indigo-600 hover:bg-indigo-700 text-white px-4 py-2 rounded-lg font-medium transition text-sm">
          {{ t.btnConnect }}
        </button>
        <template v-if="device || hasRules">
          <button @click="copyToClipboard" class="bg-slate-600 hover:bg-slate-700 text-white px-3 py-2 rounded-lg font-medium transition text-sm">
            {{ t.btnCopy }}
          </button>
          <button @click="downloadIni" class="bg-emerald-600 hover:bg-emerald-700 text-white px-4 py-2 rounded-lg font-medium transition text-sm">
            {{ t.btnDownload }}
          </button>
        </template>
      </div>
    </header>

    <!-- Состояние, раскладка по умолчанию, проблемы ini -->
    <div v-if="device || importNote || problems.length" class="bg-white p-4 rounded-xl shadow space-y-2 text-sm">
      <div v-if="status" class="flex items-center justify-between gap-2">
        <span :class="calibrating || recording ? 'text-amber-600 font-semibold' : 'text-slate-600'">{{ status }}</span>
        <button v-if="device && !calibrating && !recording" @click="calibrate" class="text-xs px-3 py-1 rounded bg-slate-100 hover:bg-slate-200">{{ t.btnRecalibrate }}</button>
      </div>
      <p v-if="device" :class="autoMap.length ? 'text-emerald-700' : 'text-slate-500'">
        {{ autoMap.length ? t.defaultMapYes : t.defaultMapNo }}
      </p>
      <p v-if="importNote" class="text-amber-700">{{ importNote }}</p>
      <div v-if="problems.length && hasRules" class="text-red-700">
        {{ t.problemsTitle }}
        <ul class="list-disc pl-5"><li v-for="p in problems" :key="p">{{ p }}</li></ul>
      </div>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
      <!-- Панель маппинга -->
      <div class="bg-white p-4 rounded-xl shadow space-y-2">
        <div class="flex items-center justify-between border-b pb-2">
          <h2 class="font-bold">{{ t.mappingTitle }}</h2>
          <button v-if="device && !recording" @click="startRecordAll" :disabled="calibrating"
            class="text-xs px-3 py-1 rounded bg-indigo-600 hover:bg-indigo-700 text-white disabled:opacity-50">{{ t.btnRecordAll }}</button>
          <div v-else-if="recording" class="flex gap-1">
            <button v-if="recordAll" @click="skipButton" class="text-xs px-2 py-1 rounded bg-slate-100 hover:bg-slate-200">{{ t.btnSkip }}</button>
            <button @click="stopRecording" class="text-xs px-2 py-1 rounded bg-slate-100 hover:bg-slate-200">{{ t.btnStop }}</button>
          </div>
        </div>
        <div v-for="btn in MD2_BUTTONS" :key="btn"
          :class="recording === btn ? 'bg-amber-50 ring-1 ring-amber-400' : 'bg-slate-50'"
          class="flex items-center justify-between gap-1 p-1 rounded">
          <span class="uppercase font-mono text-sm font-semibold text-slate-700 w-12">{{ btn }}</span>
          <span class="font-mono text-xs text-slate-500 flex-1 truncate" :title="(config.buttons[btn] ?? []).map(formatRule).join('|')">
            {{ (config.buttons[btn] ?? []).map(formatRule).join(' | ') || '—' }}
          </span>
          <template v-if="device">
            <button @click="startRecording(btn)" :disabled="calibrating"
              :class="recording === btn ? 'bg-amber-500 text-white' : 'bg-indigo-50 hover:bg-indigo-100 text-indigo-600'"
              class="text-xs px-2 py-1 rounded font-medium transition disabled:opacity-50">
              {{ recording === btn ? t.btnRecording : t.btnRecord }}
            </button>
            <button v-if="config.buttons[btn]?.length" @click="startRecording(btn, true)" :disabled="calibrating"
              class="text-xs px-2 py-1 rounded bg-indigo-50 hover:bg-indigo-100 text-indigo-600 disabled:opacity-50">{{ t.btnAdd }}</button>
          </template>
          <button v-if="config.buttons[btn]?.length" @click="clearButton(btn)"
            class="text-xs px-2 py-1 rounded bg-slate-100 hover:bg-red-100 text-slate-600">{{ t.btnClear }}</button>
        </div>
      </div>

      <!-- Монитор репортов и Визуализация Sega MD2 геймпада -->
      <div class="md:col-span-2 space-y-6">

        <!-- Схематичный геймпад Sega MD2 -->
        <div class="bg-white p-4 rounded-xl shadow flex flex-col items-center justify-center min-h-[300px] gap-4">
          <div v-if="device && autoMap.length" class="flex items-center gap-3 text-sm self-start">
            <span class="text-slate-500">{{ t.previewSource }}</span>
            <label class="flex items-center gap-1"><input type="radio" value="ini" v-model="previewSource" /> {{ t.previewIni }}</label>
            <label class="flex items-center gap-1"><input type="radio" value="default" v-model="previewSource" /> {{ t.previewDefault }}</label>
          </div>
          
          <!-- Корпус контроллера -->
          <div class="w-[450px] h-[200px] bg-slate-200 rounded-[20px] relative shadow-2xl border-4 border-slate-700 flex items-center justify-between px-8">
            
            <!-- Крестовина (D-Pad) -->
            <div class="relative w-28 h-28 flex items-center justify-center">
              <div :class="{'bg-yellow-400': activeSegaButtons['up']}" class="absolute top-0 w-8 h-10 bg-slate-600 rounded-t"></div>
              <div :class="{'bg-yellow-400': activeSegaButtons['down']}" class="absolute bottom-0 w-8 h-10 bg-slate-600 rounded-b"></div>
              <div :class="{'bg-yellow-400': activeSegaButtons['left']}" class="absolute left-0 w-10 h-8 bg-slate-600 rounded-l"></div>
              <div :class="{'bg-yellow-400': activeSegaButtons['right']}" class="absolute right-0 w-10 h-8 bg-slate-600 rounded-r"></div>
              <div class="w-8 h-8 bg-slate-700 rounded-full z-10"></div>
            </div>

            <!-- Системные кнопки по центру -->
            <div class="flex flex-col space-y-4 items-center absolute left-1/2 transform -translate-x-1/2 top-12">
              <div 
                :class="activeSegaButtons['mode'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-white'" 
                class="px-2 py-0.5 rounded text-[9px] uppercase font-bold transition"
              >
                Mode
              </div>
              
              <div 
                :class="activeSegaButtons['start'] ? 'bg-yellow-400' : 'bg-slate-600'" 
                class="w-10 h-4 rounded-full rotate-[-15deg] transition"
              ></div>
              <span class="text-[10px] text-slate-400 uppercase font-bold tracking-widest">Start</span>
            </div>

            <!-- Блок кнопок действия (XYZ / ABC) -->
            <div class="grid grid-rows-2 gap-2 transform rotate-[-10deg]">
              <!-- Верхний ряд: X Y Z -->
              <div class="flex space-x-2 justify-end pr-4">
                <div :class="activeSegaButtons['x'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-white'" class="w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold transition">X</div>
                <div :class="activeSegaButtons['y'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-white'" class="w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold transition">Y</div>
                <div :class="activeSegaButtons['z'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-white'" class="w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold transition">Z</div>
              </div>
              
              <!-- Нижний ряд: A B C -->
              <div class="flex space-x-3">
                <div :class="activeSegaButtons['a'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-slate-300'" class="w-9 h-9 rounded-full flex items-center justify-center font-bold transition">A</div>
                <div :class="activeSegaButtons['b'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-white'" class="w-9 h-9 rounded-full flex items-center justify-center font-bold transition">B</div>
                <div :class="activeSegaButtons['c'] ? 'bg-yellow-400 text-slate-900' : 'bg-slate-600 text-slate-300'" class="w-9 h-9 rounded-full flex items-center justify-center font-bold transition">C</div>
              </div>
            </div>

          </div>
        </div>

        <!-- Лог сырых данных -->
        <div class="bg-slate-900 text-emerald-400 p-4 rounded-xl shadow font-mono text-sm space-y-2">
          <div class="text-slate-400 text-xs uppercase tracking-wider font-sans">{{ t.rawReportTitle }}</div>
          <div class="break-all tracking-widest text-lg">{{ rawReportHex || t.waitingInput }}</div>
          <div class="grid grid-cols-8 gap-1 pt-2 text-center text-xs text-slate-400">
            <div v-for="(byte, idx) in lastReportBytes" :key="idx" class="bg-slate-800 p-1 rounded">
              B{{ idx }}: <span class="text-white">{{ byte }}</span>
            </div>
          </div>
        </div>

      </div>
    </div>
  </div>
</template>
