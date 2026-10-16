#nullable enable
using System;
using System.IO;
using System.Xml;
using Avalonia.Media;
using AvaloniaEdit.Highlighting;
using AvaloniaEdit.Highlighting.Xshd;

namespace RowlEngine.Editor.Services.Scripting
{
    // AvaloniaEdit için kodla kurulan Lua vurgulama tanımı (XSHD + tema fırçaları).
    public static class LuaHighlighting
    {
        private const string Xshd = @"<SyntaxDefinition name=""Lua"" extensions="".lua"" xmlns=""http://icsharpcode.net/sharpdevelop/syntaxdefinition/2008"">"
            + @"{COLORS}"
            + @"<RuleSet>"
            + @"<Rule color=""Comment"">--[^\n]*</Rule>"
            + @"<Rule color=""String"">&quot;(?:[^&quot;\\]|\\.)*&quot;</Rule>"
            + @"<Rule color=""String"">'(?:[^'\\]|\\.)*'</Rule>"
            + @"<Keywords color=""Keyword"">"
            + @"<Word>local</Word><Word>function</Word><Word>end</Word><Word>if</Word><Word>then</Word><Word>else</Word><Word>elseif</Word>"
            + @"<Word>return</Word><Word>true</Word><Word>false</Word><Word>nil</Word><Word>for</Word><Word>do</Word><Word>while</Word>"
            + @"<Word>repeat</Word><Word>until</Word><Word>and</Word><Word>or</Word><Word>not</Word><Word>in</Word><Word>break</Word>"
            + @"</Keywords>"
            + @"<Rule color=""Number"">\b\d+(\.\d+)?\b</Rule>"
            + @"</RuleSet></SyntaxDefinition>";

        /// <summary>
        /// Renkler yalnız temadan gelir (LuaCommentBrush … LuaNumberBrush, ThemeStyles); tanım her
        /// çağrıda bu renklerle XSHD olarak kurulur. Fırça yoksa o renk tanımsız kalır (düz metin).
        /// </summary>
        public static IHighlightingDefinition Create(IBrush? comment, IBrush? keyword, IBrush? str, IBrush? number)
        {
            string colors = ColorElement("Comment", comment) + ColorElement("Keyword", keyword)
                + ColorElement("String", str) + ColorElement("Number", number);
            using var sr = new StringReader(Xshd.Replace("{COLORS}", colors));
            using var reader = XmlReader.Create(sr);
            return HighlightingLoader.Load(reader, HighlightingManager.Instance);
        }

        private static string ColorElement(string name, IBrush? brush)
        {
            string foreground = brush is ISolidColorBrush solid
                ? $@" foreground=""#{solid.Color.R:X2}{solid.Color.G:X2}{solid.Color.B:X2}"""
                : string.Empty;
            return $@"<Color name=""{name}""{foreground} />";
        }
    }
}
