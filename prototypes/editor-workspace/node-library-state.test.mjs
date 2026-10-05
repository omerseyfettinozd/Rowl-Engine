import test from 'node:test';
import assert from 'node:assert/strict';
import {toggleFavorite,instantiateFavorite} from './node-library-state.mjs';
test('Yıldızlı düğüm bağımsız şablondur; eski bağlantı hedeflerini taşımaz',()=>{
 const scene={id:'original',title:'Yol',objects:{dialogue:{x:5,components:[{type:'choice',values:{option1:'Git',target1:'2'}}]}}};
 const favorites=toggleFavorite([],scene);scene.title='Değişti';assert.equal(favorites[0].scene.title,'Yol');assert.equal(favorites[0].scene.objects.dialogue.components[0].values.target1,'');
 const copy=instantiateFavorite(favorites[0],'copy');copy.objects.dialogue.x=50;assert.equal(favorites[0].scene.objects.dialogue.x,5);assert.equal(copy.id,'copy');assert.deepEqual(toggleFavorite(favorites,scene),[]);
});
import {createFolder,moveFolder,removeFolder} from './node-library-state.mjs';
test('İç içe klasörler aynı adı farklı konumda kabul eder, döngüye izin vermez',()=>{
 let folders=createFolder([],'Bölüm',null,'a');folders=createFolder(folders,'Bölüm','a','b');
 assert.throws(()=>createFolder(folders,'Bölüm',null,'c'));assert.throws(()=>moveFolder(folders,'a','b','Bölüm'));assert.equal(folders[0].parentId,null);
});
test('Klasör kaldırma düğüm ve alt klasörleri üst konuma taşır; kaynak sahneleri korur',()=>{
 const folders=[{id:'a',name:'Bölüm',parentId:null},{id:'b',name:'Alt bölüm',parentId:'a'}];const items=[{id:'node',folderId:'a',scene:{title:'Giriş'}}];
 const result=removeFolder(folders,items,'a');assert.equal(result.folders[0].parentId,null);assert.equal(result.items[0].folderId,null);assert.equal(result.items[0].scene.title,'Giriş');assert.equal(items[0].folderId,'a');
});
test('Üst konuma taşınan aynı adlı klasör kaybolmaz; benzersiz ad alır',()=>{
 const folders=[{id:'a',name:'Bölüm',parentId:null},{id:'b',name:'Giriş',parentId:'a'},{id:'c',name:'Giriş',parentId:null}];
 const result=removeFolder(folders,[],'a');assert.equal(result.folders.length,2);assert.equal(result.folders.find(f=>f.id==='b').name,'Giriş (2)');
 let restored=[];for(const f of result.folders)restored=createFolder(restored,f.name,f.parentId,f.id);assert.equal(restored.length,2);assert.equal(folders[1].name,'Giriş');
});
test('Kayıt tüm hiyerarşiyi ve Inspector ayarlarını kopyadan bağımsız korur',()=>{
 const scene={id:'source',title:'Sahne',speaker:'Ada',dialogue:'Merhaba',fontSize:28,objects:{group:{visible:false,x:12,y:30,w:40,h:20,children:['portrait'],components:[{type:'camera',values:{zoom:1.5,duration:2}}]},portrait:{parent:'group',x:15,y:25,w:10,h:40,components:[{type:'sprite',values:{source:'ada.png',opacity:.7}}]}}};
 const saved=toggleFavorite([],scene)[0],copy=instantiateFavorite(saved,'new');
 assert.deepEqual(saved.scene.objects,scene.objects);assert.equal(copy.fontSize,28);assert.equal(copy.objects.portrait.parent,'group');
 scene.objects.group.components[0].values.zoom=3;copy.objects.portrait.components[0].values.opacity=.2;copy.objects.group.children.push('extra');
 assert.equal(saved.scene.objects.group.components[0].values.zoom,1.5);assert.equal(saved.scene.objects.portrait.components[0].values.opacity,.7);assert.deepEqual(saved.scene.objects.group.children,['portrait']);
});
