import { initialState, focusView, openView, closeView, toggleView, togglePin } from './workspace-state.mjs';
import { initialLayout, syncLayout, dockView, resizeSplit } from './layout-state.mjs';
import { TOOL_VIEWS } from './workspace-state.mjs';
import { defaultSettings, normalizeSettings, RESOLUTIONS, editObject } from './settings-state.mjs';
import { toolContent, bindTools } from './tools.js';
import { workspaceMotion } from './workspace-motion.js';
import { initialPlayback, startPlayback, togglePause, advancePlayback, playbackFrame } from './playback-state.mjs';

const names = { node: 'Node', game: 'Game', edit: 'Edit Scene', hierarchy: 'Hiyerarşi', inspector: 'Inspector', assets: 'Varlıklar', console: 'Konsol' };
const defaults = [
  { title: 'Bir gece, bir ışık', dialogue: 'Gece ne kadar sessiz… Sanki bütün dünya bir şey söylememi bekliyor.' },
  { title: 'Yol ayrımı', dialogue: 'Soldaki yol ormana, sağdaki yol denize çıkıyor. Peki, kalbim hangisini seçer?' },
  { title: 'Yeni bir sabah', dialogue: 'Işık hep buradaymış. Onu görmek için biraz yürümem gerekiyormuş.' }
];
const baseObjects = { moon: { x: 64, y: 14, w: 10, h: 16 }, character: { x: 25, y: 35, w: 13, h: 36 }, dialogue: { x: 6, y: 65, w: 88, h: 32 } };
const objectNames = { moon: 'Ay', character: 'Mira', dialogue: 'Diyalog' };
const newScenes = () => defaults.map(scene => ({ ...scene, objects: structuredClone(baseObjects) }));
let scenes = newScenes(), sceneIndex = 0, selectedObject = 'character';
let state = initialState(), layout = initialLayout(), zoom = 1, toastTimer, gesture = null, placementView = null;
let playback = initialPlayback(), playbackSceneIndex = 0, lastFrameTime = null;
const workspace = document.querySelector('#workspace'), dockRoot = document.querySelector('#dock-root');
const placement = document.querySelector('#placement-menu'), options = document.querySelector('#options-menu');
let settings = defaultSettings();
try { settings = normalizeSettings(JSON.parse(localStorage.getItem('rowl-prototype-settings'))); } catch {}
const panels = new Map();
for (const view of Object.keys(names)) {
  const panel = document.createElement('section');
  panel.className = 'panel'; panel.dataset.panel = view; panel.setAttribute('aria-label', `${names[view]} penceresi`);
  panel.innerHTML = `<header class="panel-header" data-drag="${view}" title="Pencereyi taşımak için başlığı sürükle"><div class="panel-title"><svg aria-hidden="true"><use href="#i-${view}"/></svg><span>${names[view]}</span></div><div class="panel-actions"><button class="icon-button placement-button" data-placement="${view}" aria-label="${names[view]} pencere düzeni" title="Pencereyi yerleştir"><svg aria-hidden="true"><use href="#i-layout"/></svg></button><button class="icon-button pin-button" data-pin="${view}" aria-label="${names[view]} penceresini sabitle" title="Pencereyi sabitle"><svg aria-hidden="true"><use href="#i-pin"/></svg></button><button class="icon-button close-button" data-close="${view}" aria-label="${names[view]} penceresini kapat" title="Pencereyi kapat"><svg aria-hidden="true"><use href="#i-close"/></svg></button></div></header><div class="panel-content"></div>`;
  const content = TOOL_VIEWS.includes(view) ? toolContent(view) : document.querySelector(view === 'node' ? '#node-template' : '#scene-template').content.cloneNode(true);
  if (['game','edit'].includes(view)) {
    // Each copy has its own SVG gradient identifiers.
    content.querySelectorAll('[id]').forEach(el => { el.id = `${view}-${el.id}`; });
    content.querySelectorAll('[fill^="url("]').forEach(el => { el.setAttribute('fill', el.getAttribute('fill').replace('url(#', `url(#${view}-`)); });
    if (view === 'edit') {
      content.querySelector('.game-frame').classList.add('editable');
      content.querySelectorAll('[data-object]').forEach(el => { el.tabIndex = 0; el.setAttribute('role', 'group'); el.setAttribute('aria-label', `${objectNames[el.dataset.object]} objesi, sürükle veya ok tuşlarıyla taşı`); });
      content.querySelector('[data-continue]').tabIndex = -1;
      content.querySelector('[data-continue]').setAttribute('aria-hidden', 'true');
    }
  }
  panel.querySelector('.panel-content').append(content);
  panels.set(view, panel);
}

