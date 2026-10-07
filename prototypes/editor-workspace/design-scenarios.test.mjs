import test from 'node:test';import assert from 'node:assert/strict';
import {selectScenario,makeDesignData} from './design-scenarios.mjs';
import {createPrototypeStorage} from './prototype-storage.mjs';
const source={settings:{project:'Örnek'},scenes:[{title:'Sahne',dialogue:'Metin',objects:{moon:{x:64,y:14,w:10,h:16},character:{x:25,y:35,w:13,h:36},dialogue:{x:6,y:65,w:88,h:32}}}]};
test('review scenarios are allowlisted and dense fixtures preserve original project',()=>{
  assert.equal(selectScenario('crowded'),'crowded');assert.equal(selectScenario('unexpected'),null);
  const snapshot=structuredClone(source),dense=makeDesignData('crowded',source);
  assert.equal(dense.scenes.length,30);assert.equal(dense.assets.length,50);assert.equal(Object.keys(dense.scripts).length,20);
  assert.equal(dense.scenes[0].objects.character.components.length,10);assert.equal(dense.scenes[0].dialogue.length,2000);
  assert.equal(new Set(dense.scenes.map(s=>s.id)).size,30);assert.deepEqual(source,snapshot);
  dense.scenes[0].objects.moon.x=0;assert.equal(dense.scenes[1].objects.moon.x,64);
  assert.equal(makeDesignData('hub-empty',source).projects.length,0);assert.equal(makeDesignData('hub-many',source).projects.length,12);
});
test('review save, preferences, scripts and library never touch real storage',()=>{
  const backing={getItem(){throw Error('real read');},setItem(){throw Error('real write');}};
  const review=createPrototypeStorage(backing,true);
  for(const key of ['rowl-authoring-project','rowl-prototype-settings','rowl-design-preferences','rowl-script-drafts','rowl-user-node-library']){assert.equal(review.getItem(key),null);review.setItem(key,'review');assert.equal(review.getItem(key),'review');}
  const stored=new Map(),normal=createPrototypeStorage({getItem:k=>stored.get(k),setItem:(k,v)=>stored.set(k,v)});
  normal.setItem('project','original');assert.equal(normal.getItem('project'),'original');
});
