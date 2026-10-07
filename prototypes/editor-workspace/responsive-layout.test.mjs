import test from 'node:test';
import assert from 'node:assert/strict';
import {initialLayout,syncLayout,removeView,leaves} from './layout-state.mjs';
import {initialState,openView,closeView} from './workspace-state.mjs';
import {minimumWidth,stackLayout,fitDockTree,preferredStackHeight,resizeStackPair,responsivePresentation,minimumDockHeight} from './responsive-layout.mjs';

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

const leaf=view=>({type:'leaf',view});
const split=(a,b,axis='x')=>({type:'split',id:leaves(a).join('-')+'-'+leaves(b).join('-'),axis,ratio:.5,a,b});
const laptopTree=()=>split(split(split(leaf('assets'),leaf('inspector'),'y'),split(leaf('node'),leaf('lua'),'y')),split(leaf('edit'),leaf('game')));
function verifyProjection(tree,width,height) {
  assert.ok(width+1e-6>=minimumWidth(tree));
  assert.ok(height+1e-6>=minimumDockHeight(tree));
  if(tree.type==='leaf')return;
  verifyProjection(tree.a,tree.axis==='x'?(width-8)*tree.ratio:width,tree.axis==='y'?(height-8)*tree.ratio:height);
  verifyProjection(tree.b,tree.axis==='x'?(width-8)*(1-tree.ratio):width,tree.axis==='y'?(height-8)*(1-tree.ratio):height);
}
test('reviewed four-column laptop keeps all six panels in three readable columns',()=>{
  const tree=laptopTree(),snapshot=structuredClone(tree);
  const result=responsivePresentation(tree,1340,704);
  assert.equal(result.mode,'adaptive');
  assert.deepEqual(leaves(result.tree),leaves(tree));
  assert.ok(result.height<=900,'no 3856-pixel all-panel stack');
  verifyProjection(result.tree,1340,result.height);
  assert.deepEqual(tree,snapshot);
  assert.equal(responsivePresentation(tree,1424,836).mode,'dock');
  assert.equal(responsivePresentation(tree,742,836).mode,'stack');
});
test('adaptive resizing clamps both dimensions and preserves canonical ratios on wide return',()=>{
  const tree=laptopTree(),snapshot=structuredClone(tree);
  const first=responsivePresentation(tree,1340,704);
  for(const ratio of [-10,10]) {
    const fitted=responsivePresentation(tree,1340,704,{[first.tree.id]:ratio});
    verifyProjection(fitted.tree,1340,fitted.height);
    assert.deepEqual(tree,snapshot);
  }
  const wide=responsivePresentation(tree,1424,836);
  assert.deepEqual(wide.tree,fitDockTree(tree,1424,wide.height));
});
test('ten open panels survive every sampled adaptive width without narrow columns',()=>{
  const tree=['node','lua','game','edit','inspector','library','assets','store','console','hierarchy'].map(leaf).reduce((a,b)=>split(a,b));
  const snapshot=structuredClone(tree);
  for(const width of [1040,1100,1340,1424,1900]) {
    const result=responsivePresentation(tree,width,704);
    assert.equal(result.mode,'adaptive');
    assert.deepEqual(leaves(result.tree),leaves(tree));
    assert.equal(new Set(leaves(result.tree)).size,10);
    verifyProjection(result.tree,width,result.height);
  }
  assert.deepEqual(tree,snapshot);
});

test('readable columns also keep Inspector and Lua tall enough in normal docking',()=>{
  const tree=split(split(leaf('node'),leaf('lua'),'y'),split(split(leaf('inspector'),leaf('library'),'y'),leaf('assets'),'y'));
  const snapshot=structuredClone(tree),result=responsivePresentation(tree,1340,704);
  assert.equal(result.mode,'dock');assert.ok(result.height>704);verifyProjection(result.tree,1340,result.height);assert.deepEqual(tree,snapshot);
});
test('nested and reordered layouts retain unique leaves and immutable source across breakpoints',()=>{
  const trees=[laptopTree(),split(laptopTree(),split(leaf('store'),leaf('console')),'y'),split(split(leaf('lua'),leaf('game')),split(leaf('node'),leaf('inspector')))];
  for(const tree of trees){const original=structuredClone(tree);for(const width of [1039,1040,1340,1424,1900]){const result=responsivePresentation(tree,width,704);assert.deepEqual(leaves(result.tree),leaves(tree));assert.equal(new Set(leaves(result.tree)).size,leaves(tree).length);if(result.mode!=='stack')verifyProjection(result.tree,width,result.height);}assert.deepEqual(tree,original);}
});
