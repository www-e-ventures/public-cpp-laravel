// livewire_client.hpp — the browser runtime served at GET /livewire.js.
//
// Each [wire:id] element is an independent component root (so nesting works). Event
// delegation per root handles wire:click (actions) and wire:model (two-way binding).
// Responses are applied by DOM morphing (patch in place), which preserves the focused
// input's cursor and skips nested [wire:id] subtrees (a parent re-render never clobbers
// a child's live state).
#pragma once
#include <string>

inline std::string livewire_js() {
    return R"JS(
(function () {
  function getState(el) { return JSON.parse(el.getAttribute('wire:state')); }
  function setState(el, s) { el.setAttribute('wire:state', JSON.stringify(s)); }

  // wire:loading — show matching elements only while a request is in flight.
  // An element may scope itself to an action with wire:target="action".
  function setLoading(root, on, action) {
    root.querySelectorAll('[wire\\:loading]').forEach(function (el) {
      var target = el.getAttribute('wire:target');
      if (!target || target === action) el.style.display = on ? '' : 'none';
    });
  }
  function hideLoading(root) {
    root.querySelectorAll('[wire\\:loading]').forEach(function (el) { el.style.display = 'none'; });
  }

  function post(el, action, arg) {
    setLoading(el, true, action);
    return fetch('/livewire', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: el.getAttribute('wire:component'), action: action, arg: arg || '', state: getState(el)
      })
    })
      .then(function (r) { return r.json(); })
      .then(function (res) { setState(el, res.state); morph(el, res.html); })
      .then(function () { setLoading(el, false, action); })
      .catch(function () { setLoading(el, false, action); });
  }

  // ---- DOM morphing (a tiny morphdom) ----
  function morph(rootEl, html) {
    var tmp = document.createElement('div');
    tmp.innerHTML = html;
    reconcile(rootEl, tmp);
  }
  function reconcile(oldEl, newEl) {
    var n = Array.prototype.slice.call(newEl.childNodes);
    // Keyed reconciliation: if the new children carry wire:key, match by key so
    // nodes keep their identity (and DOM state) across reordering/insertion/removal.
    if (n.some(function (c) { return c.nodeType === 1 && c.hasAttribute('wire:key'); })) {
      reconcileKeyed(oldEl, newEl);
      return;
    }
    var o = Array.prototype.slice.call(oldEl.childNodes);
    for (var i = 0; i < n.length; i++) {
      var nc = n[i], oc = o[i];
      if (!oc) { oldEl.appendChild(nc.cloneNode(true)); continue; }
      if (oc.nodeType !== nc.nodeType || (oc.nodeType === 1 && oc.tagName !== nc.tagName)) {
        oldEl.replaceChild(nc.cloneNode(true), oc); continue;
      }
      if (oc.nodeType === 3) { if (oc.nodeValue !== nc.nodeValue) oc.nodeValue = nc.nodeValue; continue; }
      if (oc.nodeType === 1 && oc.hasAttribute('wire:id')) continue; // a nested component: leave it
      syncAttrs(oc, nc);
      reconcile(oc, nc);
    }
    for (var j = n.length; j < o.length; j++) oldEl.removeChild(o[j]);
  }
  function reconcileKeyed(oldEl, newEl) {
    var oldByKey = {};
    Array.prototype.slice.call(oldEl.childNodes).forEach(function (c) {
      if (c.nodeType === 1 && c.hasAttribute('wire:key')) oldByKey[c.getAttribute('wire:key')] = c;
    });
    var result = [];
    Array.prototype.slice.call(newEl.childNodes).forEach(function (nc) {
      if (nc.nodeType !== 1) return; // ignore whitespace between items
      var key = nc.getAttribute('wire:key');
      var oc = key ? oldByKey[key] : null;
      if (oc) {
        if (!oc.hasAttribute('wire:id')) { syncAttrs(oc, nc); reconcile(oc, nc); } // patch in place
        result.push(oc);
        delete oldByKey[key];
      } else {
        result.push(nc.cloneNode(true)); // brand-new item
      }
    });
    // drop old items no longer present, then append in the new order (moves nodes).
    Array.prototype.slice.call(oldEl.childNodes).forEach(function (c) {
      if (result.indexOf(c) === -1) oldEl.removeChild(c);
    });
    result.forEach(function (node) { oldEl.appendChild(node); });
  }

  function syncAttrs(o, n) {
    Array.prototype.slice.call(o.attributes).forEach(function (a) {
      if (!n.hasAttribute(a.name)) o.removeAttribute(a.name);
    });
    Array.prototype.slice.call(n.attributes).forEach(function (a) {
      if (o.getAttribute(a.name) !== a.value) o.setAttribute(a.name, a.value);
    });
    if (o.tagName === 'INPUT' && o !== document.activeElement) {
      var want = n.getAttribute('value') || '';
      if (o.value !== want) o.value = want;
    }
  }

  // ---- per-root event delegation ----
  function initRoot(root) {
    root.addEventListener('click', function (e) {
      var b = e.target.closest('[wire\\:click]');
      if (!b || b.closest('[wire\\:id]') !== root) return; // belongs to a nested root
      var m = b.getAttribute('wire:click').match(/^(\w+)(?:\((.*)\))?$/);
      post(root, m[1], m[2] || '');
    });
    root.addEventListener('input', function (e) {
      var i = e.target;
      if (!i.hasAttribute || !i.hasAttribute('wire:model')) return;
      if (i.closest('[wire\\:id]') !== root) return;
      var state = getState(root);
      state[i.getAttribute('wire:model')] = i.value;
      setState(root, state);
      post(root, '$refresh', '');
    });
  }

  document.addEventListener('DOMContentLoaded', function () {
    document.querySelectorAll('[wire\\:id]').forEach(function (root) {
      hideLoading(root); // hidden until a request is in flight
      initRoot(root);
    });
  });
})();
)JS";
}
