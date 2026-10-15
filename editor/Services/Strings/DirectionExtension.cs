using System;
using Avalonia.Data;
using Avalonia.Markup.Xaml;

namespace RowlEngine.Editor.Services.Strings;

/// <summary>
/// <c>FlowDirection="{l:Direction}"</c>: pencerenin yazı yönünü etkin
/// arayüz diline bağlar; sağdan-sola dillerde tüm yerleşim aynalanır (K6).
/// </summary>
public sealed class DirectionExtension : MarkupExtension
{
    public override object ProvideValue(IServiceProvider serviceProvider) =>
        new Binding(nameof(EditorStrings.FlowDirection))
        {
            Source = EditorStrings.Instance,
            Mode = BindingMode.OneWay,
        };
}
