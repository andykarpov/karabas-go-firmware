<script setup lang="ts">
import { ref, reactive } from 'vue';
import { HidParserService } from '../services/HidParserService';
import type { GamepadConfig } from '../services/HidParserService';

// Список стандартных кнопок Sega MD2 для маппинга
const MD2_BUTTONS = ['up', 'down', 'left', 'right', 'a', 'b', 'c', 'x', 'y', 'z', 'start', 'mode'];

const device = ref<any>(null);
const rawReportHex = ref<string>('');
const lastReportBytes = ref<number[]>([]);
const activeSegaButtons = ref<Record<string, boolean>>({});

// Храним текущую конфигурацию маппинга
const config = reactive<GamepadConfig>({
  vid: '',
  pid: '',
  name: '',
  buttons: {}
});

// Выбранная пользователем кнопка для назначения прямо сейчас
const currentTargetButton = ref<string | null>(null);

// Храним слепок репорта в момент начала записи кнопки
const baselineReport = ref<number[] | null>(null);

// Хранит базовый репорт покоя всего устройства для рендеринга подсветки
const deviceBaseline = ref<number[]>([]);

const connectDevice = async () => {
  try {
    const nav = navigator as any;
    const devices = await nav.hid.requestDevice({ filters: [] });
    if (devices.length === 0) return;
    
    device.value = devices[0];
    await device.value.open();
    
    config.vid = device.value.vendorId.toString(16).padStart(4, '0');
    config.pid = device.value.productId.toString(16).padStart(4, '0');
    config.name = device.value.productName || 'Unknown HID Gamepad';
    
    device.value.oninputreport = handleInputReport;
  } catch (err) {
    console.error('Ошибка подключения HID:', err);
  }
};

const handleInputReport = (event: Event) => {
  // Приводим к any, чтобы прочитать свойство .data без ошибок компиляции
  const hidEvent = event as any;
  const data = hidEvent.data;
  if (!data) return;

  const bytes: number[] = [];

  for (let i = 0; i < data.byteLength; i++) {
    bytes.push(data.getUint8(i));
  }

  lastReportBytes.value = bytes;
  rawReportHex.value = bytes.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');

  // Если базовый слепок устройства еще не сделан, сохраняем первый пришедший пакет как покой
  if (deviceBaseline.value.length === 0) {
    deviceBaseline.value = [...bytes];
  }

  // 1. Логика записи (автодетекта) кнопки
  if (currentTargetButton.value) {
    autoDetectMapping(data, currentTargetButton.value);
  }

  // 2. Рассчитываем подсветку кнопок, передавая deviceBaseline
  const active: Record<string, boolean> = {};
  for (const btn of MD2_BUTTONS) {
    const mapping = config.buttons[btn];
    if (mapping) {
      // Передаем сохраненный deviceBaseline третьим аргументом
      active[btn] = HidParserService.isButtonPressed(data, mapping, deviceBaseline.value);
    }
  }
  activeSegaButtons.value = active;
};

// Модифицируем функцию старта записи (вызывается при клике на "Записать")
const startRecording = (buttonName: string) => {
  currentTargetButton.value = buttonName;
  // Фиксируем текущее состояние геймпада как "покой"
  if (lastReportBytes.value.length > 0) {
    baselineReport.value = [...lastReportBytes.value];
  } else {
    baselineReport.value = null;
  }
};

const autoDetectMapping = (dataView: DataView, buttonName: string) => {
  if (!baselineReport.value || baselineReport.value.length !== dataView.byteLength) {
    const current: number[] = [];
    for (let i = 0; i < dataView.byteLength; i++) {
      current.push(dataView.getUint8(i));
    }
    baselineReport.value = current;
    return;
  }

  for (let i = 0; i < dataView.byteLength; i++) {
    const currentVal = dataView.getUint8(i);
    const baseVal = baselineReport.value[i];

    if (currentVal === baseVal) continue;

    // --- ПРИОРИТЕТ 1: АНАЛОГОВЫЕ ОСИ И HAT-SWITCH ---
    // Если значение улетело в крайние точки (0 или 255) — это гарантированно ось/крестовина.
    // Также проверяем классический HAT-switch (если в покое было 15 или 0x0F, а стало от 0 до 7).
    if (currentVal === 0 || currentVal === 255 || (baseVal === 0x0F && currentVal <= 7)) {
      config.buttons[buttonName] = { byteIndex: i, isBitmask: 0, valueOrBit: currentVal };
      currentTargetButton.value = null;
      baselineReport.value = null;
      return;
    }

    // --- ПРИОРИТЕТ 2: ЦИФРОВЫЕ КНОПКИ (БИТЫ) ---
    // Выполняется только если значение НЕ является крайним для оси.
    for (let bit = 0; bit < 8; bit++) {
      const bitMask = 1 << bit;
      const currentBit = (currentVal & bitMask) !== 0;
      const baseBit = (baseVal & bitMask) !== 0;

      if (currentBit !== baseBit && currentBit === true) {
        config.buttons[buttonName] = { byteIndex: i, isBitmask: 1, valueOrBit: bit };
        currentTargetButton.value = null;
        baselineReport.value = null;
        return;
      }
    }
  }
};

