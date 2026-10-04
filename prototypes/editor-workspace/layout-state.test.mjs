import test from 'node:test';
import assert from 'node:assert/strict';
import { initialState, VIEWS, openView, togglePin, closeView } from './workspace-state.mjs';
import { initialLayout, layoutViews, leaves, syncLayout, dockView, resizeSplit } from './layout-state.mjs';

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
  checkNonoverlap(layout.tree);
}
function three() {
  const state = openView(openView(initialState(),'game').state,'edit').state;
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

test('Araç pencereleri ayrı bir sütunda toplanır; sabit kaynak taşınamaz', () => {
  let {layout,state}=three();
  for(const view of ['hierarchy','inspector','assets','console']){const next=openView(state,view).state;layout=syncLayout(layout,state,next);state=next;check(layout,state);}
  assert.equal(layout.tree.ratio,.74);
  assert.deepEqual(leaves(layout.tree.a),['node','game','edit']);
  assert.deepEqual(leaves(layout.tree.b),['hierarchy','inspector','assets','console']);
  state=togglePin(state,'hierarchy').state;
  assert.equal(dockView(layout,'hierarchy','node','top',state.pinned),layout);
});

test('Pencereler dört yönde alan bölerek yerleşir ve kopyalanmaz', () => {
  for (const edge of ['left','right','top','bottom']) {
    let {layout, state} = three();
    layout = dockView(layout,'edit','node',edge);
    check(layout,state);
    assert.equal(layout.tree.a.axis, ['left','right'].includes(edge)?'x':'y');
    assert.equal('floating' in layout,false);
  }
});

// Project the split tree onto a unit workspace and check actual area separation.
function checkNonoverlap(tree, rect={x:0,y:0,w:1,h:1}) {
  function areas(node, box) {
    if (!node) return [];
    if (node.type === 'leaf') return [box];
    const vertical=node.axis==='y', first=vertical?{...box,h:box.h*node.ratio}:{...box,w:box.w*node.ratio};
    const second=vertical?{...box,y:box.y+first.h,h:box.h-first.h}:{...box,x:box.x+first.w,w:box.w-first.w};
    return [...areas(node.a,first),...areas(node.b,second)];
  }
  const boxes=areas(tree,rect);
  for (let i=0;i<boxes.length;i++) {
    const a=boxes[i]; assert.ok(a.w>0 && a.h>0);
    for (const b of boxes.slice(i+1)) {
      const width=Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x);
      const height=Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y);
      assert.ok(width<=1e-10 || height<=1e-10, 'Pencere alanları üst üste bindi');
    }
  }
  if (tree) assert.ok(Math.abs(boxes.reduce((sum,b)=>sum+b.w*b.h,0)-rect.w*rect.h)<1e-10);
}

test('İç içe bölme uç boyutlarda da pencereleri üst üste bindirmez', () => {
  let {layout,state}=three();
  for (const edge of ['left','right','top','bottom']) {
    layout=dockView(layout,'node','edit',edge);
    for (const ratio of [.2,.8,.35,.65]) {
      layout=resizeSplit(layout,layout.tree.id,ratio);
      check(layout,state);
    }
  }
});

test('Mobil başlangıçtan üçlü görünüm pencere alanını eşit paylaşır', () => {
  const state = openView(openView(initialState(),'game').state,'edit').state;
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

test('Elle kapatma dalı kaldırır, diğer sabit pencereyi korur',()=>{
  let {layout,state}=three();state=togglePin(state,'game').state;
  const next=closeView(state,'edit').state;layout=syncLayout(layout,state,next);check(layout,next);
  assert.ok(leaves(layout.tree).includes('game'));
});

test('Yeniden boyutlandırma erişilebilir sınırları korur', () => {
  const {layout} = three();
  assert.equal(resizeSplit(layout,layout.tree.id,0).tree.ratio,.2);
  assert.equal(resizeSplit(layout,layout.tree.id,1).tree.ratio,.8);
  checkNonoverlap(resizeSplit(layout,layout.tree.id,.8).tree);
});

test('1000 karma işlemde her ekran tek alanda bulunur ve alanlar çakışmaz', () => {
  let {state,layout} = three(), seed=7;
  const random = n => { seed=(seed*1664525+1013904223)>>>0; return seed%n; };
  for (let i=0;i<1000;i++) {
    const view = VIEWS[random(VIEWS.length)], target = VIEWS[random(VIEWS.length)];
    const action = random(5);
    if (action === 0) layout = dockView(layout,view,target,['left','right','top','bottom'][random(4)],state.pinned);
    else if (action === 1 && layout.tree?.type==='split') layout = resizeSplit(layout,layout.tree.id,random(101)/100);
    else {
      const next = action === 2 ? openView(state,view).state : action === 3 ? closeView(state,view).state : togglePin(state,view).state;
      layout = syncLayout(layout,state,next); state = next;
    }
    check(layout,state);
  }
});
