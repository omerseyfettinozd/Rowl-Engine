/**
 * test_audio_pump_race_red_probe.cpp — D04 pump/stream-ring RED kilit probu.
 *
 * Bağımsız ikili: rowl_audio_pump_race_red_probe
 * (ctest -R audio_pump_race_red_probe, TIMEOUT 120).
 * D03 konvansiyonu: header + test-köprüsü YOK, doğrudan AudioEngine
 * (VFSManager + remountProject, test_audio_lock.cpp deseni) + dummy driver
 * (SDL_AUDIODRIVER=dummy, SDL_VIDEODRIVER=dummy). Linkaj D03 gibidir
 * (rowl_engine_objects: tek kopya), çünkü AudioEngine üye-sembolleri
 * paylaşılan RowlEngineCore'dan ihraç edilmez. rowl_tests gövdesine
 * gömülmez ki kırmızı-yeşil döngüsü tüm süiti koşmadan kanıtlansın.
 *
 * Bacak 1 — pump hammer vs okuyucu hammer (TSan altında KIRMIZI verir):
 *   thread-A: pumpBgmStream() döngüsü (yazım: audio_engine.cpp pump
 *     gövdesi — m_bgmRing[], m_bgmRingWriteFrames, m_bgmStreamPcmPos,
 *     m_bgmStreamEos, recordPumpSample m_pumpCount/pencere).
 *   thread-B: bgmStreamBufferedSeconds() + bgmPumpStatsJson() + update()
 *     döngüsü (okuma: m_bgmStreamPcmPos, m_pumpCount/pencere; update içindeki
 *     ikinci pump + updateTelemetry ring-okuması da yazar/okur).
 *   Üretimde pump bugün tek update-thread'den sürülür (prefetch-thread
 *   YOKTUR — K1); hammer, D13/D18 thread'lerinin göreceği paylaşımı
 *   bugünden kurar (kopyala-bırak pump deseninin kilitli hali).
 *   TSan KIRMIZI: 'WARNING: ThreadSanitizer: data race' + pump/okuyucu
 *   satırları (çıkış 66). Eşzamanlı vorbis-kaynak çöküşü de KIRMIZI
 *   sayılır (korumasız paylaşımın kanıtıdır; fix sonrası serileşir).
 *
 * Bacak 2 — fonksiyonel parite çipası (TSan'sız YEŞİL, fix-sonrası
 * TSan-YEŞİL'de birebir aynı kalmalı):
 *   kurulum intact (isStreaming + buffered>0 + kaynak açık), pump sayacı
 *   ilerler, stats JSON şeması (count/last_us/avg_us/max_us), suspend
 *   altında pump no-op'tur (sayaç + buffered donar — K2), close sonrası
 *   pump güvenli no-op'tur (kilit/isStreaming düşer, çöküş yok).
 *
 * Bacak 3 — perf-floor kaydı (S kapısı): tek-thread N pump çağrısının
 *   count/avg/max'ı stdout'a yazılır (aynı-cihaz karşılaştırma zemini;
 *   kilit, tabanı oynatmamalıdır).
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: TSan'lı ctest exit 66 + race warning;
 * TSan'sız exit 0. Fix (m_streamMutex kilitli pump/stream-ring; setter ->
 * applyChannelGains zinciri kilitsiz kalır, D03 atomikleri aynen) sonrası
 * TSan exit 0, Bacak-2/3 değişmez. Kırmızıda commit YOK, max 3 fix.
 */
#include "rowl_test_harness.hpp"

#include <thread>

