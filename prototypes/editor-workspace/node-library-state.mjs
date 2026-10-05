export function reusableScene(scene) {
  const copy=structuredClone(scene);delete copy.id;delete copy.kind;
  for(const object of Object.values(copy.objects))for(const component of object.components??[]){
    for(const key of ['target','target1','target2','trueTarget','falseTarget'])if(key in component.values)component.values[key]='';
  }
  return copy;
}
export function toggleFavorite(favorites, scene) {
  return favorites.some(item=>item.id===scene.id)?favorites.filter(item=>item.id!==scene.id):[...favorites,{id:scene.id,scene:reusableScene(scene)}];
}
export function instantiateFavorite(favorite,id) {return {...reusableScene(favorite.scene),id};}

export function createFolder(folders,name,parentId=null,id=crypto.randomUUID()) {
  name=String(name).trim().slice(0,60);
  if(!name||parentId&&!folders.some(folder=>folder.id===parentId))throw Error('Geçersiz klasör');
  if(folders.some(folder=>folder.parentId===parentId&&folder.name.toLocaleLowerCase('tr')===name.toLocaleLowerCase('tr')))throw Error('Bu konumda aynı adlı klasör var.');
  return [...folders,{id,name,parentId}];
}
export function moveFolder(folders,id,parentId,name) {
  const folder=folders.find(folder=>folder.id===id);if(!folder)throw Error('Klasör bulunamadı.');
  let ancestor=parentId,seen=new Set();
  while(ancestor){if(ancestor===id||seen.has(ancestor))throw Error('Klasör kendi içine taşınamaz.');seen.add(ancestor);const parent=folders.find(f=>f.id===ancestor);if(!parent)throw Error('Hedef klasör bulunamadı.');ancestor=parent.parentId;}
  const without=folders.filter(f=>f.id!==id);return createFolder(without,name,parentId,id);
}
export function removeFolder(folders,items,id) {
  const folder=folders.find(folder=>folder.id===id);if(!folder)return {folders,items};
  const remaining=folders.filter(f=>f.id!==id&&f.parentId!==id);
  for(const child of folders.filter(f=>f.parentId===id)){
    let name=child.name,suffix=2;
    while(remaining.some(f=>f.parentId===folder.parentId&&f.name.toLocaleLowerCase('tr')===name.toLocaleLowerCase('tr')))name=`${child.name.slice(0,50)} (${suffix++})`;
    remaining.push({...child,name,parentId:folder.parentId});
  }
  return {folders:remaining,items:items.map(item=>item.folderId===id?{...item,folderId:folder.parentId}:item)};
}
