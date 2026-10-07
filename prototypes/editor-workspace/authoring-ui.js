import {bindFormFeedback} from './design-status.js';
import {prototypeStorage} from './prototype-storage.mjs';
const escape = value => String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
const types = [
  ['dialogue','Diyalog','Konuşmacı, metin ve yazı boyutu','story'],
  ['background','Arka plan','Sahnenin arka plan görseli','assets'],
  ['character','Karakter','Karakter görseli ve sahne konumu','edit'],
  ['audio','Ses','Müzik, efekt ve seslendirme kaynağı','audio'],
  ['choice','Seçim','Oyuncu seçenekleri ve hedef düğümler','node'],
  ['variable','Değişken','Hikâye durumuna değer ata','inspector'],
  ['condition','Koşul','Değişken değerine göre yönlendir','node'],
  ['script','Lua script','Objeye script dosyası bağla','lua'],
  ['camera','Kamera','Görüş alanı ve hareket ayarları','game'],
  ['transition','Geçiş','Sahneler arası geçiş süresi','arrow']
];
const field = (name,label,value,type='text',extra='') => `<label class="author-field">${label}<input name="${name}" type="${type}" value="${escape(value)}" ${extra}></label>`;
const button = (action,label,icon) => `<button class="author-button" data-author="${action}">${icon?`<svg><use href="#i-${icon}"/></svg>`:''}${label}</button>`;
export function bindAuthoring(panels, context) {
  const dialog = document.createElement('dialog'); dialog.id='author-dialog'; dialog.setAttribute('aria-labelledby','author-title'); document.body.append(dialog);
  const inspectorTools=document.createElement('div');inspectorTools.className='author-toolbar';inspectorTools.innerHTML=button('component','Bileşen ekle','plus');panels.get('inspector').querySelector('.inspector-object-properties').append(inspectorTools);
  const group=document.createElement('details');group.className='inspector-components';group.open=true;
  const groupTitle=document.createElement('summary');groupTitle.textContent='Bileşenler';group.append(groupTitle);
  const components=document.createElement('div');components.className='component-summary';group.append(components);panels.get('inspector').querySelector('.inspector-object-properties').append(group);
  let assets=[], activeType='dialogue', opener;
  const close=()=>dialog.close();
  dialog.addEventListener('close',()=>{const target=opener?.isConnected&&opener.getClientRects().length?opener:panels.get('inspector').querySelector(`[data-component-type="${activeType}"]`)??document.querySelector('#options');target?.focus({preventScroll:true});});
  function show(title, subtitle, body, primary, submit) {
    if(!dialog.open)opener=document.activeElement;
    dialog.innerHTML=`<form><header class="author-heading"><div><span class="tool-caption">ROWL / EDİTÖR</span><h2 id="author-title">${title}</h2><p>${subtitle}</p></div><button type="button" class="icon-button" data-dismiss aria-label="Pencereyi kapat"><svg><use href="#i-close"/></svg></button></header><div class="author-body">${body}</div><footer class="author-footer"><span>Tarayıcı prototipi</span><button type="button" class="author-button" data-dismiss>Vazgeç</button>${primary?`<button class="primary-button" type="submit">${primary}</button>`:''}</footer></form>`;
    dialog.querySelectorAll('[data-dismiss]').forEach(el=>el.onclick=close);
    bindFormFeedback(dialog.querySelector('form'));
    dialog.querySelector('form').onsubmit=event=>{event.preventDefault();submit?.(new FormData(event.currentTarget));};
    if(!dialog.open)dialog.showModal();
  }
  function snapshot(){return {...context.snapshot(),assets};}
  function save(){try{prototypeStorage.setItem('rowl-authoring-project',JSON.stringify(snapshot()));context.notify(context.review?'Örnek proje bu oturumda tutuluyor.':'Proje bu tarayıcıda kaydedildi.');context.log('Yerel proje kaydedildi.');}catch{context.notify('Yerel kayıt alanı dolu. Ayarların dışa aktarma bölümünden JSON indirebilirsin.');}}
  function summary(){
    const {scene,selected}=context.get(),entries=scene.objects[selected].components??[];
    groupTitle.textContent=`Bileşenler · ${entries.length}`;components.replaceChildren();
    const labels={source:'Varlık',speaker:'Konuşmacı',fontSize:'Yazı boyutu',key:'Değişken',value:'Değer',duration:'Süre',volume:'Ses düzeyi',option1:'İlk seçenek',target1:'Hedef',operator:'Karşılaştırma',x:'X',y:'Y',zoom:'Zoom',effect:'Geçiş'};
    const fields={dialogue:['speaker','fontSize'],choice:['option1','target1'],variable:['key','value'],condition:['key','operator'],camera:['zoom','duration'],transition:['effect','duration'],audio:['source','volume'],background:['source'],character:['source'],script:['source']};
    for(const entry of entries){
      const row=document.createElement('button');row.className='component-row';row.dataset.componentType=entry.type;
      const title=document.createElement('strong');title.textContent=types.find(type=>type[0]===entry.type)?.[1]??entry.type;
      const detail=document.createElement('span');detail.textContent=(fields[entry.type]??[]).filter(key=>entry.values[key]!==undefined).slice(0,2).map(key=>`${labels[key]}: ${entry.values[key]}`).join(' · ')||'Özellikleri düzenle';
      row.append(title,detail);row.onclick=()=>configure(entry.type,entry);components.append(row);
    }
  }
  function picker(){const {names,selected}=context.get();show('Bileşen ekle',`${names[selected]} objesine bir davranış veya içerik ekle.`,`<label class="author-field">Bileşen ara<input type="search" placeholder="Diyalog, ses, kamera…" data-component-search></label><div class="component-catalog">${types.map(([id,label,description,icon])=>`<button type="button" data-component="${id}"><svg><use href="#i-${icon}"/></svg><span><strong>${label}</strong><small>${description}</small></span><span>→</span></button>`).join('')}</div><p class="catalog-empty" hidden>Bu isimde bileşen bulunamadı.</p>`,null);
    dialog.querySelector('[data-component-search]').oninput=event=>{let count=0;dialog.querySelectorAll('[data-component]').forEach(el=>{el.hidden=!el.textContent.toLocaleLowerCase('tr').includes(event.target.value.toLocaleLowerCase('tr'));if(!el.hidden)count++;});dialog.querySelector('.catalog-empty').hidden=count>0;};
    dialog.querySelectorAll('[data-component]').forEach(el=>el.onclick=()=>configure(el.dataset.component));
  }
  function configure(type,existing){activeType=type;const {scene,selected,names}=context.get();const values=existing?.values??{};const sourceOptions=assets.map(asset=>`<option value="${escape(asset.name)}">${escape(asset.name)}</option>`).join('');
    const targetField=(key,label)=>`<label class="author-field">${label}<select name="${key}"><option value="">Hedef seç…</option>${context.snapshot().scenes.map((s,i)=>`<option value="${i}">${escape(s.title)}</option>`).join('')}</select></label>`;
    const forms={
      dialogue:`${field('speaker','Konuşmacı',values.speaker??scene.speaker??'Mira','text','required')}<label class="author-field">Diyalog<textarea name="text" rows="4" required>${escape(values.text??scene.dialogue)}</textarea></label>${field('fontSize','Yazı boyutu',values.fontSize??18,'number','min="8" max="72" required')}`,
      choice:`${field('option1','1. Seçenek metni',values.option1??'Ormana git','text','required')}${targetField('target1','1. Seçeneğin hedef düğümü')}${field('option2','2. Seçenek metni',values.option2??'Denize git','text','required')}${targetField('target2','2. Seçeneğin hedef düğümü')}`,
      variable:`${field('key','Değişken adı',values.key??'mira_trust','text','required pattern="[A-Za-z_][A-Za-z0-9_]*"')}${field('value','Değer',values.value??1,'number','required')}`,
      condition:`${field('key','Değişken adı',values.key??'mira_trust','text','required')}<label class="author-field">Karşılaştırma<select name="operator"><option>≥</option><option>=</option><option>≤</option></select></label>${field('value','Karşılaştırılacak değer',values.value??1,'number','required')}${targetField('trueTarget','Koşul doğruysa')}${targetField('falseTarget','Koşul yanlışsa')}`,
      camera:`${field('x','Kamera X',values.x??0,'number','required')}${field('y','Kamera Y',values.y??0,'number','required')}${field('zoom','Yakınlaştırma',values.zoom??1,'number','min="0.1" max="10" step="0.1" required')}${field('duration','Hareket süresi (sn)',values.duration??1,'number','min="0" max="60" step="0.1" required')}`,
      transition:`<label class="author-field">Geçiş<select name="effect"><option>Kararma</option><option>Çözülme</option><option>Kaydırma</option></select></label>${field('duration','Süre (sn)',values.duration??0.5,'number','min="0" max="10" step="0.1" required')}`
    };
    const media=`<label class="author-field">Varlık yolu<input name="source" value="${escape(values.source??'')}" list="author-sources" required placeholder="Assets/${type==='script'?'Scripts/mira.lua':type==='audio'?'Audio/night.ogg':'Images/mira.png'}"><datalist id="author-sources">${sourceOptions}</datalist></label>${type==='audio'?field('volume','Ses düzeyi (%)',values.volume??80,'number','min="0" max="100" required'):''}`;
    show(`${types.find(t=>t[0]===type)[1]} bileşeni`,`${names[selected]} / ${scene.title}`,`<div class="author-columns"><section>${forms[type]??media}</section><aside class="author-preview"><h3>Bileşen özellikleri</h3><p>Obje: ${escape(names[selected])}</p><p>${type==='dialogue'?'Konuşmacı, metin ve yazı boyutu sahneye uygulanır.':'Bu özellikler proje taslağına kaydedilir. Davranışların motor içinde çalıştırılması bu prototipte bulunmuyor.'}</p><div class="author-note">${existing?'Mevcut bileşen güncellenir.':'Aynı tür varsa özellikleri güncellenir.'}</div></aside></div>`,'Uygula',data=>{const entry={type:activeType,values:Object.fromEntries(data)};context.component(entry);summary();close();context.log(`${names[selected]}: ${types.find(t=>t[0]===type)[1]} bileşeni kaydedildi.`);});
    for(const select of dialog.querySelectorAll('select[name]'))if(values[select.name]!=null)select.value=values[select.name];
  }
  function hub(){
    save();
    const current=context.snapshot();
    const projects=context.projects??[{id:'current',name:current.settings.project,nodes:current.scenes.length}];
    show('Projeler',context.review?'Tasarım örneği · kartlar gerçek proje dosyası açmaz.':'Çalışma alanından proje listesine dön.',`<label class="author-field">Proje ara<input type="search" aria-label="Proje ara" placeholder="İsimle ara…"></label><div class="hub-projects"></div><p class="hub-empty" hidden></p>`,null);
    dialog.classList.add('hub-dialog');dialog.addEventListener('close',()=>dialog.classList.remove('hub-dialog'),{once:true});
    function paint(query=''){
      const list=dialog.querySelector('.hub-projects'),empty=dialog.querySelector('.hub-empty');list.replaceChildren();
      for(const project of projects.filter(p=>p.name.toLocaleLowerCase('tr').includes(query.toLocaleLowerCase('tr')))){
        const button=document.createElement('button');button.type='button';button.className='hub-project';button.innerHTML='<svg><use href="#i-moon"/></svg><strong></strong><span></span><small></small>';
        button.querySelector('strong').textContent=project.name;button.querySelector('span').textContent=`${project.nodes} düğüm · ${context.review?'Tasarım örneği':'Bu tarayıcıdaki örnek proje'}`;
        button.querySelector('small').textContent=context.review?'Kart önizlemesini aç →':'Çalışma alanına dön →';
        button.onclick=()=>{if(!context.review){close();return;}show(project.name,'Tasarım örneği',`<div class="author-preview"><h3>Proje kartı</h3><p>${project.nodes} düğüm · Örnek içerik</p><p>Bu görünüm proje listesinin tasarımını gösterir.</p><button type="button" class="author-button" data-back>Proje listesine dön</button></div>`,null);dialog.querySelector('[data-back]').onclick=hub;};list.append(button);
      }
      empty.hidden=!!list.children.length;empty.textContent=projects.length?'Bu isimde proje bulunamadı.':'Henüz proje yok. İlk hikâyen için bir proje oluştur.';
      if(!projects.length){const button=document.createElement('button');button.type='button';button.className='author-button';button.textContent='Örnek projeleri gör';button.onclick=()=>{projects.push(...Array.from({length:3},(_,i)=>({id:`sample-${i}`,name:`Örnek hikâye ${i+1}`,nodes:3})));paint();};empty.append(button);}
    }
    dialog.querySelector('[aria-label="Proje ara"]').oninput=event=>paint(event.target.value);paint();
  }
  document.addEventListener('click',event=>{const action=event.target.closest('[data-author]')?.dataset.author;if(!action)return;context.beforeOpen();({save,hub,component:picker})[action]?.();});
  document.addEventListener('keydown',event=>{if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s'&&!dialog.open&&!document.querySelector('#settings-dialog').open&&!event.target.closest('[data-panel="lua"]')){event.preventDefault();save();}});
  return {update:summary,openHub:hub,restore:data=>{context.restore(data);assets=Array.isArray(data.assets)?data.assets:[];}};
}
