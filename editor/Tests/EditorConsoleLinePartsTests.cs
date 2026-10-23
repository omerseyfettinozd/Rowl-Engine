using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 8 A2: Konsol satırı prototip .console-lines gibi iki parçadır — soluk
/// zaman/etiket ve mesaj. Ayrıştırma <c>AppendLog</c> biçimine ("[HH:mm:ss] mesaj")
/// ve çekirdek satıra ("[System] …") dayanır; biçimsiz satır olduğu gibi mesajdır.
/// </summary>
public sealed class EditorConsoleLinePartsTests
{
    private static string Part(string? line, string part) =>
        (string)LogLinePartConverter.Instance.Convert(line, typeof(string), part, System.Globalization.CultureInfo.InvariantCulture)!;

    [Theory]
    [InlineData("[21:04:05] Proje yüklendi", "21:04:05", "Proje yüklendi")]
    [InlineData("[System] Rowl Engine Editor", "System", "Rowl Engine Editor")]
    [InlineData("düz satır", "", "düz satır")]
    [InlineData("[] boş etiket", "", "[] boş etiket")]
    [InlineData("[bu etiket on altı karakterden uzun] x", "", "[bu etiket on altı karakterden uzun] x")]
    [InlineData("", "", "")]
    public void SplitsTimeAndMessage(string line, string time, string message)
    {
        Assert.Equal(time, Part(line, "time"));
        Assert.Equal(message, Part(line, "message"));
    }

    [Fact]
    public void NullIsEmpty()
    {
        Assert.Equal(string.Empty, Part(null, "time"));
        Assert.Equal(string.Empty, Part(null, "message"));
    }
}
