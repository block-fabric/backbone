// The pages of a sidechain in a frame of the Chains app (doc/unified-app.md §13). They are served under an origin of
// their own (chains-pack://<stem>/), so they cannot reach the app's page: their bridge sends each request to it as a
// message, and the app (lib/packs.svelte.ts), knowing which frame sent it, sends it to this sidechain's node or answers
// it itself (webui/sidechainpages.h). Its answers and the node's events come back as messages from the app alone.
// Loaded before the pages' own script (a classic script runs before a module): window.__chains is ready when they start.
(function () {
  'use strict';
  var APP = 'chains-app://app';
  var next = 1;
  var pending = new Map();
  window.__chains = function (request) {
    return new Promise(function (resolve, reject) {
      var id = next++;
      pending.set(id, { resolve: resolve, reject: reject });
      window.parent.postMessage({ chainsPack: 1, id: id, request: request }, APP);
    });
  };
  // Module mode (doc/unified-app.md §14): these pages bring no chrome of their own; they tell the app what they have
  // and where they are, the app tells them its look and the page to show. What the app said before they listened waits.
  var listeners = [];
  var early = [];
  window.__chainsModule = {
    version: 1,
    post: function (type, data) {
      window.parent.postMessage({ chainsModule: 1, type: type, data: data }, APP);
    },
    on: function (fn) {
      listeners.push(fn);
      var waiting = early;
      early = [];
      waiting.forEach(function (m) { fn(m.type, m.data); });
    },
  };
  window.addEventListener('message', function (event) {
    // Only the app that shows these pages speaks to them.
    if (event.source !== window.parent) return;
    var data = event.data;
    if (!data || typeof data !== 'object') return;
    if (data.chainsPackReply === 1) {
      var waiting = pending.get(data.id);
      if (!waiting) return;
      pending.delete(data.id);
      if (data.ok) waiting.resolve(data.value);
      else waiting.reject(data.error);
    } else if (data.chainsPackEvents === 1 && typeof data.batch === 'string' && typeof window.__chainsEvents === 'function') {
      window.__chainsEvents(data.batch);
    } else if (data.chainsModuleHost === 1 && typeof data.type === 'string') {
      if (listeners.length === 0) early.push(data);
      else listeners.forEach(function (fn) { fn(data.type, data.data); });
    }
  });
})();
