# C1 — Görsel-Dil Mock + Tasarım-Token Paketi (C1a, ~~ONAY BEKLİYOR~~ SUPERSEDED 2026-09-20)

> **SUPERSEDED:** Siyah + kemik-beyazı (`#0A0A0B` + `#F2EFE6`) yönü kararı (UI Sadeleştirme,
> KARAR-0) bu paketi geçersiz kıldı. Q1–Q4 soruları HÜKÜMSÜZDÜR; token önerileri uygulanmayacak.
> Belge tarihsel kayıt olarak korunur.
>
> **KARAR-0 KİLİDİ (2026-09-20 revizyonu — GEÇERLİ):** Siyah `#0A0A0B` +
> kemik-beyazı `#F2EFE6` geçerlidir (zeminler `#0A0A0B`/`#131315`/`#17171A`,
> metin `#F2EFE6`; vurgu renkli değildir). Q1–Q4 HÜKÜMSÜZDÜR; aşağıdaki §2
> token önerileri uygulanmayacak, §4 devir listesi §2'ye dayandığı ölçüde
> geçersizdir. SUPERSEDED statüsü sürer — bu belge yalnızca tarihsel kayıttır.

Faz C strangler'ının (C2 LivePreview + C3 MainWindow kabuğu + C4
lint/markup/validation) görsel-dil kilidi. Dayanak:
`docs/CREATIVE_STORY_STUDIO_DESIGN.md` (sıcak mürdüm + mercan `#F09A78`,
8 px düğme, 12 px kart, fiil-eylem dili). Bu paket YENİ renk icat etmez;
mevcut-uyumsuzlukları tespit edip KARAR sorularına dönüştürür. C1b
(katı-sunumsal uygulama) SADECE onaylanan token'ları uygular (`.cs` logic
diff'i kapıdan geçemez).

## 1. Mevcut-durum tespiti (2026-09-19)

| Yüzey | Dosya | Dil durumu |
|---|---|---|
| Ana kabuk | `editor/Views/MainWindow.axaml` (424) | Tema `DynamicResource` (`PrimaryText`, `PanelBackground`) — dille uyumlu iskelet; üst menü + 7 panel (Hierarchy/Inspector/Log/Assets/Backlog/SaveSlots/ProjectIssues) |
| LivePreview | `editor/Views/LivePreviewControl.axaml` (281) + `.axaml.cs` (1037) | SOĞUK palet: `#0B0F19/#121624` zemin, `#3B82F6` mavi seçim, `#00F0FF` cyan vurgu, `#A855F7` mor handle — tasarım-diliyle UYUMSUZ |
| Validation rozetleri | `editor/Controls/FieldValidationBadge` + C4 hedefleri | Durum-renkleri (⛔/⚠) — tasarım-dilinde "yalnızca durum anlamı için" kuralıyla uyumlu, ton-onayı gerekir |

## 2. Token tablosu (öneri — onaylanmadan token DEĞİLDİR)

| Token | Mevcut (LivePreview) | Önerilen (tasarım-dili) | Kapsar |
|---|---|---|---|
| `AccentPrimary` | `#3B82F6` mavi | `#F09A78` mercan | Seçim çerçevesi, birincil eylem |
| `AccentSecondary` | `#00F0FF` cyan | karar Q2 | Telemetri/VU vurgusu |
| `HandleGrab` | `#A855F7` mor | karar Q2 | Rotate/resize handle'ları |
| `SurfaceDeep` | `#0B0F19` | mürdüm-koyu (ton Q1) | Viewport zemini |
| `SurfaceCard` | `#121624/#161B2E` | mürdüm-kart (ton Q1) | Panel kartları |
| `RadiusCard` | — | 12 px | Kart yarıçapı |
| `RadiusButton` | — | 8 px | Düğme yarıçapı |

## 3. ASCII-mock'lar

### (a) LivePreview paneli — MEVCUT
```
+-- LivePreview (soğuk) ------------------+
| [viewport #0B0F19]                       |
|   +-- BackgroundBox (mavi çerçeve) --+  |
|   | [cyan handle]          [mor döndür]|  |
|   +-----------------------------------+  |
| VU: [cyan bar] [cyan bar]  0.005 eşik   |
+-----------------------------------------+
```

### (a') LivePreview paneli — ÖNERİLEN (onayda)
```
+-- Canlı Önizleme (sıcak) ----------------+
| [viewport mürdüm-koyu]                    |
|   +-- ArkaPlan (mercan çerçeve) ------+  |
|   | [ikincil handle]      [kulp]      |  |
|   +-----------------------------------+  |
| Ses: [mercan bar] [mercan bar]  0.005 eşik|
+-----------------------------------------+
```
Davranış aynen (C0 kilidi: eşik `>0.005f` değişmez; C2 pointer/resize
helper'lara + UI-thread boşaltma yapar, renkten başka piksel-değişmez).

### (b) Ana kabuk — KORUNUR (strangler sunumsal)
```
+-- Rowl Engine Editor --------------------+
| Pencereler | Projeler | ...               |
+----------+------------+----------+--------+
| Hierarchy|  merkez    | Inspector|        |
| (panel)  |  (graph /  | (panel)  | Log /  |
|          |  önizleme) |          | Assets |
+----------+------------+----------+--------+
```
Panel iskeleti değişmez; C3 (IDialogCoordinator + EngineHost enjeksiyonu +
ConnectEngineAsync worker'a) + C4 (lint rozetleri) token-dile uyar.

### (c) Validation-satırı — C4 hedefi (ton Q3)
```
[⛔ hata-kırmızısı] düğüm başlığı boş — başlık yaz
[⚠ uyarı-altını] izole düğüm — bağlantı ekle ya da sil
```

## 4. C1b devir listesi (onay SONRASI)

1. `LivePreviewControl.axaml`: 6 renk-token eşlemesi (tablo §2) — markup-only.
2. `MainWindow.axaml`: panel-yüzey token'ları (iskelet aynen) — markup-only.
3. C4 rozet tonları (Q3 kararına göre) — markup-only.
4. YASAK: `.cs` logic diff'i (C2/C3 işi ayrı dilimlerde, kilitli).

## 5. ONAY SORULARI (Chaple'e — K4 durağı)

- **Q1:** LivePreview soğuk-paleti (`#0B0F19` ailesi) sıcak-mürdüme taşınsın mı (öneri EVET)?
- **Q2:** Cyan (`#00F0FF`) + mor (`#A855F7`) handle/vurgu renkleri mercan-ailesine mi (öneri: ikincil = mercan-%60-soluk, handle = mercan), yoksa soğuk-vurgu korunup SADECE seçim-mercan mı olsun?
- **Q3:** C4 validation tonları (⛔/⚠) mevcut kalsın mı, yoksa tasarım-dilinin durum-renkleri setine mi kilitlensin?
- **Q4:** C1b kapsamı: (a)+(b)+(c) üçü birden mi, yoksa önce (a) LivePreview mu?
