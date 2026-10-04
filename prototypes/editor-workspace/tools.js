export function toolContent(view) {
  const content = document.createElement('div'); content.className = `tool-content ${view}-content`;
  const markup = {
    hierarchy: `<div class="tool-caption">SEÇİLİ NODE</div><strong class="hierarchy-scene"></strong><div class="tree-root">⌄ Sahne objeleri</div><div class="object-tree">${[['moon','Ay','i-moon'],['character','Mira','i-edit'],['dialogue','Diyalog','i-inspector']].map(([id,name,icon])=>`<button data-select-object="${id}" aria-pressed="false"><svg><use href="#${icon}"/></svg><span>${name}</span><span class="tree-status"></span></button>`).join('')}</div><p class="tool-hint">Bir objeyi seç. Özelliklerini Inspector’dan düzenle.</p>`,
    inspector: `<div class="tool-caption">SEÇİLİ OBJE</div><strong class="inspector-name"></strong><div class="property-grid">${[['x','X'],['y','Y'],['w','Genişlik'],['h','Yükseklik']].map(([field,label])=>`<label>${label} <span>%</span><input type="number" min="0" max="100" step="0.5" data-property="${field}" aria-label="${label} yüzdesi"></label>`).join('')}</div><label class="check-row"><input type="checkbox" data-visible> Sahnede görünür</label><label class="dialogue-property" hidden>Diyalog metni<textarea data-dialogue rows="4"></textarea></label><p class="tool-hint">Konum ve boyut sahneye göre yüzde olarak ölçülür.</p>`,
    assets: `<label class="asset-search">Varlık ara<input type="search" placeholder="İsimle filtrele…" aria-label="Varlık ara"></label><div class="asset-grid">${[['moon','Ay','i-moon','Görsel'],['character','Mira','i-edit','Karakter'],['dialogue','Diyalog','i-inspector','Arayüz']].map(([id,name,icon,type])=>`<button data-asset="${id}"><div class="asset-preview"><svg><use href="#${icon}"/></svg></div><strong>${name}</strong><span>${type}</span></button>`).join('')}</div><p class="tool-hint">Bir varlık seçerek Edit Scene’de düzenle.</p>`,
    console: `<div class="console-toolbar"><span>Oturum olayları</span><button class="text-button" data-clear-log>Temizle</button></div><ol class="console-lines" aria-label="Oturum olayları"></ol>`
  };
  content.innerHTML = markup[view]; return content;
}
export function bindTools(panels, context) {
  const logs = []; const hierarchy = panels.get('hierarchy'), inspector = panels.get('inspector'), assets = panels.get('assets'), consolePanel = panels.get('console');
  function update() {
    const {scene, selected, names} = context.get();
    hierarchy.querySelector('.hierarchy-scene').textContent = scene.title;
    hierarchy.querySelectorAll('[data-select-object]').forEach(button=>{
      button.setAttribute('aria-pressed', String(button.dataset.selectObject === selected));
      button.querySelector('.tree-status').textContent = scene.objects[button.dataset.selectObject].visible === false ? 'gizli' : '';
    });
    inspector.querySelector('.inspector-name').textContent = names[selected];
    for (const input of inspector.querySelectorAll('[data-property]')) {
      if (document.activeElement !== input) input.value = Math.round(scene.objects[selected][input.dataset.property]*100)/100;
    }
    inspector.querySelector('[data-visible]').checked = scene.objects[selected].visible !== false;
    inspector.querySelector('.dialogue-property').hidden = selected !== 'dialogue';
    if (document.activeElement !== inspector.querySelector('[data-dialogue]')) inspector.querySelector('[data-dialogue]').value = scene.dialogue;
  }
  hierarchy.addEventListener('click', event=>{ const button=event.target.closest('[data-select-object]'); if(button) context.select(button.dataset.selectObject); });
  inspector.addEventListener('input',event=>{
    if(event.target.matches('[data-property]') && event.target.value!=='') context.edit(event.target.dataset.property,Number(event.target.value));
    else if(event.target.matches('[data-dialogue]')) context.dialogue(event.target.value);
  });
  inspector.addEventListener('focusout',event=>{
    if(event.target.matches('[data-property]')){const current=context.get();event.target.value=Math.round(current.scene.objects[current.selected][event.target.dataset.property]*100)/100;}
  });
  inspector.addEventListener('change',event=>{
    if(event.target.matches('[data-property]')) { const field=event.target.dataset.property; context.edit(field, Number(event.target.value)); const current=context.get(); event.target.value=Math.round(current.scene.objects[current.selected][field]*100)/100; update(); }
    else if(event.target.matches('[data-visible]')) context.visibility(event.target.checked);
    else if(event.target.matches('[data-dialogue]')) context.dialogue(event.target.value);
  });
  assets.addEventListener('click',event=>{const button=event.target.closest('[data-asset]'); if(button){ context.select(button.dataset.asset); context.openEdit(); }});
  assets.querySelector('input').addEventListener('input',event=>{
    const filter=event.target.value.toLocaleLowerCase('tr'); assets.querySelectorAll('[data-asset]').forEach(button=>{button.hidden=!button.textContent.toLocaleLowerCase('tr').includes(filter);});
  });
  function paintLogs(){
    const list=consolePanel.querySelector('.console-lines'); list.replaceChildren();
    for(const log of logs){ const row=document.createElement('li'), time=document.createElement('time'), message=document.createElement('span'); time.textContent=log.time; message.textContent=log.message; row.append(time,message); list.append(row); }
    list.scrollTop=list.scrollHeight;
  }
  consolePanel.querySelector('[data-clear-log]').addEventListener('click',()=>{logs.length=0;paintLogs();});
  return {update, log(message){logs.push({time:new Date().toLocaleTimeString('tr-TR'),message}); if(logs.length>100) logs.shift(); paintLogs();}};
}
