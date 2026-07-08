const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync('/home/runner/work/wavin_no_mqtt/wavin_no_mqtt/wavin_helene.ino', 'utf8');
const defines = Object.fromEntries(
  Array.from(source.matchAll(/^#define\s+(\w+)\s+(\d+)$/gm), (match) => [match[1], Number(match[2])]),
);

function parseHtmlAppendLine(line) {
  const match = line.match(/html \+= (.*);$/);
  if (!match) {
    return null;
  }

  const tokens = match[1].match(/"(?:[^"\\]|\\.)*"|String\([^)]+\)/g);
  return (tokens || []).map((token) => {
    if (token.startsWith('"')) {
      return JSON.parse(token);
    }

    const key = token.slice(7, -1).trim();
    assert.ok(Object.prototype.hasOwnProperty.call(defines, key), `missing define for ${key}`);
    return String(defines[key]);
  }).join('');
}

function extractScript() {
  const startMarker = 'html += "</style><script>";';
  const start = source.indexOf(startMarker);
  const end = source.indexOf('html += "</script></head><body><h1>Wavin Styring</h1>";');

  assert.notEqual(start, -1, 'script start marker should exist');
  assert.notEqual(end, -1, 'script end marker should exist');
  const lines = source.slice(start + startMarker.length, end).split('\n');
  return lines
    .map(parseHtmlAppendLine)
    .filter((line) => line !== null)
    .join('');
}

function createElement(textContent = '') {
  return {
    textContent,
    className: '',
    attributes: {},
    setAttribute(name, value) {
      this.attributes[name] = String(value);
    },
    getAttribute(name) {
      return Object.prototype.hasOwnProperty.call(this.attributes, name) ? this.attributes[name] : null;
    },
  };
}

function createHarness() {
  const timers = new Map();
  let nextTimerId = 1;
  const fetchCalls = [];
  const refreshRequests = [];
  const serverState = { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 1' };
  const target = createElement('21.0°C');
  target.setAttribute('data-target-tenths', '210');
  const elements = {
    'target-0': target,
    'temp-0': createElement('20.0°C'),
    'name-0': createElement('Zone 1'),
    'standby-0': createElement('18.0°C'),
    'battery-0': createElement('90%'),
    'heat-0': createElement('Sluk'),
    'mode-0': createElement('Manuel'),
  };

  const context = {
    document: {
      getElementById(id) {
        return elements[id] || null;
      },
    },
    fetch(url) {
      fetchCalls.push(url);
      if (url === '/data') {
        return Promise.resolve({
          json() {
            refreshRequests.push(url);
            return Promise.resolve([{
              ch: serverState.ch,
              temp: serverState.temp,
              target: serverState.target,
              standby: serverState.standby,
              battery: serverState.battery,
              heating: serverState.heating,
              mode: serverState.mode,
              name: serverState.name,
            }]);
          },
        });
      }

      return Promise.resolve({
        ok: true,
      });
    },
    setTimeout(callback, delay) {
      const id = nextTimerId++;
      timers.set(id, { callback, delay, cleared: false });
      return id;
    },
    clearTimeout(id) {
      const timer = timers.get(id);
      if (timer) {
        timer.cleared = true;
      }
    },
    setInterval() {
      return 0;
    },
    console,
    Object,
    Number,
    String,
    Math,
    parseFloat,
    parseInt,
    isNaN,
  };

  vm.createContext(context);
  vm.runInContext(extractScript(), context);

  return {
    context,
    elements,
    fetchCalls,
    refreshRequests,
    serverState,
    runActiveTimeouts(delay) {
      for (const [id, timer] of Array.from(timers.entries())) {
        if (!timer.cleared && timer.delay === delay) {
          timers.delete(id);
          timer.callback();
        }
      }
    },
    activeTimeouts(delay) {
      return Array.from(timers.values()).filter((timer) => !timer.cleared && timer.delay === delay);
    },
  };
}

async function flushPromises() {
  await Promise.resolve();
  await Promise.resolve();
}

test('adjust uses 0.5°C steps without drift', () => {
  const harness = createHarness();

  harness.context.adjust(0, 5);
  assert.equal(harness.elements['target-0'].textContent, '21.5°C');

  harness.context.adjust(0, 5);
  assert.equal(harness.elements['target-0'].textContent, '22.0°C');

  harness.context.adjust(0, -5);
  assert.equal(harness.elements['target-0'].textContent, '21.5°C');
});

test('adjust updates the displayed target immediately and waits to push', () => {
  const harness = createHarness();

  harness.context.adjust(0, 5);

  assert.equal(harness.elements['target-0'].textContent, '21.5°C');
  assert.deepEqual(harness.fetchCalls, []);
  assert.equal(harness.activeTimeouts(800).length, 1);
});

test('rapid changes debounce into a single final push', () => {
  const harness = createHarness();

  harness.context.adjust(0, 5);
  harness.context.adjust(0, 5);
  harness.context.adjust(0, -5);

  assert.equal(harness.elements['target-0'].textContent, '21.5°C');
  assert.equal(harness.activeTimeouts(800).length, 1);
  assert.deepEqual(harness.fetchCalls, []);

  harness.runActiveTimeouts(800);

  assert.deepEqual(harness.fetchCalls, ['/set?ch=0&val=21.5']);
});

test('adjust keeps the target within configured min and max bounds', () => {
  const harness = createHarness();

  harness.elements['target-0'].textContent = '5.0°C';
  harness.elements['target-0'].setAttribute('data-target-tenths', '50');
  harness.context.adjust(0, -5);
  assert.equal(harness.elements['target-0'].textContent, '5.0°C');

  harness.elements['target-0'].textContent = '35.0°C';
  harness.elements['target-0'].setAttribute('data-target-tenths', '350');
  harness.context.adjust(0, 5);
  assert.equal(harness.elements['target-0'].textContent, '35.0°C');
});

test('refreshData does not overwrite a pending local setpoint change', async () => {
  const harness = createHarness();

  harness.context.adjust(0, 5);
  harness.context.refreshData();
  await flushPromises();

  assert.equal(harness.elements['target-0'].textContent, '21.5°C');
  assert.deepEqual(harness.fetchCalls, ['/data']);
});

test('toggleMode optimistically updates mode display to standby', async () => {
  const harness = createHarness();

  harness.context.toggleMode(0);

  assert.equal(harness.elements['mode-0'].textContent, 'Standby');
  assert.ok(harness.fetchCalls.some((u) => u === '/setmode?ch=0&mode=1'), 'should call /setmode with mode=1');
});

test('toggleMode switches standby back to manual', async () => {
  const harness = createHarness();
  harness.elements['mode-0'].textContent = 'Standby';

  harness.context.toggleMode(0);

  assert.equal(harness.elements['mode-0'].textContent, 'Manuel');
  assert.ok(harness.fetchCalls.some((u) => u === '/setmode?ch=0&mode=0'), 'should call /setmode with mode=0');
});

test('refreshData does not overwrite a pending mode change to standby', async () => {
  const harness = createHarness();

  harness.context.toggleMode(0);
  await flushPromises();

  harness.context.refreshData();
  await flushPromises();

  assert.equal(harness.elements['mode-0'].textContent, 'Standby');
});

test('mode remains standby across multiple refresh cycles until device confirms', async () => {
  const harness = createHarness();

  harness.context.toggleMode(0);
  await flushPromises();

  for (let i = 0; i < 3; i++) {
    harness.context.refreshData();
    await flushPromises();
    assert.equal(harness.elements['mode-0'].textContent, 'Standby', `refresh ${i + 1} should still show Standby`);
  }
});

test('refreshData clears pending mode once device confirms standby', async () => {
  const harness = createHarness();

  harness.context.toggleMode(0);
  await flushPromises();

  // device now reports standby
  harness.serverState.mode = 1;
  harness.context.refreshData();
  await flushPromises();

  assert.equal(harness.elements['mode-0'].textContent, 'Standby');

  // pending is cleared; further refreshes use device value directly
  harness.context.refreshData();
  await flushPromises();
  assert.equal(harness.elements['mode-0'].textContent, 'Standby');
});
