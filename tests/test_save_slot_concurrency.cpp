// Save-slot concurrency tests — R1 bulgu #3.
//
// Eski kod her yazar için aynı "<slot>.json.tmp" adını O_TRUNC ile
// paylaşıyordu: aynı slota eşzamanlı yazan yazarlar birbirinin baytını
// ezer, son rename sessizce önce yazanları kaybederdi (yırtık/karışık
// final, ama her yazar true dönerdi).
//
// Kural: aynı slota çekiçle vuran yazarların ardından final dosya,
// yazarlardan BİRİNİN tam payload'uyla bayt-bayt aynı olmalı (yapısal
// bütünlük + FNV-1a sağlama). Yırtık/karışık/boş final = kırmızı.
#include <atomic>
#include <barrier>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "rowl/state/save_durability.hpp"
#include "rowl_test_harness.hpp"

namespace {

int g_concFailures = 0;

void checkConc(bool cond, const char* what) {
    if (!cond) {
        std::cerr << "SAVE-SLOT-CONC FAIL: " << what << std::endl;
        ++g_concFailures;
    }
}

uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Payload: "WRITER:<i> ROUND:<r>\n" + <boy> bayt dolgu + "CHECKSUM:<fnv>\n".
// Dolgu baytı yazara özgüdür ('A'+i), sağlama başlık+govdeyi kapsar.
std::string makePayload(int writer, int round, std::size_t bodySize) {
    std::ostringstream head;
    head << "WRITER:" << writer << " ROUND:" << round << "\n";
    const std::string header = head.str();
    const std::string body(bodySize, static_cast<char>('A' + writer));
    std::ostringstream out;
    out << header << body << "CHECKSUM:" << fnv1a(header + body) << "\n";
    return out.str();
}

// Final dosya yazarlardan birinin TAM payload'u mu? Başlığı parse et,
// gövde tekdüzeliğini ve sağlama toplamını doğrula.
int parseDigits(const std::string& s) {
    if (s.empty()) return -1;
    int value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

uint64_t parseU64(const std::string& s, bool* ok) {
    uint64_t value = 0;
    *ok = !s.empty();
    for (char c : s) {
        if (c < '0' || c > '9') {
            *ok = false;
            return 0;
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return value;
}

bool isOneCompletePayload(const std::string& bytes, std::size_t bodySize) {
    if (bytes.size() < 32) return false;
    const std::string::size_type nl = bytes.find('\n');
    if (nl == std::string::npos) return false;
    // Başlık: "WRITER:<i> ROUND:<r>" (tek satır, iki token).
    int writer = -1, round = -1;
    {
        std::istringstream head(bytes.substr(0, nl));
        std::string wtok, rtok, extra;
        if (!(head >> wtok >> rtok) || (head >> extra)) return false;
        if (wtok.rfind("WRITER:", 0) != 0 || rtok.rfind("ROUND:", 0) != 0) {
            return false;
        }
        writer = parseDigits(wtok.substr(7));
        round = parseDigits(rtok.substr(6));
    }
    if (writer < 0 || writer >= 8 || round < 0) return false;
    const std::string::size_type bodyEnd = nl + 1 + bodySize;
    if (bytes.size() < bodyEnd + 10) return false;
    const char fill = static_cast<char>('A' + writer);
    for (std::size_t i = nl + 1; i < bodyEnd; ++i) {
        if (bytes[i] != fill) return false;
    }
    // Kuyruk: "CHECKSUM:<u64>\n" ve fazlası yok.
    const std::string tail = bytes.substr(bodyEnd);
    const std::string::size_type tailNl = tail.find('\n');
    if (tailNl == std::string::npos || tailNl + 1 != tail.size()) return false;
    const std::string ctok = tail.substr(0, tailNl);
    if (ctok.rfind("CHECKSUM:", 0) != 0) return false;
    bool ok = false;
    const uint64_t sum = parseU64(ctok.substr(9), &ok);
    if (!ok) return false;
    return sum == fnv1a(bytes.substr(0, bodyEnd));
}

}  // namespace

void test_save_slot_concurrency() {
    namespace fs = std::filesystem;
    static std::atomic<int> rootCounter{0};
    const fs::path root = fs::temp_directory_path() /
                          ("rowl_save_slot_conc_" +
                           std::to_string(rootCounter.fetch_add(1)));
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }
    const fs::path slotPath = root / "slot.json";

    constexpr int kWriters = 8;
    constexpr int kRounds = 25;
    constexpr std::size_t kBodySize = 128 * 1024;

    std::barrier<> startGate(kWriters);
    std::atomic<int> writeFailures{0};
    std::vector<std::thread> threads;
    threads.reserve(kWriters);
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            startGate.arrive_and_wait();  // tüm yazarlar aynı anda başlar
            for (int r = 0; r < kRounds; ++r) {
                std::string error;
                if (!Rowl::State::writeSlotFileAtomically(
                        slotPath, makePayload(w, r, kBodySize), &error)) {
                    writeFailures.fetch_add(1);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    checkConc(writeFailures.load() == 0, "concurrent writes must all succeed");
    std::string finalBytes;
    {
        std::ifstream input(slotPath, std::ios::binary);
        checkConc(input.is_open(), "final slot file missing after hammer");
        if (input.is_open()) {
            finalBytes.assign((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
        }
    }
    checkConc(!finalBytes.empty(), "final slot file empty after hammer");
    if (!finalBytes.empty()) {
        checkConc(isOneCompletePayload(finalBytes, kBodySize),
                  "final slot is torn/mixed: not one writer's complete payload");
    }

    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    if (g_concFailures != 0) {
        std::cerr << "SAVE-SLOT-CONC: " << g_concFailures << " failure(s)"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Save-slot concurrency: winner is always one complete payload (R1 #3)");
}
