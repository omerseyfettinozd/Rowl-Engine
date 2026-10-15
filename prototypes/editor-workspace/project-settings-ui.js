// prototypes/editor-workspace/project-settings-ui.js
// Unity Paritesi: Proje ve Oyun Ayarları (Project Settings)
// Bu penceredeki tüm ayarlar oyun projesine (project.rowlproj) aittir, Git ile versiyonlanır ve build paketine gömülür.

const PROJECT_STORE_KEY = 'rowl.project.settings.v1';

export const DEFAULT_PROJECT_SETTINGS = {
  player: {
    productName: 'Bir gece, bir ışık',
    companyName: 'Rowl Studio',
    version: '1.0.0',
    description: 'Ay ışığında başlayan küçük bir görsel roman yolculuğu.',
    startScene: 'selected'
  },
  display: {
    resolution: '1920x1080',
    scaleMode: 'Oranı koru',
    vsync: true,
    standaloneWindow: false,
    letterboxColor: '#0a0a0c'
  },
  audio: {
    defaultBgmTransition: 'fade',
    defaultBgmTransitionDurationSeconds: 1.0,
    masterVolume: 100,
    musicVolume: 80,
    voiceVolume: 100,
    sfxVolume: 80
  },
  narrative: {
    defaultTextSpeed: 32,
    autoAdvanceDelay: 2.0,
    saveSlotCount: 10,
    autosaveEnabled: true,
    autosaveTrigger: 'Sahne geçişlerinde'
  },
  build: {
    platform: 'Linux',
    packageName: 'bir-gece-bir-isik',
    compression: 'Zstd (Önerilen)',
    includeSourceAssets: true,
    includeScripts: true,
    includeLanguages: true
  }
};

const CATEGORIES = [
  { id: 'player', icon: 'assets', title: 'Player (Oyun Kimliği)', desc: 'Oyun adı, şirket, sürüm ve başlangıç sahnesi' },
  { id: 'display', icon: 'game', title: 'Görüntü ve Çözünürlük', desc: '1920×1080 tuvali, AspectGuardian, VSync' },
  { id: 'audio', icon: 'audio', title: 'Ses Runtime (AudioEngine)', desc: 'BGM geçiş türü, geçiş süreleri ve mikser' },
  { id: 'narrative', icon: 'story', title: 'Hikâye ve Kayıt Sistemi', desc: 'Metin hızı defaultları, kayıt slot tavanı' },
  { id: 'build', icon: 'export', title: 'Build ve Paketleme', desc: 'Zstd .rowlpkg paketi, platform derlemesi' }
];

