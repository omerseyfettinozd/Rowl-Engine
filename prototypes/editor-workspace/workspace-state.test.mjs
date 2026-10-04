import test from 'node:test';
import assert from 'node:assert/strict';
import { VIEWS, initialState, focusView, openView, closeView, toggleView, togglePin } from './workspace-state.mjs';
test('Her simge ikinci tıklamada sabit ve son pencereyi de kapatır',()=>{
  for(const view of VIEWS){const empty=closeView(initialState(),'node').state;const pinned=togglePin(toggleView(empty,view).state,view).state;const closed=toggleView(pinned,view).state;assert.deepEqual(closed.visible,[]);assert.deepEqual(closed.pinned,[]);assert.equal(closed.focused,null);}
});
test('Yedi pencere sabitlemelerden bağımsız açılır; mevcut pencere kapanmaz',()=>{
  let state=initialState();for(const view of VIEWS){const before=state.visible;state=openView(state,view).state;assert.ok(before.every(v=>state.visible.includes(v)));state=togglePin(state,view).state;}assert.deepEqual(state.visible,VIEWS);assert.deepEqual(state.pinned,VIEWS);assert.equal('capacity' in state,false);
});
test('Tekrar açma kopyalamaz ve yalnız odak değiştirir',()=>{
  let state=openView(initialState(),'game').state;state=openView(state,'node').state;assert.deepEqual(state.visible,['node','game']);assert.equal(state.focused,'node');assert.equal(openView(state,'missing').state,state);
});
test('Elle kapatma sabitlemeyi temizler ve odak açık pencereye döner',()=>{
  const state=togglePin(openView(initialState(),'game').state,'game').state;const next=closeView(state,'game').state;assert.deepEqual(next.visible,['node']);assert.deepEqual(next.pinned,[]);assert.equal(next.focused,'node');
});
test('1000 karma işlemde açık pencereler, sabitlemeler ve odak tutarlı kalır',()=>{
  let state=initialState(),seed=42;const random=n=>{seed=(seed*1664525+1013904223)>>>0;return seed%n;};const transitions=[openView,closeView,toggleView,togglePin];
  for(let i=0;i<1000;i++){const view=VIEWS[random(VIEWS.length)];state=transitions[random(4)](state,view).state;state=focusView(state,VIEWS[random(VIEWS.length)]);assert.equal(new Set(state.visible).size,state.visible.length);assert.ok(state.visible.length?state.visible.includes(state.focused):state.focused===null);assert.ok(state.pinned.every(v=>state.visible.includes(v)));assert.ok(state.visible.every(v=>state.recent.includes(v)));assert.equal(new Set(state.recent).size,state.recent.length);}
});