namespace {

// D04 streaming fixture: miss-guard uzun-tonu (test_audio_lock.cpp'daki
// kMissGuardLongToneOggBase64 ile bayt-baytı aynı; derleme-sırasında
// doldurulur — splice betiği karşılaştırır) + son-sayfa granule 1e9
// (header probe + ov_pcm_total over-threshold raporlar).
const char* kPumpBgmOggBase64 = "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4AAAEGCDpsTiAIWGrAQAQgIAHIpiWZrmeZ4niqapmiRJ0zxPFEXRNE1TVUmSpnmeKIqiaZqmqrIsTfM8URRF01RVVYWmeZ4oiqJpqqrqwvM8TxRF0TRV1XXheZ4niqJomqrquhBFUTRN01RNVXVdIIqmaZqqqqquC0RPFE1TVV3XdYHniaJpqqqrui4QTdNUVVV1XVkGmKZpqqrryjJAVVXVdV1XlgGqqqqu67qyDFBV13VdWZZlAK7rurIsywIAAA4cAAACjKCTjCqLsNGECw9AoSErAoAoAADAGKYUU8owJiGkEBrGJIQUQiYlpdJSqiCkUlIpFYRUSiolo5RSailVEFIpqZQKQiollVIAANiBAwDYgYVQaMhKACAPAIAwRinGGHNOIqQUY845JxFSijHnnJNKMeacc85JKRlzzDnnpJTOOeecc1JK5pxzzjkppXPOOeeclFJK55xzTkopJYTOQSellNI555wTAABU4AAAEGCjyOYEI0GFhqwEAFIBAAyOY1ma5nmiaJqWJGma53meKJqmJkma5nmeJ4qqyfM8TxRF0TRVled5niiKommqKtcVRdM0TVVVXbIsiqZpmqrqujBN01RV13VdmKZpqqrrui5sW1VV1XVlGbatqqrqurIMXNd1ZdmWgSy7ruzasgAA8AQHAKACG1ZHOCkaCyw0ZCUAkAEAQBiDkEIIIWUQQgohhJRSCAkAABhwAAAIMKEMFBqyEgBIBQAAjLHWWmuttdZAZ6211lprrYDMWmuttdZaa6211lprrbXWUmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaay2llFJKKaWUUkoppZRSSimllFJKBQD6VTgA+D/YsDrCSdFYYKEhKwGAcAAAwBilGHMMQimlVAgx5px0VFqLsUKIMeckpNRabMVzzkEoIZXWYiyecw5CKSnFVmNRKYRSUkottliLSqGjklJKrdVYjDGppNZai63GYoxJKbTUWosxFiNsTam12GqrsRhjayottBhjjMUIX2RsLabaag3GCCNbLC3VWmswxhjdW4ultpqLMT742lIsMdZcAAB3gwMARIKNM6wknRWOBhcashIACAkAIBBSijHGGHPOOeekUow55pxzDkIIoVSKMcaccw5CCCGUjDHmnHMQQgghhFJKxpxzEEIIIYSQUuqccxBCCCGEEEopnXMOQgghhBBCKaWDEEIIIYQQSiilpBRCCCGEEEIIqaSUQgghhFJCKCGVlFIIIYQQQiklpJRSCiGEUkIIoYSUUkophRBCCKWUklJKKaUSSgklhBJSKSmlFEoIIZRSSkoppVRKCaGEEkopJaWUUkohhBBKKQUAABw4AAAEGEEnGVUWYaMJFx6AQkNWAgBkAACQopRSKS1FgiKlGKQYS0YVc1BaiqhyDFLNqVLOIOYklogxhJSTVDLmFEIMQuocdUwpBi2VGELGGKTYckuhcw4AAABBAICAkAAAAwQFMwDA4ADhcxB0AgRHGwCAIERmiETDQnB4UAkQEVMBQGKCQi4AVFhcpF1cQJcBLujirgMhBCEIQSwOoIAEHJxwwxNveMINTtApKnUgAAAAAAANAPAAAJBcABER0cxhZGhscHR4fICEiIyQCAAAAAAAGQB8AAAkJUBERDRzGBkaGxwdHh8gISIjJAEAgAACAAAAACCAAAQEBAAAAAAAAgAAAAQET2dnUwAAQK4AAAAAAACJoyILAgAAAEv56HAtJ1k7OTg4Ojk5OTo4Nzs4OTk5OTk6PTk7OTo6PDk5OTk5ODo4Ozk5OTs5ODo4TNsrXau7aXula3W3TtStBRUIAAAgYhX7/DWPHz9+HB+NRqPRaDQKOqg9B6/SnltcBTYwqD0Hr9KeW1wFNvCC7fV6AQAgKAAAAAAAAAAAAAAAAAAAgKjFQVDDbnN0tDlKZT0AMDHTmJtozHV6nVaj1eg1PXv07NHtdDvdpm3aVAB+uL1YFynzqDAxdvyZ4HB7sS5S5lFhYuz4MwEsAQAAAAAAAAEAAAAAAAAAAACgrBQAoGpMNRoTAQBABn64vVhXKfMWZcLO/Q0cbi/WVcq8RZmwc38DsAQAAAAAAAAAAAAAAAAAAAAAQDSQAUAIM63QmQIAAH64vVhXKfNWbcLO/QkOtxfrKmXeqk3YuT8BLAEAAAAAAAAAAAAAAAAAAAAAUMkqAOikQaeREgAAfri92Fep8xZtwsmtCQ63F/sqdd6iTTi5NQEsAQAAAAAAAAAAAAAAAAAAAACgQQlAYmkw1ZsAAAB+uL1YVynziDIxTu5v4HB7sa5S5hFlYpzc3wAsAQAAAAAAAAAAAAAAAAAAAABQLQsA0KpmwkwrAAAAfri9WFcp8xZhwo4/ExxuL9ZVyrxFmLDjzwSwBAAAAAAAAAAAAAAAAAAAAABANhABQEgTU0udOQAAfrjd2jep61Fpwo4/Gzjcbu2b1PWoNGHHnw3AEgAAAAAAAAAAAAAAAAAAAAAARU0JAIpOqzcxkwAAfri92Fep8xZhwok/GzjcXuyr1HmLMOHEnw3AEgAAAAAAAAAAAAAAAAAAAAAARbUEAEUvTXWmEgAAfri9WFcp84gyMU782cDh9mJdpcwjysQ48WcDsAQAAAAAAAAAAAAAAAAAAAAAQDYYCUBKU1UjTAAAAH643Vo3KfMRZcKOPxMcbrfWTcp8RJmw488EsAQAAAAAAAAAAAAAAAAAAAAAQDUqAKBVsDBVBAAAfri9WBcp84gyMU7uT3C4vVgXKfOIMjFO7k8ASwAAAAAAAAAAAAAAAAAAAAAAaEgAEFigxQgAAH64vVhXKXOpNjF37m/gcHuxrlLmUm1i7tzfACwBAAAAAAAAAQAAAAAAAAAAAKASVQDQCGFiokgAAAIAfrjdWjcp8xFpwsn9DRxut9ZNynxEmnByfwOwBAAAAAAAAAAAAAAAAAAAAABANJgJQApToWACAAB+uL3YN6nzFmnCjD8bONxe7JvUeYs0YcafDcASAAAAAAAAAAAAAAAAAAAAAABltQAAVSv0WlMBAAB+uL3YV6nzFmHCjj8bONxe7KvUeYswYcefDcASAAAAAAAAAAAAAAAAAAAAAABlTQEAqk6nNTUTAAB+uN1aNynzUWnCzv0DHG631k3KfFSasHP/ALAEAAAAAAAAAAAAAAAAAAAAAEDUygAghMGgMZoDAAB+uL1YVynzFmnCzv0NHG4v1lXKvEWasHN/A7AEAAAAAAAAAAAAAAAAAAAAAEClLAFAo5pLc70EAAB+uL2oVyn9iDIwTm5t4HB7Ua9S+hFlYJzc2gAsAQAAAAAAAAAAAAAAAAAAAACgYQlAYmmqNdEBAAB+uL1YVynzFm3Czv0JDrcX6ypl3qJN2Lk/ASwCAAAAAAAAAAAAAAAAAAAAAEA1KwBAA700mqhSAAAAfri9WFcp84gyMU782cDh9mJdpcwjysQ48WcDsAgAAAAAAEAAAAAAAAAAAAAAANlABADICGmmUfSmAAAAGX64vVhXKfMWZcKOPxs43F6sq5R5izJhx58NwBIAAAAAAAAAAAAAAAAAAAAAAEWlBABFY6LoTSQAAH643do3qWsXYWLs+LOBw+3WvklduwgTY8efDcASAAAAAABAAAAAAAAAAAAAAABFPQFAMZrrLS0BAAAMfri92Fep8xZpwsn9DRxuL/ZV6rxFmnByfwOwBAAAAAAAAAAAAAAAAAAAAABA1gMAKY2WliaWAAAAfri9WFcp84g0MU7ub+Bwe7GuUuYRaWKc3N8ALAEAAAAAAAAAAAAAAAAAAAAAUFYKANBqLI0GgwAAAH643Vo3KXNXbWLs3NrA4XZr3aTMXbWJsXNrA7AEAAAAAAAAAAAAAAAAAAAAAEA0lAFACAujqZkRAAB+uL1YVynziDYxdu5XcLi9WFcp84g2MXbuVwCLAAAAAAAABAEAAAAAAAAAAACgRhUA2KDDqEokAADgyAB+uL1YNynzFmXCzP0JDrcX6yZl3qJMmLk/ASwBAAAAAAAAAAAAAAAAAAAAABANZwKQwtLcYGEAAAB+uN1aNynzEWXCzv0NHG631k3KfESZsHN/A7AEAAAAAAAAAAAAAAAAAAAAAEC1LABAq7WwNNELAAB+uL1YVynzVmHCjj8bONxerKuUeaswYcefDcASAAAAAAAAAAAAAAAAAAAAAABZKwIA0mApzcwBAAB+uL3YV6nzFmnCjj8bONxe7KvUeYs0YcefDcASAAAAAAAAAAAAAAAAAAAAAABFTQkAis5UsbAAAAB+uN1aNynzUWHCjj8THG631k3KfFSYsOPPBLAEAAAAAAAAAAAAAAAAAAAAAEBRLQFA0ZsYtaYSAAB+uL1YVynzFmXCyf0NHG4v1lXKvEWZcHJ/A7AEAAAAAAAAAAAAAAAAAAAAAEA2GAlASnMFrQkAAH64vVhXKfOINjFO7k9wuL1YVynziDYxTu5PAEsAAAAAAAABAAAAAAAAAAAAAFSjAgB6YZRaRQAAAAF+uL3YV6nzFm3Czq0JDrcX+yp13qJN2Lk1ASwBAAAAAAAAAAAAAAAAAAAAAKABAUBgoTfRmAIAAH64vVgXKfOINjHu3D/A4fZiXaTMI9rEuHP/ALAEAAAAAAAQAAAAAAAAAAAAAEClKAFAo5hrzTUSAABwfri9WFcp8xZhwo4/ExxuL9ZVyrxFmLDjzwSwBAAAAAAAAAAAAAAAAAAAAABANJgJQApTg4nWDAAAfrjd2jep6xFpwo4/Gzjcbu2b1PWINGHHnw3AEgAAAAAAAAAAAAAAAAAAAAAAZb0AAFWvMTGaCwAAfrjd2jep6xFhwok/Gzjcbu2b1PWIMOHEnw3AEgAAAAAAAAAAAAAAAAAAAAAAZaUAAFWnmOlNBAAAfri92Fep84gyMXbub+Bwe7GvUucRZWLs3N8ALAEAAAAAAAQAAAAAAAAAAAAAELUyAAhhojHDEgAAwAB+uN1aNynzUWXCjj8THG631k3KfFSZsOPPBLAEAAAAAAAAAAAAAAAAAAAAAEClrAKARsXMTCsBAAB+uN3aN6nrEWXCzv0KDrdb+yZ1PaJM2LlfASwBAAAAAAAAAAAAAAAAAAAAAEgNSwASC1UjTAAAAH64vVhXKfOINjFO7m/gcHuxrlLmEW1inNzfACwBAAAAAAAEAAAAAAAAAAAAAFDNCgBopdSaqgIAAAh+uL3YV6nzFmnCzv0JDrcX+yp13iJN2Lk/ASwBAAAAAAAAAAAAAAAAAAAAAJANRAAQ0gRL1RwAAE9nZ1MABIhYAQAAAAAAiaMiCwMAAAB1b2AoKzk5OTk7ODk5OTk5ODc4OTs4Ozo5ODg5OTk5ODg6ODs4PTk6Nzo5OTk5Spl+uN1aNynzUWnCzv0DHG631k3KfFSasHP/ALAEAAAAAAAAAAAAAAAAAAAAAEBRKQFA0SgmqpkEAAB+uL3YV6nzFmHCjj8bONxe7KvUeYswYcefDcASAAAAAAAAAAAAAAAAAAAAAABFvQQARW9UzMwlAAB+uN1aNynzUWnCzv0DHG631k3KfFSasHP/ALAEAAAAAAAAAAAAAAAAAAAAAEDWjgQgpdFUNTEDAAB+uL1YVynzFmnCyf0NHG4v1lXKvEWacHJ/A7AEAAAAAAAAAAAAAAAAAAAAAEC1KABAq5hpTHQCAAB+uL2oFyn9iDIw7tzawOH2ol6k9CPKwLhzawOwBAAAAAAAEAAAAAAAAAAAAACAhgQAgYWZxlwPAABgAH64vVhXKfMWZcKOPxUcbi/WVcq8RZmw408FsAQAAAAAAAAAAAAAAAAAAAAAQEUVADTCYKEICQAAfrjdWjcp8xFlws79CQ63W+smZT6iTNi5PwEsAQAAAAAAAAAAAAAAAAAAAAAQDWcCkMJSa2EwAQAAfri9WFcp8xZlwok/GzjcXqyrlHmLMuHEnw3AEgAAAAAAAAAAAAAAAAAAAAAAZbUAAFVrjs4oAAAAfrjd2jep6xFhwo4/Gzjcbu2b1PWIMGHHnw3AEgAAAAAAAAAAAAAAAAAAAAAAZU0AgGqwMDGzAAAAfrjd2jep6xFpwsn9DRxut/ZN6npEmnByfwOwBAAAAAAAAAAAAAAAAAAAAABA1CQAQjFYmJlaAAAAfri9WFcp8xZhwo4/ExxuL9ZVyrxFmLDjzwSwBAAAAAAAAAAAAAAAAAAAAABAUS0BQNFaaIxGCQAAfri9qDcp/RZpwMz9DRxuL+pNSr9FGjBzfwOwBAAAAAAAAAAAAAAAAAAAAABANhwJQEpLHUYdAAB+uN1aNynzEW3Czv0JDrdb6yZlPqJN2Lk/ASwBAAAAAAAAAAAAAAAAAAAAAFBVAQA9RoOKAAAAfri9WFcp81Zlwsn9CQ63F+sqZd6qTDi5PwEsAQAAAAAAAAAAAAAAAAAAAACQDQkAAgsLo4URAAB+uL1YVynzFmXCzv0NHG4v1lXKvEWZsHN/A7AEAAAAAAAAAAAAAAAAAAAAAEClKAFAo1iamOokAAB+uN1aVylzF2FinPizgcPt1rpKmbsIE+PEnw3AEgAAAAAAQAAAAAAAAAAAAAAAUTsTgBRGM8zNAAAAAn64vdhXqfMWacKOPxs43F7sq9R5izRhx58NwBIAAAAAAAAAAAAAAAAAAAAAAGW9AABVb8DcHAAAfrjdWjcpcxdhYpz4s4HD7da6SZm7CBPjxJ8NwBIAAAAAAEAAAAAAAAAAAAAAAGWlAABVo9UpJgIAAHB+uL1YVynzFmXCjj8bONxerKuUeYsyYcefDcAiAAAAAAAAAAAAAAAAAAAAAABEAxkAICOEmUQxBQAAfri9qFcp/RZlwM79CQ63F/Uqpd+iDNi5PwEsAgAAAAAAAAAAAAAAAAAAAABAJasAwAadtNCiSgAAfri92Fep8xZlws79Cg63F/sqdd6iTNi5XwEsAQAAAAAAAAAAAAAAAAAAAABIDUsAEguNRjUBAAB+uN1aNynzEW3Czv0NHG631k3KfESbsHN/A7AEAAAAAAAAAAAAAAAAAAAAAEC1rACAVkVnrhUAAH64vVg3KfNmacLJ/Q0cbi/WTcq8WZpwcn8DsAQAAAAAAAAAAAAAAAAAAAAAQDYQAUBIE71RYwoAAH643do3qetRacLO/Q0cbrf2Tep6VJqwc38DsAQAAAAAAAAAAAAAAAAAAAAAQFFTAoCiUy0NZhIAAH643dp3qesWYcKMPxs43G7tu9R1izBhxp8NwBIAAAAAAAAAAAAAAAAAAAAAAEW9BABFr1oYzCUAAH64vdhXqfMWacLO/QkOtxf7KnXeIk3YuT8BLAEAAAAAAAAAAAAAAAAAAAAAkLUjAUhpqjcVFgAAAH643Vo3KfNRacKOPxMcbrfWTcp8VJqw488EsAQAAAAAAAAAAAAAAAAAAAAAQLWoAIBWQW+uEQAAfri92Fep8xZlws79CQ63F/sqdd6iTNi5PwEsAQAAAAAAAAAAAAAAAAAAAACgAQFAYKGYqaYAAAB+uL1YFynzqDYxdu5v4HB7sS5S5lFtYuzc3wAsAQAAAAAABAAAAAAAAAAAAABQiSoAaIQQJooEAAAyfri9WFcp8xZpws79DRxuL9ZVyrxFmrBzfwOwBAAAAAAAAAAAAAAAAAAAAABANJgJQApzhGICAAB+uN1aNylzF2li7PhzgMPt1rpJmbtIE2PHnwPAEgAAAAAAQAAAAAAAAAAAAAAAZbUAAFWr1SumAgAAMH64vdhXqfMWYcKOPxs43F7sq9R5izBhx58NwBIAAAAAAAAAAAAAAAAAAAAAAGVNAQCqzoC5GQAAfrjdWjcpcxdlYpz4c4DD7da6SZm7KBPjxJ8DwBIAAAAAAEAQAAAAAAAAAAAAAKJWBgAhDGaYmwMAAAQyAH64vVhXKfMWacLO/Q0cbi/WVcq8RZqwc38DsAQAAAAAAAAAAAAAAAAAAAAAQKUsAUCjWhpN9RIAAH64vVhXKfOINjFObm3gcHuxrlLmEW1inNzaACwBAAAAAAAEAAAAAAAAAAAAAKBhCUBiaWkwMwAAADh+uN1aNynzEWXCjj8THG631k3KfESZsOPPBLAEAAAAAAAAAAAAAAAAAAAAAEBVBQC0qEYjAgAAfrjdWjcp86gyMXfuT3C43Vo3KfOoMjF37k8ASwAAAAAAAAAAAAAAAAAAAAAAZEMRAIS00JkbjQAAAH64vVhXKfMWacLJ/Q0cbi/WVcq8RZpwcn8DsAQAAAAAAAAAAAAAAAAAAAAAQFEpAUCjMVP1BgkAAH643do3qesRYcKOPxs43G7tm9T1iDBhx58NwBIAAAAAAAAAAAAAAAAAAAAAAFFPAFCMFubmlgAAAH64vdhXqfOINDF27m/gcHuxr1LnEWli7NzfACwBAAAAAAAAAAAAAAAAAAAAAFDWAwBUo4WpmSUAAH64vVhXKfMWYcKJPxMcbi/WVcq8RZhw4s8EsAQAAAAAAAAAAAAAAAAAAAAAQFkpAEDVmEudQQAAAF643eauEvejPm6MDRRut7mrxP2ojxtjAxMAABAAACAgAAAAAAAAAAAAAKJWAqCq2rbbFknSaBQzS63BBEVRhBCCv/9amgVkANQBvgZdhj/Ws4t+wgTWoMvwx3p20U+YwGmqLFdFAiAEAAAAAABWGImNiY2Lj4uPi4+JKoxLNDEJI4SR2Jj4uNgePTttVNqm2+l2up2XxfyvXo23XFzUl5dUW1y075cXFhcr++VFlvVU0fO/OkaiXr58WWbm1fHSFhft++WFxUV7v7wwL9q8vEhbtLlfPJbAE+ZF5grcHsBTALAB";

std::vector<uint8_t> pumpBgmLongToneOggBytes() {
    const auto bytes = decodeBase64(kPumpBgmOggBase64);
    if (bytes.empty() || bytes.size() <= 64 ||
        std::memcmp(bytes.data(), "OggS", 4) != 0) {
        std::cerr << "D04 pump race probe: uzun OGG fixture cozule medi" << std::endl;
        std::exit(1);
    }
    return bytes;
}

// OGG page CRC (poly 0x04C11DB7, init 0) — test_audio_lock.cpp'deki
// missGuardOggPageCrc ile aynı algoritma (CRC alanı sıfırlanır, tüm sayfa).
uint32_t pumpOggPageCrc(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) {
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
            }
            table[i] = r;
        }
        ready = true;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFFu];
    }
    return crc;
}

