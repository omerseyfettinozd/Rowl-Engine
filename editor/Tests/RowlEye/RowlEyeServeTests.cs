using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye.Serve;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using Xunit;

namespace RowlEngine.Editor.Tests.RowlEye;

[CollectionDefinition("RowlEyeServe", DisableParallelization = true)]
public class RowlEyeServeCollection { }

[Collection("RowlEyeServe")]
public class RowlEyeServeTests : IAsyncLifetime
{
    private RowlEyeWebServer? _server;
    private HttpClient _http = null!;
    private string _tempGoldens = null!;

    public async Task InitializeAsync()
    {
        RowlEngine.Editor.Tools.RowlEye.RowlEyeHeadlessRuntime.EnsureInitialized();

        _tempGoldens = Path.Combine(Path.GetTempPath(), $"rowleye_srv_test_{Guid.NewGuid():N}");
        Directory.CreateDirectory(_tempGoldens);

        var opts = new RowlEyeServeOptions(
            Port: 0, // Automatically binds to free port
            GoldensRoot: _tempGoldens,
            DefaultScale: 1.0
        );

        _server = new RowlEyeWebServer(opts);
        await _server.StartAsync();

        _http = new HttpClient { BaseAddress = new Uri(_server.BaseUrl) };
    }

    public async Task DisposeAsync()
    {
        _http.Dispose();
        if (_server != null)
        {
            await _server.DisposeAsync();
        }

        try
        {
            if (Directory.Exists(_tempGoldens))
                Directory.Delete(_tempGoldens, true);
        }
        catch { }
    }

    [Fact]
    public async Task Targets_Returns_Seven_Allowed_Targets()
    {
        var targets = await _http.GetFromJsonAsync<string[]>("/api/targets");

        Assert.NotNull(targets);
        Assert.Equal(7, targets.Length);
        Assert.Contains(nameof(RowlEyeTarget.MainWindow), targets);
        Assert.Contains(nameof(RowlEyeTarget.SettingsDialog), targets);
        Assert.Contains(nameof(RowlEyeTarget.NodeGraph), targets);
        Assert.Contains(nameof(RowlEyeTarget.Hierarchy), targets);
        Assert.Contains(nameof(RowlEyeTarget.Inspector), targets);
        Assert.Contains(nameof(RowlEyeTarget.AssetBrowser), targets);
        Assert.Contains(nameof(RowlEyeTarget.LivePreview), targets);
    }

    [Fact]
    public async Task Index_Returns_Offline_Cyber_Dark_Inspector_Html()
    {
        var response = await _http.GetAsync("/");
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.NotNull(response.Content.Headers.ContentType);
        Assert.Contains("text/html", response.Content.Headers.ContentType.MediaType);

        string html = await response.Content.ReadAsStringAsync();
        Assert.Contains("ROWL.EYE", html);
        Assert.Contains("LOCAL INSPECTOR", html);
        Assert.Contains("id=\"liveImg\"", html);
        Assert.Contains("id=\"cmpSlider\"", html);
        Assert.DoesNotContain("https://cdn.", html); // Zero-dependency offline guarantee
    }

    [Fact]
    public async Task Snapshot_Returns_Valid_Png_Stream()
    {
        var response = await _http.GetAsync("/api/snapshot?target=SettingsDialog&theme=Dark&scale=1.0");
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("image/png", response.Content.Headers.ContentType?.MediaType);

        byte[] bytes = await response.Content.ReadAsByteArrayAsync();
        Assert.True(bytes.Length > 200, "PNG output must be larger than 200 bytes.");

        // Verify PNG magic header: 0x89, 'P', 'N', 'G'
        Assert.Equal(0x89, bytes[0]);
        Assert.Equal((byte)'P', bytes[1]);
        Assert.Equal((byte)'N', bytes[2]);
        Assert.Equal((byte)'G', bytes[3]);
    }

    [Fact]
    public async Task ViewModel_Returns_Json_For_Target()
    {
        var response = await _http.GetAsync("/api/viewmodel?target=SettingsDialog&theme=Dark");
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string json = await response.Content.ReadAsStringAsync();
        using var doc = JsonDocument.Parse(json);
        Assert.True(doc.RootElement.ValueKind != JsonValueKind.Undefined);
    }

    [Fact]
    public async Task Audit_And_Golden_Update_RoundTrip_Passes()
    {
        // 1. Initial audit should mark target as NeedsBaseline
        var initialAudit = await _http.GetFromJsonAsync<JsonElement>("/api/audit?theme=Dark&scale=1.0");
        Assert.Equal(JsonValueKind.Array, initialAudit.ValueKind);

        // 2. Promote live capture to Golden Master
        var promoteRes = await _http.PostAsync("/api/goldens/update?target=SettingsDialog&theme=Dark&scale=1.0", null);
        Assert.Equal(HttpStatusCode.OK, promoteRes.StatusCode);

        // 3. Golden image endpoint now serves PNG
        var goldenImgRes = await _http.GetAsync("/api/golden-image?target=SettingsDialog&theme=Dark&scale=1.0");
        Assert.Equal(HttpStatusCode.OK, goldenImgRes.StatusCode);
        Assert.Equal("image/png", goldenImgRes.Content.Headers.ContentType?.MediaType);

        // 4. Second audit must show Passed status for SettingsDialog
        var secondAudit = await _http.GetFromJsonAsync<JsonElement>("/api/audit?theme=Dark&scale=1.0");
        bool foundPassed = false;
        foreach (var item in secondAudit.EnumerateArray())
        {
            if (item.GetProperty("target").GetString() == "SettingsDialog" &&
                item.GetProperty("status").GetString() == "Passed")
            {
                foundPassed = true;
                break;
            }
        }
        Assert.True(foundPassed, "SettingsDialog must pass visual audit against newly promoted golden.");
    }

    [Fact]
    public async Task VisualTree_Returns_Valid_Control_Hierarchy()
    {
        var response = await _http.GetAsync("/api/visual-tree?target=SettingsDialog&theme=Dark");
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string json = await response.Content.ReadAsStringAsync();
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("type", out var typeProp));
        Assert.Contains("SettingsDialog", typeProp.GetString());

        Assert.True(root.TryGetProperty("children", out var childrenProp));
        Assert.True(childrenProp.GetArrayLength() > 0);

        Assert.True(root.TryGetProperty("width", out var widthProp));
        Assert.True(widthProp.GetDouble() > 0);
    }
}
