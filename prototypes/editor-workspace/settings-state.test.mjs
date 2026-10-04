import test from 'node:test';
import assert from 'node:assert/strict';
import {defaultSettings,normalizeSettings,editObject} from './settings-state.mjs';
test('Bozuk kayıt varsayılanlara döner; izin verilmeyen alanlar elenir',()=>{
  assert.deepEqual(normalizeSettings(null),defaultSettings());assert.deepEqual(normalizeSettings({project:' ',resolution:'bad',startScene:'99',grid:'false',guides:1}),defaultSettings());assert.equal('unknown' in normalizeSettings({unknown:true}),false);
});
test('Geçerli ayarlar JSON kaydı sonrası aynı kalır',()=>{
  const settings={project:'Test projesi',resolution:'1080x1920',startScene:'2',grid:false,guides:false};assert.deepEqual(normalizeSettings(JSON.parse(JSON.stringify(settings))),settings);
});
test('Inspector konum ve boyutu sınırlar; orijinal objeyi değiştirmez',()=>{
  const object={x:60,y:40,w:10,h:20};assert.deepEqual(editObject(object,'x',200),{...object,x:90});assert.deepEqual(editObject(object,'w',80),{...object,x:20,w:80});assert.deepEqual(editObject(object,'h',-3),{...object,h:2});assert.equal(editObject(object,'x',NaN),object);assert.equal(editObject(object,'unknown',50),object);assert.equal(object.w,10);
});
