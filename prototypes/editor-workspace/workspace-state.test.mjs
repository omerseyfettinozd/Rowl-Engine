import test from 'node:test';
import assert from 'node:assert/strict';
import { VIEWS, initialState, focusView, openView, closeView, toggleView, togglePin, setCapacity } from './workspace-state.mjs';

test('Üst simge ikinci tıklamada sabit ve son pencereyi de kapatır', () => {
  for (const view of VIEWS) {
    const empty = closeView(initialState(), 'node').state;
    const opened = toggleView(empty, view).state;
    const pinned = togglePin(opened, view).state;
    const closed = toggleView(pinned, view).state;
    assert.deepEqual(closed.visible, []);
    assert.deepEqual(closed.pinned, []);
    assert.equal(closed.focused, null);
  }
});

test('Game sabitken üçüncü ekran Node alanını değiştirir', () => {
  let state = openView(initialState(), 'game').state;
  state = togglePin(state, 'game').state;
  state = openView(state, 'edit').state;
  assert.deepEqual(state.visible, ['edit', 'game']);
  assert.deepEqual(state.pinned, ['game']);
  state = openView(state, 'node').state;
  assert.deepEqual(state.visible, ['node', 'game']);
});

test('Üçlü görünüm bütün ekranları açar; ikiye dönüş sabit ekranı korur', () => {
  let state = togglePin(openView(initialState(), 'game').state, 'game').state;
  state = setCapacity(state, 3).state;
  assert.deepEqual(state.visible, VIEWS);
  state = focusView(state, 'edit');
  state = setCapacity(state, 2).state;
  assert.deepEqual(state.visible, ['game', 'edit']);
  assert.equal(state.focused, 'edit');
});

test('Bütün paneller sabitken yer değiştirme ve kapasite daraltma açıklanır', () => {
  let state = setCapacity(initialState(), 3).state;
  for (const view of VIEWS) state = togglePin(state, view).state;
  assert.equal(setCapacity(state, 2).state, state);
  assert.ok(setCapacity(state, 2).message);
  state = togglePin(state, 'edit').state;
  state = setCapacity(state, 2).state;
  assert.deepEqual(state.visible, ['node', 'game']);
  assert.equal(openView(state, 'edit').state, state);
  assert.ok(openView(state, 'edit').message);
});

test('Elle kapatma sabit pencereyi de kapatır; sabitleme temizlenir', () => {
  const state = initialState();
  const pinned = togglePin(openView(state, 'game').state, 'game').state;
  const closed = closeView(pinned, 'game').state;
  assert.deepEqual(closed.visible, ['node']);
  assert.deepEqual(closed.pinned, []);
  assert.equal(closed.focused, 'node');
});

test('Son pencere kapanabilir; boş alandan herhangi bir ekran yeniden açılır', () => {
  const closed = closeView(initialState(), 'node').state;
  assert.deepEqual(closed.visible, []);
  assert.equal(closed.focused, null);
  for (const view of VIEWS) {
    const reopened = openView(closed, view).state;
    assert.deepEqual(reopened.visible, [view]);
    assert.equal(reopened.focused, view);
  }
  const three = setCapacity(closed, 3).state;
  assert.deepEqual(three.visible, VIEWS);
  assert.equal(three.focused, 'node');
});

test('Erişilebilir tüm durumlarda kapasite, odak ve sabit panel kuralları korunur', () => {
  const queue = [initialState()], seen = new Set();
  while (queue.length) {
    const state = queue.pop(), key = JSON.stringify(state);
    if (seen.has(key)) continue;
    seen.add(key);
    assert.ok(state.visible.length <= state.capacity);
    assert.equal(new Set(state.visible).size, state.visible.length);
    assert.ok(state.visible.length ? state.visible.includes(state.focused) : state.focused === null);
    assert.ok(state.pinned.every(v => state.visible.includes(v)));
    assert.ok(state.visible.every(v => state.recent.includes(v)));
    for (const view of VIEWS) {
      for (const transition of [openView, closeView, toggleView, togglePin]) {
        const next = transition(state, view).state;
        // Automatic replacement preserves pins; explicit closing is allowed.
        if (transition === openView) assert.ok(state.pinned.every(v => next.visible.includes(v)));
        queue.push(next);
      }
      queue.push(focusView(state, view));
    }
    for (const capacity of [2, 3]) queue.push(setCapacity(state, capacity).state);
  }
  assert.ok(seen.size > 100);
});
