// prototypes/editor-workspace/settings-ui.js
// Meta Muse Spark 1.3 Contributor Mimari Tasarımı
// Unity Preferences Paritesi: Yalnızca Makine-Yerel Editör Tercihleri
// Asla oyun projesine veya dağıtım paketine dahil edilmez.

const STORE_KEY = 'rowl.editor.preferences.v1';
const VERSION = 1;

export const DEFAULT_PROFILE = {
  version: VERSION,
  general: {
    language: 'system',
    openLastProject: true,
    showSplash: false,
    confirmOnExit: true,
    autoSaveEnabled: true,
    autoSaveIntervalMin: 5,
    backupCount: 5
  },
  appearance: {
    theme: 'dark',
    accent: '#4cc2ff',
    fontSize: 13,
    fontFamily: 'Inter',
    density: 'comfortable',
    showIcons: true
  },
  viewport: {
    bgColor: '#1e1e24',
    gridVisibleDefault: true,
    cameraSpeed: 5.0,
    orbitSensitivity: 1.0,
    showGizmosDefault: true,
    showStatsOverlay: false
  },
  gridSnapping: {
    sizeX: 1.0,
    sizeY: 1.0,
    divisions: 10,
    colorMinor: '#2a2a33',
    colorMajor: '#3a3a44',
    snapMove: true,
    snapRotate: true,
    snapScale: false,
    stepMove: 0.25,
    stepRotate: 15,
    stepScale: 0.1
  },
  external: {
    ide: 'vscode',
    idePath: '',
    ideArgs: '{file}:{line}',
    terminalPath: '',
    blenderPath: '',
    gitPath: ''
  },
  shortcuts: {
    profile: 'rowl-default',
    bindings: {
      'save': 'Ctrl + S',
      'saveAs': 'Ctrl + Shift + S',
      'undo': 'Ctrl + Z',
      'redo': 'Ctrl + Y',
      'newNode': 'Ctrl + N',
      'delete': 'Delete',
      'rename': 'F2',
      'build': 'Ctrl + B',
      'play': 'F5',
      'fullscreen': 'F11',
      'find': 'Ctrl + F',
      'hub': 'Ctrl + H',
      'preferences': 'Ctrl + ,'
    }
  },
  pipeline: {
    cachePath: '~/.cache/rowl',
    maxCacheSizeGB: 10,
    autoReimport: true,
    compressThumbnails: true,
    thumbnailSize: 128
  },
  performance: {
    vsyncEditor: true,
    fpsCap: 120,
    hardwareAccel: true,
    logLevel: 'Info',
    showProfilerOnPlay: false,
    telemetry: false,
    crashReport: true
  }
};

// Yasak anahtar guard'ı - Oyun ayarlarının Preferences içine sızmasını kesin olarak engeller
const FORBIDDEN_PATTERNS = [
  /^game\./i,
  /^project\./i,
  /resolution/i,
  /masterVolume/i,
  /musicVolume/i,
  /voiceVolume/i,
  /sfxVolume/i,
  /textSpeed/i,
  /startupScene/i,
  /saveSlot/i,
  /gameplay/i,
  /difficulty/i
];

function assertPreferencesOnly(obj, path = '') {
  for (const [key, value] of Object.entries(obj)) {
    const full = path ? `${path}.${key}` : key;
    if (FORBIDDEN_PATTERNS.some(p => p.test(full))) {
      throw new Error(`[Preferences Mimarisi Hatası] Yasak oyun anahtarı saptandı: '${full}'. Oyun ayarları yalnızca Proje Ayarları'nda yer alabilir.`);
    }
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      assertPreferencesOnly(value, full);
    }
  }
}

