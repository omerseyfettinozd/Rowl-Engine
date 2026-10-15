export function toolContent(view) {
  const content = document.createElement('div');
  content.className = `tool-content ${view}-content`;
  const markup = {
    hierarchy: `<div class="tool-caption">SEÇİLİ NODE</div><strong class="hierarchy-scene"></strong><div class="tree-root">⌄ Sahne objeleri</div><div class="object-tree">${[['moon','Ay','i-moon'],['character','Mira','i-edit'],['dialogue','Diyalog','i-inspector']].map(([id,name,icon])=>`<button data-select-object="${id}" aria-pressed="false"><svg><use href="#${icon}"/></svg><span>${name}</span><span class="tree-status"></span></button>`).join('')}</div><p class="tool-hint">Bir objeyi seç. Özelliklerini Inspector’dan düzenle.</p>`,
    inspector: `<div class="inspector-node-properties"><div class="tool-caption">SEÇİLİ DÜĞÜM</div><label>Başlık<input data-scene-field="title" aria-label="Düğüm başlığı" maxlength="80"></label><label>Konuşmacı<input data-scene-field="speaker" aria-label="Konuşmacı" maxlength="60"></label><label>Diyalog<textarea data-scene-field="dialogue" aria-label="Düğüm diyaloğu" rows="3"></textarea></label></div><section class="inspector-library"><div class="tool-caption">DÜĞÜM KÜTÜPHANESİ</div><button class="author-button" data-library-star>☆ Kütüphaneye kaydet</button><button class="author-button" data-library-update hidden>Kaydı güncelle</button><p class="tool-hint" data-library-status>Tüm hiyerarşi, obje özellikleri ve bileşen ayarları kaydedilir.</p></section><div class="tool-caption">SEÇİLİ OBJE</div><strong class="inspector-name"></strong><div class="property-grid">${[['x','X'],['y','Y'],['w','Genişlik'],['h','Yükseklik']].map(([field,label])=>`<label>${label} <span>%</span><input type="number" min="0" max="100" step="0.5" data-property="${field}" aria-label="${label} yüzdesi"></label>`).join('')}</div><label class="check-row"><input type="checkbox" data-visible> Sahnede görünür</label><p class="tool-hint">Konum ve boyut sahneye göre yüzde olarak ölçülür.</p>`,
    assets: `<label class="asset-search">Varlık ara<input type="search" placeholder="İsimle filtrele…" aria-label="Varlık ara"></label><div class="asset-grid">${[['moon','Ay','i-moon','Görsel'],['character','Mira','i-edit','Karakter'],['dialogue','Diyalog','i-inspector','Arayüz']].map(([id,name,icon,type])=>`<button data-asset="${id}"><div class="asset-preview"><svg><use href="#${icon}"/></svg></div><strong>${name}</strong><span>${type}</span></button>`).join('')}</div><p class="tool-hint">Bir varlık seçerek Edit Scene’de düzenle.</p>`,
    store: `<div class="assets-coming"><svg aria-hidden="true"><use href="#i-assets"/></svg><h2>Yakında gelecek</h2><p>Assets Store</p></div>`,
    console: `<div class="console-toolbar"><span>Oturum olayları</span><button class="text-button" data-clear-log>Temizle</button></div><ol class="console-lines" aria-label="Oturum olayları"></ol>`,
    store: `<div class="assets-coming"><svg aria-hidden="true"><use href="#i-assets"/></svg><h2>Yakında gelecek</h2><p>Assets Store</p></div>`,
    hierarchyV2: `
      <div class="tool-caption">HİYERARŞİ VE SAHNE AKIŞI</div>
      <strong class="hierarchy-scene"></strong>
      <div class="tree-root">⌄ Aktif Düğümler ve Objeler</div>
      <div class="object-tree">
        ${[
          ['node_dialogue', 'Düğüm: Yol Ayrımı', 'i-node'],
          ['moon', 'Obje: Ay', 'i-moon'],
          ['character', 'Obje: Mira', 'i-edit'],
          ['dialogue', 'Obje: Diyalog Kutusu', 'i-inspector']
        ].map(([id, name, icon]) => `
          <button data-select-object="${id}" aria-pressed="false">
            <svg><use href="#${icon}"/></svg>
            <span>${name}</span>
            <span class="tree-status"></span>
          </button>
        `).join('')}
      </div>
      <p class="tool-hint">Bir düğüm veya sahne objesi seçerek özelliklerini Inspector üzerinden düzenleyin.</p>
    `,

    inspectorV2: `
      <div class="tool-caption">ÖZELLİKLER (INSPECTOR)</div>
      <strong class="inspector-name"></strong>

      <!-- Düğüm Davranış ve İçerik Bloğu (Diyalog/Sahne Seviyesi) -->
      <div class="inspector-node-section">
        <label class="setting-field" for="insp-speaker">Konuşmacı Karakter
          <select id="insp-speaker" data-node-prop="speaker">
            <option value="mira">Mira (Ana Karakter)</option>
            <option value="narrator">Anlatıcı</option>
            <option value="none">İsimsiz / Düşünce</option>
          </select>
        </label>

        <label class="setting-field" for="insp-expression">Karakter İfadesi
          <select id="insp-expression" data-node-prop="expression">
            <option value="calm">Sakin / Durgun</option>
            <option value="thoughtful">Düşünceli</option>
            <option value="surprised">Şaşkın</option>
          </select>
        </label>

        <label class="dialogue-property" for="insp-dialogue-text">Diyalog Metni (LocKey: ch1_mira_001)
          <textarea id="insp-dialogue-text" data-dialogue rows="3" placeholder="Replik metnini yazın…"></textarea>
        </label>

        <div class="override-group" style="margin-top:12px; padding:10px; background:#141418; border-radius:6px; border:1px solid #282830;">
          <span style="font-size:9px; letter-spacing:1px; color:#8e8a96; font-weight:600; text-transform:uppercase; display:block; margin-bottom:6px;">Düğüm Davranış Ezmeleri</span>
          
          <label class="check-row" style="min-height:28px; margin:4px 0;">
            <input type="checkbox" data-override-speed>
            <span style="font-size:11px;">Metin Hızını Ez</span>
          </label>
          <div class="field-pair speed-override-row" style="margin-bottom:8px;" hidden>
            <input type="number" min="5" max="120" value="45" data-node-speed placeholder="Hız (cps)">
            <span style="font-size:10px; color:#85828f; align-self:center;">cps (Varsayılan: 35)</span>
          </div>

          <label class="check-row" style="min-height:28px; margin:4px 0;">
            <input type="checkbox" data-override-delay>
            <span style="font-size:11px;">Satır Beklemesini Ez</span>
          </label>
          <div class="field-pair delay-override-row" style="margin-bottom:8px;" hidden>
            <input type="number" min="0.5" max="10" step="0.5" value="1.5" data-node-delay placeholder="Bekleme (sn)">
            <span style="font-size:10px; color:#85828f; align-self:center;">sn (Varsayılan: 2.0)</span>
          </div>

          <label class="check-row" style="min-height:28px; margin:4px 0;">
            <input type="checkbox" data-skippable checked>
            <span style="font-size:11px;">Oyuncu Tarafından Atlanabilir</span>
          </label>
        </div>
      </div>

      <!-- Sahne Obje Transform Bloğu -->
      <div class="inspector-transform-section" style="margin-top:14px; padding-top:12px; border-top:1px solid #282830;">
        <span style="font-size:9px; letter-spacing:1px; color:#8e8a96; font-weight:600; text-transform:uppercase; display:block; margin-bottom:8px;">Sahne Konum ve Boyut</span>
        <div class="property-grid">
          ${[['x', 'X'], ['y', 'Y'], ['w', 'Genişlik'], ['h', 'Yükseklik']].map(([field, label]) => `
            <label for="insp-${field}">${label} <span>%</span>
              <input id="insp-${field}" type="number" min="0" max="100" step="0.5" data-property="${field}" aria-label="${label} yüzdesi">
            </label>
          `).join('')}
        </div>
        <label class="check-row" style="margin-top:8px;">
          <input type="checkbox" data-visible> Sahnede görünür
        </label>
      </div>

      <p class="tool-hint">Diyalog hızı ve gecikmesi bu düğüm için özelleştirilebilir. Global ayarlar Proje Ayarları'nda saklanır.</p>
    `,

    assets: `
      <label class="asset-search" for="asset-search-input">Varlık Ara (res://)
        <input id="asset-search-input" type="search" placeholder="İsimle filtrele…" aria-label="Varlık ara">
      </label>
      <div class="asset-grid">
        ${[
          ['moon', 'Ay (moon.webp)', 'i-moon', 'res://sprites/'],
          ['character', 'Mira (mira.webp)', 'i-edit', 'res://characters/'],
          ['dialogue', 'UI Diyalog Kutusu', 'i-inspector', 'res://ui/'],
          ['bgm', 'Gece Ambiyansı (bgm.ogg)', 'i-audio', 'res://audio/']
        ].map(([id, name, icon, path]) => `
          <button data-asset="${id}">
            <div class="asset-preview"><svg><use href="#${icon}"/></svg></div>
            <strong>${name}</strong>
            <span>${path}</span>
          </button>
        `).join('')}
      </div>
      <p class="tool-hint">Varlıklar VFS sanal kökünden (res://) yüklenir.</p>
    `,

    assetsV2: `
      <label class="asset-search">Varlık ara (yer tutucu)</label>
    `,

    consoleV2: `
      <div class="console-toolbar">
        <span>Oturum ve Motor Günlüğü</span>
        <button class="text-button" data-clear-log>Temizle</button>
      </div>
      <ol class="console-lines" aria-label="Oturum olayları"></ol>
    `
  };

  content.innerHTML = markup[view];
  return content;
}

