using System.IO;
using RowlEngine.Editor.Native;
using System;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>C04 — slot kaydet/yükle hatasının nedeni motorun son sonuç kodundan kullanıcıya ulaşır.</summary>
[Collection("StaticRootSequential")]
public sealed class EditorSaveSlotFailureMessageTests
{
    [Theory]
    [InlineData(RuntimeErrorCode.IoError, "SaveSlots.Error.Io")]
    [InlineData(RuntimeErrorCode.ValidationError, "SaveSlots.Error.Invalid")]
    [InlineData(RuntimeErrorCode.ParseError, "SaveSlots.Error.Corrupt")]
    [InlineData(RuntimeErrorCode.FileNotFound, "SaveSlots.Error.Missing")]
    [InlineData(RuntimeErrorCode.InvalidArgument, "SaveSlots.Error.BadSlot")]
    [InlineData(RuntimeErrorCode.StateError, "SaveSlots.Error.NotStarted")]
    public void Describe_MapsCodeToLocalizedReason(RuntimeErrorCode code, string key)
    {
        string text = SaveSlotFailureMessages.Describe(code)!;
        Assert.Equal(EditorStrings.T(key), text);
        Assert.NotEqual(key, text);
    }

    [Fact]
    public void WithReason_UnknownCode_LeavesGenericMessage()
        => Assert.Equal("x", SaveSlotFailureMessages.WithReason("x", RuntimeErrorCode.UnknownError));

    [Fact]
    public void RealEngine_MissingSlotAndBadIndex_ReportSpecificReasons()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        Assert.True(host.Initialize(64, 64, false));
        // Kayıtlar kullanıcının gerçek XDG kayıt klasörüne değil, boş geçici projeye gider.
        string project = Path.Combine(Path.GetTempPath(), "RowlSlotErr_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(project);
        try
        {
            host.SetProjectDirectory(project);
            Assert.False(host.LoadGameSlot(99));
            Assert.Equal(EditorStrings.T("SaveSlots.Error.Missing"), SaveSlotFailureMessages.Describe(host.LastResultCode));
            Assert.False(host.SaveGameSlot(-3));
            Assert.Equal(EditorStrings.T("SaveSlots.Error.BadSlot"), SaveSlotFailureMessages.Describe(host.LastResultCode));
        }
        finally { Directory.Delete(project, recursive: true); }
    }
}
