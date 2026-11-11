using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 2 Dilim 1 — persistent content_id (UUIDv5 migration, duplicate gate)
/// and versioned atomic PlayerProfile skeleton.
/// </summary>
public sealed class EditorPlayerLoopSlice1Tests
{
    private const string ProjectUuid = "12345678-1234-5678-1234-567812345678";

    // ── UUIDv5 ──────────────────────────────────────────────────────────

    [Fact]
    public void UuidV5_MatchesRfc4122TestVector()
    {
        // Reference vector: uuid5(DNS, "www.example.com").
        var dns = Guid.Parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        Guid actual = ContentIdService.ToUuidV5(dns, "www.example.com");
        Assert.Equal(Guid.Parse("2ed6657d-e927-568b-95e1-2665a8aea6a2"), actual);
    }

    [Fact]
    public void UuidV5_IsDeterministicAndWellFormed()
    {
        Guid first = ContentIdService.ToUuidV5(Guid.Parse(ProjectUuid), "node:101/component:dlg1c");
        Guid second = ContentIdService.ToUuidV5(Guid.Parse(ProjectUuid), "node:101/component:dlg1c");
        Assert.Equal(first, second);

        string text = first.ToString("D");
        Assert.Equal(text, text.ToLowerInvariant());
        // Version nibble must be 5 and the variant RFC 4122 (8/9/a/b).
        Assert.Equal('5', text.Split('-')[2][0]);
        Assert.Contains(text.Split('-')[3][0], new[] { '8', '9', 'a', 'b' });

        Assert.NotEqual(first, ContentIdService.ToUuidV5(Guid.Parse(ProjectUuid), "node:101/component:other"));
        Assert.NotEqual(first, ContentIdService.ToUuidV5(Guid.NewGuid(), "node:101/component:dlg1c"));
    }

    [Fact]
    public void NewContentId_IsUniqueValidUuid()
    {
        string first = ContentIdService.NewContentId();
        string second = ContentIdService.NewContentId();
        Assert.True(ContentIdService.IsValidContentId(first));
        Assert.NotEqual(first, second);
        Assert.False(ContentIdService.IsValidContentId(null));
        Assert.False(ContentIdService.IsValidContentId(string.Empty));
        Assert.False(ContentIdService.IsValidContentId("not-a-uuid"));
    }

    [Fact]
    public void MigrateContentId_RejectsBadInputs()
    {
        Assert.Throws<ArgumentException>(() => ContentIdService.MigrateContentId("nope", 101, "dlg1c"));
        Assert.Throws<ArgumentException>(() => ContentIdService.MigrateContentId(ProjectUuid, 0, "dlg1c"));
        Assert.Throws<ArgumentException>(() => ContentIdService.MigrateContentId(ProjectUuid, 101, ""));
    }

    // ── Migration ───────────────────────────────────────────────────────

    [Fact]
    public void EnsureContentIds_MigratesOnceAndPreserves()
    {
        var node = new NodeViewModel(101, "A", 0, 0, bare: true);
        var migrated = node.AddComponent<DialogueComponentViewModel>();
        var preserved = node.AddComponent<DialogueComponentViewModel>();
        const string existing = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
        preserved.ContentId = existing.ToUpperInvariant();

        var result = ContentIdService.EnsureContentIds(new[] { node }, ProjectUuid);

        Assert.Equal(2, result.DialogueCount);
        Assert.Equal(1, result.MigratedCount);
        Assert.Equal(1, result.PreservedCount);
        string expected = ContentIdService.MigrateContentId(ProjectUuid, 101, migrated.ComponentId);
        Assert.Equal(expected, migrated.ContentId);
        Assert.Equal(existing, preserved.ContentId);

        var rerun = ContentIdService.EnsureContentIds(new[] { node }, ProjectUuid);
        Assert.Equal(0, rerun.MigratedCount);
        Assert.Equal(expected, migrated.ContentId);
    }