export function bindTools(panels, context) {
  const logs = [];
  const hierarchy = panels.get('hierarchy');
  const inspector = panels.get('inspector');
  const assets = panels.get('assets');
  const consolePanel = panels.get('console');

  function update() {
    const { scene, selected, names } = context.get();
    const isNode = selected === 'node_dialogue';
    const effectiveSelected = isNode ? 'dialogue' : selected;
    hierarchy.querySelector('.hierarchy-scene').textContent = scene.title;
    hierarchy.querySelectorAll('[data-select-object]').forEach(button => {
      const match = button.dataset.selectObject === selected;
      button.setAttribute('aria-pressed', String(match));
      const obj = scene.objects[button.dataset.selectObject];
      button.querySelector('.tree-status').textContent = obj && obj.visible === false ? 'gizli' : '';
    });

    inspector.querySelector('.inspector-name').textContent = names[selected] || 'Düğüm: ' + scene.title;

    // Transform alanlarını doldur
    for (const input of inspector.querySelectorAll('[data-property]')) {
      if (document.activeElement !== input) {
        const targetObj = scene.objects[effectiveSelected];
        input.value = targetObj ? Math.round(targetObj[input.dataset.property] * 100) / 100 : 0;
      }
    }

    const vis = inspector.querySelector('[data-visible]');
    if (vis) {
      const targetObj = scene.objects[effectiveSelected];
      vis.checked = targetObj ? targetObj.visible !== false : true;
    }

    const dialogueInput = inspector.querySelector('[data-dialogue]');
    if (dialogueInput && document.activeElement !== dialogueInput) {
      dialogueInput.value = scene.dialogue || '';
    }
  }

  // Override Toggle Dinamikleri
  const speedToggle = inspector.querySelector('[data-override-speed]');
  const speedRow = inspector.querySelector('.speed-override-row');
  if (speedToggle && speedRow) {
    speedToggle.addEventListener('change', () => {
      speedRow.hidden = !speedToggle.checked;
    });
  }

  const delayToggle = inspector.querySelector('[data-override-delay]');
  const delayRow = inspector.querySelector('.delay-override-row');
  if (delayToggle && delayRow) {
    delayToggle.addEventListener('change', () => {
      delayRow.hidden = !delayToggle.checked;
    });
  }

  hierarchy.addEventListener('click', event => {
    const button = event.target.closest('[data-select-object]');
    if (button) {
      const target = button.dataset.selectObject;
      if (target === 'node_dialogue') {
        context.select('dialogue');
      } else {
        context.select(target);
      }
      update();
    }
  });

  inspector.addEventListener('input', event => {
    if (event.target.matches('[data-property]') && event.target.value !== '') {
      context.edit(event.target.dataset.property, Number(event.target.value));
    } else if (event.target.matches('[data-dialogue]')) {
      context.dialogue(event.target.value);
    }
  });

  inspector.addEventListener('focusout', event => {
    if (event.target.matches('[data-property]')) {
      const current = context.get();
      const targetObj = current.scene.objects[current.selected] || current.scene.objects['dialogue'];
      if (targetObj) {
        event.target.value = Math.round(targetObj[event.target.dataset.property] * 100) / 100;
      }
    }
  });

  inspector.addEventListener('change', event => {
    if (event.target.matches('[data-property]')) {
      const field = event.target.dataset.property;
      context.edit(field, Number(event.target.value));
      const current = context.get();
      const targetObj = current.scene.objects[current.selected] || current.scene.objects['dialogue'];
      if (targetObj) {
        event.target.value = Math.round(targetObj[field] * 100) / 100;
      }
      update();
    } else if (event.target.matches('[data-visible]')) {
      context.visibility(event.target.checked);
    } else if (event.target.matches('[data-dialogue]')) {
      context.dialogue(event.target.value);
    }
  });

  assets.addEventListener('click', event => {
    const button = event.target.closest('[data-asset]');
    if (button) {
      const assetId = button.dataset.asset;
      if (assetId === 'bgm') {
        // Ses varlığı logla
        paintLogs();
      } else {
        context.select(assetId);
        context.openEdit();
      }
    }
  });

  const searchInput = assets.querySelector('input');
  if (searchInput) {
    searchInput.addEventListener('input', event => {
      const filter = event.target.value.toLocaleLowerCase('tr');
      assets.querySelectorAll('[data-asset]').forEach(button => {
        button.hidden = !button.textContent.toLocaleLowerCase('tr').includes(filter);
      });
    });
  }

  function paintLogs() {
    const list = consolePanel.querySelector('.console-lines');
    if (!list) return;
    list.replaceChildren();
    for (const log of logs) {
      const row = document.createElement('li');
      const time = document.createElement('time');
      const message = document.createElement('span');
      time.textContent = log.time;
      message.textContent = log.message;
      row.append(time, message);
      list.append(row);
    }
    list.scrollTop = list.scrollHeight;
  }

  const clearBtn = consolePanel.querySelector('[data-clear-log]');
  if (clearBtn) {
    clearBtn.addEventListener('click', () => {
      logs.length = 0;
      paintLogs();
    });
  }

  return {
    update,
    log(message) {
      logs.push({ time: new Date().toLocaleTimeString('tr-TR'), message });
      if (logs.length > 100) logs.shift();
      paintLogs();
    }
  };
}