export function bindProjectSettings(dialog, context) {
  let projectSettings = loadProjectSettings();
  let activeCategory = 'player';
  let dirty = false;

  function loadProjectSettings() {
    try {
      const raw = localStorage.getItem(PROJECT_STORE_KEY);
      if (!raw) return structuredClone(DEFAULT_PROJECT_SETTINGS);
      return { ...structuredClone(DEFAULT_PROJECT_SETTINGS), ...JSON.parse(raw) };
    } catch {
      return structuredClone(DEFAULT_PROJECT_SETTINGS);
    }
  }

  function saveProjectSettings() {
    localStorage.setItem(PROJECT_STORE_KEY, JSON.stringify(projectSettings));
    dirty = false;
    render();
    if (context?.apply) {
      context.apply({
        project: projectSettings.player.productName,
        resolution: projectSettings.display.resolution,
        startScene: projectSettings.player.startScene
      });
    }
  }

  const toggle = (path, title, desc, checked) => `
    <label class="setting-toggle" for="pj-toggle-${path}">
      <span><strong>${title}</strong><small>${desc}</small></span>
      <input id="pj-toggle-${path}" data-path="${path}" type="checkbox" ${checked ? 'checked' : ''}>
      <i aria-hidden="true"></i>
    </label>`;

  const field = (path, title, value, type = 'text', extra = '') => `
    <label class="setting-field" for="pj-field-${path}">
      ${title}
      <input id="pj-field-${path}" data-path="${path}" type="${type}" value="${value}" ${extra}>
    </label>`;

  const select = (path, title, value, options) => `
    <label class="setting-field" for="pj-select-${path}">
      ${title}
      <select id="pj-select-${path}" data-path="${path}">
        ${options.map(item => {
          const [val, label] = Array.isArray(item) ? item : [item, item];
          return `<option value="${val}" ${String(val) === String(value) ? 'selected' : ''}>${label}</option>`;
        }).join('')}
      </select>
    </label>`;

  const range = (path, title, value, min, max, step = 1, suffix = '') => `
    <label class="setting-range" for="pj-range-${path}">
      <span>${title} <output data-out="${path}">${value}${suffix}</output></span>
      <input id="pj-range-${path}" data-path="${path}" type="range" min="${min}" max="${max}" step="${step}" value="${value}" data-suffix="${suffix}">
    </label>`;

  const card = (title, desc, content) => `
    <section class="settings-card">
      <header><h3>${title}</h3><p>${desc}</p></header>
      ${content}
    </section>`;

  const sections = {
    player: () => `
      <div class="settings-columns">
        ${card('Oyun Kimliği ve Meta Veriler (project.rowlproj)', 'Oyun pencere başlığında ve yürütülebilir pakette görünen bilgiler.', `
          ${field('player.productName', 'Oyun Adı (Product Name)', projectSettings.player.productName, 'text', 'maxlength="80" required')}
          <div class="field-pair">
            ${field('player.companyName', 'Şirket / Stüdyo', projectSettings.player.companyName, 'text', 'maxlength="80"')}
            ${field('player.version', 'Sürüm (SemVer)', projectSettings.player.version, 'text', 'maxlength="24"')}
          </div>
          ${select('player.startScene', 'Başlangıç Düğümü (Entry Node)', projectSettings.player.startScene, [
            ['selected', 'Seçili Düğüm (Node)'],
            ['0', 'Düğüm 0: Bir gece, bir ışık'],
            ['1', 'Düğüm 1: Yol ayrımı'],
            ['2', 'Düğüm 2: Yeni bir sabah']
          ])}
          <label class="setting-field" for="pj-field-desc">Proje Özeti
            <textarea id="pj-field-desc" data-path="player.description" rows="2" maxlength="400">${projectSettings.player.description}</textarea>
          </label>
        `)}
        ${card('Proje Kapsamı ve Varlık Durumu', 'Aktif hikâye ağacının motor seviyesi özeti.', `
          <div class="info-note">
            <svg><use href="#i-assets"/></svg>
            <span>Proje Yolu: /home/chaple/Belgeler/Rowl Engine</span>
          </div>
          <div class="info-note" style="margin-top: 8px;">
            <svg><use href="#i-story"/></svg>
            <span>3 Sahne Düğümü · 1 Karakter · 1 Arka Plan · Türkçe</span>
          </div>
        `)}
      </div>`,

    display: () => `
      <div class="settings-columns">
        ${card('Ekran Çözünürlüğü (EngineConfig)', 'AspectGuardian en-boy koruma ve tuval yapılandırması.', `
          ${select('display.resolution', 'Sanal Çözünürlük (Virtual Resolution)', projectSettings.display.resolution, [
            ['1920x1080', '1920 × 1080 (16:9) · Standart (Önerilen)'],
            ['1280x800', '1280 × 800 (16:10) · Kompakt'],
            ['1080x1920', '1080 × 1920 (9:16) · Dikey Mobil']
          ])}
          <div class="field-pair">
            ${select('display.scaleMode', 'Ölçekleme Modu', projectSettings.display.scaleMode, ['Oranı koru', 'Ekranı doldur'])}
            ${field('display.letterboxColor', 'Letterbox Boş Alan Rengi', projectSettings.display.letterboxColor, 'color')}
          </div>
          ${toggle('display.vsync', 'Oyun İçi VSync (Dikey Senkronizasyon)', 'Ekran yırtılmalarını önlemek için kare hızını ekran yenilemesine kitle.', projectSettings.display.vsync)}
          ${toggle('display.standaloneWindow', 'Ayrık Pencere Modu', 'Açıkken bağımsız SDL3 penceresi oluşturur; kapalıyken editöre gömülür.', projectSettings.display.standaloneWindow)}
        `)}
        ${card('Çözünürlük Önizlemesi', 'Hedef cihaz ekran oranı ve çerçeve görünümü.', `
          <div class="display-preview">
            <div data-display-frame style="aspect-ratio: 16/9;">
              <span class="preview-moon"></span>
              <span class="preview-hill"></span>
              <div>Mira<br><small>Bir gece, bir ışık</small></div>
            </div>
          </div>
          <div class="preview-caption">1920 × 1080 · Aspect Preserved</div>
        `)}
      </div>`,

    audio: () => `
      <div class="settings-columns">
        ${card('Müzik ve Geçişler (C++ AudioEngine)', 'SDL3 Audio Engine sahne müzik geçiş parametreleri.', `
          ${select('audio.defaultBgmTransition', 'Varsayılan BGM Geçiş Türü', projectSettings.audio.defaultBgmTransition, [
            ['fade', 'Fade (Kararma ile Yumuşak Geçiş)'],
            ['crossfade', 'Crossfade (Çapraz Karışım)'],
            ['instant', 'Instant (Anında Değişim)']
          ])}
          ${range('audio.defaultBgmTransitionDurationSeconds', 'Geçiş Süresi', projectSettings.audio.defaultBgmTransitionDurationSeconds, 0, 10, 0.5, ' sn')}
          <div class="info-note">
            <svg><use href="#i-audio"/></svg>
            <span>Değerler doğrudan project.rowlproj içine kaydedilir ve C++ AudioEngine tarafından yürütülür.</span>
          </div>
        `)}
        ${card('Varsayılan Ses Mikseri Düzeyleri', 'Oyun ilk kurulduğunda geçerli olan varsayılan ses seviyeleri.', `
          ${range('audio.masterVolume', 'Master Volume', projectSettings.audio.masterVolume, 0, 100, 1, '%')}
          ${range('audio.musicVolume', 'Müzik Kanalı (BGM)', projectSettings.audio.musicVolume, 0, 100, 1, '%')}
          <div class="field-pair">
            ${range('audio.voiceVolume', 'Seslendirme', projectSettings.audio.voiceVolume, 0, 100, 1, '%')}
            ${range('audio.sfxVolume', 'Efektler', projectSettings.audio.sfxVolume, 0, 100, 1, '%')}
          </div>
        `)}
      </div>`,

    narrative: () => `
      <div class="settings-columns">
        ${card('Diyalog ve Metin Akışı Defaultları', 'Düğüm ezmesi olmayan sahnelerde geçerli motor varsayılanları.', `
          <div class="field-pair">
            ${range('narrative.defaultTextSpeed', 'Varsayılan Metin Hızı', projectSettings.narrative.defaultTextSpeed, 10, 60, 1, ' cps')}
            ${range('narrative.autoAdvanceDelay', 'Otomatik İlerleme Beklemesi', projectSettings.narrative.autoAdvanceDelay, 0, 10, 0.5, ' sn')}
          </div>
          <div class="info-note">
            <svg><use href="#i-inspector"/></svg>
            <span>Sahne düğümlerinde "Metin Hızını Ez" seçeneği işaretlenirse düğümün kendi değeri öncelikli olur.</span>
          </div>
        `)}
        ${card('Kayıt Sistemi (ProjectRuntimeSettings)', 'Faz 4.5 kayıt slot bütçesi ve kalıcılık kuralları.', `
          <div class="field-pair">
            ${field('narrative.saveSlotCount', 'Kayıt Yuvası Sayısı (1–100)', projectSettings.narrative.saveSlotCount, 'number', 'min="1" max="100"')}
            ${select('narrative.autosaveTrigger', 'Otomatik Kayıt Noktası', projectSettings.narrative.autosaveTrigger, ['Sahne geçişlerinde', 'Bölüm sonlarında', 'Kritik seçimlerde'])}
          </div>
          ${toggle('narrative.autosaveEnabled', 'Otomatik İlerleme Kaydı', 'Sahne geçişlerinde son durumu otomatik olarak kaydet.', projectSettings.narrative.autosaveEnabled)}
        `)}
      </div>`,

    build: () => `
      <div class="settings-columns">
        ${card('Hedef Platform ve Paket Formatı', 'ProjectBuildService derleme çıktısı.', `
          <div class="platform-options">
            ${['Linux', 'Windows', 'macOS', 'Android'].map(name => `
              <label for="pj-platform-${name.toLowerCase()}">
                <input id="pj-platform-${name.toLowerCase()}" data-path="build.platform" type="radio" name="pj-platform" value="${name}" ${projectSettings.build.platform === name ? 'checked' : ''}>
                <span>${name === 'Android' ? '▯' : '▱'}</span>
                <strong>${name}</strong>
              </label>
            `).join('')}
          </div>
          <div class="field-pair">
            ${field('build.packageName', 'Paket Dağıtım Adı', projectSettings.build.packageName, 'text', 'maxlength="80"')}
            ${select('build.compression', 'Sıkıştırma Algoritması', projectSettings.build.compression, ['Zstd (Önerilen)', 'LZ4 Hızlı', 'Ham Depolama'])}
          </div>
        `)}
        ${card('Paket İçeriği ve Varlık Koruma', 'Oyun arşivine dahil edilecek bileşenler.', `
          ${toggle('build.includeSourceAssets', 'Kaynak Varlıkları Dahil Et (SourceAssets)', 'Yeniden dönüşüm ve editör kaynaklarını koru.', projectSettings.build.includeSourceAssets)}
          ${toggle('build.includeScripts', 'Lua Script Dosyaları', 'Hikâye grafiği ile ilişkili scriptleri pakete ekle.', projectSettings.build.includeScripts)}
          ${toggle('build.includeLanguages', 'Yerelleştirme Tabloları', 'i18n kataloglarını ve çeviri dosyalarını ekle.', projectSettings.build.includeLanguages)}
        `)}
      </div>`
  };

  function render() {
    const activeData = CATEGORIES.find(c => c.id === activeCategory) || CATEGORIES[0];

    dialog.innerHTML = `
      <form id="project-settings-form" class="settings-layout">
        <aside class="settings-sidebar">
          <div class="settings-sidebar-header">
            <span class="settings-badge" style="background: #2a2723; color: #baa88c; border: 1px solid #4a433a;">Proje Yapılandırması</span>
            <h2>Proje Ayarları</h2>
          </div>
          <nav class="settings-nav" aria-label="Proje ayar kategorileri">
            ${CATEGORIES.map(c => `
              <button type="button" class="settings-nav-item" data-category="${c.id}" aria-current="${c.id === activeCategory ? 'page' : 'false'}">
                <svg class="settings-nav-icon"><use href="#i-${c.icon}"/></svg>
                <span class="settings-nav-label">${c.title}</span>
              </button>
            `).join('')}
          </nav>
          <div class="settings-sidebar-footer">
            <span class="settings-dirty" data-dirty>${dirty ? 'Değişiklikler var' : 'Kayıtlı'}</span>
          </div>
        </aside>

        <section class="settings-main">
          <header class="settings-main-header">
            <div class="settings-title-group">
              <h3 id="project-settings-title">${activeData.title}</h3>
              <p class="settings-desc">${activeData.desc}</p>
            </div>
            <button type="button" class="icon-button settings-close-btn" data-dismiss aria-label="Ayarları kapat" title="Kapat">
              <svg><use href="#i-close"/></svg>
            </button>
          </header>

          <div class="settings-scroll">
            ${sections[activeCategory] ? sections[activeCategory]() : sections.player()}
          </div>

          <footer class="settings-main-footer">
            <button type="button" class="text-button" data-dismiss>Vazgeç</button>
            <button type="submit" class="primary-button">Proje Ayarlarını Kaydet</button>
          </footer>
        </section>
      </form>
    `;

    bindFormEvents();
  }

  function bindFormEvents() {
    dialog.querySelectorAll('[data-path]').forEach(el => {
      const path = el.dataset.path.split('.');
      el.addEventListener('change', () => {
        setNestedValue(projectSettings, path, el.type === 'checkbox' ? el.checked : el.value);
        dirty = true;
        const dirtyEl = dialog.querySelector('[data-dirty]');
        if (dirtyEl) dirtyEl.textContent = 'Değişiklikler var';
      });
      el.addEventListener('input', () => {
        if (el.type === 'range') {
          const out = dialog.querySelector(`[data-out="${el.dataset.path}"]`);
          if (out) out.textContent = `${el.value}${el.dataset.suffix || ''}`;
        }
        setNestedValue(projectSettings, path, el.type === 'checkbox' ? el.checked : (el.type === 'range' || el.type === 'number' ? Number(el.value) : el.value));
        dirty = true;
        const dirtyEl = dialog.querySelector('[data-dirty]');
        if (dirtyEl) dirtyEl.textContent = 'Değişiklikler var';
      });
    });
  }

  function setNestedValue(obj, pathArr, val) {
    let curr = obj;
    for (let i = 0; i < pathArr.length - 1; i++) {
      if (!curr[pathArr[i]]) curr[pathArr[i]] = {};
      curr = curr[pathArr[i]];
    }
    curr[pathArr[pathArr.length - 1]] = val;
  }

  dialog.addEventListener('click', event => {
    if (event.target.closest('[data-dismiss]')) {
      dialog.close();
      return;
    }
    const catBtn = event.target.closest('[data-category]');
    if (catBtn) {
      activeCategory = catBtn.dataset.category;
      render();
    }
  });

  dialog.addEventListener('submit', event => {
    event.preventDefault();
    saveProjectSettings();
    dialog.close();
  });

  return {
    open(catId) {
      context.beforeOpen();
      activeCategory = CATEGORIES.some(c => c.id === catId) ? catId : 'player';
      projectSettings = loadProjectSettings();
      dirty = false;
      render();
      dialog.showModal();
    }
  };
}
