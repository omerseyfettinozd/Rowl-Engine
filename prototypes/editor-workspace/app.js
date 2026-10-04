import { initialState, focusView, openView, closeView, togglePin, setCapacity } from './workspace-state.mjs';

const names = { node: 'Node', game: 'Game', edit: 'Düzenle' };
const baseScenes = [
  { title: 'Bir gece, bir ışık', speaker: 'Mira', dialogue: 'Gece ne kadar sessiz… Sanki bütün dünya bir şey söylememi bekliyor.', textSize: 18, opacity: 88 },
  { title: 'Yol ayrımı', speaker: 'Mira', dialogue: 'Soldaki yol ormana, sağdaki yol denize çıkıyor. Peki, kalbim hangisini seçer?', textSize: 18, opacity: 88 },
  { title: 'Yeni bir sabah', speaker: 'Mira', dialogue: 'Işık hep buradaymış. Onu görmek için biraz yürümem gerekiyormuş.', textSize: 18, opacity: 88 }
];
let scenes = structuredClone(baseScenes), sceneIndex = 0, state = initialState(), zoom = 1, toastTimer;
const workspace = document.querySelector('#workspace');
const panels = new Map();
for (const view of Object.keys(names)) {
  const panel = document.createElement('section');
  panel.className = 'panel'; panel.dataset.panel = view; panel.setAttribute('aria-label', `${names[view]} paneli`);
  panel.innerHTML = `<header class="panel-header"><div class="panel-title"><svg aria-hidden="true"><use href="#i-${view}"/></svg><span>${names[view]}</span><span class="pinned-label" hidden>Sabit</span></div><div class="panel-actions"><button class="icon-button pin-button" data-pin="${view}" aria-label="${names[view]} panelini sabitle" title="Paneli sabitle"><svg aria-hidden="true"><use href="#i-pin"/></svg></button><button class="icon-button" data-close="${view}" aria-label="${names[view]} panelini kapat" title="Paneli kapat"><svg aria-hidden="true"><use href="#i-close"/></svg></button></div></header><div class="panel-content"></div>`;
  panel.querySelector('.panel-content').append(document.querySelector(`#${view}-template`).content.cloneNode(true));
  panels.set(view, panel);
}