export const CATEGORIES = [
  { id: 'general', icon: 'tools', title: 'Genel', desc: 'Başlangıç, bildirimler, otomatik kaydetme' },
  { id: 'appearance', icon: 'layout', title: 'Görünüm & Tema', desc: 'Tema, yazı tipi, arayüz yoğunluğu' },
  { id: 'viewport', icon: 'game', title: 'Sahne Görünümü / Tuval', desc: 'Editör kamerası, kılavuzlar, arka plan' },
  { id: 'gridSnapping', icon: 'node', title: 'Izgara & Yapışma', desc: 'Tuval ızgarası, snap adımları' },
  { id: 'external', icon: 'lua', title: 'Harici Araçlar', desc: 'IDE, kod editörü, terminal, Git' },
  { id: 'shortcuts', icon: 'input', title: 'Kısayollar', desc: 'Editör klavye tuş eşlemleri' },
  { id: 'pipeline', icon: 'assets', title: 'Varlık Hattı & Önbellek', desc: 'Varlık önbelleği, küçük resim boyutu' },
  { id: 'performance', icon: 'tools', title: 'Performans & Tanılama', desc: 'Editör FPS, GPU hızlandırma, loglar' }
];

const GAME_SEARCH_TERMS = ['çözünürlük', 'ses', 'müzik', 'volume', 'kayıt', 'slot', 'metin hızı', 'sahne', 'fps oyunu', 'zorluk', 'oyuncu'];

