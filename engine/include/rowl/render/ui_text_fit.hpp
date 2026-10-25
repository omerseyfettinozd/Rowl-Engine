#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Rowl::Render {

// D-K3: saf yerlesim kararlari (SDL / font gerektirmez; kilit testi dogrudan cagirir).
//
// Yazi boyutu (textScale) buyudugunde konusmaci etiketi ve secenek dugmeleri
// ayni olcumden gelen genislik / yukseklikle cizilmelidir; olcum ile cizim
// farkli sayilarla calisinca ad ikinci satira dusup gorunmez, secenek metni
// dugmeden tasiyordu.

/// Konusmaci etiketinin (fiziksel piksel) genisligi: cizilen metnin genisligi +
/// 32 sanal piksel yan bosluk; alt sinir 120, ust sinir diyalog kutusunun %80'i
/// (dar kutuda alt sinir kazanir). `textWidth` cizimde kullanilan ayni sekil
/// olcumunden gelmelidir (textScale dahil).
float speakerTagWidth(float textWidth, float dialogueWidth, float scale);

/// `text` `maxWidth` icine sigmiyorsa sonuna "…" gelecek sekilde kod noktasi
/// kod noktasi kisaltilir (sarma degil kisaltma). Sigiyorsa ayni metin doner.
/// `measure` metnin tek satirlik genisligini verir; maxWidth <= 0 ise bos doner.
std::string ellipsizeToWidth(const std::string& text, float maxWidth,
                             const std::function<float(const std::string&)>& measure);

/// Secenek dugmesinin yerlesim kutusu (sanal 1920x1080 birimleri).
struct ChoiceBox {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    /// Cizilen secenek mi (devre disi / gizli kutular yerlesime karismaz).
    bool visible = true;
    /// Metnin ihtiyac duydugu en az yukseklik (0 = sorun yok, yazarin kutusu kalir).
    float neededHeight = 0.0f;
};

/// Metni sigmayan kutular alt kenarlari sabit kalacak sekilde YUKARI dogru
/// buyur (diyalog kutusunu ve portreyi ortmez); ustteki kutular, yazarin
/// biraktigi aralik (en cok 24 sanal piksel) korunarak alttaki komsunun
/// ustune itilir; yeterince uzaktakiler yerinde kalir. Yazarin zaten
/// ust uste koydugu (ayni yerde, birbirini dislayan) kutular birbirine
/// itilmez. Hicbir kutunun metni sigmiyorsa girdi bayt-birebir kalir.
void fitChoiceBoxes(std::vector<ChoiceBox>& boxes);

/// Secenek metninin dugme icindeki ust kenari (fiziksel piksel). Tek satir ve
/// %100 yazida eski formul (yazar boyutu ortada); buyuk yazida ya da cok satirda
/// metin blogu dugmenin ortasina oturur.
float choiceTextTop(float buttonTop, float buttonHeight, float fontPx, float blockHeight,
                    std::size_t lines, float textScale);

}  // namespace Rowl::Render