const downloadIni = () => {
  const iniText = HidParserService.generateIni(config);
  const blob = new Blob([iniText], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${config.name.replace(/\s+/g, '_')}.ini`;
  a.click();
  URL.revokeObjectURL(url);
};
</script>

<template>
  <div class="p-6 max-w-5xl mx-auto space-y-6 text-slate-800">
    <header class="flex justify-between items-center bg-white p-4 rounded-xl shadow">
      <div>
        <h1 class="text-xl font-bold">Karabas USB HID Gamepad Mapper</h1>
        <p class="text-sm text-slate-500" v-if="device">Устройство: {{ config.name }} (VID: {{ config.vid }}, PID: {{ config.pid }})</p>
      </div>
      <button v-if="!device" @click="connectDevice" class="bg-indigo-600 hover:bg-indigo-700 text-white px-4 py-2 rounded-lg font-medium transition">
        Подключить геймпад
      </button>
      <button v-else @click="downloadIni" class="bg-emerald-600 hover:bg-emerald-700 text-white px-4 py-2 rounded-lg font-medium transition">
        Скачать .INI драйвер
      </button>
    </header>

    <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
      <!-- Панель маппинга -->
      <div class="bg-white p-4 rounded-xl shadow space-y-3">
        <h2 class="font-bold border-b pb-2">Назначение кнопок</h2>
        <div v-for="btn in MD2_BUTTONS" :key="btn" class="flex flex-col p-1 rounded bg-slate-50 space-y-0">
          <div class="flex items-center justify-between">
            <span class="uppercase font-mono text-sm font-semibold text-slate-700">{{ btn }}</span>
            <button 
              @click="startRecording(btn)"
              :class="currentTargetButton === btn ? 'bg-amber-500 text-white' : config.buttons[btn] ? 'bg-green-500 text-white' : 'bg-indigo-50 hover:bg-indigo-100 text-indigo-600'"
              class="text-xs px-3 py-1 rounded font-medium transition"
            >
              {{ currentTargetButton === btn ? 'Нажмите кнопку...' : config.buttons[btn] ? 'Перезаписать' : 'Записать' }}
            </button>
          </div>
          
          <!-- Инпуты ручной корректировки, если маппинг уже существует -->
          <!-- <div v-if="config.buttons[btn]" class="grid grid-cols-3 gap-1 pt-1">
            <label class="text-[10px] text-slate-400">Байт:
              <input type="number" v-model.number="config.buttons[btn].byteIndex" class="w-full text-xs p-1 border rounded bg-white font-mono" />
            </label>
            <label class="text-[10px] text-slate-400">Тип (0/1):
              <input type="number" v-model.number="config.buttons[btn].isBitmask" min="0" max="1" class="w-full text-xs p-1 border rounded bg-white font-mono" />
            </label>
            <label class="text-[10px] text-slate-400">Бит/Знач:
              <input type="number" v-model.number="config.buttons[btn].valueOrBit" class="w-full text-xs p-1 border rounded bg-white font-mono" />
            </label>
          </div> -->
        </div>
      </div>

      <!-- Монитор репортов и Визуализация Sega MD2 геймпада -->
      <div class="md:col-span-2 space-y-6">

        <!-- Схематичный геймпад Sega MD2 -->
        <div class="bg-white p-4 rounded-xl shadow flex flex-col items-center justify-center min-h-[300px]">
          
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
          <div class="text-slate-400 text-xs uppercase tracking-wider font-sans">Сырой USB Report (Hex):</div>
          <div class="break-all tracking-widest text-lg">{{ rawReportHex || 'Ожидание ввода...' }}</div>
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
