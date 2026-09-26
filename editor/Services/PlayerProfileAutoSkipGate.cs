namespace RowlEngine.Editor.Services;

/// <summary>
/// Dilim-4 D-profil kill-switch: title girişindeki profil→sürücü
/// bağlantısının anahtarı. Varsayılan KAPALI'dır (<c>false</c>); kapalıyken
/// çağrı noktaları eski dallardan akar, davranış değişmez. Açıldığında
/// <see cref="ApplyToSession"/> merkezi yolu çalışır (oynanışa girişte auto
/// saati de sıfırlanır).
/// Dilim-7 parity (P1-D) genişletmesi: aynı anahtar altında skip politika
/// okumaları (<see cref="IsSkipDriving"/>) ve gate'li read-tracking
/// dikişleri (<see cref="NotePresentedGated"/>, <see cref="FlushGated"/>).
/// Kapalıyken hepsi no-op'tur (okuma izi kirletilmez, davranış değişmez);
/// GENEL AÇMA YOK — üretimde hiçbir çağrı noktası <see cref="Enabled"/>
/// değerini <c>true</c> yapmaz.
/// </summary>
public static class PlayerProfileAutoSkipGate
{
    /// <summary>Dilim-4 anahtarı; varsayılan kapalı (davranış değişmez).</summary>
    public static bool Enabled { get; set; } = false;

    /// <summary>
    /// Profil auto bayrağını sürücüye uygular ve satır saatini sıfırlar
    /// (yalnızca anahtar açıkken çağrılır; profil/motor imzası değişmez).
    /// Dilim-7: <see cref="ApplyToSession"/> ile aynı etkidir, uyumluluk
    /// için korunur.
    /// </summary>
    public static void ApplyToDrivers(PlayerProfile profile, AutoPlayDriver auto)
    {
        ApplyToSession(profile, auto);
    }

    /// <summary>
    /// Dilim-7 merkezi oturum-giriş uygulaması: auto bayrağı sürücüye
    /// yazılır + satır saati sıfırlanır (manuel girdi sonrası yeniden
    /// başlayan saatle aynı nokta). Skip sürücüsü durum taşımaz — skip
    /// politikası <see cref="IsSkipDriving"/> okumasıyla raporlanır, Tick
    /// akışı aynen profil üzerinden akar.
    /// </summary>
    public static void ApplyToSession(PlayerProfile profile, AutoPlayDriver auto)
    {
        System.ArgumentNullException.ThrowIfNull(profile);
        System.ArgumentNullException.ThrowIfNull(auto);
        auto.SetEnabled(profile.AutoEnabled);
        auto.NotifyManualAdvance();
    }

    /// <summary>Auto sürücüsü bu profille çalışmalı mı (saf politika okuması).</summary>
    public static bool IsAutoDriving(PlayerProfile profile)
    {
        System.ArgumentNullException.ThrowIfNull(profile);
        return profile.AutoEnabled;
    }

    /// <summary>Skip akışı bu profille çalışmalı mı (saf politika okuması).</summary>
    public static bool IsSkipDriving(PlayerProfile profile)
    {
        System.ArgumentNullException.ThrowIfNull(profile);
        return profile.SkipMode != PlayerSkipMode.Off;
    }

    /// <summary>
    /// Gate'li read-tracking: anahtar kapalıyken no-op'tur (0 döner, profil
    /// kirletilmez); açıkken <see cref="PlayerLoopService.NotePresented"/>
    /// delegesidir. Mevcut AdvanceAndTrack akışı aynen kalır — bu dikiş
    /// sonraki dilimin başlık-akışı içindir.
    /// </summary>
    public static int NotePresentedGated(
        PlayerLoopService loop, System.Collections.Generic.IEnumerable<string?> activeContentIds)
    {
        System.ArgumentNullException.ThrowIfNull(loop);
        System.ArgumentNullException.ThrowIfNull(activeContentIds);
        if (!Enabled)
            return 0;
        return loop.NotePresented(activeContentIds);
    }

    /// <summary>
    /// Gate'li profil flush: kapalıyken no-op (null), açıkken
    /// <see cref="PlayerLoopService.Flush"/> delegesidir.
    /// </summary>
    public static string? FlushGated(PlayerLoopService loop)
    {
        System.ArgumentNullException.ThrowIfNull(loop);
        if (!Enabled)
            return null;
        return loop.Flush();
    }
}
