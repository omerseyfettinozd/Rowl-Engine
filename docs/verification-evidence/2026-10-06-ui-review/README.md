# Görsel inceleme kanıtları — 6 Ekim 2026

Kaynak: `2bef579592634e9c4593f5ced953598bc1be4e3f`.
Yerel URL: `http://127.0.0.1:4173/`; Python statik sunucu,
`prototypes/editor-workspace/` içeriği. Kod değiştirilmeden tarayıcıda
örnek etkileşimler yapıldı. Görseller gerçek tarayıcı ekran yakalamalarıdır.

| Dosya | Viewport / durum |
| --- | --- |
| desktop-main-1440.jpg | 1440×900, Node/Game/Edit Scene |
| desktop-panels.jpg | 1440×900, altı panel; Game kapat/aç ve yerleşim sıfırlama sonrası |
| tablet-dense-768.jpg | 768×900, aynı yoğun düzen; yaklaşık 95 px Inspector/kütüphane |
| mobile-reopened-game.jpg | 390×844, Game yeniden açıldıktan sonra çalışma alanı aşağı kaydırılmış |
| settings-320.jpg | 320×740, Proje ayarları; yatay bölüm gezinmesi, sabit alt bant |

## Mobil karşı örnek

1. Başlangıç Node'a Game, Edit Scene, Lua, Inspector ve Düğüm kütüphanesi ekle.
2. 1440×900'den 390×844'e geç.
3. Üst kapsülden Game'i kapat ve yeniden aç.
4. Çalışma alanında Game'e ilerle: sahne küçük; panelin yüksekliği 2807 px.

DOM'dan ölçülen son yükseklikler (px, yuvarlatılmış): Node 428,35;
Lua 591,53; Edit Scene 1029,89; Inspector 358,61; kütüphane 358,61;
Game 2807. `#workspace.scrollHeight`: 5624.

390 px'deki yeniden açma öncesi metin ölçüleri: `.dialogue-text` 9,963 px;
`.continue-button` 6 px; Lua `.tool-caption` 7 px. Ölçüler tarayıcının
computed style ve bounding rect sonuçlarıdır; fiziksel cihaz kabulü değildir.

Animasyon süreleri kaynak incelemesinden alındı; FPS, dokunma gecikmesi,
azaltılmış hareket tercihi ve bütün panel permütasyonları ölçülmedi.
İnceleme sırasında projeye ait ayar/diyalog içeriği kaydedilmedi.
