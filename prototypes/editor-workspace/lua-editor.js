const samples = {
  'mira.lua': `-- Mira · sahneye giriş\nlocal mira = {\n  name = "Mira",\n  visible = true,\n  position = { x = 25, y = 35 }\n}\n\nfunction on_start()\n  -- Karakterin hikâyesi burada başlar.\n  local greeting = "Gece ne kadar sessiz…"\n  return greeting\nend\n\nreturn mira\n`,
  'sahne.lua': `-- Bir gece, bir ışık\nlocal scene = {\n  title = "Bir gece, bir ışık",\n  background = "ay-isigi",\n  music = "gece-ambiyansi"\n}\n\nreturn scene\n`,
  'diyalog.lua': `-- Mira'nın ilk cümleleri\nlocal dialogue = {\n  speaker = "Mira",\n  lines = {\n    "Gece ne kadar sessiz…",\n    "Sanki bütün dünya bir şey söylememi bekliyor."\n  }\n}\n\nreturn dialogue\n`
};
const escape = value => value.replace(/[&<>]/g, char=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[char]));
function highlight(code) {
  const tokens = /--[^\n]*|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|\b(?:local|function|end|if|then|else|return|true|false|nil|for|do|while)\b|\b\d+(?:\.\d+)?\b/g;
  let html='', cursor=0;
  for(const match of code.matchAll(tokens)) {
    html+=escape(code.slice(cursor,match.index));
    const token=match[0], kind=token.startsWith('--')?'comment':/^["']/.test(token)?'string':/^\d/.test(token)?'number':'keyword';
    html+=`<span class="lua-${kind}">${escape(token)}</span>`;cursor=match.index+token.length;
  }
  return html+escape(code.slice(cursor))+'\n';
}
export function luaContent() {
  const root=document.createElement('div');root.className='lua-editor';
  root.innerHTML=`<div class="script-tabs" role="tablist" aria-label="Lua dosyaları"></div><div class="script-context"><div><span class="tool-caption">BAĞLI OBJE</span><select aria-label="Scriptin bağlı olduğu obje"><option>Mira</option><option>Ay</option><option>Diyalog</option><option>Sahne</option></select></div><div class="script-actions"><button class="icon-button" data-script-new title="Yeni script" aria-label="Yeni script"><svg><use href="#i-plus"/></svg></button><button class="icon-button" data-script-find title="Kodda ara" aria-label="Kodda ara"><svg><use href="#i-search"/></svg></button><button class="script-save" data-script-save>Kaydet</button></div></div><div class="script-search" hidden><input type="search" aria-label="Lua kodunda ara" placeholder="Kodda ara…"><span data-search-result></span><button class="icon-button" data-search-close aria-label="Kod aramasını kapat"><svg><use href="#i-close"/></svg></button></div><div class="code-surface"><div class="line-numbers" aria-hidden="true"></div><div class="code-stack"><pre aria-hidden="true"><code></code></pre><textarea aria-label="Lua kodu" spellcheck="false" autocapitalize="off" autocomplete="off" autocorrect="off" wrap="off"></textarea></div></div><footer class="script-status"><span data-script-status>Kaydedildi</span><span><span data-cursor>Satır 1 · Sütun 1</span><span class="language-tag">Lua</span></span></footer>`;
  return root;
}
export function bindLua(panel, context) {
  const root=panel.querySelector('.lua-editor'), input=root.querySelector('textarea'), tabs=root.querySelector('.script-tabs');
  let files={...samples},saved={...samples},active='mira.lua';
  try{const value=JSON.parse(localStorage.getItem('rowl-script-drafts'));if(value&&typeof value==='object'&&!Array.isArray(value))for(const [name,code] of Object.entries(value))if(/^[\w-]+\.lua$/.test(name)&&typeof code==='string'&&code.length<50000)files[name]=code;saved={...files};}catch{}
  function cursor(){const before=input.value.slice(0,input.selectionStart),lines=before.split('\n');root.querySelector('[data-cursor]').textContent=`Satır ${lines.length} · Sütun ${lines.at(-1).length+1}`;}
  function search(){const query=root.querySelector('.script-search input').value;const count=query?input.value.split(query).length-1:0;root.querySelector('[data-search-result]').textContent=query?`${count} eşleşme`:'';}
  function paint(){
    root.querySelector('code').innerHTML=highlight(input.value);
    root.querySelector('.line-numbers').textContent=input.value.split('\n').map((_,i)=>i+1).join('\n');
    const dirty=files[active]!==saved[active];root.querySelector('[data-script-status]').textContent=dirty?'Düzenlendi':'Kaydedildi';root.querySelector('[data-script-status]').classList.toggle('is-dirty',dirty);
    tabs.querySelectorAll('[data-file]').forEach(button=>{button.setAttribute('aria-selected',String(button.dataset.file===active));button.querySelector('.file-dot').hidden=files[button.dataset.file]===saved[button.dataset.file];});cursor();search();
  }
  function renderTabs(){tabs.replaceChildren();for(const name of Object.keys(files)){const button=document.createElement('button');button.type='button';button.role='tab';button.dataset.file=name;button.innerHTML='<svg><use href="#i-lua"/></svg><span></span><i class="file-dot" hidden></i>';button.querySelector('span').textContent=name;tabs.append(button);}}
  function open(name){active=name;input.value=files[name];input.scrollTop=0;input.scrollLeft=0;paint();}
  input.addEventListener('input',()=>{files[active]=input.value;paint();});
  input.addEventListener('scroll',()=>{root.querySelector('pre').scrollTop=input.scrollTop;root.querySelector('pre').scrollLeft=input.scrollLeft;root.querySelector('.line-numbers').scrollTop=input.scrollTop;});
  input.addEventListener('click',cursor);input.addEventListener('keyup',cursor);
  input.addEventListener('keydown',event=>{if(event.key==='Tab'){event.preventDefault();const start=input.selectionStart,end=input.selectionEnd;input.value=input.value.slice(0,start)+'  '+input.value.slice(end);input.selectionStart=input.selectionEnd=start+2;files[active]=input.value;paint();}if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s'){event.preventDefault();save();}});
  tabs.addEventListener('click',event=>{const button=event.target.closest('[data-file]');if(button)open(button.dataset.file);});
  function save(){try{localStorage.setItem('rowl-script-drafts',JSON.stringify(files));saved={...files};paint();context.notify('Script taslağı kaydedildi.');}catch{context.notify('Taslak bu oturumda tutuluyor; tarayıcıya kaydedilemedi.');}}
  root.querySelector('[data-script-save]').addEventListener('click',save);
  root.querySelector('[data-script-new]').addEventListener('click',()=>{let n=1;while(files[`script-${n}.lua`]!==undefined)n++;const name=`script-${n}.lua`;files[name]='-- Yeni bir hikâye\n\n';renderTabs();open(name);input.focus();});
  root.querySelector('[data-script-find]').addEventListener('click',()=>{root.querySelector('.script-search').hidden=false;root.querySelector('.script-search input').focus();});
  root.querySelector('[data-search-close]').addEventListener('click',()=>{root.querySelector('.script-search').hidden=true;input.focus();});root.querySelector('.script-search input').addEventListener('input',search);
  renderTabs();open(active);
  return {snapshot:()=>({...files})};
}