function notify(message) {
  const toast = document.querySelector('#toast');
  toast.textContent = message; toast.classList.add('visible');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove('visible'), 3400);
}
function apply(result) {
  state = result.state;
  if (result.message) notify(result.message);
  render();
}
function updateControls() {
  document.querySelectorAll('[data-view]').forEach(button => {
    const view = button.dataset.view, open = state.visible.includes(view), pinned = state.pinned.includes(view);
    button.setAttribute('aria-pressed', String(open));
    button.setAttribute('aria-label', `${names[view]} ekranı${open ? ', açık' : 'nı aç'}${pinned ? ', sabit' : ''}`);
    button.classList.toggle('focused', state.focused === view); button.classList.toggle('pinned', pinned);
  });
  document.querySelectorAll('[data-capacity]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.capacity) === state.capacity)));
  for (const [view, panel] of panels) {
    const pinned = state.pinned.includes(view);
    panel.classList.toggle('is-pinned', pinned); panel.classList.toggle('is-focused', state.focused === view);
    panel.querySelector('.pinned-label').hidden = !pinned;
    const pin = panel.querySelector('[data-pin]'); pin.setAttribute('aria-pressed', String(pinned));
    pin.setAttribute('aria-label', `${names[view]} panelinin ${pinned ? 'sabitlemesini kaldır' : 'yerini sabitle'}`);
    pin.title = pinned ? 'Sabitlemeyi kaldır' : 'Paneli sabitle';
  }
  const hint = state.visible.length === 1 ? 'Üstteki simgelere dokunarak çalışma alanını böl.' : state.capacity === 3 ? 'Üç ekran, tek hikâye. İstediğin paneli sabitle veya kapat.' : state.pinned.length ? 'Sabit panel yerinde kalır. Diğer simgeye dokunarak yanındaki paneli değiştir.' : 'Bir paneli sabitle; diğer alanı Node, Game veya Düzenle için kullan.';
  document.querySelector('#hint').textContent = hint;
}
function render() {
  const active = document.activeElement;
  for (const [view, panel] of panels) if (!state.visible.includes(view)) panel.remove();
  state.visible.forEach((view, index) => {
    const panel = panels.get(view);
    if (workspace.children[index] !== panel) workspace.insertBefore(panel, workspace.children[index] || null);
  });
  workspace.dataset.count = state.visible.length;
  updateControls(); updateScene();
  if (active && active !== document.body && !active.isConnected) document.querySelector(`[data-view="${state.focused}"]`).focus();
  requestAnimationFrame(fitGraph);
}
function updateScene(updateInputs = true) {
  const scene = scenes[sceneIndex], game = panels.get('game'), edit = panels.get('edit');
  game.querySelector('.scene-title').textContent = scene.title;
  game.querySelector('.speaker-name').textContent = scene.speaker;
  game.querySelector('.speaker-avatar').textContent = scene.speaker.slice(0, 1) || '·';
  game.querySelector('.dialogue-text').textContent = scene.dialogue;
  game.querySelector('.dialogue-text').style.fontSize = `${scene.textSize}px`;
  game.querySelector('.dialogue-box').style.background = `rgb(15 15 18 / ${scene.opacity / 100})`;
  game.querySelector('.chapter-number').textContent = String(sceneIndex + 1).padStart(2, '0');
  game.querySelector('.scene-counter').textContent = `${sceneIndex + 1} / 3`;
  edit.querySelector('.edit-scene-title').textContent = scene.title || 'Adsız sahne';
  edit.querySelector('#text-size-output').textContent = `${scene.textSize} px`;
  edit.querySelector('#box-opacity-output').textContent = `${scene.opacity}%`;
  if (updateInputs) edit.querySelectorAll('[data-field]').forEach(input => { input.value = scene[input.dataset.field]; });
  panels.get('node').querySelectorAll('[data-scene]').forEach(node => {
    const index = Number(node.dataset.scene);
    node.classList.toggle('selected', index === sceneIndex); node.setAttribute('aria-pressed', String(index === sceneIndex));
    node.querySelector('strong').textContent = scenes[index].title || 'Adsız sahne';
  });
}
function fitGraph() {
  const viewport = panels.get('node').querySelector('.graph-viewport');
  if (!viewport.isConnected || !viewport.clientWidth) return;
  const compact = viewport.clientWidth < 600;
  viewport.classList.toggle('is-compact', compact);
  const width = compact ? 260 : 780, height = compact ? 560 : 440;
  const baseScale = compact ? Math.min((viewport.clientWidth - 24) / width, 1) : Math.min((viewport.clientWidth - 24) / width, (viewport.clientHeight - 24) / height, 1.25);
  const scale = Math.max(.25, baseScale) * zoom;
  const world = viewport.querySelector('.graph-world');
  world.style.transform = `translate(${Math.max(0, (viewport.clientWidth - width * scale) / 2)}px, ${compact ? Math.max(0, (viewport.clientHeight - height * scale) / 2) : (viewport.clientHeight - height * scale) / 2}px) scale(${scale})`;
  panels.get('node').querySelector('.zoom-value').textContent = `${Math.round(scale * 100)}%`;
}
document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click', () => apply(openView(state, button.dataset.view))));
document.querySelectorAll('[data-capacity]').forEach(button => button.addEventListener('click', () => apply(setCapacity(state, Number(button.dataset.capacity)))));
workspace.addEventListener('pointerdown', event => {
  const panel = event.target.closest('[data-panel]');
  if (panel) { state = focusView(state, panel.dataset.panel); updateControls(); }
});
workspace.addEventListener('focusin', event => {
  const panel = event.target.closest('[data-panel]');
  if (panel) { state = focusView(state, panel.dataset.panel); updateControls(); }
});
workspace.addEventListener('click', event => {
  const pin = event.target.closest('[data-pin]'), close = event.target.closest('[data-close]'), scene = event.target.closest('[data-scene]'), zoomButton = event.target.closest('[data-zoom]');
  if (pin) apply(togglePin(state, pin.dataset.pin));
  else if (close) apply(closeView(state, close.dataset.close));
  else if (scene) { sceneIndex = Number(scene.dataset.scene); updateScene(); }
  else if (event.target.closest('#continue-scene')) { sceneIndex = (sceneIndex + 1) % scenes.length; updateScene(); }
  else if (zoomButton) { zoom = zoomButton.dataset.zoom === 'fit' ? 1 : Math.max(.5, Math.min(2, zoom + (zoomButton.dataset.zoom === 'in' ? .15 : -.15))); fitGraph(); }
});
workspace.addEventListener('input', event => {
  if (!event.target.matches('[data-field]')) return;
  const field = event.target.dataset.field;
  scenes[sceneIndex][field] = ['textSize', 'opacity'].includes(field) ? Number(event.target.value) : event.target.value;
  updateScene(false);
});
document.querySelector('#reset').addEventListener('click', () => {
  scenes = structuredClone(baseScenes); sceneIndex = 0; zoom = 1; state = initialState();
  for (const panel of panels.values()) panel.querySelectorAll('.edit-area,.graph-viewport').forEach(element => { element.scrollTop = 0; element.scrollLeft = 0; });
  render(); notify('İlk görünüme döndük.');
});
new ResizeObserver(fitGraph).observe(panels.get('node').querySelector('.graph-viewport'));
render();