export function bindSettings(dialog, context) {
  let profile = loadProfile();
  let activeCategory = 'general';
  let searchText = '';
  let dirty = false;

  function loadProfile() {
    try {
      const raw = localStorage.getItem(STORE_KEY);
      if (!raw) return structuredClone(DEFAULT_PROFILE);
      const parsed = JSON.parse(raw);
      const merged = { ...structuredClone(DEFAULT_PROFILE), ...parsed };
      assertPreferencesOnly(merged);
      return merged;
    } catch (err) {
      console.warn('[Rowl Preferences] Kayıtlı tercihler yüklenirken hata oluştu veya yasak anahtar bulundu, varsayılana dönülüyor:', err);
      return structuredClone(DEFAULT_PROFILE);
    }
  }

  function saveProfile() {
    try {
      assertPreferencesOnly(profile);
      localStorage.setItem(STORE_KEY, JSON.stringify(profile));
      dirty = false;
      render();
      context.apply({
        grid: profile.gridSnapping.snapMove,
        guides: profile.viewport.showGizmosDefault
      });
      document.documentElement.classList.toggle('reduce-motion', profile.appearance.density === 'compact');
    } catch (err) {
      alert(`Tercihler kaydedilemedi: ${err.message}`);
    }
  }

  const toggle = (path, title, desc, checked) => `
    <label class="setting-toggle" for="toggle-${path}">
      <span><strong>${title}</strong><small>${desc}</small></span>
      <input id="toggle-${path}" data-path="${path}" type="checkbox" ${checked ? 'checked' : ''}>
      <i aria-hidden="true"></i>
    </label>`;

  const field = (path, title, value, type = 'text', extra = '') => `
    <label class="setting-field" for="field-${path}">
      ${title}
      <input id="field-${path}" data-path="${path}" type="${type}" value="${value}" ${extra}>
    </label>`;

  const select = (path, title, value, options) => `
    <label class="setting-field" for="select-${path}">
      ${title}
      <select id="select-${path}" data-path="${path}">
        ${options.map(item => {
          const [val, label] = Array.isArray(item) ? item : [item, item];
          return `<option value="${val}" ${String(val) === String(value) ? 'selected' : ''}>${label}</option>`;
        }).join('')}
      </select>
    </label>`;

  const range = (path, title, value, min, max, step = 1, suffix = '') => `
    <label class="setting-range" for="range-${path}">
      <span>${title} <output data-out="${path}">${value}${suffix}</output></span>
      <input id="range-${path}" data-path="${path}" type="range" min="${min}" max="${max}" step="${step}" value="${value}" data-suffix="${suffix}">
    </label>`;

  const card = (title, desc, content) => `
    <section class="settings-card">
      <header><h3>${title}</h3><p>${desc}</p></header>
      ${content}
    </section>`;

  const sections = {
    general: () => `
      <div class="settings-columns">
        ${card('Otomasyon ve Yedekleme (EditorSettingsProfile)', 'Makine-yerel çalışma alanı yedekleme ve oturum davranışı.', `
          ${toggle('general.autoSaveEnabled', 'Editör Otomatik Kaydetme', 'Düzenlenen hikâye grafiğini ve sahneleri arka planda periyodik olarak kaydeder.', profile.general.autoSaveEnabled)}
          <div class="field-pair">
            ${range('general.autoSaveIntervalMin', 'Kayıt Aralığı', profile.general.autoSaveIntervalMin, 1, 30, 1, ' dk')}
            ${range('general.backupCount', 'Maksimum Yedek Sayısı', profile.general.backupCount, 1, 20, 1, ' adet')}
          </div>
          <div class="info-note">
            <svg><use href="#i-tools"/></svg>
            <span>Not: Bu ayar sahne veya oyun kayıt dosyalarını değil, editör oturum durumunu yönetir.</span>
          </div>
        `)}
        ${card('Başlangıç ve Oturum Davranışı', 'Editörün açılış ve kapanış süreçlerini yapılandır.', `
          ${toggle('general.openLastProject', 'Son Projeyi Otomatik Aç', 'Editör başlatıldığında son çalışılan projeyi doğrudan yükle.', profile.general.openLastProject)}
          ${toggle('general.confirmOnExit', 'Çıkışta Kaydedilmemiş Değişiklikleri Sor', 'Kaydedilmemiş değişiklik varken kapatma onayı iste.', profile.general.confirmOnExit)}
          ${select('general.language', 'Editör Arayüz Dili', profile.general.language, [
            ['system', 'Sistem Varsayılanı'],
            ['tr', 'Türkçe'],
            ['en', 'English']
          ])}
        `)}
      </div>`,

    appearance: () => `
      <div class="settings-columns">
        ${card('Editör Teması ve Renkleri (Avalonia)', 'Rowl Engine editör kabuk görünümü ve tema paleti.', `
          ${select('appearance.theme', 'Tema', profile.appearance.theme, [
            ['dark', 'Kemik Beyazı (Koyu) — Unity Dark Paritesi'],
            ['oled', 'Saf Siyah (OLED) — Yüksek Kontrast'],
            ['light', 'Açık Tema (Deneysel)']
          ])}
          <div class="theme-preview">
            <div><i></i><i></i><i></i></div>
            <span></span><span></span><span></span>
          </div>
          <div class="field-pair" style="margin-top: 12px;">
            ${select('appearance.accent', 'Vurgu Rengi', profile.appearance.accent, [
              ['#4cc2ff', 'Rowl Mavisi (#4CC2FF)'],
              ['#e5dfd0', 'Kemik Beyazı (#E5DFD0)'],
              ['#b5c6b2', 'Adaçayı Yeşili (#B5C6B2)'],
              ['#bfb4df', 'Lavanta (#BFB4DF)']
            ])}
            ${select('appearance.density', 'Yerleşim Yoğunluğu', profile.appearance.density, [
              ['comfortable', 'Rahat (Varsayılan)'],
              ['compact', 'Kompakt (Yüksek Bilgi Yoğunluğu)']
            ])}
          </div>
        `)}
        ${card('Tipografi ve İkonlar', 'Arayüz metin ölçeği ve simge görünürlüğü.', `
          ${range('appearance.fontSize', 'Temel Yazı Boyutu', profile.appearance.fontSize, 11, 16, 1, ' px')}
          ${select('appearance.fontFamily', 'Arayüz Font Ailesi', profile.appearance.fontFamily, [
            ['Inter', 'Inter (Varsayılan)'],
            ['Segoe UI', 'Segoe UI'],
            ['Roboto', 'Roboto'],
            ['Ubuntu', 'Ubuntu Sans']
          ])}
          ${toggle('appearance.showIcons', 'Menü ve Panel İkonlarını Göster', 'Arayüz butonlarında simge grafiklerini aktif kıl.', profile.appearance.showIcons)}
        `)}
      </div>`,

    viewport: () => `
      <div class="settings-columns">
        ${card('Editör Sahne Kamerası ve Tuvali', 'Sahne editörü kamerası ve arka plan görünümü (Oyun kamerası değildir).', `
          <div class="field-pair">
            ${field('viewport.bgColor', 'Tuval Zemin Rengi', profile.viewport.bgColor, 'color')}
            ${range('viewport.cameraSpeed', 'Kamera Kaydırma Hızı', profile.viewport.cameraSpeed, 1.0, 15.0, 0.5, 'x')}
          </div>
          ${range('viewport.orbitSensitivity', 'Yörünge / Pan Hassasiyeti', profile.viewport.orbitSensitivity, 0.2, 3.0, 0.1, 'x')}
          <div class="info-note">
            <svg><use href="#i-game"/></svg>
            <span>Kamera ayarları yalnızca Edit Scene içinde objeleri incelerken geçerlidir.</span>
          </div>
        `)}
        ${card('Kılavuzlar ve Görsel Göstergeler (Gizmos)', 'Editörde objeleri konumlandırırken yardımcı olan görsel rehberler.', `
          ${toggle('viewport.gridVisibleDefault', 'Varsayılan Olarak Izgarayı Aç', 'Yeni tuval açıldığında ızgarayı görünür tut.', profile.viewport.gridVisibleDefault)}
          ${toggle('viewport.showGizmosDefault', 'Obje Tutamaçları ve Çerçeveleri (Gizmos)', 'Seçili objelerde taşıma, döndürme ve sınır çizgilerini göster.', profile.viewport.showGizmosDefault)}
          ${toggle('viewport.showStatsOverlay', 'Editör FPS ve Bellek Göstergesi', 'Tuval köşesinde anlık editör render FPS sayacını göster.', profile.viewport.showStatsOverlay)}
        `)}
      </div>`,

    gridSnapping: () => `
      <div class="settings-columns">
        ${card('Izgara Yapılandırması (Grid Settings)', 'Hikâye grafiği ve sahne tuvali ızgara boyutları.', `
          <div class="field-pair">
            ${field('gridSnapping.sizeX', 'Hücre Genişliği (X)', profile.gridSnapping.sizeX, 'number', 'step="0.1" min="0.1"')}
            ${field('gridSnapping.sizeY', 'Hücre Yüksekliği (Y)', profile.gridSnapping.sizeY, 'number', 'step="0.1" min="0.1"')}
          </div>
          <div class="field-pair">
            ${field('gridSnapping.divisions', 'Bölüm Çizgisi Sayısı', profile.gridSnapping.divisions, 'number', 'min="2" max="20"')}
            ${field('gridSnapping.colorMajor', 'Ana Çizgi Rengi', profile.gridSnapping.colorMajor, 'color')}
          </div>
        `)}
        ${card('Manyetik Yapışma Adımları (Snapping)', 'Objeleri taşırken ve boyutlandırırken kenetlenme artışları.', `
          ${toggle('gridSnapping.snapMove', 'Konum Yapışması (Move Snap)', 'Objeleri ve düğümleri taşırken ızgaraya kenetle.', profile.gridSnapping.snapMove)}
          ${toggle('gridSnapping.snapRotate', 'Döndürme Yapışması (Rotate Snap)', 'Açı tutamaçlarını belirli adımlara kilitle.', profile.gridSnapping.snapRotate)}
          <div class="field-pair">
            ${select('gridSnapping.stepMove', 'Taşıma Adımı', profile.gridSnapping.stepMove, [
              ['0.1', '0.1 birim'],
              ['0.25', '0.25 birim (Önerilen)'],
              ['0.5', '0.5 birim'],
              ['1.0', '1.0 birim']
            ])}
            ${select('gridSnapping.stepRotate', 'Döndürme Adımı', profile.gridSnapping.stepRotate, [
              ['5', '5° derece'],
              ['15', '15° derece (Standart)'],
              ['45', '45° derece'],
              ['90', '90° derece']
            ])}
          </div>
        `)}
      </div>`,

    external: () => `
      <div class="settings-columns">
        ${card('Harici Kod Editörü (IDE)', 'Script ve metin dosyalarını çift tıklayınca açılacak geliştirici ortamı.', `
          ${select('external.ide', 'Tercih Edilen Kod Editörü', profile.external.ide, [
            ['vscode', 'Visual Studio Code (code)'],
            ['cursor', 'Cursor AI Editor (cursor)'],
            ['zed', 'Zed Editor (zed)'],
            ['sublime', 'Sublime Text (subl)'],
            ['custom', 'Özel Komut']
          ])}
          ${field('external.idePath', 'Özel Editör Yolu (Boşsa otomatik algılanır)', profile.external.idePath, 'text', 'placeholder="/usr/bin/code"')}
          ${field('external.ideArgs', 'Başlatma Parametresi', profile.external.ideArgs, 'text', 'placeholder="{file}:{line}"')}
        `)}
        ${card('Harici Sistem Araçları', 'İşletim sistemi terminali, Blender ve Git entegrasyonu.', `
          ${field('external.terminalPath', 'Terminal Yolu (Boş = Sistem Varsayılanı)', profile.external.terminalPath, 'text', 'placeholder="gnome-terminal, alacritty, kitty"')}
          ${field('external.gitPath', 'Git Yürütülebilir Dosyası', profile.external.gitPath, 'text', 'placeholder="git"')}
          ${field('external.blenderPath', 'Blender Yolu (İsteğe bağlı 3D model içe aktarımı)', profile.external.blenderPath, 'text', 'placeholder="blender"')}
        `)}
      </div>`,

    shortcuts: () => `
      <div class="settings-columns">
        ${card('Editör ve Proje Kısayolları (Keymap)', 'Unity ve Rowl Engine tuş eşlemleri.', `
          <div class="binding-list">
            <label><span>Kaydet (Save)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.save || 'Ctrl + S'}</kbd></label>
            <label><span>Farklı Kaydet (Save As)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.saveAs || 'Ctrl + Shift + S'}</kbd></label>
            <label><span>Geri Al (Undo)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.undo || 'Ctrl + Z'}</kbd></label>
            <label><span>Yinele (Redo)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.redo || 'Ctrl + Y'}</kbd></label>
            <label><span>Yeni Düğüm (New Node)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.newNode || 'Ctrl + N'}</kbd></label>
            <label><span>Seçiliyi Sil (Delete)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.delete || 'Delete'}</kbd></label>
            <label><span>Yeniden Adlandır (Rename)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.rename || 'F2'}</kbd></label>
          </div>
        `)}
        ${card('Navigasyon ve Test Kontrolleri', 'Pencere yönetimi ve Play Mode tuşları.', `
          <div class="binding-list">
            <label><span>Build Al (Paketle)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.build || 'Ctrl + B'}</kbd></label>
            <label><span>Play Mode (Önizleme)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.play || 'F5'}</kbd></label>
            <label><span>Tam Ekran (Fullscreen)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.fullscreen || 'F11'}</kbd></label>
            <label><span>Hızlı Arama (Find)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.find || 'Ctrl + F'}</kbd></label>
            <label><span>Proje Hub\\\'ı</span><kbd class="shortcut-key">${profile.shortcuts.bindings.hub || 'Ctrl + H'}</kbd></label>
            <label><span>Editör Ayarları (Preferences)</span><kbd class="shortcut-key">${profile.shortcuts.bindings.preferences || 'Ctrl + ,'}</kbd></label>
          </div>
        `)}
      </div>`,

    pipeline: () => `
      <div class="settings-columns">
        ${card('Varlık Önbelleği (Asset Cache)', 'Dönüştürülen dokular ve optimize edilmiş seslerin yerel önbelleği.', `
          ${field('pipeline.cachePath', 'Yerel Önbellek Klasörü', profile.pipeline.cachePath, 'text')}
          ${range('pipeline.maxCacheSizeGB', 'Maksimum Önbellek Boyutu', profile.pipeline.maxCacheSizeGB, 1, 50, 1, ' GB')}
          <div class="field-pair" style="margin-top: 10px;">
            <button type="button" class="text-button" id="btn-clear-cache" style="border: 1px solid #3c3944; border-radius: 6px; padding: 6px 12px; font-size: 11px;">Önbelleği Temizle</button>
            <span style="font-size: 10px; color: #8c8893; display: flex; align-items: center;">Mevcut kullanım: ~14.2 MB</span>
          </div>
        `)}
        ${card('İçe Aktarma ve Küçük Resimler (Thumbnails)', 'Asset Browser için görsel önizleme ayarları.', `
          ${toggle('pipeline.autoReimport', 'Değişen Dosyaları Otomatik İçe Aktar', 'Disk üzerinde varlık güncellendiğinde editörde anında yeniden işle.', profile.pipeline.autoReimport)}
          ${toggle('pipeline.compressThumbnails', 'Küçük Resimleri Sıkıştır', 'Thumbnail önbelleğinde RAM tasarrufu sağla.', profile.pipeline.compressThumbnails)}
          ${select('pipeline.thumbnailSize', 'Varsayılan Önizleme Çözünürlüğü', profile.pipeline.thumbnailSize, [
            ['64', '64 × 64 px (Düşük RAM)'],
            ['128', '128 × 128 px (Dengeli)'],
            ['256', '256 × 256 px (Yüksek Kalite)']
          ])}
        `)}
      </div>`,

    performance: () => `
      <div class="settings-columns">
        ${card('Editör Render Motoru ve FPS', 'Editörün GPU/CPU tüketimi ve donanım hızlandırma seçenekleri.', `
          ${select('performance.fpsCap', 'Editör Tuval FPS Sınırı', profile.performance.fpsCap, [
            ['30', '30 FPS (Pil Tasarrufu)'],
            ['60', '60 FPS (Önerilen)'],
            ['120', '120 FPS (Yüksek Yenileme)'],
            ['0', 'Sınırsız']
          ])}
          ${toggle('performance.vsyncEditor', 'Editör VSync (Dikey Senkronizasyon)', 'Editör arayüzünde ekran yırtılmalarını önle.', profile.performance.vsyncEditor)}
          ${toggle('performance.hardwareAccel', 'GPU Donanım Hızlandırma', 'SDL3 render yüzeyinde donanım hızlandırmasını aktif tut.', profile.performance.hardwareAccel)}
        `)}
        ${card('Günlükleme ve Hata Ayıklama (Logger)', 'C++ çekirdek ve Avalonia editör log filtreleme seviyeleri.', `
          ${select('performance.logLevel', 'Konsol Log Seviyesi', profile.performance.logLevel, [
            ['Verbose', 'Verbose (Tüm Çağrılar)'],
            ['Info', 'Info (Bilgilendirme)'],
            ['Warning', 'Warning (Uyarılar ve Hatalar)'],
            ['Error', 'Error (Yalnızca Kritik Hatalar)']
          ])}
          ${toggle('performance.showProfilerOnPlay', 'Play Mode Sırasında Profiler Aç', 'Önizleme başlatıldığında bellek ve kare süresi panelini göster.', profile.performance.showProfilerOnPlay)}
          ${toggle('performance.crashReport', 'Çökme Durumunda Hata Raporu Üret', 'Olası beklenmedik çökmede kaza günlüğü oluştur.', profile.performance.crashReport)}
        `)}
      </div>`,

    about: () => `
      <div class="settings-columns">
        ${card('Rowl Engine Editör Hakkında', 'Görsel Roman ve Etkileşimli Hikâye Editörü.', `
          <div style="padding: 10px 0; text-align: center;">
            <h2 style="font-size: 20px; font-weight: 700; color: #f2efe6; margin: 0 0 4px; letter-spacing: 1px;">ROWL ENGINE EDITOR</h2>
            <span style="font-size: 11px; color: #a8a49c;">Sürüm 1.0.0 (Preferences & Environment)</span>
          </div>
          <div class="info-note" style="margin-bottom: 12px;">
            <svg><use href="#i-tools"/></svg>
            <span>Yalnızca Makine-Yerel Editör Tercihleri (Unity Preferences Eşdeğeri)</span>
          </div>
          <p style="font-size: 11px; line-height: 1.6; color: #d8d4cc; margin: 0 0 10px;">
            Bu penceredeki tüm tercihler doğrudan yerel <code>EditorSettings.v1.json</code> dosyasına kaydedilir. Oyunun hikâye metni, çözünürlüğü, ses seviyeleri veya sahne başlangıçları gibi oyun içi ayarlar asla bu pencereye dahil edilmez.
          </p>
        `)}
        ${card('Editör Sistem Mimarisi', 'Makine ve ortam bileşenleri.', `
          <div class="binding-list">
            <label><span>Arayüz Çatısı</span><span class="spec-value">Avalonia 11 + .NET 10</span></label>
            <label><span>MVVM Mimarisi</span><span class="spec-value">CommunityToolkit.Mvvm</span></label>
            <label><span>Platform Host</span><span class="spec-value">Linux x86_64 · Zero-Copy Host</span></label>
            <label><span>Native Çekirdek</span><span class="spec-value">C++20 libRowlEngineCore</span></label>
            <label><span>Grafik Tuvali</span><span class="spec-value">SDL3 Hardware Surface</span></label>
            <label><span>Profil Dosyası</span><span class="spec-value">EditorSettingsProfile.cs</span></label>
          </div>
        `)}
      </div>`
  };

  function render() {
    const isGameQuery = searchText && GAME_SEARCH_TERMS.some(t => searchText.includes(t));
    const activeData = CATEGORIES.find(c => c.id === activeCategory) || CATEGORIES[0];

    dialog.innerHTML = `
      <form id="settings-form" class="settings-layout">
        <aside class="settings-sidebar">
          <div class="settings-sidebar-header">
            <span class="settings-badge">Rowl Engine</span>
            <h2>Ayarlar</h2>
          </div>
          <div class="settings-search-wrap" style="padding: 0 10px 10px;">
            <input type="search" id="settings-search-input" value="${searchText}" placeholder="Tercih ara (tema, ızgara, IDE)..." style="width: 100%; min-height: 30px; font-size: 11px; padding: 4px 8px; background: #121215; border: 1px solid #36343e; border-radius: 6px; color: #dcd6c5;">
          </div>
          <nav class="settings-nav" aria-label="Editör tercih kategorileri">
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
              <h3 id="settings-title">${activeData.title}</h3>
              <p class="settings-desc">${activeData.desc}</p>
            </div>
            <button type="button" class="icon-button settings-close-btn" data-dismiss aria-label="Ayarları kapat" title="Kapat">
              <svg><use href="#i-close"/></svg>
            </button>
          </header>

          <div class="settings-scroll">
            ${isGameQuery ? `
              <div class="info-note" style="border: 1px solid #baa88c; background: #23201d; padding: 16px; margin: 20px 0;">
                <svg style="color: #baa88c;"><use href="#i-tools"/></svg>
                <div>
                  <strong style="color: #f2efe6; display: block; font-size: 12px; margin-bottom: 4px;">Aradığınız öğe bir Oyun / Proje ayarıdır</strong>
                  <span style="font-size: 11px; color: #d8d4cc; line-height: 1.5;">Oyun çözünürlüğü, ses seviyeleri, kayıt slotları veya metin yazım hızı gibi oyun içi ayarlar <strong>Proje Ayarları</strong> (Project Settings) kapsamındadır. <strong>Editör Ayarları</strong> (Preferences) yalnızca editörün kendi çalışma ortamı, teması, tuvali ve donanım tercihlerini içerir.</span>
                  <div style="margin-top: 12px;">
                    <button type="button" class="primary-button" id="btn-jump-project-settings" style="font-size: 11px; padding: 6px 14px; background: #baa88c; color: #1b1a18; font-weight: 600;">Proje Ayarlarını Aç</button>
                  </div>
                </div>
              </div>
            ` : (sections[activeCategory] ? sections[activeCategory]() : sections.general())}
          </div>

          <footer class="settings-main-footer">
            <div style="margin-right: auto; display: flex; gap: 8px;">
              <button type="button" class="text-button" id="btn-export-prefs" style="font-size: 11px; color: #a8a49c;">Dışa Aktar</button>
              <button type="button" class="text-button" id="btn-reset-cat" style="font-size: 11px; color: #a8a49c;">Kategoriyi Sıfırla</button>
            </div>
            <button type="button" class="text-button" data-dismiss>Vazgeç</button>
            <button type="submit" class="primary-button">Tercihleri kaydet</button>
          </footer>
        </section>
      </form>
    `;

    bindFormEvents();
  }

  function bindFormEvents() {
    const jumpBtn = dialog.querySelector('#btn-jump-project-settings');
    if (jumpBtn) {
      jumpBtn.addEventListener('click', () => {
        dialog.close();
        if (context?.openProjectSettings) context.openProjectSettings();
      });
    }

    const searchInput = dialog.querySelector('#settings-search-input');
    if (searchInput) {
      searchInput.addEventListener('input', e => {
        searchText = e.target.value.toLowerCase().trim();
        render();
        const nextInput = dialog.querySelector('#settings-search-input');
        if (nextInput) {
          nextInput.focus();
          nextInput.selectionStart = nextInput.selectionEnd = nextInput.value.length;
        }
      });
    }

    dialog.querySelectorAll('[data-path]').forEach(el => {
      const path = el.dataset.path.split('.');
      el.addEventListener('change', () => {
        setNestedValue(profile, path, el.type === 'checkbox' ? el.checked : el.value);
        dirty = true;
        const dirtyEl = dialog.querySelector('[data-dirty]');
        if (dirtyEl) dirtyEl.textContent = 'Değişiklikler var';
      });
      el.addEventListener('input', () => {
        if (el.type === 'range') {
          const out = dialog.querySelector(`[data-out="${el.dataset.path}"]`);
          if (out) out.textContent = `${el.value}${el.dataset.suffix || ''}`;
        }
        setNestedValue(profile, path, el.type === 'checkbox' ? el.checked : (el.type === 'range' || el.type === 'number' ? Number(el.value) : el.value));
        dirty = true;
        const dirtyEl = dialog.querySelector('[data-dirty]');
        if (dirtyEl) dirtyEl.textContent = 'Değişiklikler var';
      });
    });

    const resetBtn = dialog.querySelector('#btn-reset-cat');
    if (resetBtn) {
      resetBtn.addEventListener('click', () => {
        if (DEFAULT_PROFILE[activeCategory]) {
          profile[activeCategory] = structuredClone(DEFAULT_PROFILE[activeCategory]);
          dirty = true;
          render();
        }
      });
    }

    const exportBtn = dialog.querySelector('#btn-export-prefs');
    if (exportBtn) {
      exportBtn.addEventListener('click', () => {
        const blob = new Blob([JSON.stringify(profile, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'rowl-editor-preferences.json';
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      });
    }
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
      const selected = dialog.querySelector('[aria-current=page]');
      if (selected) {
        selected.focus({ preventScroll: true });
        selected.scrollIntoView({ block: 'nearest', inline: 'center' });
      }
    }
  });

  dialog.addEventListener('submit', event => {
    event.preventDefault();
    saveProfile();
    dialog.close();
  });
  return {
    snapshot: () => structuredClone(profile),
    restore: data => { if (data && typeof data === 'object') { profile = { ...loadProfile(), ...data }; render(); } },
    open(catId) {
      context.beforeOpen();
      activeCategory = CATEGORIES.some(c => c.id === catId) ? catId : 'general';
      profile = loadProfile();
      dirty = false;
      searchText = '';
      render();
      dialog.showModal();
      const selected = dialog.querySelector('[aria-current=page]');
      if (selected) selected.scrollIntoView({ block: 'nearest', inline: 'center' });
    }
  };
}
