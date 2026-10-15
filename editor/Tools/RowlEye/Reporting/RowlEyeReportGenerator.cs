using System;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;

namespace RowlEngine.Editor.Tools.RowlEye.Reporting;

/// <summary>
/// Generates self-contained interactive HTML visual audit reports.
/// </summary>
public static class RowlEyeReportGenerator
{
    public static string Generate(RowlEyeReportModel model, string destinationHtmlPath)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationHtmlPath);

        var sb = new StringBuilder();
        sb.AppendLine("<!DOCTYPE html>");
        sb.AppendLine("<html lang=\"tr\">");
        sb.AppendLine("<head>");
        sb.AppendLine("  <meta charset=\"utf-8\" />");
        sb.AppendLine("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />");
        sb.AppendLine($"  <title>{HtmlEncode(model.Title)}</title>");
        sb.AppendLine("  <style>");
        sb.AppendLine(@"
    :root {
      --bg: #0f1015;
      --card-bg: #161822;
      --header-bg: #1a1d2d;
      --border: #2a2e45;
      --text: #e6e8f2;
      --text-muted: #8b92b2;
      --pass: #10b981;
      --fail: #ef4444;
      --warn: #f59e0b;
      --accent: #6366f1;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }
    body { background: var(--bg); color: var(--text); padding-bottom: 60px; line-height: 1.5; }
    header { background: var(--header-bg); border-bottom: 1px solid var(--border); padding: 20px 32px; position: sticky; top: 0; z-index: 100; display: flex; justify-content: space-between; align-items: center; }
    .title-area h1 { font-size: 20px; font-weight: 700; display: flex; align-items: center; gap: 10px; }
    .badge { font-size: 11px; padding: 3px 8px; border-radius: 6px; font-weight: 600; text-transform: uppercase; }
    .badge-pass { background: rgba(16, 185, 129, 0.2); color: var(--pass); border: 1px solid var(--pass); }
    .badge-fail { background: rgba(239, 68, 68, 0.2); color: var(--fail); border: 1px solid var(--fail); }
    .badge-warn { background: rgba(245, 158, 11, 0.2); color: var(--warn); border: 1px solid var(--warn); }
    .meta-line { font-size: 13px; color: var(--text-muted); margin-top: 4px; }
    .stats-bar { display: flex; gap: 16px; align-items: center; }
    .stat-pill { background: rgba(255,255,255,0.05); padding: 6px 14px; border-radius: 20px; font-size: 13px; font-weight: 600; border: 1px solid var(--border); }
    .container { max-width: 1400px; margin: 32px auto; padding: 0 24px; }
    .filter-bar { display: flex; gap: 10px; margin-bottom: 24px; }
    .filter-btn { background: var(--card-bg); border: 1px solid var(--border); color: var(--text); padding: 8px 16px; border-radius: 8px; cursor: pointer; font-size: 13px; font-weight: 500; transition: all 0.2s; }
    .filter-btn:hover, .filter-btn.active { background: var(--accent); border-color: var(--accent); color: #fff; }
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 12px; margin-bottom: 24px; overflow: hidden; }
    .card-header { padding: 16px 20px; background: rgba(255,255,255,0.02); border-bottom: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; }
    .card-title { font-size: 16px; font-weight: 600; display: flex; align-items: center; gap: 10px; }
    .card-body { padding: 20px; }
    .images-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(360px, 1fr)); gap: 16px; }
    .img-box { border: 1px solid var(--border); border-radius: 8px; padding: 12px; background: rgba(0,0,0,0.2); text-align: center; }
    .img-box h4 { font-size: 13px; color: var(--text-muted); margin-bottom: 8px; text-align: left; }
    .img-box img { max-width: 100%; height: auto; border-radius: 4px; border: 1px solid #333; display: block; margin: 0 auto; }
    .placeholder-box { height: 220px; display: flex; align-items: center; justify-content: center; color: var(--text-muted); font-size: 13px; background: rgba(255,255,255,0.02); border-radius: 4px; }
    details { margin-top: 16px; border: 1px solid var(--border); border-radius: 8px; padding: 12px; background: rgba(0,0,0,0.15); }
    summary { font-size: 13px; font-weight: 600; cursor: pointer; color: var(--accent); }
    pre { margin-top: 10px; font-size: 12px; color: #a5b4fc; background: #0b0c10; padding: 12px; border-radius: 6px; overflow-x: auto; max-height: 250px; }
  ");
        sb.AppendLine("  </style>");
        sb.AppendLine("</head>");
        sb.AppendLine("<body>");

        // Header
        sb.AppendLine("  <header>");
        sb.AppendLine("    <div class=\"title-area\">");
        sb.AppendLine($"      <h1>{HtmlEncode(model.Title)}</h1>");
        sb.AppendLine($"      <div class=\"meta-line\">Oluşturulma: {HtmlEncode(model.TimestampUtc)} | Commit: <code>{HtmlEncode(model.GitCommitSha[..Math.Min(8, model.GitCommitSha.Length)])}</code> | Avalonia v{HtmlEncode(model.AvaloniaVersion)}</div>");
        sb.AppendLine("    </div>");
        sb.AppendLine("    <div class=\"stats-bar\">");
        sb.AppendLine($"      <span class=\"stat-pill\">Toplam: {model.TotalCaptures}</span>");
        sb.AppendLine($"      <span class=\"stat-pill badge-pass\">Başarılı: {model.PassedCount}</span>");
        sb.AppendLine($"      <span class=\"stat-pill badge-fail\">Hatalı: {model.FailedCount}</span>");
        sb.AppendLine("    </div>");
        sb.AppendLine("  </header>");

        // Container
        sb.AppendLine("  <div class=\"container\">");

        // Filter Bar
        sb.AppendLine("    <div class=\"filter-bar\">");
        sb.AppendLine("      <button class=\"filter-btn active\" onclick=\"filterCards('all')\">Tümü</button>");
        sb.AppendLine("      <button class=\"filter-btn\" onclick=\"filterCards('failed')\">Sadece Hatalılar</button>");
        sb.AppendLine("      <button class=\"filter-btn\" onclick=\"filterCards('dark')\">Koyu Tema</button>");
        sb.AppendLine("      <button class=\"filter-btn\" onclick=\"filterCards('light')\">Açık Tema</button>");
        sb.AppendLine("    </div>");

        // Cards
        foreach (var item in model.Items)
        {
            string statusClass = item.Status == "PASSED" ? "badge-pass" : (item.Status == "MISSING_GOLDEN" ? "badge-warn" : "badge-fail");
            string themeAttr = item.Theme.ToLowerInvariant();
            string statusAttr = item.Status == "PASSED" ? "passed" : "failed";

            sb.AppendLine($"    <div class=\"card\" data-theme=\"{themeAttr}\" data-status=\"{statusAttr}\">");
            sb.AppendLine("      <div class=\"card-header\">");
            sb.AppendLine("        <div class=\"card-title\">");
            sb.AppendLine($"          <span>{HtmlEncode(item.TargetName)}</span>");
            sb.AppendLine($"          <span class=\"badge {statusClass}\">{HtmlEncode(item.Status)}</span>");
            sb.AppendLine($"          <span style=\"font-size: 12px; color: var(--text-muted);\">Tema: {HtmlEncode(item.Theme)} | Ölçek: {item.Scale}x</span>");
            sb.AppendLine("        </div>");
            sb.AppendLine($"        <div style=\"font-size: 13px; font-weight: 600;\">Fark: {item.MismatchPercentage:F3}% ({item.MismatchedPixels:N0} px)</div>");
            sb.AppendLine("      </div>");
            sb.AppendLine("      <div class=\"card-body\">");

            // Images Grid
            sb.AppendLine("        <div class=\"images-grid\">");

            // Golden / Reference
            sb.AppendLine("          <div class=\"img-box\">");
            sb.AppendLine("            <h4>Altın Referans (Golden)</h4>");
            if (!string.IsNullOrEmpty(item.GoldenImagePath) && File.Exists(item.GoldenImagePath))
                sb.AppendLine($"            <img src=\"{HtmlEncode(item.GoldenImagePath)}\" alt=\"Golden\" />");
            else
                sb.AppendLine("            <div class=\"placeholder-box\">Referans Görsel Yok</div>");
            sb.AppendLine("          </div>");

            // Actual / Current
            sb.AppendLine("          <div class=\"img-box\">");
            sb.AppendLine("            <h4>Güncel Render (Actual)</h4>");
            if (!string.IsNullOrEmpty(item.ActualImagePath) && File.Exists(item.ActualImagePath))
                sb.AppendLine($"            <img src=\"{HtmlEncode(item.ActualImagePath)}\" alt=\"Actual\" />");
            else
                sb.AppendLine("            <div class=\"placeholder-box\">Güncel Render Yok</div>");
            sb.AppendLine("          </div>");

            // Diff Heatmap
            sb.AppendLine("          <div class=\"img-box\">");
            sb.AppendLine("            <h4>Piksel Isı Haritası (Heatmap Diff)</h4>");
            if (!string.IsNullOrEmpty(item.DiffImagePath) && File.Exists(item.DiffImagePath))
                sb.AppendLine($"            <img src=\"{HtmlEncode(item.DiffImagePath)}\" alt=\"Diff Heatmap\" />");
            else
                sb.AppendLine("            <div class=\"placeholder-box\">Diff Üretilmedi</div>");
            sb.AppendLine("          </div>");

            sb.AppendLine("        </div>"); // images-grid

            // ViewModel JSON
            if (!string.IsNullOrWhiteSpace(item.ViewModelJson))
            {
                sb.AppendLine("        <details>");
                sb.AppendLine("          <summary>Mühürlenen C# ViewModel Verisi (JSON)</summary>");
                sb.AppendLine($"          <pre><code>{HtmlEncode(item.ViewModelJson)}</code></pre>");
                sb.AppendLine("        </details>");
            }

            sb.AppendLine("      </div>"); // card-body
            sb.AppendLine("    </div>"); // card
        }

        sb.AppendLine("  </div>"); // container

        // JS for filtering
        sb.AppendLine("  <script>");
        sb.AppendLine(@"
    function filterCards(type) {
      document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
      event.target.classList.add('active');
      const cards = document.querySelectorAll('.card');
      cards.forEach(card => {
        if (type === 'all') {
          card.style.display = 'block';
        } else if (type === 'failed') {
          card.style.display = card.getAttribute('data-status') === 'failed' ? 'block' : 'none';
        } else if (type === 'dark' || type === 'light') {
          card.style.display = card.getAttribute('data-theme') === type ? 'block' : 'none';
        }
      });
    }
  ");
        sb.AppendLine("  </script>");
        sb.AppendLine("</body>");
        sb.AppendLine("</html>");

        string? dir = Path.GetDirectoryName(destinationHtmlPath);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        File.WriteAllText(destinationHtmlPath, sb.ToString(), Encoding.UTF8);
        return destinationHtmlPath;
    }

    private static string HtmlEncode(string? text)
    {
        return HtmlEncoder.Default.Encode(text ?? string.Empty);
    }
}