// CRC-onarımlı granule yaması — test_audio_lock.cpp'deki
// missGuardPatchGranuleForStream ile aynı işlem.
std::vector<uint8_t> pumpPatchGranuleForStream(std::vector<uint8_t> ogg,
                                               uint64_t granule) {
    size_t lastPage = std::string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (std::memcmp(ogg.data() + i, "OggS", 4) == 0) lastPage = i;
    }
    if (lastPage == std::string::npos) {
        std::cerr << "D04 pump race probe: granule yamasi icin OggS sayfasi yok" << std::endl;
        std::exit(1);
    }
    if (lastPage + 27 > ogg.size()) {
        std::cerr << "D04 pump race probe: kesik OggS sayfasi" << std::endl;
        std::exit(1);
    }
    for (int b = 0; b < 8; ++b) {
        ogg[lastPage + 6 + b] = static_cast<uint8_t>((granule >> (8 * b)) & 0xFFu);
    }
    const size_t nseg = ogg[lastPage + 26];
    if (lastPage + 27 + nseg > ogg.size()) {
        std::cerr << "D04 pump race probe: kesik OggS segment tablosu" << std::endl;
        std::exit(1);
    }
    size_t body = 0;
    for (size_t i = 0; i < nseg; ++i) body += ogg[lastPage + 27 + i];
    const size_t pageEnd = lastPage + 27 + nseg + body;
    if (pageEnd > ogg.size()) {
        std::cerr << "D04 pump race probe: kesik OggS sayfa govdesi" << std::endl;
        std::exit(1);
    }
    ogg[lastPage + 22] = 0;
    ogg[lastPage + 23] = 0;
    ogg[lastPage + 24] = 0;
    ogg[lastPage + 25] = 0;
    const uint32_t crc = pumpOggPageCrc(ogg.data() + lastPage, pageEnd - lastPage);
    ogg[lastPage + 22] = static_cast<uint8_t>(crc & 0xFFu);
    ogg[lastPage + 23] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    ogg[lastPage + 24] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
    ogg[lastPage + 25] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
    return ogg;
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

