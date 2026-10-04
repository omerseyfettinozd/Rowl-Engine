import test from 'node:test';
import assert from 'node:assert/strict';
import { initialState, openView, setCapacity, togglePin, closeView } from './workspace-state.mjs';
import { initialLayout, layoutViews, leaves, syncLayout, dockView, dockToWorkspace, floatView, clampRect, resizeSplit } from './layout-state.mjs';

function check(layout, state) {
  const views = layoutViews(layout);
  assert.deepEqual([...views].sort(), [...state.visible].sort());
  assert.equal(new Set(views).size, views.length);
  function visit(tree) {
    if (!tree || tree.type === 'leaf') return;
    assert.ok(tree.a && tree.b);
    assert.ok(tree.ratio >= .2 && tree.ratio <= .8);
    visit(tree.a); visit(tree.b);
  }
  visit(layout.tree);
  for (const rect of Object.values(layout.floating)) {
    assert.ok(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= 100 && rect.y + rect.h <= 100);
  }
}
function three() {
  const state = setCapacity(initialState(), 3).state;
  return {state, layout:syncLayout(initialLayout(), initialState(), state)};
}

test('Node yanında Game ve Edit Scene üst üste; kardeşi taşıma ağacı sadeleştirir', () => {
  let {layout, state} = three();
  assert.equal(layout.tree.axis, 'x');
  assert.equal(layout.tree.a.view, 'node');
  assert.equal(layout.tree.b.axis, 'y');
  layout = dockView(layout, 'edit', 'game', 'top');
  assert.deepEqual(leaves(layout.tree.b), ['edit', 'game']);
  assert.equal(layout.tree.b.axis, 'y');
  layout = dockView(layout, 'node', 'edit', 'right');
  check(layout, state);
  assert.equal(layout.tree.a.axis, 'x');
  assert.deepEqual(leaves(layout.tree.a), ['edit', 'node']);
});

test('Sabit olmayan üçüncü ekran aynı serbest pencere konumunu devralır', () => {
  const initial = initialState();
  let state = togglePin(openView(initial, 'game').state, 'game').state;
  let layout = syncLayout(initialLayout(), initial, state);
  layout = floatView(layout, 'node', {x:10,y:12,w:40,h:60});
  const next = openView(state, 'edit').state;
  layout = syncLayout(layout, state, next);
  assert.deepEqual(layout.floating.edit, {x:10,y:12,w:40,h:60});
  assert.deepEqual(leaves(layout.tree), ['game']);
  assert.deepEqual(next.pinned, ['game']);
  check(layout, next);
});

test('Serbest pencereler tekrar dört yönde dock edilebilir ve kopyalanmaz', () => {
  for (const edge of ['left','right','top','bottom']) {
    let {layout, state} = three();
    layout = floatView(floatView(layout,'game'), 'edit');
    layout = dockToWorkspace(layout,'game');
    layout = dockView(layout,'edit','game',edge);
    check(layout,state);
    assert.deepEqual(Object.keys(layout.floating), []);
    assert.equal(layout.tree.b.axis, ['left','right'].includes(edge)?'x':'y');
  }
});

test('Tek serbest pencere boş çalışma alanına geri yerleşir', () => {
  const floating = floatView(initialLayout(),'node');
  assert.equal(floating.tree,null);
  const docked = dockToWorkspace(floating,'node');
  assert.deepEqual(docked.tree,{type:'leaf',view:'node'});
  assert.deepEqual(docked.floating,{});
});

test('Mobil başlangıçtan üçlü görünüm pencere alanını eşit paylaşır', () => {
  const state = setCapacity(initialState(),3).state;
  const layout = syncLayout(initialLayout(),initialState(),state,'y');
  assert.equal(layout.tree.axis,'y');
  assert.equal(layout.tree.ratio,1/3);
  assert.equal(layout.tree.b.axis,'y');
  check(layout,state);
});

test('Geçersiz ve kendi üzerine bırakma mevcut yerleşimi korur', () => {
  const {layout} = three();
  assert.equal(dockView(layout,'game','game','top'),layout);
  assert.equal(dockView(layout,'game','missing','left'),layout);
  assert.equal(dockView(layout,'game','node','wrong'),layout);
});

test('Kapasite düşürme serbest sabit paneli korur, kapatılan dalı kaldırır', () => {
  let {layout,state} = three();
  layout = floatView(layout,'game');
  state = togglePin(state,'game').state;
  const next = setCapacity(state,2).state;
  layout = syncLayout(layout,state,next); check(layout,next);
  assert.ok(layout.floating.game);
  const after = closeView(next,'edit').state;
  layout = syncLayout(layout,next,after); check(layout,after);
});

test('Yeniden boyutlandırma erişilebilir sınırları korur', () => {
  const {layout} = three();
  assert.equal(resizeSplit(layout,layout.tree.id,0).tree.ratio,.2);
  assert.equal(resizeSplit(layout,layout.tree.id,1).tree.ratio,.8);
  assert.deepEqual(clampRect({x:-40,y:100,w:150,h:50}),{x:0,y:50,w:100,h:50});
});

test('1000 karma taşıma/açma/kapama adımında her ekran yalnız bir kez bulunur', () => {
  let {state,layout} = three(), seed=7;
  const random = n => { seed=(seed*1664525+1013904223)>>>0; return seed%n; };
  for (let i=0;i<1000;i++) {
    const view = ['node','game','edit'][random(3)], target = ['node','game','edit'][random(3)];
    const action = random(5);
    if (action === 0) layout = dockView(layout,view,target,['left','right','top','bottom'][random(4)]);
    else if (action === 1) layout = floatView(layout,view,{x:random(90),y:random(90),w:30+random(60),h:30+random(60)});
    else {
      const next = action === 2 ? openView(state,view).state : action === 3 ? closeView(state,view).state : setCapacity(state,random(2)?2:3).state;
      layout = syncLayout(layout,state,next); state = next;
    }
    check(layout,state);
  }
});
