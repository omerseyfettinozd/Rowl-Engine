import { initialState, focusView, openView, closeView, togglePin, setCapacity } from './workspace-state.mjs';
import { initialLayout, syncLayout, dockView, resizeSplit } from './layout-state.mjs';

const names = { node: 'Node', game: 'Game', edit: 'Edit Scene' };
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
const workspace = document.querySelector('#workspace'), dockRoot = document.querySelector('#dock-root');
const placement = document.querySelector('#placement-menu'), options = document.querySelector('#options-menu');
const panels = new Map();
for (const view of Object.keys(names)) {
  const panel = document.createElement('section');
  panel.className = 'panel'; panel.dataset.panel = view; panel.setAttribute('aria-label', `${names[view]} penceresi`);
  panel.innerHTML = `<header class="panel-header" data-drag="${view}" title="Pencereyi taşımak için başlığı sürükle"><div class="panel-title"><svg aria-hidden="true"><use href="#i-${view}"/></svg><span>${names[view]}</span></div><div class="panel-actions"><button class="icon-button placement-button" data-placement="${view}" aria-label="${names[view]} pencere düzeni" title="Pencereyi yerleştir"><svg aria-hidden="true"><use href="#i-layout"/></svg></button><button class="icon-button pin-button" data-pin="${view}" aria-label="${names[view]} penceresini sabitle" title="Pencereyi sabitle"><svg aria-hidden="true"><use href="#i-pin"/></svg></button><button class="icon-button close-button" data-close="${view}" aria-label="${names[view]} penceresini kapat" title="Pencereyi kapat"><svg aria-hidden="true"><use href="#i-close"/></svg></button></div></header><div class="panel-content"></div>`;
  const content = document.querySelector(view === 'node' ? '#node-template' : '#scene-template').content.cloneNode(true);
  if (view !== 'node') {
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

function notify(message) {
  const toast = document.querySelector('#toast'); toast.textContent = message; toast.classList.add('visible');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove('visible'), 3200);
}
function closeMenus() {
  options.hidden = true; placement.hidden = true; document.querySelector('#options').setAttribute('aria-expanded', 'false');
}
function apply(result) {
  layout = syncLayout(layout, state, result.state, workspace.clientWidth < 650 ? 'y' : 'x');
  state = result.state; closeMenus();
  if (result.message) notify(result.message);
  render();
}
function updateControls() {
  document.querySelectorAll('[data-view]').forEach(button => {
    const view = button.dataset.view, open = state.visible.includes(view), pinned = state.pinned.includes(view);
    button.setAttribute('aria-pressed', String(open));
    button.setAttribute('aria-label', `${names[view]} ekranı${open ? ', açık' : 'nı aç'}${pinned ? ', sabit' : ''}`);
    button.classList.toggle('focused', state.focused === view);
  });
  document.querySelectorAll('[data-capacity]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.capacity) === state.capacity)));
  for (const [view, panel] of panels) {
    panel.classList.toggle('is-focused', state.focused === view); panel.classList.toggle('is-pinned', state.pinned.includes(view));
    const pin = panel.querySelector('[data-pin]'), pinned = state.pinned.includes(view);
    pin.setAttribute('aria-pressed', String(pinned));
    pin.setAttribute('aria-label', `${names[view]} penceresinin ${pinned ? 'sabitlemesini kaldır' : 'yerini sabitle'}`);
    pin.title = pinned ? 'Sabitlemeyi kaldır' : 'Pencereyi sabitle';
  }
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
function render() {
  const active = document.activeElement;
  for (const panel of panels.values()) panel.remove();
  dockRoot.replaceChildren();
  const tree = treeElement(layout.tree); if (tree) dockRoot.append(tree);
  updateControls(); updateScene(); fitViews();
  document.querySelector('#empty-state').hidden = state.visible.length > 0;
  if (active && active !== document.body && active.isConnected) active.focus({ preventScroll: true });
}
function updateScene() {
  const scene = scenes[sceneIndex];
  for (const view of ['game', 'edit']) {
    const panel = panels.get(view);
    panel.querySelector('.scene-title').textContent = scene.title;
    panel.querySelector('.speaker-name').textContent = 'Mira'; panel.querySelector('.dialogue-text').textContent = scene.dialogue;
    panel.querySelector('.scene-help').textContent = view === 'edit' ? `${objectNames[selectedObject]} · tut ve sürükle` : 'Önizleme';
    for (const [name, object] of Object.entries(scene.objects)) {
      const el = panel.querySelector(`[data-object="${name}"]`);
      el.style.left = `${object.x}%`; el.style.top = `${object.y}%`; el.style.width = `${object.w}%`; el.style.height = `${object.h}%`;
      el.classList.toggle('selected', view === 'edit' && selectedObject === name);
    }
  }
  panels.get('node').querySelectorAll('[data-scene]').forEach(node => {
    const index = Number(node.dataset.scene); node.classList.toggle('selected', index === sceneIndex); node.setAttribute('aria-pressed', String(index === sceneIndex));
  });
}
function fitViews() {
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
    const width = Math.max(0, Math.min(wrap.clientWidth, wrap.clientHeight * 1.6));
    const frame = wrap.querySelector('.game-frame'); frame.style.width = `${width}px`; frame.style.height = `${width / 1.6}px`;
  }
}
function showPlacement(view, button) {
  closeMenus(); placementView = view;
  document.querySelector('#placement-title').textContent = `${names[view]} · yerleştir`;
  const target = document.querySelector('#placement-target'); target.replaceChildren();
  state.visible.filter(v => v !== view).forEach(v => { const option = document.createElement('option'); option.value = v; option.textContent = names[v]; target.append(option); });
  placement.querySelectorAll('[data-edge]').forEach(el => { el.disabled = !target.options.length; });
  document.querySelector('#menu-pin span').textContent = state.pinned.includes(view) ? 'Sabitlemeyi kaldır' : 'Pencereyi sabitle';
  document.querySelector('#menu-pin').title = 'Diğer ekran açıldığında bu pencere açık kalır.';
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
  x -= bounds.left; y -= bounds.top;
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
  const panel = event.target.closest('[data-panel]'); if (panel) focus(panel.dataset.panel);
  const object = event.target.closest('.editable [data-object]');
  const divider = event.target.closest('[data-divider]'), header = event.target.closest('[data-drag]');
  if (object) {
    selectedObject = object.dataset.object; object.focus({preventScroll:true}); updateScene();
    startGesture(event, {type:'object', object:selectedObject, scene:sceneIndex, original:{...scenes[sceneIndex].objects[selectedObject]}, bounds:object.closest('.game-frame').getBoundingClientRect()});
  } else if (divider) startGesture(event, {type:'divider', id:divider.dataset.divider, element:divider, axis:divider.parentElement.dataset.axis, bounds:divider.parentElement.getBoundingClientRect(), originalLayout:layout});
  else if (header && !event.target.closest('button')) {
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
  if (current.type === 'panel' && current.moved && !cancel && !current.outside && current.drop) layout = dockView(layout,current.view,current.drop.target,current.drop.edge);
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
  if (close) { apply(closeView(state,close.dataset.close)); document.querySelector(`[data-view="${close.dataset.close}"]`).focus({preventScroll:true}); }
  else if (pin) apply(togglePin(state,pin.dataset.pin));
  else if (menu) showPlacement(menu.dataset.placement,menu);
  else if (node) { sceneIndex = Number(node.dataset.scene); updateScene(); }
  else if (event.target.closest('[data-continue]') && !event.target.closest('.editable')) { sceneIndex = (sceneIndex+1)%scenes.length; updateScene(); }
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
document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click',()=>apply(openView(state,button.dataset.view))));
document.querySelectorAll('[data-capacity]').forEach(button => button.addEventListener('click',()=>apply(setCapacity(state,Number(button.dataset.capacity)))));
document.querySelector('#options').addEventListener('click',()=>{ const open=options.hidden; closeMenus(); options.hidden=!open; document.querySelector('#options').setAttribute('aria-expanded',String(open)); });
placement.addEventListener('click',event=>{
  const edge=event.target.closest('[data-edge]');
  if (edge) { layout=dockView(layout,placementView,document.querySelector('#placement-target').value,edge.dataset.edge); closeMenus(); render(); }
  else if (event.target.closest('#menu-pin')) apply(togglePin(state,placementView));
  else if (event.target.closest('#menu-close')) apply(closeView(state,placementView));
});
document.addEventListener('pointerdown',event=>{ if (!event.target.closest('.popover,#options,[data-placement]')) closeMenus(); });
document.addEventListener('keydown',event=>{ if (event.key==='Escape') { endGesture(true); closeMenus(); } });
document.querySelector('#reset').addEventListener('click',()=>{
  endGesture(true); scenes=newScenes(); sceneIndex=0; selectedObject='character'; zoom=1; state=initialState(); layout=initialLayout(); closeMenus();
  panels.get('node').querySelector('.graph-viewport').scrollTop=0; render();
});
const observer=new ResizeObserver(fitViews); observer.observe(workspace); observer.observe(panels.get('node').querySelector('.graph-viewport'));
for (const view of ['game','edit']) observer.observe(panels.get(view).querySelector('.scene-stage-wrap'));
render();
