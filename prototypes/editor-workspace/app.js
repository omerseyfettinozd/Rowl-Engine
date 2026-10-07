import {activeScenario,makeDesignData} from './design-scenarios.mjs';
import {mountReviewState} from './design-review.js';
import {prototypeStorage} from './prototype-storage.mjs';
import {bindSceneView} from './scene-view.js';
import {percentDelta,graphGeometry} from './scene-view-state.mjs';
import { initialState, focusView, openView, closeView, toggleView, togglePin } from './workspace-state.mjs';
import { initialLayout, syncLayout, dockView, resizeSplit } from './layout-state.mjs';
import { leaves } from './layout-state.mjs';
import { responsivePresentation, preferredStackHeight, minimumStackHeight, resizeStackPair } from './responsive-layout.mjs';
import { TOOL_VIEWS } from './workspace-state.mjs';
import { defaultSettings, normalizeSettings, RESOLUTIONS, editObject } from './settings-state.mjs';
import { toolContent, bindTools } from './tools.js';
import { luaContent, bindLua } from './lua-editor.js';
import { bindSettings } from './settings-ui.js';
import { libraryContent, bindLibrary } from './node-library.js';
import { bindAuthoring } from './authoring-ui.js';
import { workspaceMotion } from './workspace-motion.js';
import { initialPlayback, startPlayback, togglePause, advancePlayback, playbackFrame } from './playback-state.mjs';

