namespace RowlEngine.Editor.Services;

/// <summary>
/// Dilim-4 D-profil kill-switch: title girişindeki auto/skip profil→sürücü
/// bağlantısının anahtarı. Varsayılan KAPALI'dır (<c>false</c>); kapalıyken
/// çağrı noktaları eski dallardan akar, davranış değişmez. Açıldığında
/// <see cref="ApplyToDrivers"/> merkezi yolu çalışır (oynanışa girişte auto
/// saati de sıfırlanır).
/// </summary>
public static class PlayerProfileAutoSkipGate
{
    /// <summary>Dilim-4 anahtarı; varsayılan kapalı (davranış değişmez).</summary>
    public static bool Enabled { get; set; } = false;

    /// <summary>
    /// Profil auto bayrağını sürücüye uygular ve satır saatini sıfırlar
    /// (yalnızca anahtar açıkken çağrılır; profil/motor imzası değişmez).
    /// </summary>
    public static void ApplyToDrivers(PlayerProfile profile, AutoPlayDriver auto)
    {
        System.ArgumentNullException.ThrowIfNull(profile);
        System.ArgumentNullException.ThrowIfNull(auto);
        auto.SetEnabled(profile.AutoEnabled);
        auto.NotifyManualAdvance();
    }
}
