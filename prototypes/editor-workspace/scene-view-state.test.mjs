import test from 'node:test';
import assert from 'node:assert/strict';
import {changeSceneZoom,sceneGeometry,resizedScroll,percentDelta} from './scene-view-state.mjs';
test('scene zoom bounds and fit preserve scale-independent authoring deltas',()=>{
  let zoom=1;for(let i=0;i<20;i++)zoom=changeSceneZoom(zoom,'in');assert.equal(zoom,3);
  for(let i=0;i<20;i++)zoom=changeSceneZoom(zoom,'out');assert.equal(zoom,1);
  assert.equal(changeSceneZoom(2.5,'fit'),1);
  for(const scale of [1,1.25,2,3]){
    const frame=sceneGeometry(400,300,1/1.6,scale);
    assert.equal(frame.frameWidth/frame.frameHeight,1.6);
    assert.deepEqual(percentDelta(frame.frameWidth*.1,frame.frameHeight*.05,frame.frameWidth,frame.frameHeight),{x:10,y:5});
  }
});
test('zoom center and fit scroll clamp keep a reachable scene at narrow and portrait sizes',()=>{
  assert.equal(resizedScroll(400,800,400,0),200);
  assert.equal(resizedScroll(800,400,400,200),0);
  for(const [w,h,aspect] of [[280,120,.625],[280,120,1.777],[400,700,1.6]]){
    const fit=sceneGeometry(w,h,aspect);
    assert.ok(fit.frameWidth<=w && fit.frameHeight<=h+1e-8);
    assert.equal(fit.canvasWidth,w);assert.equal(fit.canvasHeight,h);
    const large=sceneGeometry(w,h,aspect,3);
    assert.ok(large.canvasWidth>=large.frameWidth && large.canvasHeight>=large.frameHeight);
  }
});
test('large graph retains a readable default scale and every node stays reachable by scrolling',async()=>{
  const {graphGeometry}=await import('./scene-view-state.mjs');
  const wide=graphGeometry(720,500,7530,440,false);
  assert.equal(wide.scale,.75);assert.ok(wide.width>=7530*wide.scale);
  const compact=graphGeometry(360,500,260,5570,true);
  assert.equal(compact.scale,1);assert.equal(compact.height,5570);
  assert.ok(wide.left+(30+29*250+240)*wide.scale<=wide.width);
  assert.ok(compact.top+(20+29*185+150)*compact.scale<=compact.height);
});
