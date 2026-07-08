const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const source = fs.readFileSync('/home/runner/work/wavin_no_mqtt/wavin_no_mqtt/wavin_helene.ino', 'utf8');

test('handleRoot streams chunked HTML and yields per channel', () => {
  assert.match(source, /server\.setContentLength\(CONTENT_LENGTH_UNKNOWN\);/);
  assert.match(source, /server\.send\(200,\s*"text\/html",\s*""\);/);
  assert.match(source, /server\.sendContent\(F\("<!doctype html>/);
  assert.match(source, /appendChannelCard\(card,\s*data\);\s*[\s\S]*server\.sendContent\(card\);\s*[\s\S]*yield\(\);/);
  assert.match(source, /footer \+= F\("<\/div><\/body><\/html>"\);/);
});

test('setup prints startup diagnostics', () => {
  assert.match(source, /Serial\.begin\(115200\);/);
  assert.match(source, /Serial\.println\(ESP\.getResetReason\(\)\);/);
  assert.match(source, /Serial\.printf\("Free heap at boot: %u\\n",\s*ESP\.getFreeHeap\(\)\);/);
});