    [Fact]
    public void Dialogue_SerializeOmitsEmptyContentId_WritesAssignedOne()
    {
        var empty = new DialogueComponentViewModel();
        Assert.DoesNotContain(empty.Serialize(), pair => pair.Key == ContentIdService.StorageKey);

        var filled = new DialogueComponentViewModel { ContentId = ContentIdService.NewContentId() };
        var data = filled.Serialize();
        Assert.Equal(filled.ContentId, data[ContentIdService.StorageKey]);

        var restored = new DialogueComponentViewModel();
        restored.Deserialize(data.ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
        Assert.Equal(filled.ContentId, restored.ContentId);
    }

    // ── Validator gate ──────────────────────────────────────────────────

    [Fact]
    public void Validate_DuplicateContentId_IsBuildBlockingError()
    {
        const string shared = "11111111-2222-3333-4444-555555555555";
        var first = NewDialogueNode(101, shared);
        var second = NewDialogueNode(102, shared);
        var connections = new[] { new ConnectionViewModel(first, second) };

        var issues = ProjectValidationService.Validate(
            new[] { first, second }, connections, Path.GetTempPath(), 101);

        Assert.Contains(issues, issue =>
            issue.IsError && issue.Message.Contains("duplicate content_id"));
    }

    [Fact]
    public void Validate_MalformedContentId_IsBuildBlockingError()
    {
        var node = NewDialogueNode(101, "definitely-not-a-uuid");
        var issues = ProjectValidationService.Validate(
            new[] { node }, Array.Empty<ConnectionViewModel>(), Path.GetTempPath(), 101);
        Assert.Contains(issues, issue =>
            issue.IsError && issue.Message.Contains("invalid content_id"));
    }

    [Fact]
    public void Validate_LegacyGraphWithoutIds_HasNoContentErrors()
    {
        string repoRoot = FindRepoRoot();
        foreach (string relative in new[]
                 {
                     Path.Combine("samples", "first_light", "Assets", "json", "full_story_graph.json"),
                     Path.Combine("samples", "second_signal", "Assets", "json", "full_story_graph.json"),
                 })
        {
            string path = Path.Combine(repoRoot, relative);
            Assert.True(File.Exists(path), $"Golden sample missing: {path}");
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var loaded = StoryGraphLoaderService.Load(doc);
            Assert.True(loaded.Success, $"Golden sample failed to load: {relative}");
            var issues = ProjectValidationService.Validate(
                loaded.Nodes, loaded.Connections, Path.GetTempPath(), null, loaded.Structure);
            Assert.DoesNotContain(issues, issue =>
                issue.IsError && issue.Message.Contains("content_id"));
        }
    }

    [Fact]
    public void Validate_DisabledDuplicate_DoesNotBlock()
    {
        const string shared = "22222222-3333-4444-5555-666666666666";
        var first = NewDialogueNode(101, shared);
        var second = NewDialogueNode(102, shared);
        second.AllComponents.OfType<DialogueComponentViewModel>().First().IsEnabled = false;
        var connections = new[] { new ConnectionViewModel(first, second) };

        var issues = ProjectValidationService.Validate(
            new[] { first, second }, connections, Path.GetTempPath(), 101);
        Assert.DoesNotContain(issues, issue =>
            issue.IsError && issue.Message.Contains("duplicate content_id"));
    }

    // ── Project identity ────────────────────────────────────────────────

    [Fact]
    public void ProjectIdentity_EnsureCreatesPersistsAndReuses()
    {
        string directory = NewTempDirectory();
        try
        {
            string manifest = Path.Combine(directory, "project.rowlproj");
            File.WriteAllText(manifest, """{ "name": "Slice1", "version": "1.0.0" }""");

            var created = ProjectIdentityService.EnsureProjectUuid(manifest);
            Assert.True(created.Created);
            Assert.Null(created.Error);
            Assert.True(Guid.TryParse(created.ProjectUuid, out _));

            using (var doc = JsonDocument.Parse(File.ReadAllText(manifest)))
            {
                Assert.Equal("Slice1", doc.RootElement.GetProperty("name").GetString());
                Assert.Equal(created.ProjectUuid, doc.RootElement.GetProperty("project_uuid").GetString());
            }

            var reused = ProjectIdentityService.EnsureProjectUuid(manifest);
            Assert.False(reused.Created);
            Assert.Null(reused.Error);
            Assert.Equal(created.ProjectUuid, reused.ProjectUuid);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── PlayerProfile ───────────────────────────────────────────────────

    [Fact]
    public void PlayerProfile_RoundTripsAtomically()
    {
        string directory = NewTempDirectory();
        try
        {
            var profile = new PlayerProfile
            {
                Language = "tr",
                MasterVolume = 0.8f,
                BgmVolume = 0.7f,
                VoiceVolume = 0.6f,
                SfxVolume = 0.5f,
                TextSpeedMultiplier = 1.5f,
                AutoAdvanceDelay = 3.25f,
                SkipMode = PlayerSkipMode.All,
                AutoEnabled = true,
            };
            string readId = ContentIdService.NewContentId();
            Assert.True(profile.MarkRead(readId));
            Assert.True(profile.IsRead(readId.ToUpperInvariant()));
            Assert.False(profile.MarkRead("junk"));

            Assert.Null(PlayerProfileStore.Save(directory, profile));
            string path = Path.Combine(directory, PlayerProfileStore.FileName);
            Assert.True(File.Exists(path));
            Assert.Empty(Directory.GetFiles(directory, "*.tmp"));

            var loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loaded.Status);
            Assert.Equal("tr", loaded.Profile.Language);
            Assert.Equal(0.8f, loaded.Profile.MasterVolume);
            Assert.Equal(1.5f, loaded.Profile.TextSpeedMultiplier);
            Assert.Equal(PlayerSkipMode.All, loaded.Profile.SkipMode);
            Assert.True(loaded.Profile.AutoEnabled);
            Assert.True(loaded.Profile.IsRead(readId));

            // Defaults: read-only skip and no auto-advance.
            var fresh = new PlayerProfile();
            Assert.Equal(PlayerSkipMode.ReadOnly, fresh.SkipMode);
            Assert.False(fresh.AutoEnabled);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void PlayerProfile_MissingFile_ReturnsDefaultsWithStatus()
    {
        string directory = NewTempDirectory();
        try
        {
            var result = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.MissingDefaults, result.Status);
            Assert.NotNull(result.Diagnostic);
            Assert.Equal(PlayerProfile.CurrentVersion, result.Profile.Version);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void PlayerProfile_CorruptFile_FallsBackWithDiagnostic()
    {
        string directory = NewTempDirectory();
        try
        {
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, PlayerProfileStore.FileName), "not-json");
            var result = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.CorruptDefaults, result.Status);
            Assert.NotNull(result.Diagnostic);
            Assert.Equal(1.0f, result.Profile.MasterVolume);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void PlayerProfile_UnsupportedVersion_FallsBackWithDiagnostic()
    {
        string directory = NewTempDirectory();
        try
        {
            Directory.CreateDirectory(directory);
            File.WriteAllText(
                Path.Combine(directory, PlayerProfileStore.FileName),
                """{ "version": 99, "language": "tr" }""");
            var result = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.UnsupportedVersionDefaults, result.Status);
            Assert.NotNull(result.Diagnostic);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void PlayerProfile_SanitizesRangesLanguageAndReadIds()
    {
        var profile = new PlayerProfile
        {
            MasterVolume = 5.0f,
            BgmVolume = -2.0f,
            TextSpeedMultiplier = 99.0f,
            AutoAdvanceDelay = -10.0f,
            Language = "not a tag!",
            SkipMode = (PlayerSkipMode)42,
            ReadContentIds = new System.Collections.Generic.HashSet<string>(
                new[] { ContentIdService.NewContentId(), "broken-id" },
                StringComparer.OrdinalIgnoreCase),
        };
        int dropped = profile.Sanitized();
        Assert.Equal(1, dropped);
        Assert.Equal(1.0f, profile.MasterVolume);
        Assert.Equal(0.0f, profile.BgmVolume);
        Assert.Equal(4.0f, profile.TextSpeedMultiplier);
        Assert.Equal(0.0f, profile.AutoAdvanceDelay);
        Assert.Equal(PlayerProfile.DefaultLanguage, profile.Language);
        Assert.Equal(PlayerSkipMode.ReadOnly, profile.SkipMode);
        Assert.Single(profile.ReadContentIds);

        // D05-1: any well-formed tag is a valid choice (the game decides support);
        // case and "_" normalize, a region is kept.
        var regional = new PlayerProfile { Language = "tr-TR" };
        regional.Sanitized();
        Assert.Equal("tr-tr", regional.Language);
        var unusual = new PlayerProfile { Language = "xx-YY" };
        unusual.Sanitized();
        Assert.Equal("xx-yy", unusual.Language);
    }

    // ── D05-1: profile language = "not chosen" contract (mirror of native) ──

    [Fact]
    public void PlayerProfile_FreshProfileHasNoLanguageChosen()
    {
        Assert.True(PlayerProfile.DefaultLanguage.Length == 0);
        Assert.Equal(string.Empty, new PlayerProfile().Language);
    }

    [Theory]
    [InlineData("", "", true)]
    [InlineData("   ", "", true)]
    [InlineData("en", "en", true)]
    [InlineData(" EN ", "en", true)]
    [InlineData("tr", "tr", true)]
    [InlineData("de-DE", "de-de", true)]
    [InlineData("pt_br", "pt-br", true)]
    [InlineData("zh-Hans-CN", "zh-hans-cn", true)]
    [InlineData("fil", "fil", true)]
    [InlineData("es-419", "es-419", true)]
    [InlineData("e", "", false)]
    [InlineData("english", "", false)]
    [InlineData("en-", "", false)]
    [InlineData("-en", "", false)]
    [InlineData("en--us", "", false)]
    [InlineData("e1", "", false)]
    [InlineData("en-us!", "", false)]
    [InlineData("en us", "", false)]
    [InlineData("abcdefgh-ijklmnop", "", false)]
    [InlineData("en-abcdefghi", "", false)]
    [InlineData("çç", "", false)]
    public void NormalizeLanguage_MatchesTheNativeRule(string input, string expected, bool valid)
    {
        Assert.Equal(expected, PlayerProfile.NormalizeLanguage(input, out bool isValid));
        Assert.Equal(valid, isValid);
        Assert.Equal(expected, PlayerProfile.NormalizeLanguage(input));
    }

    [Fact]
    public void PlayerProfile_EmptyLanguageIsKeptAndRoundTrips()
    {
        string directory = NewTempDirectory();
        try
        {
            var profile = new PlayerProfile { Language = string.Empty, MasterVolume = 0.5f };
            Assert.Null(PlayerProfileStore.Save(directory, profile));
            string raw = File.ReadAllText(Path.Combine(directory, PlayerProfileStore.FileName));
            using (var document = JsonDocument.Parse(raw))
                Assert.Equal(string.Empty, document.RootElement.GetProperty("language").GetString());
            var loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loaded.Status);
            Assert.Null(loaded.Diagnostic);
            Assert.Equal(string.Empty, loaded.Profile.Language);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Theory]
    [InlineData("en")]
    [InlineData("tr")]
    [InlineData("de-de")]
    [InlineData("pt-br")]
    public void PlayerProfile_ExplicitLanguageRoundTripsUnchanged(string language)
    {
        string directory = NewTempDirectory();
        try
        {
            Assert.Null(PlayerProfileStore.Save(directory, new PlayerProfile { Language = language }));
            var loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(language, loaded.Profile.Language);
            // Rewriting what was read leaves the value (and the document) as it was.
            string first = File.ReadAllText(Path.Combine(directory, PlayerProfileStore.FileName));
            Assert.Null(PlayerProfileStore.Save(directory, loaded.Profile));
            Assert.Equal(first, File.ReadAllText(Path.Combine(directory, PlayerProfileStore.FileName)));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void PlayerProfile_LegacyEnFileIsAnExplicitChoiceAndInvalidTagResetsWithWarning()
    {
        string directory = NewTempDirectory();
        try
        {
            File.WriteAllText(Path.Combine(directory, PlayerProfileStore.FileName),
                "{\"version\":1,\"language\":\"en\"}");
            var legacy = PlayerProfileStore.Load(directory);
            Assert.Equal("en", legacy.Profile.Language);
            Assert.Null(legacy.Diagnostic);

            File.WriteAllText(Path.Combine(directory, PlayerProfileStore.FileName),
                "{\"version\":1,\"language\":\"Klingon Warrior\"}");
            var invalid = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, invalid.Status);
            Assert.Equal(string.Empty, invalid.Profile.Language);
            Assert.Contains("Invalid language", invalid.Diagnostic);

            File.WriteAllText(Path.Combine(directory, PlayerProfileStore.FileName),
                "{\"version\":1,\"language\":\"DE-de\"}");
            Assert.Equal("de-de", PlayerProfileStore.Load(directory).Profile.Language);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    private static NodeViewModel NewDialogueNode(ulong id, string contentId)
    {
        var node = new NodeViewModel(id, $"Node #{id}", 0, 0, bare: true);
        var dialogue = node.AddComponent<DialogueComponentViewModel>();
        dialogue.ContentId = contentId;
        return node;
    }

    private static string NewTempDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlSlice1_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }

    private static string FindRepoRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory, "samples", "first_light")))
                return directory;
            directory = Directory.GetParent(directory)?.FullName;
        }
        throw new Exception("Repository root with samples/ could not be located.");
    }
}
