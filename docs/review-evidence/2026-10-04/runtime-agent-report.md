# Rowl Engine runtime/state/ABI/Lua/VFS denetimi — 2026-10-04

Kapsam: 435f worktree güncel kaynak; salt okunur kaynak/test incelemesi; ana ajanın aynı kaynak için ürettiği temiz Release nesneleri ve libRowlEngineCore.so ile bağımsız /tmp probe'ları. Bu inceleme engine.cpp'nin tamamını ya da her testi satır satır okumaz; parser, state, sandbox, package ve yaşam döngüsü kritik yollarını hedefler. Ana CTest sonucu ana ajan tarafından birleştirilecek.

## Güncel bulgular

1. **P1 / Kritik — catch-and-respin Lua çağrısı 5 saniyelik bütçeyi aşarak engine thread'ini kilitliyor.**
   - Kaynak: `engine/src/scripting/lua_sandbox.cpp:183` (hook), `:210` (yakalanabilir lua_error), `:307` (süre aşıldığında deadline disarm), `:1215` (host pcall). `tests/test_lua_hardening.cpp:387` yalnız 66 kez hata yakalayıp kendisi sona eren script'i dener.
   - Yeni dinamik kanıt: `while true do pcall(function() while true do end end) end`. Güncel kaynak doğrudan Lua5.4 ve ana build'in kullandığı Lua5.5 ile derlendi; her ikisinde 10s timeout exit124. Aynı Release lib üzerinden Init=1, ExecuteScript 12s sonunda dönmedi; TERM+1s KILL exit137. /tmp/rowl-audit-lua-{respin.cpp,respin.log,respin55.log,capi.py,capi.log}.
   - Etki: authored ya da paketten gelen tek script oyun/editör engine thread'ini durdurabilir. Poison yalnız sonraki giriş kapısını kapatır; süren Lua pcall'i iptal etmez.
   - Çözüm: host tarafından yönetilen coroutine/yield bütçesi veya süreç izolasyonu; sonsuz catch probe ayrı process'te timeout ile release-blocking olmalı. Normal Lua error'u yeniden fırlatmak tek başına çözmez.
   - İlgili ek etki: `poisonSession` her trip'te Logger::error çağırır (`lua_sandbox.cpp:320`); C API probe 13s'de ~7.2MiB çıktı üretti. Freeze'e log/CPU yükü de eşlik ediyor. Aynı kök soruna dahildir.

