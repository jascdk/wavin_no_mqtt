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
  const end = source.indexOf('html += "</script></head><body><h1>Wavin Styring</h1>');

  assert.notEqual(start, -1, 'script start marker should exist');
  assert.notEqual(end, -1, 'script end marker should exist');
  const lines = source.slice(start + startMarker.length, end).split('\n');
  return lines
    .map(parseHtmlAppendLine)
    .filter((line) => line !== null)
    .join('');
}

function createElement(registerElement, textContent = '') {
  const element = {
    textContent,
    className: '',
    attributes: {},
    style: {},
    children: [],
    parentNode: null,
    appendChild(child) {
      child.parentNode = this;
      this.children.push(child);
      return child;
    },
    insertBefore(child, referenceChild) {
      child.parentNode = this;
      if (!referenceChild) {
        this.children.push(child);
        return child;
      }

      const index = this.children.indexOf(referenceChild);
      if (index === -1) {
        this.children.push(child);
        return child;
      }

      this.children.splice(index, 0, child);
      return child;
    },
    setAttribute(name, value) {
      this.attributes[name] = String(value);
      if (name === 'id') {
        this.id = String(value);
      }
      if (name === 'class') {
        this.className = String(value);
      }
    },
    getAttribute(name) {
      if (name === 'id' && this.id) {
        return this.id;
      }
      return Object.prototype.hasOwnProperty.call(this.attributes, name) ? this.attributes[name] : null;
    },
  };

  let elementId = '';
  Object.defineProperty(element, 'id', {
    get() {
      return elementId;
    },
    set(value) {
      elementId = String(value);
      registerElement(elementId, element);
    },
  });

  Object.defineProperty(element, 'innerHTML', {
    get() {
      return this._innerHTML || '';
    },
    set(value) {
      this._innerHTML = String(value);
      const tagPattern = /<([a-z]+)([^>]*)id='([^']+)'([^>]*)>([^<]*)</g;
      let match;
      while ((match = tagPattern.exec(this._innerHTML)) !== null) {
        const [, , beforeIdAttributes, id, afterIdAttributes, text] = match;
        const child = createElement(registerElement, text);
        const attributes = `${beforeIdAttributes}${afterIdAttributes}`;
        const classMatch = attributes.match(/class='([^']+)'/);
        if (classMatch) {
          child.className = classMatch[1];
        }
        const dataTargetMatch = attributes.match(/data-target-tenths='([^']+)'/);
        if (dataTargetMatch) {
          child.setAttribute('data-target-tenths', dataTargetMatch[1]);
        }
        child.id = id;
      }
    },
  });

  return element;
}

function cloneItem(item) {
  return { ...item };
}

function renderCardMarkup(item) {
  const targetTenths = Math.round(Number(item.target) * 10);
  return `<div class='row'><div class='col-left'><b id='name-${item.ch}'>${item.name}</b><br><span id='temp-${item.ch}'>${Number(item.temp).toFixed(1)}°C</span><div class='badges'><span class='badge ${item.heating ? 'badge-heat' : 'badge-off'}' id='heat-${item.ch}'>${item.heating ? '🔥 Varme' : 'Sluk'}</span><span class='badge badge-mode' id='mode-${item.ch}' onclick='toggleMode(${item.ch})' style='cursor:pointer'>${item.mode === 0 ? 'Manuel' : 'Standby'}</span></div></div><div class='col-right'><div class='battery'>Batteri: <span id='battery-${item.ch}'>${item.battery}%</span></div><div class='controls'><button class='btn' onclick='adjust(${item.ch},-5)'>-</button><span class='target' id='target-${item.ch}' data-target-tenths='${targetTenths}'>${Number(item.target).toFixed(1)}°C</span><button class='btn' onclick='adjust(${item.ch},5)'>+</button></div><div class='standby'>Standby: <span id='standby-${item.ch}'>${Number(item.standby).toFixed(1)}°C</span></div></div></div>`;
}

