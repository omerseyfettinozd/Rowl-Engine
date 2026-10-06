import test from 'node:test';
import assert from 'node:assert/strict';
import {initialLayout,syncLayout,removeView,leaves} from './layout-state.mjs';
import {initialState,openView,closeView} from './workspace-state.mjs';
import {minimumWidth,stackLayout,fitDockTree,preferredStackHeight,resizeStackPair} from './responsive-layout.mjs';

function dense() {
  let state=initialState(), layout=initialLayout();
  for (const view of ['game','edit','lua','inspector','library']) {
    const next=openView(state,view).state; layout=syncLayout(layout,state,next); state=next;
  }
  return {state,layout};
}
test('Game reopen does not multiply mobile panel heights by desktop ratios',()=>{
  let {state,layout}=dense();
  const before=leaves(layout.tree).reduce((sum,view)=>sum+preferredStackHeight(view,390),0);
  const closed=closeView(state,'game').state; layout=syncLayout(layout,state,closed,'y');
  const reopened=openView(closed,'game').state; layout=syncLayout(layout,closed,reopened,'y');
  assert.ok(stackLayout(layout.tree,390));
  assert.equal(preferredStackHeight('game',390),334);
  assert.equal(leaves(layout.tree).reduce((sum,view)=>sum+preferredStackHeight(view,390),0),before);
  assert.equal(stackLayout(layout.tree,1413),false,'desktop still has room after mobile reopen');
  assert.deepEqual(leaves(fitDockTree(layout.tree,1413)),leaves(layout.tree));
});
test('dense tablet stacks, desktop preserves every panel and readable widths',()=>{
  const {layout}=dense(), snapshot=structuredClone(layout);
  assert.equal(stackLayout(layout.tree,768),true);
  assert.equal(stackLayout(layout.tree,1440),false);
  const fitted=fitDockTree(layout.tree,1440);
  function verify(tree,width) {
    assert.ok(width+1e-6>=minimumWidth(tree));
    if(tree.type==='leaf') return;
    verify(tree.a,tree.axis==='x'?(width-8)*tree.ratio:width);
    verify(tree.b,tree.axis==='x'?(width-8)*(1-tree.ratio):width);
  }
  verify(fitted,1440);
  assert.deepEqual(leaves(fitted),leaves(layout.tree));
  assert.deepEqual(layout,snapshot);
  // Closing a tool can return to docking without changing the user's tree.
  const simple=removeView(removeView(removeView(layout,'lua'),'inspector'),'library');
  assert.equal(stackLayout(simple.tree,768),false);
});
test('extreme desktop ratio still keeps both columns readable',()=>{
  const tree={type:'split',axis:'x',ratio:.2,a:{type:'leaf',view:'inspector'},b:{type:'leaf',view:'game'}};
  const fitted=fitDockTree(tree,768);
  assert.equal((768-8)*fitted.ratio,320);
  assert.ok((768-8)*(1-fitted.ratio)>=360);
});
test('stack resizing conserves total height and respects both minimums',()=>{
  assert.deepEqual(resizeStackPair(440,334,10000,320,240),[534,240]);
  assert.deepEqual(resizeStackPair(440,334,-10000,320,240),[320,454]);
  assert.deepEqual(resizeStackPair(440,334,24,320,240),[464,310]);
});