2. **P2 / Önemli — yasal runtime geçmişi kayıt tavanını aşabiliyor, kullanıcı yeni kayıt oluşturamıyor.**
   - Kaynak: `engine/src/state/game_state.cpp:471` (500 girdiye kadar, girdi başına 64KiB'ye kadar kabul), `:570` (aktif durum + dört geçmiş durumun tam dialogue_history kopyası), `engine/include/rowl/state/game_state.hpp:80` (4 halka), `engine/src/state/session_persistence.cpp:65` (4MiB write gate), `game_state.cpp:588` (read gate).
   - Aynı Release object'leriyle /tmp/rowl-audit-state.cpp: 500 yasal dialogue_history girdisi ×2000 karakter, dört previousState => 5,361,414 bayt JSON; saveSlot=0; kendi JSON'unun decode'u=0.
   - Bu **sessiz bozuk kayıt ya da eski iyi kayıt kaybı değil**: bounded-write koruması doğru reddediyor. Sorun kabul edilen runtime veri bütçesinin save bütçesiyle uyuşmaması; uzun hikâyede kaydetme ergonomisi.
   - Çözüm: geçmişi/diyalogları normalize ederek tekrarları tekilleştirme veya yazılabilir byte bütçesine göre history truncation; root state korunmalı ve kullanıcıya tam tanı gösterilmeli. Ortalama metin hacmine dayanan uzun oturum kapısı eklenmeli.

3. **P2 / Önemli — rewind RAM zinciri toplam byte bütçesiyle sınırlandırılmıyor.**
   - Kaynak: `engine/src/state/game_state.cpp:339` previousState=current; `:475` her dialogue append geçmiş vektörünü kopyalar; `:500` zincirin özellikle kırpılmadığı belgelenir. `KNOWN_ISSUES.md:12` KI-07 kabul edilmiş sınırlama. `tests/test_rewind_history_budget_probe.cpp:63` yalnız 64 halka için vector capacity muhasebesi yapar, toplam yaşam süresi ve string heap'lerini sınırlandırmaz.
   - Kanıt: kaynak ve mevcut test yapısı; bu tur yeni RSS ölçümü yapılmadı. Kaynak yorumlarındaki 1200 advance/53,764,400B eski ölçüm bu tur doğrulanmış sayı olarak sunulmamalı. Asimptotik davranış güncel kodda açık.
   - Etki: oyun süresi ve metin/variable hacmi arttıkça RAM büyür; 500 satırlık backlog tavanı toplam history tavanı değildir. shrink_to_fit yalnız boşa ayrılan kapasiteyi azaltır.
   - Çözüm: persistent deque/delta checkpoint veya disk destekli rewind; public root'a dönme sözleşmesi korunacaksa budama tek başına uygun olmaz. Gerçek byte ve oturum süresi bütçesi ölçülmeli.

4. **P2 / Önemli — Lua koşulları mevcut global ve stdlib değerlerini değiştirebiliyor.**
   - Kaynak: `engine/include/rowl/scripting/lua_sandbox.hpp:49` açıkça pre-existing value-clobber kapsam dışı; `engine/src/scripting/lua_sandbox.cpp:1066` map snapshot, `:1072` ad-kümesi guard; `:1138` yalnız bridge rebind. Tam repairGlobals condition yolunda yok.
   - Aynı Release C ABI probe: önce `x=10`, sonra `(function() x=20; math.abs=nil; return true end)()` evaluateCondition=1; sonraki `x==20`=1 ve `math.abs==nil`=1. /tmp/rowl-audit-capi-diagnostics.py/.log.
   - Bu kayıtlı tasarım sınırı; eski _G metatable kaçışı olarak sunulmamalı (o kapanmış). Yine de koşul önizleme/tekrar değerlendirme sırası sonucu değiştirebilir, sonraki koşul bir stdlib fonksiyonunu kaybedebilir.
   - Çözüm: salt okunur condition environment; gerekiyorsa mevcut scalar/table mutations için açık purity sözleşmesi. Script linter/author diagnostic de bu sınırı görünür kılmalı.

5. **P2 / İkincil — checked fade API başarı sonucuyla last-result kanalı farklı sonuç gösteriyor.**
   - Kaynak: `engine/src/c_api_thread_contract_guard.cpp:56` başarı setSuccess damgası yok; `:89` getter başarısı da damga yazmıyor. RuntimeContext lastResult `engine/src/core/runtime_context.cpp:58` ile güncellenebiliyor.
   - Aynı Release C ABI probe: SetFadeCurveChecked(9) => return2,last2; ardından SetFadeCurveChecked(1) => return0,last2. /tmp/rowl-audit-capi-diagnostics.log.
   - Etki: doğrudan checked dönüşünü kullanan host doğru sonucu görür; global tanı paneli önceki hatayı güncel işlem hatası sanabilir. Baseline'ın strict Checked coverage/diagnosis eksikliğiyle uyumlu.
   - Çözüm: başarıda operation/target ile setSuccess veya last-result kanalının yalnız sticky-error sözleşmesi olduğunu başlıkta tutarlı biçimde tanımlamak; mevcut API ailelerinde politika birleştirilmeli.

6. **P2 / Kayıtlı ürün sınırı — atomic save güç/OS kaybına kalıcılık sağlamıyor.**
   - Kaynak: `engine/src/state/save_durability.cpp:34` fsync OFF açık karar; `:39` POSIX fsync/fdatasync/dir fsync ve Windows FlushFileBuffers kapsam dışı.
   - Kanıt: kaynak/sözleşme; bu tur güç-kaybı deneyi yapılmadı. Temp+rename, eşzamanlı slot writer kilidi ve retry gerçek güçlü yanlar; süreç-crash atomicity güç-kaybı durability ile aynı değildir.
   - Etki: güç kesilmesinden hemen önce başarılı görünen kayıt kalıcı storage'a ulaşmamış olabilir.
   - Çözüm: seçilebilir durable-save düzeyi ve fsync/dir-sync/FlushFileBuffers; maliyet hedef cihazda ölçülmeli. Ürün iddiası mevcut L1/L2 sınırını açık tutmalı.

7. **P2 / Ürün sınırı — raw package openStream girdinin tamamını RAM'e alıyor.**
   - Kaynak: `engine/src/vfs/rowlpkg_reader.cpp:761` readEntry ardından string/istringstream; `:27` 128MiB/girdi tavanı. Zstd kolu :739 bounded decoder kullanıyor.
   - Kanıt: kod yolunun doğrudan incelemesi; bu tur yeni allocation peak ölçümü yapılmadı. raw vektör -> string inşa edildiğinde geçici iki tam payload tutma ihtimali vardır (~256MiB, azami tek 128MiB asset'te).
   - Etki: openStream adı tüm biçimlerde sabit-memory stream anlamına gelmiyor; sıkıştırılmamış büyük ses/görsel aynı açılışta materialize oluyor.
   - Çözüm: file offset/length ile bounded raw range stream, hash + erişim politikasını koruyarak. Zstd streaming optimizasyonu mevcut ve değerli.

8. **P3 / İkincil — handle generation sayaç genişliği token ile uyuşmuyor.**
   - Kaynak: `engine/src/c_api_lifecycle.cpp:31` token düşük32 generation; `:37` counter uint64; `:55` record64 ile decoded32 eşitliği aranıyor.
   - Kanıt: statik deterministik aritmetik; 2^32 oluşturma deneyi yapılmadı. 2^32 sonrasında yeni handle'lar generation mismatch nedeniyle geçersizleşir; 2^64 sarma yorumu gerçek token kapasitesini yansıtmıyor.
   - Pratik normal kullanım etkisi düşük; generational pool eski tombstone retention problemini gerçekten kapatıyor.
   - Çözüm: 32-bit nesil sınırını tutarlı işletmek ve rollover'da slotu emekliye ayırmak; kontrollü test setter ile sınır testi. Desteklenen 64-bit pointer hedefini de compile-time guard ile pinlemek yararlı.

## Güçlü yanlar ve kapanmış sorunlar

- StoryGraphParser ve StoryRuntime commit ayrımı, graph validation, duplicate/missing edge checks; geçersiz parse aktif graph'ı değiştirmiyor. Transactional temel olgun.
- Save v4 bounded read/write, 128 nesting pre-scan, variable/history/type validation; migration sonucu ve graph kimliği; aktif durum fail-closed.
- Save temporary isimleri writer başına benzersiz; Windows transient retry ve slot commit mutex; temp/backup cleanup. Eski concurrent-writer sorununu yeniden açık diye yazmamak gerekir.
- GameState değişmeyen variableMap'i paylaşabiliyor; mixer volume restore kapsamı korunuyor; dört history halkası reload sonrası sınırlı rewind sağlar.
- C ABI opaque generational handles: peak-live kadar slot havuzu; shared_ptr kopyası in-flight erişimi alive tutar; owner-thread ayrımı, process-wide SDL video serial ve reclaim yolu.
- Utf8 caller buffers NUL dahil size query ve BUFFER_TOO_SMALL sözleşmesini merkezileştirir; managed borrowed-string riskini azaltır.
- Lua io/os/debug/package/load/dofile/loadfile erişimi kapalı; instruction/memory/source/module/variable-map bütçeleri mevcut. Modül bridge pin/quarantine, recovery reserve ve _G metatable temizleme eski birden fazla gerçek kusuru kapatmış.
- Paket traversal, duplicate canonical path, overlapping ranges, count/size/ratio checks; raw VE zstd manifest SHA256 bağı ve bounded zstd stream. Eski raw digest eksikliği ve ikinci tam decode sorunu kapanmış.
- Paket imzasız; malicious manifest rewriting tehdidi docs/PACKAGE_THREAT_MODEL.md'de açıkça kapsam dışında. Bu audit imzasızlığı accidental corruption denetim başarısızlığı gibi puanlamıyor. Legacy/malformed manifest'in warn-open sınırı korunuyor.

## Baseline ile fark

`docs/PRODUCTIZATION_BASELINE.md:27` save/load/rewind Ready sınıflaması küçük/normal regresyon fixture'leri için desteklenir; toplam RAM ve yasal-büyük-state save ergonomisi açısından tam ürün güvence değildir. `:42` C ABI Partial sınıflaması doğru. Lua denial-of-service başarısızlığından dolayı “açık P0/P1 yok” KNOWN_ISSUES üst cümlesi bu bağımsız probe sonucundan sonra güncel kabul edilmemeli. Docs'un eski feature inventory'si güncel kod/sonraki contract'lar ile eşlenmeli; örneğin package streaming, layers ve rich-text/shaping için eski yok ifadeleri mevcut son kodu anlatmıyor.

## Puan (runtime katmanı; GUI/platform kanıtı hariç)

| Alan | /10 | Gerekçe |
|---|---:|---|
| Graph/runtime transaction | 8.0 | Güçlü ayrım ve regresyon temeli; bu audit tam navigation e2e taraması değil |
| Save/load ve veri güvenliği | 7.2 | Atomic bounded writes güçlü; save byte budgets, history RAM ve güç-kaybı sınırları |
| C ABI/lifecycle/diagnostics | 7.5 | Ownership/thread guards ve caller buffers iyi; tanı politikası tam birleşmemiş |
| Lua güvenilirlik/izolasyon | 4.5 | Birçok guard mevcut fakat doğrulanmış engine-thread freeze P1 |
| VFS/package reliability | 8.0 | Format/path/hash savunması ve zstd stream güçlü; raw materialization sınırı |
| **Bu denetim kapsamı genel** | **7.0** | İyi mühendislik temeli; script freeze kapanmadan güvenilir üretim runtime'ı kabulü uygun değil |

Puan mutlak ürün/yetenek sayısı puanı değildir; incelenen katmanın kaynak ve çalıştırılmış probe kanıtına göre mühendislik olgunluğudur. Ana rapor player/editor/render/platform skoruyla ayrıca birleştirmeli.