[[noreturn]] void pumpFail(const std::string& message) {
    rowlLockFail("d04-audio-pump-race-probe", message);
}

void setupPumpProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    if (!audio.initialize() || !audio.isInitialized()) {
        pumpFail("audio init failed");
    }
    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_pump_race_project";
    const auto dir = root / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    const auto patched =
        pumpPatchGranuleForStream(pumpBgmLongToneOggBytes(),
                                  static_cast<uint64_t>(1000000000));
    writeBytes(dir / "pump_bgm.ogg", patched);
    vfs.remountProject(root.string());
}

// TSan'ın göreceği gerçek yük/depolama trafiği: sayaçlar biriktirilip
// yazdırılır (ölü-kod eleme yok). Vorbis-decode maliyetli olduğundan
// tur sayısı düşük tutulur (TSan altında bile TIMEOUT 120'nin altında).
constexpr int kHammerIters = 400;

void hammerPump(Rowl::Audio::AudioEngine* audio, double& sink) {
    for (int i = 0; i < kHammerIters; ++i) {
        audio->pumpBgmStream();
        sink += audio->bgmStreamBufferedSeconds();
    }
}

void hammerReaders(Rowl::Audio::AudioEngine* audio, double& sink) {
    for (int i = 0; i < kHammerIters; ++i) {
        sink += audio->bgmStreamBufferedSeconds();
        sink += static_cast<double>(audio->bgmPumpSampleCount());
        sink += static_cast<double>(audio->bgmPumpAvgMicroseconds());
        sink += static_cast<double>(audio->bgmPumpStatsJson().size());
        audio->update();
    }
}

}  // namespace

