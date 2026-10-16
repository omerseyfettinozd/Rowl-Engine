using System;
using Avalonia.Data;
using Avalonia.Markup.Xaml;

namespace RowlEngine.Editor.Services.Strings;

/// <summary>
/// XAML çeviri uzantısı: <c>Text="{l:T Menu.Windows}"</c>. Sabit metin değil
/// <see cref="EditorStrings"/> indeksine tek yönlü bağlama döndürür; dil
/// değişince metin yeniden başlatmasız güncellenir (K7).
/// </summary>
public sealed class TExtension : MarkupExtension
{
    public TExtension() { }

    public TExtension(string key) => Key = key;

    public string Key { get; set; } = string.Empty;

    public override object ProvideValue(IServiceProvider serviceProvider) =>
        new Binding($"[{Key}]")
        {
            Source = EditorStrings.Instance,
            Mode = BindingMode.OneWay,
        };
}