const motion = workspaceMotion(dockRoot,panels);
const tools = bindTools(panels, {
  get:()=>({scene:scenes[sceneIndex], selected:selectedObject, names:objectNames}),
  select:object=>{selectedObject=object;updateScene();tools.log(`${objectNames[object]} seçildi.`);},
  edit:(field,value)=>{scenes[sceneIndex].objects[selectedObject]=editObject(scenes[sceneIndex].objects[selectedObject],field,value);updateScene();},
  visibility:value=>{scenes[sceneIndex].objects[selectedObject].visible=value;updateScene();},
  dialogue:value=>{scenes[sceneIndex].dialogue=value;updateScene();},
  openEdit:()=>apply(openView(state,'edit'))
});
tools.log('Çalışma alanı hazır.');
function notify(message) {
  const toast = document.querySelector('#toast'); toast.textContent = message; toast.classList.add('visible');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove('visible'), 3200);
}
function closeMenus() {
  options.hidden = true; placement.hidden = true; document.querySelector('#tools-menu').hidden=true; document.querySelector('#tools-toggle').setAttribute('aria-expanded','false'); document.querySelector('#tools-toggle').setAttribute('aria-label','Araçları aç'); document.querySelector('#options').setAttribute('aria-expanded', 'false');
}
function apply(result) {
  motion.cancel();
  const removed=state.visible.filter(view=>!result.state.visible.includes(view));
  layout = syncLayout(layout, state, result.state, workspace.clientWidth < 650 ? 'y' : 'x');
  state = result.state; closeMenus();
  if (result.message) notify(result.message);
  updateControls();
  motion.exit(removed,state.visible,()=>render(!removed.length),workspaceHeight());
}
function updateControls() {
  document.querySelectorAll('[data-view]').forEach(button => {
    const view = button.dataset.view, open = state.visible.includes(view), pinned = state.pinned.includes(view);
    button.setAttribute('aria-pressed', String(open));
    button.setAttribute('aria-label', `${names[view]} ekranını ${open ? 'kapat' : 'aç'}${pinned ? ', sabit' : ''}`);
    button.title = `${names[view]} · ${open ? 'kapat' : 'aç'}`;
    button.classList.toggle('focused', state.focused === view);
  });
  document.querySelectorAll('[data-tool]').forEach(button=>{ const open=state.visible.includes(button.dataset.tool); button.setAttribute('aria-pressed',String(open)); button.title=`${names[button.dataset.tool]} · ${open?'kapat':'aç'}`; });
  for (const [view, panel] of panels) {
    panel.classList.toggle('is-focused', state.focused === view); panel.classList.toggle('is-pinned', state.pinned.includes(view));
    const pin = panel.querySelector('[data-pin]'), pinned = state.pinned.includes(view);
    pin.setAttribute('aria-pressed', String(pinned));
    pin.setAttribute('aria-label', `${names[view]} penceresinin ${pinned ? 'sabitlemesini kaldır' : 'yerini sabitle'}`);
    pin.title = pinned ? 'Sabitlemeyi kaldır' : 'Pencereyi sabitle';
  }
  const running = playback.status !== 'stopped', paused = playback.status === 'paused';
  const play = document.querySelector('#play-toggle'), pause = document.querySelector('#pause-toggle');
  play.setAttribute('aria-label', running ? 'Oyunu kapat' : 'Oyunu başlat');
  play.title = running ? 'Oyunu kapat' : 'Oyunu başlat';
  play.classList.toggle('is-running', running);
  play.querySelector('use').setAttribute('href', running ? '#i-stop' : '#i-play');
  pause.disabled = !running;
  pause.setAttribute('aria-pressed', String(paused));
  pause.setAttribute('aria-label', paused ? 'Oyuna devam et' : 'Oyunu duraklat');
  pause.title = paused ? 'Oyuna devam et' : 'Oyunu duraklat';
  pause.querySelector('use').setAttribute('href', paused ? '#i-play' : '#i-pause');
}
function focus(view) { state = focusView(state, view); updateControls(); }
function treeElement(tree) {
  if (!tree) return null;
  if (tree.type === 'leaf') return panels.get(tree.view);
  const element = document.createElement('div'); element.className = 'split'; element.dataset.axis = tree.axis; element.dataset.split = tree.id;
  const a = document.createElement('div'), b = document.createElement('div'); a.className = b.className = 'split-child';
  a.style.flex = `${tree.ratio} 1 0`; b.style.flex = `${1 - tree.ratio} 1 0`;
  a.append(treeElement(tree.a)); b.append(treeElement(tree.b));
  const divider = document.createElement('div'); divider.className = 'splitter'; divider.dataset.divider = tree.id; divider.tabIndex = 0;
  divider.setAttribute('role', 'separator'); divider.setAttribute('aria-label', 'Pencerelerin boyutunu ayarla'); divider.setAttribute('aria-orientation', tree.axis === 'x' ? 'vertical' : 'horizontal');
  divider.setAttribute('aria-valuemin', '20'); divider.setAttribute('aria-valuemax', '80'); divider.setAttribute('aria-valuenow', String(Math.round(tree.ratio * 100)));
  element.append(a, divider, b); return element;
}
function render(animated=false) {
  const before=animated?motion.capture():null;
  const active = document.activeElement;
  for (const panel of panels.values()) panel.remove();
  dockRoot.replaceChildren();
  const tree = treeElement(layout.tree); if (tree) dockRoot.append(tree);
  updateControls(); updateScene(); fitViews();
  document.querySelector('#empty-state').hidden = state.visible.length > 0;
  if (active && active !== document.body && active.isConnected) active.focus({ preventScroll: true });
  if(before) motion.enter(before);
}
function paintScene(view, scene) {
    const panel = panels.get(view);
    panel.querySelector('.scene-title').textContent = scene.title;
    panel.querySelector('.speaker-name').textContent = 'Mira'; panel.querySelector('.dialogue-text').textContent = scene.dialogue;
    panel.querySelector('.scene-help').textContent = view === 'edit' ? `${objectNames[selectedObject]} · tut ve sürükle` : { stopped:'Önizleme', playing:'Çalışıyor', paused:'Duraklatıldı' }[playback.status];
    if (view === 'game') {
      panel.dataset.playback = playback.status;
      panel.querySelector('[data-continue]').disabled = playback.status === 'paused';
    }
    for (const [name, object] of Object.entries(scene.objects)) {
      const el = panel.querySelector(`[data-object="${name}"]`);
      el.style.left = `${object.x}%`; el.style.top = `${object.y}%`; el.style.width = `${object.w}%`; el.style.height = `${object.h}%`;
      el.hidden = object.visible === false;
      el.classList.toggle('selected', view === 'edit' && selectedObject === name);
    }
}
function updateScene() {
  paintScene('game', playbackFrame(playback) ?? scenes[sceneIndex]);
  paintScene('edit', scenes[sceneIndex]);
  panels.get('node').querySelector('.node-area').classList.toggle('no-grid',!settings.grid);
  panels.get('edit').querySelector('.editable').classList.toggle('hide-guides',!settings.guides);
  tools.update();
  panels.get('node').querySelectorAll('[data-scene]').forEach(node => {
    const index = Number(node.dataset.scene); node.classList.toggle('selected', index === sceneIndex); node.setAttribute('aria-pressed', String(index === sceneIndex));
  });
}
function minimumHeight(tree) {
  if (!tree) return 0;
  if (tree.type==='leaf') return TOOL_VIEWS.includes(tree.view)?260:240;
  const a=minimumHeight(tree.a), b=minimumHeight(tree.b);
  return tree.axis==='x'?Math.max(a,b):8+Math.max(a/tree.ratio,b/(1-tree.ratio));
}
function workspaceHeight() {
  return workspace.clientWidth<650?Math.max(workspace.clientHeight,minimumHeight(layout.tree)):workspace.clientHeight;
}
function fitViews() {
  dockRoot.style.height=`${workspaceHeight()}px`;
  const viewport = panels.get('node').querySelector('.graph-viewport');
  if (viewport.isConnected && viewport.clientWidth) {
    const compact = viewport.clientWidth < 600; viewport.classList.toggle('is-compact', compact);
    const width = compact ? 260 : 780, height = compact ? 560 : 440;
    const scale = Math.max(.25, compact ? Math.min((viewport.clientWidth - 20) / width, 1) : Math.min((viewport.clientWidth - 24) / width, (viewport.clientHeight - 20) / height, 1.25)) * zoom;
    viewport.querySelector('.graph-world').style.transform = `translate(${Math.max(0, (viewport.clientWidth - width * scale) / 2)}px, ${Math.max(0, (viewport.clientHeight - height * scale) / 2)}px) scale(${scale})`;
    panels.get('node').querySelector('.zoom-value').textContent = `${Math.round(scale * 100)}%`;
  }
  for (const view of ['game', 'edit']) {
    const wrap = panels.get(view).querySelector('.scene-stage-wrap');
    if (!wrap.isConnected) continue;
    const [rw,rh]=RESOLUTIONS[settings.resolution], aspect=rw/rh;
    const width = Math.max(0, Math.min(wrap.clientWidth, wrap.clientHeight * aspect));
    const frame = wrap.querySelector('.game-frame'); frame.style.width = `${width}px`; frame.style.height = `${width / aspect}px`;
  }
}
function showPlacement(view, button) {
  closeMenus(); placementView = view;
  document.querySelector('#placement-title').textContent = `${names[view]} · yerleştir`;
  const target = document.querySelector('#placement-target'); target.replaceChildren();
  state.visible.filter(v => v !== view).forEach(v => { const option = document.createElement('option'); option.value = v; option.textContent = names[v]; target.append(option); });
  placement.querySelectorAll('[data-edge]').forEach(el => { el.disabled = !target.options.length || state.pinned.includes(view); });
  document.querySelector('#menu-pin span').textContent = state.pinned.includes(view) ? 'Sabitlemeyi kaldır' : 'Pencereyi sabitle';
  document.querySelector('#menu-pin').title = 'Sabitleme, pencere başlığının sürüklenmesini kilitler.';
  placement.hidden = false;
  const rect = button.getBoundingClientRect();
  placement.style.left = `${Math.max(8, Math.min(innerWidth - 250, rect.right - 242))}px`;
  placement.style.top = `${Math.max(60, Math.min(innerHeight - placement.offsetHeight - 8, rect.bottom + 5))}px`;
}
function dropTarget(x, y, source) {
  const target = document.elementsFromPoint(x, y).map(el => el.closest?.('[data-panel]')).find(el => el && el.dataset.panel !== source);
  if (!target) return null;
  const rect = target.getBoundingClientRect(), px = (x - rect.left) / rect.width, py = (y - rect.top) / rect.height;
  const distances = { left: px, right: 1 - px, top: py, bottom: 1 - py };
  const edge = Object.keys(distances).sort((a, b) => distances[a] - distances[b])[0];
  return { target: target.dataset.panel, edge, rect };
}
function showDrop(drop) {
  const preview = document.querySelector('#drop-preview');
  if (!drop) { preview.hidden = true; return; }
  const bounds = workspace.getBoundingClientRect();
  let { left:x, top:y, width:w, height:h } = drop.rect;
  x -= bounds.left; y = y - bounds.top + workspace.scrollTop;
  if (['left', 'right'].includes(drop.edge)) { w /= 2; if (drop.edge === 'right') x += w; }
  else { h /= 2; if (drop.edge === 'bottom') y += h; }
  preview.hidden = false; preview.style.cssText = `left:${x}px;top:${y}px;width:${w}px;height:${h}px;`;
  preview.querySelector('span').textContent = { left:'Sola yerleştir', right:'Sağa yerleştir', top:'Üste yerleştir', bottom:'Alta yerleştir' }[drop.edge];
}
function startGesture(event, data) {
  gesture = { ...data, pointerId:event.pointerId, startX:event.clientX, startY:event.clientY };
  event.currentTarget.setPointerCapture(event.pointerId); event.preventDefault();
}
workspace.addEventListener('pointerdown', event => {
  if (event.button !== 0 || gesture) return;
  motion.cancel();
  if(!event.target.isConnected) return;
  const panel = event.target.closest('[data-panel]'); if (panel) focus(panel.dataset.panel);
  const object = event.target.closest('.editable [data-object]');
  const divider = event.target.closest('[data-divider]'), header = event.target.closest('[data-drag]');
  if (object) {
    selectedObject = object.dataset.object; object.focus({preventScroll:true}); updateScene();
    startGesture(event, {type:'object', object:selectedObject, scene:sceneIndex, original:{...scenes[sceneIndex].objects[selectedObject]}, bounds:object.closest('.game-frame').getBoundingClientRect()});
  } else if (divider) startGesture(event, {type:'divider', id:divider.dataset.divider, element:divider, axis:divider.parentElement.dataset.axis, bounds:divider.parentElement.getBoundingClientRect(), originalLayout:layout});
  else if (header && !event.target.closest('button')) {
    if(state.pinned.includes(panel.dataset.panel)){notify('Yerleşim sabit. Taşımak için sabitlemeyi kaldır.');return;}
    closeMenus();
    startGesture(event, {type:'panel', view:panel.dataset.panel, moved:false});
  }
});
workspace.addEventListener('pointermove', event => {
  if (!gesture || gesture.pointerId !== event.pointerId) return;
  const dx = event.clientX - gesture.startX, dy = event.clientY - gesture.startY;
  if (gesture.type === 'object') {
    const object = gesture.original;
    scenes[gesture.scene].objects[gesture.object] = { ...object, x:Math.max(0,Math.min(100-object.w,object.x+dx/gesture.bounds.width*100)), y:Math.max(0,Math.min(100-object.h,object.y+dy/gesture.bounds.height*100)) };
    updateScene();
  } else if (gesture.type === 'divider') {
    const ratio = gesture.axis === 'x' ? (event.clientX-gesture.bounds.left)/gesture.bounds.width : (event.clientY-gesture.bounds.top)/gesture.bounds.height;
    layout = resizeSplit(layout, gesture.id, ratio);
    const value = Math.max(.2, Math.min(.8, ratio));
    gesture.element.previousElementSibling.style.flex = `${value} 1 0`; gesture.element.nextElementSibling.style.flex = `${1-value} 1 0`;
    gesture.element.setAttribute('aria-valuenow', String(Math.round(value*100))); fitViews();
  } else if (Math.abs(dx)+Math.abs(dy)>7 || gesture.moved) {
    gesture.moved = true; document.body.classList.add('dragging');
    const label = document.querySelector('#drag-label'); label.hidden=false; label.textContent=names[gesture.view]; label.style.left=`${event.clientX+14}px`; label.style.top=`${event.clientY+14}px`;
    gesture.drop = dropTarget(event.clientX,event.clientY,gesture.view);
    const bounds=workspace.getBoundingClientRect(); gesture.outside=event.clientX<bounds.left||event.clientX>bounds.right||event.clientY<bounds.top||event.clientY>bounds.bottom;
    showDrop(gesture.outside ? null : gesture.drop);
  }
});
function endGesture(cancel = false) {
  if (!gesture) return;
  const current = gesture; gesture = null;
  if (workspace.hasPointerCapture(current.pointerId)) workspace.releasePointerCapture(current.pointerId);
  if (current.type === 'panel' && current.moved && !cancel && !current.outside && current.drop) layout = dockView(layout,current.view,current.drop.target,current.drop.edge,state.pinned);
  if (cancel && current.type === 'object') { scenes[current.scene].objects[current.object] = current.original; updateScene(); }
  if (cancel && current.type === 'divider') layout = current.originalLayout;
  document.body.classList.remove('dragging'); document.querySelector('#drop-preview').hidden=true; document.querySelector('#drag-label').hidden=true;
  if (current.type !== 'object') render();
}
workspace.addEventListener('pointerup', event => { if (gesture?.pointerId===event.pointerId) endGesture(); });
workspace.addEventListener('pointercancel', event => { if (gesture?.pointerId===event.pointerId) endGesture(true); });
workspace.addEventListener('lostpointercapture', () => { if (gesture) endGesture(true); });
workspace.addEventListener('click', event => {
  const menu = event.target.closest('[data-placement]'), node = event.target.closest('[data-scene]'), zoomButton = event.target.closest('[data-zoom]'), close = event.target.closest('[data-close]'), pin = event.target.closest('[data-pin]');
  if (close) { apply(closeView(state,close.dataset.close)); (document.querySelector(`[data-view="${close.dataset.close}"]`)??document.querySelector("#tools-toggle")).focus({preventScroll:true}); }
  else if (pin) apply(togglePin(state,pin.dataset.pin));
  else if (menu) showPlacement(menu.dataset.placement,menu);
  else if (node) { sceneIndex = Number(node.dataset.scene); updateScene(); }
  else if (event.target.closest('[data-continue]') && !event.target.closest('.editable')) {
    if (playback.status === 'paused') return;
    if (playback.status === 'playing') {
      playbackSceneIndex = (playbackSceneIndex+1)%scenes.length;
      playback = startPlayback(scenes[playbackSceneIndex]); lastFrameTime = null;
    } else sceneIndex = (sceneIndex+1)%scenes.length;
    updateScene();
  }
  else if (zoomButton) { zoom = zoomButton.dataset.zoom==='fit'?1:Math.max(.5,Math.min(2,zoom+(zoomButton.dataset.zoom==='in'?.15:-.15))); fitViews(); }
});
workspace.addEventListener('focusin', event => { const panel = event.target.closest('[data-panel]'); if (panel) focus(panel.dataset.panel); });
workspace.addEventListener('keydown', event => {
  const object = event.target.closest('.editable [data-object]');
  if (object && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) {
    event.preventDefault(); selectedObject = object.dataset.object; const step=event.shiftKey?5:1, item=scenes[sceneIndex].objects[selectedObject];
    item.x=Math.max(0,Math.min(100-item.w,item.x+(event.key==='ArrowLeft'?-step:event.key==='ArrowRight'?step:0)));
    item.y=Math.max(0,Math.min(100-item.h,item.y+(event.key==='ArrowUp'?-step:event.key==='ArrowDown'?step:0))); updateScene();
  }
  const divider = event.target.closest('[data-divider]');
  if (divider && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) {
    event.preventDefault(); const delta=['ArrowRight','ArrowDown'].includes(event.key)?.05:-.05;
    layout=resizeSplit(layout,divider.dataset.divider,Number(divider.getAttribute('aria-valuenow'))/100+delta); render();
    document.querySelector(`[data-divider="${divider.dataset.divider}"]`)?.focus({preventScroll:true});
  }
});
document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click',()=>{ endGesture(true); apply(toggleView(state,button.dataset.view)); }));
document.querySelector('#play-toggle').addEventListener('click',()=>{
  endGesture(true);
  if (playback.status === 'stopped') {
    const result = openView(state,'game');
    if (!result.state.visible.includes('game')) { apply(result); return; }
    playbackSceneIndex = settings.startScene==='selected'?sceneIndex:Number(settings.startScene); playback = startPlayback(scenes[playbackSceneIndex]); tools.log('Oyun başlatıldı.'); lastFrameTime = null;
    apply(result);
  } else {
    playback = initialPlayback(); tools.log('Oyun kapatıldı.'); lastFrameTime = null; closeMenus(); updateControls(); updateScene();
  }
});
document.querySelector('#pause-toggle').addEventListener('click',()=>{
  playback = togglePause(playback); tools.log(playback.status==='paused'?'Oyun duraklatıldı.':'Oyuna devam edildi.'); lastFrameTime = null; updateControls(); updateScene();
});
document.querySelector('#options').addEventListener('click',()=>{ const open=options.hidden; closeMenus(); options.hidden=!open; document.querySelector('#options').setAttribute('aria-expanded',String(open)); });
placement.addEventListener('click',event=>{
  const edge=event.target.closest('[data-edge]');
  if (edge) { layout=dockView(layout,placementView,document.querySelector('#placement-target').value,edge.dataset.edge,state.pinned); closeMenus(); render(); }
  else if (event.target.closest('#menu-pin')) apply(togglePin(state,placementView));
  else if (event.target.closest('#menu-close')) apply(closeView(state,placementView));
});
document.addEventListener('pointerdown',event=>{ if (!event.target.closest('.popover,#options,[data-placement],.tools-launcher')) closeMenus(); });
document.addEventListener('keydown',event=>{ if (event.key==='Escape') { endGesture(true); closeMenus(); } });
document.querySelector('#reset').addEventListener('click',()=>{
  endGesture(true); motion.cancel(); layout=syncLayout({tree:null},{visible:[]},state,workspace.clientWidth<650?'y':'x'); zoom=1; workspace.scrollTop=0; closeMenus(); render(); tools.log('Pencere yerleşimi sıfırlandı.');
});
document.querySelector('#tools-toggle').addEventListener('click',()=>{
  const open=document.querySelector('#tools-menu').hidden;closeMenus();document.querySelector('#tools-menu').hidden=!open;
  document.querySelector('#tools-toggle').setAttribute('aria-expanded',String(open));document.querySelector('#tools-toggle').setAttribute('aria-label',open?'Araçları kapat':'Araçları aç');
});
document.querySelectorAll('[data-tool]').forEach(button=>button.addEventListener('click',()=>{endGesture(true);apply(toggleView(state,button.dataset.tool));document.querySelector('#tools-toggle').focus({preventScroll:true});}));
const settingsDialog=document.querySelector('#settings-dialog'), settingsForm=document.querySelector('#settings-form');
let settingsCategory='project';
document.querySelectorAll('[data-settings]').forEach(button=>button.addEventListener('click',()=>{
  closeMenus(); settingsCategory=button.dataset.settings;
  document.querySelector('#settings-title').textContent={project:'Proje ayarları',game:'Oyun ayarları',editor:'Editör tercihleri',export:'Dışa aktar'}[settingsCategory];
  for(const section of settingsDialog.querySelectorAll('[data-settings-section]')) section.hidden=section.dataset.settingsSection!==settingsCategory;
  for(const [name,value] of Object.entries(settings)){const input=settingsForm.elements.namedItem(name);if(typeof value==='boolean')input.checked=value;else input.value=value;}
  // Hidden categories cannot block validation of the active form.
  settingsForm.elements.namedItem('project').required=settingsCategory==='project';
  document.querySelector('#settings-submit').textContent=settingsCategory==='export'?'JSON indir':'Uygula';settingsDialog.showModal();
}));
settingsDialog.querySelectorAll('[data-dismiss]').forEach(button=>button.addEventListener('click',()=>settingsDialog.close()));
settingsForm.addEventListener('submit',event=>{
  event.preventDefault();
  if(settingsCategory==='export'){
    const data={format:'rowl-workspace-prototype/v1',settings,scenes,workspace:{...state,layout}};
    const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
    const link=document.createElement('a');link.href=url;link.download='rowl-proje-taslagi.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);tools.log('Proje taslağı JSON olarak dışa aktarıldı.');
  }else{
    const changes=settingsCategory==='project'?{project:settingsForm.elements.project.value}:settingsCategory==='game'?{resolution:settingsForm.elements.resolution.value,startScene:settingsForm.elements.startScene.value}:{grid:settingsForm.elements.grid.checked,guides:settingsForm.elements.guides.checked};
    settings=normalizeSettings({...settings,...changes});
    try{localStorage.setItem('rowl-prototype-settings',JSON.stringify(settings));}catch{notify('Ayarlar bu oturumda uygulandı; tarayıcıya kaydedilemedi.');}
    tools.log(`${document.querySelector('#settings-title').textContent} uygulandı.`);updateScene();fitViews();
  }
  settingsDialog.close();
});
const observer=new ResizeObserver(fitViews); observer.observe(workspace); observer.observe(panels.get('node').querySelector('.graph-viewport'));
for (const view of ['game','edit']) observer.observe(panels.get(view).querySelector('.scene-stage-wrap'));
render();
function animate(timestamp) {
  if (playback.status === 'playing' && lastFrameTime !== null) {
    playback = advancePlayback(playback, Math.min(.1, (timestamp-lastFrameTime)/1000));
    paintScene('game', playbackFrame(playback));
  }
  lastFrameTime = timestamp;
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