int main() {
    TEST_SECTION("Audio pump/stream-ring race probe (D04 kilidi)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupPumpProject(vfs, audio);
    if (!audio.isAudioDeviceAvailable()) {
        std::cout << "  SKIP pump race probe (cihaz yok; mandal yalnizca cihazli stream yolunda yazilir)"
                  << std::endl;
        return 0;
    }

    // ── Kurulum: akan streaming BGM (ilerleme kanıtlı baz) ──
    audio.playAudio("audio/pump_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (!audio.isStreaming()) pumpFail("kurulum: isStreaming false (stream rotasi kurulamadi)");
    if (!audio.testBgmStreamSourceOpen()) pumpFail("kurulum: stream kaynagi acik degil");
    if (!(audio.bgmStreamBufferedSeconds() > 0.0))
        pumpFail("kurulum: akis ilerlemedi (buffered_seconds==0): " + audio.streamInfoJson());
    const uint64_t count0 = audio.bgmPumpSampleCount();
    if (count0 == 0) pumpFail("kurulum: pump sayaci ilerlemedi");
    TEST_PASS("kurulum — streaming BGM akan baz (isStreaming + buffered>0 + kaynak acik)");

    // ── Bacak 1: pump vs okuyucu hammer (TSan altında KIRMIZI verir) ──
    {
        double sinkA = 0.0;
        double sinkB = 0.0;
        std::thread pumpThread([&]() { hammerPump(&audio, sinkA); });
        std::thread readerThread([&]() { hammerReaders(&audio, sinkB); });
        pumpThread.join();
        readerThread.join();
        std::cout << "  bacak1 sink=" << (sinkA + sinkB) << std::endl;
    }
    TEST_PASS("Bacak1 — pump vs okuyucu hammer tamamlandi");
    if (!audio.testBgmStreamSourceOpen())
        pumpFail("bacak1 sonrasi: stream kaynagi kapandi (hammer akisi yikti)");
    if (!(audio.bgmStreamBufferedSeconds() > 0.0))
        pumpFail("bacak1 sonrasi: akis konumu sifirlandi");

    // ── Bacak 2: fonksiyonel parite çipası (her konfigürasyonda YEŞİL) ──
    // Suspend altında pump no-op'tur (K2): sayaç + buffered donar.
    {
        audio.setOutputSuspended(true);
        const uint64_t cBefore = audio.bgmPumpSampleCount();
        const double bBefore = audio.bgmStreamBufferedSeconds();
        for (int i = 0; i < 8; ++i) audio.pumpBgmStream();
        audio.update();
        if (audio.bgmPumpSampleCount() != cBefore)
            pumpFail("bacak2 suspend: pump sayaci ilerledi (suspend no-op degil)");
        if (audio.bgmStreamBufferedSeconds() != bBefore)
            pumpFail("bacak2 suspend: akis konumu ilerledi (suspend no-op degil)");
        audio.setOutputSuspended(false);
        TEST_PASS("Bacak2 — suspend altinda pump no-op (sayac + konum donar)");
    }
    // Stats JSON şeması (count/last_us/avg_us/max_us anahtarları).
    {
        const std::string json = audio.bgmPumpStatsJson();
        for (const char* key : {"\"count\"", "\"last_us\"", "\"avg_us\"", "\"max_us\""}) {
            if (json.find(key) == std::string::npos)
                pumpFail(std::string("bacak2 stats semasi: anahtar yok: ") + key + " (" + json + ")");
        }
        std::cout << "  pump-stats: " << json << std::endl;
        TEST_PASS("Bacak2 — pump stats JSON semasi (count/last_us/avg_us/max_us)");
    }
    // Suspend dönüşü akış kaldığı yerden sürer (canlılık probu).
    {
        const uint64_t cBefore = audio.bgmPumpSampleCount();
        audio.update();
        if (audio.bgmPumpSampleCount() <= cBefore)
            pumpFail("bacak2 canlilik: suspend donusu pump sayaci ilerlemedi");
        TEST_PASS("Bacak2 — suspend donusu akis surer (sayac ilerler)");
    }
    // Close sonrası pump güvenli no-op'tur (kilit düşer, çöküş yok).
    audio.stopBgm();
    if (audio.isStreaming()) pumpFail("bacak2 close: stopBgm sonrasi isStreaming true");
    for (int i = 0; i < 8; ++i) audio.pumpBgmStream();
    audio.update();
    TEST_PASS("Bacak2 — close sonrasi pump guvenli no-op (cokus yok)");

    // ── Bacak 3: perf-floor kaydı (S kapısı zemini) ──
    audio.playAudio("audio/pump_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (!audio.isStreaming()) pumpFail("bacak3: re-kurulum stream degil");
    for (int i = 0; i < 200; ++i) audio.pumpBgmStream();
    std::cout << "  perf-floor: count=" << audio.bgmPumpSampleCount()
              << " avg_us=" << audio.bgmPumpAvgMicroseconds()
              << " max_us=" << audio.bgmPumpMaxMicroseconds()
              << " last_us=" << audio.bgmPumpLastMicroseconds() << std::endl;
    TEST_PASS("Bacak3 — perf-floor olcum kaydi (ayni-cihaz karsilastirma zemini)");

    audio.stopAll();
    audio.shutdown();

    TEST_PASS("d04 audio pump/stream-ring kilidi yesil");
    return 0;
}