const names = { node: 'Node', game: 'Game', edit: 'Edit Scene', hierarchy: 'Hiyerarşi', inspector: 'Inspector', assets: 'Varlıklar', store: 'Assets Store', console: 'Konsol', lua: 'Lua editörü', library: 'Düğüm kütüphanesi' };
const defaults = [
  { title: 'Bir gece, bir ışık', dialogue: 'Gece ne kadar sessiz… Sanki bütün dünya bir şey söylememi bekliyor.' },
  { title: 'Yol ayrımı', dialogue: 'Soldaki yol ormana, sağdaki yol denize çıkıyor. Peki, kalbim hangisini seçer?' },
  { title: 'Yeni bir sabah', dialogue: 'Işık hep buradaymış. Onu görmek için biraz yürümem gerekiyormuş.' }
];
const baseObjects = { moon: { x: 64, y: 14, w: 10, h: 16 }, character: { x: 25, y: 35, w: 13, h: 36 }, dialogue: { x: 6, y: 65, w: 88, h: 32 } };
const objectNames = { moon: 'Ay', character: 'Mira', dialogue: 'Diyalog' };
const newScenes = () => defaults.map((scene,index) => ({ ...scene, id:`sample-${index}`, objects: structuredClone(baseObjects) }));
let scenes = newScenes(), sceneIndex = 0, selectedObject = 'character';
let state = initialState(), layout = initialLayout(), zoom = 1, toastTimer, gesture = null, placementView = null;
let playback = initialPlayback(), playbackSceneIndex = 0, lastFrameTime = null;
let renderGeneration=0;
const workspace = document.querySelector('#workspace'), dockRoot = document.querySelector('#dock-root');
const placement = document.querySelector('#placement-menu'), options = document.querySelector('#options-menu');
let settings = defaultSettings();
try { settings = normalizeSettings(JSON.parse(prototypeStorage.getItem('rowl-prototype-settings'))); } catch {}
const reviewData=activeScenario?makeDesignData(activeScenario,{settings,scenes}):null;
if(reviewData){prototypeStorage.setItem('rowl-script-drafts',JSON.stringify(reviewData.scripts));prototypeStorage.setItem('rowl-user-node-library',JSON.stringify({folders:[],items:activeScenario==='crowded'?reviewData.scenes.map(scene=>({id:scene.id,scene,folderId:null})):[]}));}
const panels = new Map();
const stackHeights = new Map();
const adaptiveRatios = {};
const presentation = () => responsivePresentation(layout.tree,workspace.clientWidth,workspace.clientHeight,adaptiveRatios);
const isStacked = () => presentation().mode === 'stack';
function resizePresentation(id,ratio) {
  if(id.startsWith('adaptive:')) adaptiveRatios[id]=ratio;
  else layout=resizeSplit(layout,id,ratio);
}
for (const view of Object.keys(names)) {
  const panel = document.createElement('section');
  panel.className = 'panel'; panel.dataset.panel = view; panel.setAttribute('aria-label', `${names[view]} penceresi`);
  panel.innerHTML = `<header class="panel-header" data-drag="${view}" title="Pencereyi taşımak için başlığı sürükle"><div class="panel-title"><svg aria-hidden="true"><use href="#i-${view==='library'?'node':view==='store'?'assets':view}"/></svg><span>${names[view]}</span></div><div class="panel-actions"><button class="icon-button placement-button" data-placement="${view}" aria-label="${names[view]} pencere düzeni" title="Pencereyi yerleştir"><svg aria-hidden="true"><use href="#i-layout"/></svg></button><button class="icon-button pin-button" data-pin="${view}" aria-label="${names[view]} penceresini sabitle" title="Pencereyi sabitle"><svg aria-hidden="true"><use href="#i-pin"/></svg></button><button class="icon-button close-button" data-close="${view}" aria-label="${names[view]} penceresini kapat" title="Pencereyi kapat"><svg aria-hidden="true"><use href="#i-close"/></svg></button></div></header><div class="panel-content"></div>`;
  const content = view==='library'?libraryContent():view==='lua'?luaContent():TOOL_VIEWS.includes(view) ? toolContent(view) : document.querySelector(view === 'node' ? '#node-template' : '#scene-template').content.cloneNode(true);
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

const graphViewport=panels.get('node').querySelector('.graph-viewport');
const graphSpace=document.createElement('div');graphSpace.className='graph-scroll-space';graphSpace.append(graphViewport.querySelector('.graph-world'));graphViewport.append(graphSpace);
graphViewport.tabIndex=0;graphViewport.setAttribute('role','region');graphViewport.setAttribute('aria-label','Hikâye akışı gezinmesi');
const sceneViews=new Map(['game','edit'].map(view=>[view,bindSceneView(panels.get(view),names[view])]));

const scenePreview=document.createElement('dialog');scenePreview.className='scene-preview-dialog';
scenePreview.setAttribute('aria-labelledby','scene-preview-title');
scenePreview.innerHTML='<div class="preview-shell"><header class="dialog-header"><h2 id="scene-preview-title">Sahne önizlemesi</h2><button class="icon-button" aria-label="Büyük önizlemeyi kapat"><svg><use href="#i-close"/></svg></button></header><div class="preview-stage"></div><div class="preview-reading"><span class="preview-caption">Açıldığı andaki sahne · canlı oynatım değil</span><strong></strong><p></p></div></div>';
document.body.append(scenePreview);
scenePreview.querySelector('button').addEventListener('click',()=>scenePreview.close());
function fitPreview() {
  const stage=scenePreview.querySelector('.preview-stage'), frame=stage.querySelector('.game-frame');if(!frame)return;
  const [w,h]=RESOLUTIONS[settings.resolution], width=Math.min(stage.clientWidth,stage.clientHeight*w/h);
  frame.style.width=`${width}px`;frame.style.height=`${width*h/w}px`;
}
new ResizeObserver(fitPreview).observe(scenePreview);
for(const view of ['game','edit']) {
  const button=document.createElement('button');button.className='scene-enlarge';button.textContent='Büyüt';
  button.setAttribute('aria-label',`${names[view]} sahnesini ve metnini büyüt`);
  panels.get(view).querySelector('.scene-controls').append(button);
  button.addEventListener('click',()=>{
    const scene=view==='game'?(playbackFrame(playback)??scenes[sceneIndex]):scenes[sceneIndex];
    const frame=panels.get(view).querySelector('.game-frame').cloneNode(true);frame.classList.remove('editable');
    frame.querySelectorAll('[tabindex]').forEach(el=>el.removeAttribute('tabindex'));
    frame.querySelectorAll('[data-object]').forEach(el=>{el.removeAttribute('role');el.removeAttribute('aria-label');});
    frame.querySelectorAll('[id]').forEach(el=>{const id=el.id;el.id=`preview-${id}`;frame.querySelectorAll(`[fill="url(#${id})"]`).forEach(el=>el.setAttribute('fill',`url(#preview-${id})`));});
    frame.querySelectorAll('.object-label,.continue-button').forEach(el=>el.remove());
    scenePreview.querySelector('.preview-stage').replaceChildren(frame);
    scenePreview.querySelector('h2').textContent=`${names[view]} · ${scene.title}`;
    scenePreview.querySelector('.preview-reading strong').textContent=scene.speaker||'Mira';
    scenePreview.querySelector('.preview-reading p').textContent=scene.dialogue;
    scenePreview.showModal();fitPreview();scenePreview.querySelector('button').focus();
  });
}

// A temporary caption explains icon controls without adding a second toolbar.
const toolbarHint=document.createElement('div');toolbarHint.className='toolbar-hint';toolbarHint.hidden=true;
document.querySelector('.app-header').append(toolbarHint);
document.querySelectorAll('.view-button').forEach(button=>{
  const show=()=>{if(button.disabled||button.getAttribute('aria-expanded')==='true')return;toolbarHint.textContent=button.title;toolbarHint.hidden=false;};
  button.addEventListener('pointerenter',event=>{if(event.pointerType==='mouse')show();});
  button.addEventListener('pointerleave',()=>toolbarHint.hidden=true);
  button.addEventListener('focus',()=>{if(button.matches(':focus-visible'))show();});
  button.addEventListener('blur',()=>toolbarHint.hidden=true);
  button.addEventListener('click',()=>toolbarHint.hidden=true);
});

const lua = bindLua(panels.get('lua'),{notify,review:activeScenario});
const motion = workspaceMotion(dockRoot,panels);
const tools = bindTools(panels, {
  get:()=>({scene:scenes[sceneIndex], selected:selectedObject, names:objectNames}),
  select:object=>{selectedObject=object;updateScene();tools.revealObject();tools.log(`${objectNames[object]} seçildi.`);},
  edit:(field,value)=>{scenes[sceneIndex].objects[selectedObject]=editObject(scenes[sceneIndex].objects[selectedObject],field,value);updateScene();},
  visibility:value=>{scenes[sceneIndex].objects[selectedObject].visible=value;updateScene();},
  dialogue:value=>{scenes[sceneIndex].dialogue=value;updateScene();},
  nodeField:(field,value)=>{if(['title','speaker','dialogue'].includes(field)){scenes[sceneIndex][field]=value;if(field!=='title')for(const object of Object.values(scenes[sceneIndex].objects))for(const component of object.components??[])if(component.type==='dialogue')component.values[field==='dialogue'?'text':'speaker']=value;updateScene();}},
  openEdit:()=>apply(openView(state,'edit'))
});
tools.log('Çalışma alanı hazır.');
function notify(message) {
  const toast = document.querySelector('#toast'); toast.setAttribute('role','status');toast.setAttribute('aria-live','polite');toast.textContent = message; toast.classList.add('visible');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove('visible'), 3200);
}
function closeMenus() {
  options.hidden = true; placement.hidden = true; document.querySelector('#tools-menu').hidden=true; document.querySelector('#tools-toggle').setAttribute('aria-expanded','false'); document.querySelector('#tools-toggle').setAttribute('aria-label','Araçları aç'); document.querySelector('#options').setAttribute('aria-expanded', 'false');
}
function apply(result) {
  motion.cancel();
  const removed=state.visible.filter(view=>!result.state.visible.includes(view));
  const added=result.state.visible.filter(view=>!state.visible.includes(view));
  layout = syncLayout(layout, state, result.state, 'x');
  state = result.state; closeMenus();
  if (result.message) notify(result.message);
  updateControls();
  motion.exit(removed,state.visible,()=>render(!removed.length,added.includes(state.focused)),workspaceHeight());
}
const windowLinks=document.createElement('div');windowLinks.className='window-links';
options.prepend(windowLinks);
function updateWindowLinks() {
  windowLinks.replaceChildren();
  if(!state.visible.length)return;
  const label=document.createElement('span');label.className='menu-label';label.textContent='AÇIK PENCEREYE GİT';windowLinks.append(label);
  for(const view of leaves(layout.tree)) {
    const button=document.createElement('button');button.className='menu-row';button.textContent=names[view];
    button.setAttribute('aria-label',`${names[view]} penceresine git`);button.setAttribute('aria-pressed',String(state.focused===view));
    button.addEventListener('click',()=>{closeMenus();focus(view);panels.get(view).scrollIntoView({block:'nearest'});panels.get(view).querySelector('[data-placement]').focus({preventScroll:true});});
    windowLinks.append(button);
  }
}
function updateControls() {
  updateWindowLinks();
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
  divider.setAttribute('aria-valuemin', '0'); divider.setAttribute('aria-valuemax', '100'); divider.setAttribute('aria-valuenow', String(Math.round(tree.ratio * 100)));
  element.append(a, divider, b); return element;
}
function render(animated=false,reveal=false) {
  const generation=++renderGeneration;
  const before=animated?motion.capture():null;
  const active = document.activeElement;
  for (const panel of panels.values()) panel.remove();
  dockRoot.replaceChildren();
  dockRoot.classList.toggle('is-stacked',isStacked());
  dockRoot.classList.toggle('is-adaptive',presentation().mode==='adaptive');
  if (isStacked()) {
    const views=leaves(layout.tree);
    views.forEach((view,index)=>{
      dockRoot.append(panels.get(view));
      if(index===views.length-1) return;
      const divider=document.createElement('div'); divider.className='splitter stack-divider';
      divider.dataset.stackBefore=view; divider.dataset.stackAfter=views[index+1]; divider.tabIndex=0;
      divider.setAttribute('role','separator'); divider.setAttribute('aria-label',`${names[view]} ve ${names[views[index+1]]} boyutunu ayarla`);
      divider.setAttribute('aria-orientation','horizontal'); dockRoot.append(divider);
    });
  } else {
    for(const panel of panels.values()) panel.style.height='';
    const tree = treeElement(presentation().tree); if (tree) dockRoot.append(tree);
  }
  updateControls(); updateScene(); fitViews();
  document.querySelector('#empty-state').hidden = state.visible.length > 0;
  if (active && active !== document.body && active.isConnected) active.focus({ preventScroll: true });
  if(before) motion.enter(before);
  if(reveal && isStacked() && state.focused) {
    const focused=state.focused;
    motion.settled(()=>{if(generation===renderGeneration && state.focused===focused && state.visible.includes(focused))panels.get(focused).scrollIntoView({block:'start'});});
  }
}
function paintScene(view, scene) {
    const panel = panels.get(view);
    sceneViews.get(view).update(scene);
    panel.querySelector('.scene-title').textContent = scene.title;
    panel.querySelector('.speaker-name').textContent = scene.speaker || 'Mira';
    panel.querySelector('.dialogue-text').style.fontSize = scene.fontSize ? `${scene.fontSize / 8}cqw` : '';  panel.querySelector('.dialogue-text').textContent = scene.dialogue;
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
  authoring?.update();
  library?.update();
  panels.get('node').querySelectorAll('[data-scene]').forEach(node => {
    const index = Number(node.dataset.scene); node.classList.toggle('selected', index === sceneIndex); node.setAttribute('aria-pressed', String(index === sceneIndex));
    node.querySelector('.node-type').childNodes[1].textContent=index===0?'BAŞLANGIÇ ':'DÜĞÜM ';
    node.querySelector('strong').textContent=scenes[index].title;node.querySelector('strong').title=scenes[index].title;
    node.querySelector('.node-description').textContent=scenes[index].dialogue.slice(0,75);
    node.querySelector('.node-bottom').firstChild.textContent=scenes[index].speaker || 'Mira';
  });
}
function panelHeight(view) {
  const [w,h]=RESOLUTIONS[settings.resolution];
  return stackHeights.get(view) ?? preferredStackHeight(view,workspace.clientWidth,w/h);
}
function workspaceHeight() {
  if(!isStacked()) return presentation().height;
  const views=leaves(layout.tree);
  const total=views.reduce((sum,view)=>sum+panelHeight(view),0)+Math.max(0,views.length-1)*8;
  return Math.max(workspace.clientHeight,total);
}
function fitViews() {
  dockRoot.style.height=`${workspaceHeight()}px`;
  if(isStacked()) {
    const views=leaves(layout.tree);
    for(const view of views) panels.get(view).style.height=`${views.length===1?Math.max(workspace.clientHeight,panelHeight(view)):panelHeight(view)}px`;
    for(const divider of dockRoot.querySelectorAll('[data-stack-before]')) {
      const first=panelHeight(divider.dataset.stackBefore), second=panelHeight(divider.dataset.stackAfter);
      divider.setAttribute('aria-valuemin',String(minimumStackHeight(divider.dataset.stackBefore)));
      divider.setAttribute('aria-valuemax',String(first+second-minimumStackHeight(divider.dataset.stackAfter)));
      divider.setAttribute('aria-valuenow',String(Math.round(first)));
      divider.setAttribute('aria-valuetext',`${Math.round(first)} / ${Math.round(second)} piksel`);
    }
  } else {
    function syncRatios(tree) {
      if(!tree || tree.type==='leaf')return;
      const split=dockRoot.querySelector(`[data-split="${tree.id}"]`);
      if(split){split.children[0].style.flex=`${tree.ratio} 1 0`;split.children[2].style.flex=`${1-tree.ratio} 1 0`;split.children[1].setAttribute('aria-valuenow',String(Math.round(tree.ratio*100)));}
      syncRatios(tree.a);syncRatios(tree.b);
    }
    syncRatios(presentation().tree);
  }
  const viewport = panels.get('node').querySelector('.graph-viewport');
  if (viewport.isConnected && viewport.clientWidth) {
    const compact = viewport.clientWidth < 600; viewport.classList.toggle('is-compact', compact);
    const width = compact ? 260 : Math.max(780, scenes.length*250+30), height = compact ? Math.max(560,scenes.length*185+20) : 440;
    const world=viewport.querySelector('.graph-world');world.style.width=`${width}px`;world.style.height=`${height}px`;
    const fitted=graphGeometry(viewport.clientWidth,viewport.clientHeight,width,height,compact,zoom);
    graphSpace.style.width=`${fitted.width}px`;graphSpace.style.height=`${fitted.height}px`;
    world.style.transform=`translate(${fitted.left}px,${fitted.top}px) scale(${fitted.scale})`;
    panels.get('node').querySelector('.zoom-value').textContent=`${Math.round(fitted.scale*100)}%`;
  }
  const [rw,rh]=RESOLUTIONS[settings.resolution];
  for(const view of ['game','edit'])sceneViews.get(view).fit(rh/rw);
  library?.update();
}
function showPlacement(view, button) {
  closeMenus(); placementView = view;
  document.querySelector('#placement-title').textContent = `${names[view]} · yerleştir`;
  const target = document.querySelector('#placement-target'); target.replaceChildren();
  state.visible.filter(v => v !== view).forEach(v => { const option = document.createElement('option'); option.value = v; option.textContent = names[v]; target.append(option); });
  placement.querySelectorAll('[data-edge]').forEach(el => { el.disabled = !target.options.length || state.pinned.includes(view) || (isStacked() && ['left','right'].includes(el.dataset.edge)); });
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
  const edge = isStacked() ? (py<.5?'top':'bottom') : Object.keys(distances).sort((a, b) => distances[a] - distances[b])[0];
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
workspace.addEventListener('dblclick',event=>{if(event.target.closest('[data-scene]'))apply(openView(state,'inspector'));});
workspace.addEventListener('pointerdown', event => {
  if (event.button !== 0 || gesture) return;
  motion.cancel();
  if(!event.target.isConnected) return;
  const panel = event.target.closest('[data-panel]'); if (panel) focus(panel.dataset.panel);
  const object = event.target.closest('.editable [data-object]');
  const divider = event.target.closest('[data-divider]'), stackDivider=event.target.closest('[data-stack-before]'), header = event.target.closest('[data-drag]');
  if (object) {
    selectedObject = object.dataset.object; object.focus({preventScroll:true}); updateScene(); tools.revealObject();
    startGesture(event, {type:'object', object:selectedObject, scene:sceneIndex, original:{...scenes[sceneIndex].objects[selectedObject]}, bounds:object.closest('.game-frame').getBoundingClientRect()});
  } else if (stackDivider) startGesture(event,{type:'stack-divider',before:stackDivider.dataset.stackBefore,after:stackDivider.dataset.stackAfter,first:panelHeight(stackDivider.dataset.stackBefore),second:panelHeight(stackDivider.dataset.stackAfter),originalHeights:new Map(stackHeights)});
  else if (divider) startGesture(event, {type:'divider', id:divider.dataset.divider, element:divider, axis:divider.parentElement.dataset.axis, bounds:divider.parentElement.getBoundingClientRect(), originalLayout:layout,originalRatios:{...adaptiveRatios}});
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
    const object = gesture.original, delta=percentDelta(dx,dy,gesture.bounds.width,gesture.bounds.height);
    scenes[gesture.scene].objects[gesture.object] = { ...object, x:Math.max(0,Math.min(100-object.w,object.x+delta.x)), y:Math.max(0,Math.min(100-object.h,object.y+delta.y)) };
    updateScene();
  } else if(gesture.type==='stack-divider') {
    setStackPair(gesture.before,gesture.after,gesture.first,gesture.second,dy); fitViews();
  } else if (gesture.type === 'divider') {
    const ratio = gesture.axis === 'x' ? (event.clientX-gesture.bounds.left)/gesture.bounds.width : (event.clientY-gesture.bounds.top)/gesture.bounds.height;
    resizePresentation(gesture.id,ratio);
    const fitted=presentation().tree;
    const find=node=>node.type==='leaf'?null:node.id===gesture.id?node:find(node.a)??find(node.b);
    const value = find(fitted).ratio;
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
  if (cancel && current.type === 'divider') {layout = current.originalLayout;for(const id of Object.keys(adaptiveRatios))delete adaptiveRatios[id];Object.assign(adaptiveRatios,current.originalRatios);}
  if(cancel && current.type==='stack-divider') {stackHeights.clear();for(const [view,height] of current.originalHeights)stackHeights.set(view,height);}
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
  else if (node) { sceneIndex = Number(node.dataset.scene); updateScene(); tools.revealNode(); }
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
workspace.addEventListener('focusin', event => {
  const panel = event.target.closest('[data-panel]'); if (panel) focus(panel.dataset.panel);
  const object=event.target.closest('.editable [data-object]');
  if(object && selectedObject!==object.dataset.object){selectedObject=object.dataset.object;updateScene();tools.revealObject();}
});
function setStackPair(before,after,first,second,delta) {
  const heights=resizeStackPair(first,second,delta,minimumStackHeight(before),minimumStackHeight(after));
  stackHeights.set(before,heights[0]);stackHeights.set(after,heights[1]);
}
workspace.addEventListener('keydown', event => {
  const object = event.target.closest('.editable [data-object]');
  if (object && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) {
    event.preventDefault(); selectedObject = object.dataset.object; tools.revealObject(); const step=event.shiftKey?5:1, item=scenes[sceneIndex].objects[selectedObject];
    item.x=Math.max(0,Math.min(100-item.w,item.x+(event.key==='ArrowLeft'?-step:event.key==='ArrowRight'?step:0)));
    item.y=Math.max(0,Math.min(100-item.h,item.y+(event.key==='ArrowUp'?-step:event.key==='ArrowDown'?step:0))); updateScene();
  }
  const divider = event.target.closest('[data-divider]');
  const stackDivider=event.target.closest('[data-stack-before]');
  if(stackDivider && ['ArrowUp','ArrowDown'].includes(event.key)) {
    event.preventDefault();const {stackBefore:before,stackAfter:after}=stackDivider.dataset;
    setStackPair(before,after,panelHeight(before),panelHeight(after),event.key==='ArrowDown'?24:-24);fitViews();
  }
  if (divider && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)) {
    event.preventDefault(); const delta=['ArrowRight','ArrowDown'].includes(event.key)?.05:-.05;
    resizePresentation(divider.dataset.divider,Number(divider.getAttribute('aria-valuenow'))/100+delta); render();
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
  endGesture(true); motion.cancel(); stackHeights.clear(); for(const id of Object.keys(adaptiveRatios))delete adaptiveRatios[id]; layout=syncLayout({tree:null},{visible:[]},state,'x'); zoom=1; workspace.scrollTop=0; closeMenus(); render(); tools.log('Pencere yerleşimi sıfırlandı.');
});
document.querySelector('#tools-toggle').addEventListener('click',()=>{
  const open=document.querySelector('#tools-menu').hidden;closeMenus();document.querySelector('#tools-menu').hidden=!open;
  document.querySelector('#tools-toggle').setAttribute('aria-expanded',String(open));document.querySelector('#tools-toggle').setAttribute('aria-label',open?'Araçları kapat':'Araçları aç');
});
document.querySelectorAll('[data-tool]').forEach(button=>button.addEventListener('click',()=>{endGesture(true);apply(toggleView(state,button.dataset.tool));document.querySelector('#tools-toggle').focus({preventScroll:true});}));
const preferences=bindSettings(document.querySelector('#settings-dialog'),{
  get:()=>settings,
  beforeOpen:()=>{motion.cancel();closeMenus();},
  apply:changes=>{settings=normalizeSettings({...settings,...changes});try{prototypeStorage.setItem('rowl-prototype-settings',JSON.stringify(settings));}catch{}updateScene();fitViews();tools.log('Tasarım tercihleri kaydedildi.');notify(activeScenario?'Tercihler bu tasarım oturumunda tutuluyor.':'Tercihler kaydedildi.');},
  export:details=>{const data={format:'rowl-workspace-prototype/v2',settings,preferences:details,scenes,scripts:details.includeScripts?lua.snapshot():{},workspace:{...state,layout}};const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));const link=document.createElement('a');link.href=url;link.download='rowl-proje-taslagi.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);notify('Proje taslağı indirildi.');}
});
document.querySelectorAll('[data-settings]').forEach(button=>button.addEventListener('click',()=>preferences.open(button.dataset.settings)));
let library=bindLibrary(panels.get('library'),panels.get('inspector'),{
  scenes:()=>scenes,selectedScene:()=>scenes[sceneIndex],notify,
  add:template=>{
    if(scenes.length>=100){notify('Prototipte en fazla 100 düğüm kullanılabilir.');return;}
    const scene=template?structuredClone(template):{id:crypto.randomUUID(),title:`Düğüm ${scenes.length+1}`,speaker:'Anlatıcı',dialogue:'Yeni diyalog…',objects:structuredClone(baseObjects)};
    delete scene.kind;
    scenes.push(scene);sceneIndex=scenes.length-1;selectedObject='dialogue';rebuildGraph();apply(openView(state,'inspector'));tools.log(`${scene.title} eklendi.`);
  }
});
let authoring = bindAuthoring(panels, {
  get:()=>({scene:scenes[sceneIndex],selected:selectedObject,names:objectNames}),
  review:activeScenario,projects:reviewData?.projects,
  beforeOpen:()=>{endGesture(true);motion.cancel();closeMenus();},notify,log:message=>tools.log(message),
  snapshot:()=>({format:'rowl-authoring-prototype/v1',settings,scenes:structuredClone(scenes),scripts:lua.snapshot(),designPreferences:preferences.snapshot()}),
  component:entry=>{const object=scenes[sceneIndex].objects[selectedObject];object.components??=[];const old=object.components.findIndex(c=>c.type===entry.type);if(old<0)object.components.push(entry);else object.components[old]=entry;
    if(entry.type==='dialogue'){Object.assign(scenes[sceneIndex],{speaker:entry.values.speaker,dialogue:entry.values.text,fontSize:Number(entry.values.fontSize)});}updateScene();},
  restore:data=>{
    if(data?.format!=='rowl-authoring-prototype/v1'||!Array.isArray(data.scenes)||!data.scenes.length||data.scenes.length>100||!data.settings)throw Error('Invalid project');
    const restored=data.scenes.map(scene=>{
      if(typeof scene.title!=='string'||typeof scene.dialogue!=='string'||!scene.objects)throw Error('Invalid scene');
      const objects={};for(const name of Object.keys(baseObjects)){const object=scene.objects[name];if(!object||!['x','y','w','h'].every(key=>Number.isFinite(object[key])&&object[key]>=0&&object[key]<=100))throw Error('Invalid object');
        if(object.components&&!Array.isArray(object.components))throw Error('Invalid components');
        if(object.components?.some(c=>typeof c?.type!=='string'||!c.values||typeof c.values!=='object'))throw Error('Invalid component');objects[name]=structuredClone(object);}
      const restoredScene={...scene,id:scene.id??crypto.randomUUID(),objects};delete restoredScene.kind;return restoredScene;
    });
    if(data.assets&&(!Array.isArray(data.assets)||data.assets.some(a=>typeof a?.name!=='string'||typeof a.type!=='string'||!Number.isFinite(a.size))))throw Error('Invalid assets');
    if(data.scripts&&(!data.scripts||Array.isArray(data.scripts)||typeof data.scripts!=='object'||!Object.entries(data.scripts).every(([name,code])=>/^[\w-]+\.lua$/.test(name)&&typeof code==='string'&&code.length<50000)))throw Error('Invalid scripts');
    scenes=restored;lua.restore(data.scripts??{});preferences.restore(data.designPreferences??{});settings=normalizeSettings(data.settings);sceneIndex=0;playback=initialPlayback();rebuildGraph();updateControls();updateScene();fitViews();
  }
});
function rebuildGraph(){
  const world=panels.get('node').querySelector('.graph-world');world.classList.add('dynamic-graph');world.style.width=`${Math.max(780,scenes.length*250+30)}px`;
  const template=document.querySelector('#node-template .story-node')??document.querySelector('#node-template').content.querySelector('.story-node');
  world.querySelectorAll('.story-node').forEach(node=>node.remove());
  scenes.forEach((scene,index)=>{const node=template.cloneNode(true);node.className='story-node';node.querySelector('.node-type').childNodes[1].textContent=index===0?'BAŞLANGIÇ ':'DÜĞÜM ';node.dataset.scene=index;node.style.setProperty('--node-x',`${30+250*index}px`);node.style.setProperty('--node-y',`${20+185*index}px`);node.querySelector('.node-number').textContent=String(index+1).padStart(2,'0');world.append(node);});
  const svg=world.querySelector('.connections');svg.setAttribute('viewBox',`0 0 ${Math.max(780,scenes.length*250+30)} 440`);svg.replaceChildren();
  for(let i=0;i<scenes.length-1;i++){const path=document.createElementNS('http://www.w3.org/2000/svg','path');path.setAttribute('d',`M${220+250*i} 215H${280+250*i}`);svg.append(path);}
  panels.get('node').querySelector('.canvas-caption .muted').textContent=`${scenes.length} sahne`;
}
let previousWorkspaceWidth=workspace.clientWidth;
const observer=new ResizeObserver(()=>{const width=workspace.clientWidth;if(width!==previousWorkspaceWidth){previousWorkspaceWidth=width;endGesture(true);motion.cancel();render(false,true);}else fitViews();}); observer.observe(workspace); observer.observe(panels.get('node').querySelector('.graph-viewport'));
for (const view of ['game','edit']) observer.observe(panels.get(view).querySelector('.scene-stage-wrap'));
try{const stored=reviewData??JSON.parse(prototypeStorage.getItem('rowl-authoring-project'));if(stored)authoring.restore(stored);}catch{}
if(reviewData){
  document.title+=' · Tasarım örneği';
  const label=document.createElement('span');label.className='menu-label review-label';label.textContent=`TASARIM ÖRNEĞİ · ${activeScenario}`;options.prepend(label);
  for(const view of ['game','edit','inspector',...(activeScenario==='crowded'?['lua','library','assets']:[])]){const next=openView(state,view);layout=syncLayout(layout,state,next.state);state=next.state;}
  if(reviewData.assets.length)tools.setAssetCatalog(reviewData.assets);
  if(activeScenario==='crowded')tools.revealObject();
  mountReviewState(activeScenario,panels,authoring);
}
render();
if(activeScenario && !['normal','long','crowded','hub-empty','hub-many'].includes(activeScenario))panels.get('inspector').scrollIntoView({block:'start'});
function animate(timestamp) {
  if (playback.status === 'playing' && lastFrameTime !== null) {
    playback = advancePlayback(playback, Math.min(.1, (timestamp-lastFrameTime)/1000));
    paintScene('game', playbackFrame(playback));
  }
  lastFrameTime = timestamp;
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