function createHarness(options = {}) {
  const timers = new Map();
  let nextTimerId = 1;
  const fetchCalls = [];
  const refreshRequests = [];
  const defaultItems = [{ ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 1' }];
  const initialItems = (options.initialItems || defaultItems).map(cloneItem);
  let serverItems = (options.serverItems || initialItems).map(cloneItem);
  const elements = {};
  const registerElement = (id, element) => {
    elements[id] = element;
    return element;
  };
  const container = createElement(registerElement);
  container.id = 'channels';

  function addCard(item) {
    const card = createElement(registerElement);
    card.className = 'card';
    card.id = `card-${item.ch}`;
    card.setAttribute('data-ch', String(item.ch));
    card.innerHTML = renderCardMarkup(item);
    container.appendChild(card);
    return card;
  }

  initialItems.forEach(addCard);

  const context = {
    document: {
      getElementById(id) {
        return elements[id] || null;
      },
      createElement() {
        return createElement(registerElement);
      },
    },
    fetch(url) {
      fetchCalls.push(url);
      if (url === '/data') {
        return Promise.resolve({
          json() {
            refreshRequests.push(url);
            return Promise.resolve(serverItems.map(cloneItem));
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
    container,
    fetchCalls,
    refreshRequests,
    serverState: serverItems[0],
    setServerItems(items) {
      serverItems = items.map(cloneItem);
      this.serverState = serverItems[0];
    },
    cardOrder() {
      return container.children.map((child) => Number(child.getAttribute('data-ch')));
    },
    isCardVisible(ch) {
      const card = elements[`card-${ch}`];
      return !!card && card.style.display !== 'none';
    },
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
  for (let i = 0; i < 4; i++) {
    await Promise.resolve();
  }
}

test('battery status mapping treats 9 and above as fully charged', () => {
  const batteryMappingFn = source.match(/int batteryStatusToPercent\(uint16_t status\) \{([\s\S]*?)\n\}/);

  assert.notEqual(batteryMappingFn, null, 'battery status mapping function should exist');
  assert.match(batteryMappingFn[1], /status >= 9/, 'battery mapping should treat 9 as fully charged');
  assert.match(batteryMappingFn[1], /return 100;/, 'battery mapping should return 100 for full charge');
  assert.match(batteryMappingFn[1], /return status \* 10;/, 'battery mapping should keep 10% units');
});

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

test('refreshData inserts newly seen channels in channel order', async () => {
  const harness = createHarness({
    initialItems: [
      { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
      { ch: 2, temp: 22.0, target: 23.0, standby: 19.0, battery: 80, heating: true, mode: 1, name: 'Zone 2' },
    ],
  });

  harness.setServerItems([
    { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
    { ch: 1, temp: 21.0, target: 22.0, standby: 18.5, battery: 85, heating: false, mode: 0, name: 'Zone 1' },
    { ch: 2, temp: 22.0, target: 23.0, standby: 19.0, battery: 80, heating: true, mode: 1, name: 'Zone 2' },
  ]);

  harness.context.refreshData();
  await flushPromises();

  assert.deepEqual(harness.cardOrder(), [0, 1, 2]);
  assert.equal(harness.elements['name-1'].textContent, 'Zone 1');
});

test('refreshData keeps cards visible through transient misses before hiding them', async () => {
  const harness = createHarness({
    initialItems: [
      { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
      { ch: 1, temp: 21.0, target: 22.0, standby: 19.0, battery: 80, heating: true, mode: 1, name: 'Zone 1' },
    ],
  });

  harness.setServerItems([
    { ch: 0, temp: 20.5, target: 21.5, standby: 18.0, battery: 88, heating: true, mode: 0, name: 'Zone 0' },
  ]);

  harness.context.refreshData();
  await flushPromises();
  assert.equal(harness.isCardVisible(1), true);

  harness.context.refreshData();
  await flushPromises();
  assert.equal(harness.isCardVisible(1), true);

  harness.context.refreshData();
  await flushPromises();
  assert.equal(harness.isCardVisible(1), false);
});

test('refreshData restores a hidden card in place when data returns', async () => {
  const harness = createHarness({
    initialItems: [
      { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
      { ch: 1, temp: 21.0, target: 22.0, standby: 19.0, battery: 80, heating: true, mode: 1, name: 'Zone 1' },
      { ch: 2, temp: 22.0, target: 23.0, standby: 20.0, battery: 70, heating: false, mode: 0, name: 'Zone 2' },
    ],
  });

  harness.setServerItems([
    { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
    { ch: 2, temp: 22.0, target: 23.0, standby: 20.0, battery: 70, heating: false, mode: 0, name: 'Zone 2' },
  ]);

  for (let i = 0; i < 3; i++) {
    harness.context.refreshData();
    await flushPromises();
  }

  assert.equal(harness.isCardVisible(1), false);

  harness.setServerItems([
    { ch: 0, temp: 20.0, target: 21.0, standby: 18.0, battery: 90, heating: false, mode: 0, name: 'Zone 0' },
    { ch: 1, temp: 21.5, target: 22.5, standby: 19.5, battery: 82, heating: false, mode: 0, name: 'Zone 1 returned' },
    { ch: 2, temp: 22.0, target: 23.0, standby: 20.0, battery: 70, heating: false, mode: 0, name: 'Zone 2' },
  ]);

  harness.context.refreshData();
  await flushPromises();

  assert.equal(harness.isCardVisible(1), true);
  assert.deepEqual(harness.cardOrder(), [0, 1, 2]);
  assert.equal(harness.elements['name-1'].textContent, 'Zone 1 returned');
  assert.equal(harness.elements['target-1'].textContent, '22.5°C');
});
