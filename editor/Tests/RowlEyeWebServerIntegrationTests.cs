using System;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using RowlEngine.Editor.Tools.RowlEye.Serve;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class RowlEyeWebServerIntegrationTests : IDisposable
{
    public void Dispose()
    {
        RowlEyeEngineService.Instance.Shutdown();
    }
    private const int TestPort = 7088;
    private static readonly string BaseUrl = $"http://127.0.0.1:{TestPort}";

    [Fact]
    public async Task WebServer_EndToEnd_Serves_All_AI_Endpoints()
    {
        // 1. Ensure engine initialized
        RowlEyeEngineService.Instance.EnsureInitialized(640, 360);

        // 2. Start server on test port (port 0 = auto-select free port)
        var options = new RowlEyeServeOptions(Port: 0);

        await using var server = new RowlEyeWebServer(options);
        using var cts = new CancellationTokenSource();
        await server.StartAsync(cts.Token);

        string baseUrl = server.BaseUrl.TrimEnd('/');
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };

        try
        {
            // 3. Test Story Graph
            var graphRes = await client.GetAsync($"{baseUrl}/api/v1/ai/story/graph");
            Assert.True(graphRes.IsSuccessStatusCode, "GET /story/graph should return 200 OK");
            string graphBody = await graphRes.Content.ReadAsStringAsync();
            using var graphDoc = JsonDocument.Parse(graphBody);
            Assert.True(graphDoc.RootElement.TryGetProperty("totalNodes", out _));

            // 4. Test VFS Status
            var vfsRes = await client.GetAsync($"{baseUrl}/api/v1/ai/vfs/status?path=Assets/json/full_story_graph.json");
            Assert.True(vfsRes.IsSuccessStatusCode, "GET /vfs/status should return 200 OK");
            string vfsBody = await vfsRes.Content.ReadAsStringAsync();
            using var vfsDoc = JsonDocument.Parse(vfsBody);
            Assert.True(vfsDoc.RootElement.TryGetProperty("mounts", out _));

            // 5. Test Script Eval
            var evalPayload = new StringContent("{\"code\":\"1 + 1 == 2\",\"isCondition\":true}", Encoding.UTF8, "application/json");
            var evalRes = await client.PostAsync($"{baseUrl}/api/v1/ai/script/eval", evalPayload);
            Assert.True(evalRes.IsSuccessStatusCode, "POST /script/eval should return 200 OK");
            string evalBody = await evalRes.Content.ReadAsStringAsync();
            using var evalDoc = JsonDocument.Parse(evalBody);
            Assert.True(evalDoc.RootElement.GetProperty("ok").GetBoolean());
            Assert.Equal("true", evalDoc.RootElement.GetProperty("result").GetString());

            // 6. Test Editor Undo/Redo
            var undoRes = await client.GetAsync($"{baseUrl}/api/v1/ai/editor/undo_redo");
            Assert.True(undoRes.IsSuccessStatusCode, "GET /editor/undo_redo should return 200 OK");

            // 7. Test Editor Inject
            var injectPayload = new StringContent("{\"type\":\"shortcut\",\"id\":\"undo\"}", Encoding.UTF8, "application/json");
            var injectRes = await client.PostAsync($"{baseUrl}/api/v1/ai/editor/inject", injectPayload);
            Assert.True(injectRes.IsSuccessStatusCode, "POST /editor/inject should return 200 OK");

            // 8. Test Memory Stats
            var memRes = await client.GetAsync($"{baseUrl}/api/v1/ai/engine/memory_stats");
            Assert.True(memRes.IsSuccessStatusCode, "GET /engine/memory_stats should return 200 OK");
            string memBody = await memRes.Content.ReadAsStringAsync();
            using var memDoc = JsonDocument.Parse(memBody);
            Assert.True(memDoc.RootElement.TryGetProperty("managedHeapBytes", out _));
            Assert.True(memDoc.RootElement.TryGetProperty("frameStats", out _));

            // 9. Test Story Coverage
            var covRes = await client.GetAsync($"{baseUrl}/api/v1/ai/story/coverage");
            Assert.True(covRes.IsSuccessStatusCode, "GET /story/coverage should return 200 OK");
            string covBody = await covRes.Content.ReadAsStringAsync();
            using var covDoc = JsonDocument.Parse(covBody);
            Assert.True(covDoc.RootElement.TryGetProperty("visitedCount", out _));
            Assert.True(covDoc.RootElement.TryGetProperty("coveragePercent", out _));

            // 10. Test Story Drive
            var drivePayload = new StringContent("{\"scenarioName\":\"IntegrationDrive\",\"steps\":[{\"action\":\"step\"}]}", Encoding.UTF8, "application/json");
            var driveRes = await client.PostAsync($"{baseUrl}/api/v1/ai/story/drive", drivePayload);
            Assert.True(driveRes.IsSuccessStatusCode, "POST /story/drive should return 200 OK");
            string driveBody = await driveRes.Content.ReadAsStringAsync();
            using var driveDoc = JsonDocument.Parse(driveBody);
            Assert.True(driveDoc.RootElement.TryGetProperty("executedSteps", out _));

            // 11. Test Aggregate Quality Report
            var aggRes = await client.GetAsync($"{baseUrl}/api/v1/ai/report/aggregate");
            Assert.True(aggRes.IsSuccessStatusCode, "GET /report/aggregate should return 200 OK");
            string aggBody = await aggRes.Content.ReadAsStringAsync();
            using var aggDoc = JsonDocument.Parse(aggBody);
            Assert.True(aggDoc.RootElement.TryGetProperty("overallHealth", out _));
            Assert.True(aggDoc.RootElement.TryGetProperty("nodeFrameHashes", out _));

            // 12. Test Dialog Answer Enqueue and List
            var dialogPayload = new StringContent("{\"dialogType\":\"confirm\",\"approve\":true,\"selectedPath\":\"/tmp/test.rowl\"}", Encoding.UTF8, "application/json");
            var dialogRes = await client.PostAsync($"{baseUrl}/api/v1/ai/editor/dialog_answer", dialogPayload);
            Assert.True(dialogRes.IsSuccessStatusCode, "POST /editor/dialog_answer should return 200 OK");

            var answersListRes = await client.GetAsync($"{baseUrl}/api/v1/ai/editor/dialog_answers");
            Assert.True(answersListRes.IsSuccessStatusCode, "GET /editor/dialog_answers should return 200 OK");
            string answersBody = await answersListRes.Content.ReadAsStringAsync();
            using var answersDoc = JsonDocument.Parse(answersBody);
            Assert.True(answersDoc.RootElement.TryGetProperty("count", out var cntProp) && cntProp.GetInt32() >= 1);

            // 13. Test Story Mutate (add and remove node)
            var mutatePayload = new StringContent("{\"action\":\"add_node\",\"nodeId\":8888,\"title\":\"HttpNode\"}", Encoding.UTF8, "application/json");
            var mutateRes = await client.PostAsync($"{baseUrl}/api/v1/ai/story/mutate", mutatePayload);
            Assert.True(mutateRes.IsSuccessStatusCode, "POST /story/mutate should return 200 OK");
            string mutateBody = await mutateRes.Content.ReadAsStringAsync();
            using var mutateDoc = JsonDocument.Parse(mutateBody);
            Assert.True(mutateDoc.RootElement.GetProperty("success").GetBoolean());

            var delMutatePayload = new StringContent("{\"action\":\"remove_node\",\"nodeId\":8888}", Encoding.UTF8, "application/json");
            var delMutateRes = await client.PostAsync($"{baseUrl}/api/v1/ai/story/mutate", delMutatePayload);
            Assert.True(delMutateRes.IsSuccessStatusCode);

            // 14. Test Engine Resize
            var resizePayload = new StringContent("{\"width\":1920,\"height\":1080}", Encoding.UTF8, "application/json");
            var resizeRes = await client.PostAsync($"{baseUrl}/api/v1/ai/engine/resize", resizePayload);
            Assert.True(resizeRes.IsSuccessStatusCode, "POST /engine/resize should return 200 OK");

            // 15. Test Engine TimeScale
            var timeScalePayload = new StringContent("{\"scale\":2.0}", Encoding.UTF8, "application/json");
            var timeScaleRes = await client.PostAsync($"{baseUrl}/api/v1/ai/engine/time_scale", timeScalePayload);
            Assert.True(timeScaleRes.IsSuccessStatusCode, "POST /engine/time_scale should return 200 OK");

            // 16. Test Engine RngSeed
            var rngPayload = new StringContent("{\"seed\":42}", Encoding.UTF8, "application/json");
            var rngRes = await client.PostAsync($"{baseUrl}/api/v1/ai/engine/rng_seed", rngPayload);
            Assert.True(rngRes.IsSuccessStatusCode, "POST /engine/rng_seed should return 200 OK");

            // 17. Test Watchdog Status
            var watchdogRes = await client.GetAsync($"{baseUrl}/api/v1/ai/watchdog/status");
            Assert.True(watchdogRes.IsSuccessStatusCode, "GET /watchdog/status should return 200 OK");
            string watchdogBody = await watchdogRes.Content.ReadAsStringAsync();
            using var watchdogDoc = JsonDocument.Parse(watchdogBody);
            Assert.True(watchdogDoc.RootElement.TryGetProperty("isHung", out _));

            // 18. Test Dead Assets
            var deadAssetsRes = await client.GetAsync($"{baseUrl}/api/v1/ai/vfs/dead_assets");
            Assert.True(deadAssetsRes.IsSuccessStatusCode, "GET /vfs/dead_assets should return 200 OK");
            string deadAssetsBody = await deadAssetsRes.Content.ReadAsStringAsync();
            using var deadAssetsDoc = JsonDocument.Parse(deadAssetsBody);
            Assert.True(deadAssetsDoc.RootElement.TryGetProperty("totalScannedFiles", out _));

            // 19. Test Project Lint
            var lintRes = await client.GetAsync($"{baseUrl}/api/v1/ai/project/lint");
            Assert.True(lintRes.IsSuccessStatusCode, "GET /project/lint should return 200 OK");
            string lintBody = await lintRes.Content.ReadAsStringAsync();
            using var lintDoc = JsonDocument.Parse(lintBody);
            Assert.True(lintDoc.RootElement.TryGetProperty("passed", out _));

            // 20. Test Omni Project Comprehension (360-degree all-inclusive state)
            var compRes = await client.GetAsync($"{baseUrl}/api/v1/ai/comprehension");
            Assert.True(compRes.IsSuccessStatusCode, "GET /comprehension should return 200 OK");
            string compBody = await compRes.Content.ReadAsStringAsync();
            using var compDoc = JsonDocument.Parse(compBody);
            Assert.True(compDoc.RootElement.TryGetProperty("narrativeSummary", out var narrProp) && !string.IsNullOrEmpty(narrProp.GetString()));
            Assert.True(compDoc.RootElement.TryGetProperty("projectInfo", out _));
            Assert.True(compDoc.RootElement.TryGetProperty("liveScene", out _));
            Assert.True(compDoc.RootElement.TryGetProperty("storyState", out _));

            // 21. Test AI Tools Schema
            var toolsRes = await client.GetAsync($"{baseUrl}/api/v1/ai/tools");
            Assert.True(toolsRes.IsSuccessStatusCode, "GET /tools should return 200 OK");
            string toolsBody = await toolsRes.Content.ReadAsStringAsync();
            using var toolsDoc = JsonDocument.Parse(toolsBody);
            Assert.True(toolsDoc.RootElement.TryGetProperty("tools", out var toolsArr) && toolsArr.GetArrayLength() >= 5);
        }
        finally
        {
            cts.Cancel();
            await server.DisposeAsync();
        }
    }
}
