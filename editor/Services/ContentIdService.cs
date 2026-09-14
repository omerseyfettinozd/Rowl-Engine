using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 1 — persistent dialogue content identity contract.
/// <list type="bullet">
/// <item>Every dialogue component carries a stable <c>content_id</c> in UUID
/// (<c>8-4-4-4-12</c>, lowercase) form. It is the key for read-tracking,
/// backlog, skip and localization catalogs.</item>
/// <item>New dialogues receive a random v4 id (<see cref="NewContentId"/>).
/// Clones/duplicates must never reuse the source id.</item>
/// <item>Legacy content without an id is migrated exactly once with a
/// deterministic UUIDv5 derived from
/// <c>project_uuid + node_id + component_id</c>
/// (<see cref="MigrateContentId"/>). Existing ids are never overwritten.</item>
/// <item>An empty id means "not migrated yet" and is valid; a non-empty id
/// that is not a UUID is a build-blocking validation error.</item>
/// </list>
/// Native loaders read component <c>data</c> with key-default lookups, so the
/// extra <c>content_id</c> entry is forward-compatible and needs no C ABI
/// change in this slice.
/// </summary>
public static class ContentIdService
{
    /// <summary>Canonical storage key inside dialogue component data.</summary>
    public const string StorageKey = "content_id";

    /// <summary>Deterministic migration name prefix (v1).</summary>
    public const string MigrationNamePrefix = "node:";

    /// <summary>Generates a fresh random (v4) content id for a new dialogue.</summary>
    public static string NewContentId() => Guid.NewGuid().ToString("D").ToLowerInvariant();

    /// <summary>Reports whether <paramref name="value"/> is a well-formed UUID.</summary>
    public static bool IsValidContentId(string? value) =>
        !string.IsNullOrWhiteSpace(value) && Guid.TryParse(value, out _);

    /// <summary>
    /// Normalizes a stored id to canonical lowercase UUID form.
    /// Returns null when the value is not a UUID.
    /// </summary>
    public static string? Normalize(string? value)
    {
        if (string.IsNullOrWhiteSpace(value) || !Guid.TryParse(value, out var parsed))
            return null;
        return parsed.ToString("D").ToLowerInvariant();
    }

    /// <summary>
    /// Deterministic one-time migration id (RFC 4122 UUIDv5):
    /// namespace = <paramref name="projectUuid"/>,
    /// name = <c>node:{nodeId}/component:{componentId}</c>.
    /// </summary>
    /// <exception cref="ArgumentException">
    /// Thrown when <paramref name="projectUuid"/> is not a UUID or when the
    /// node/component identity is missing.
    /// </exception>
    public static string MigrateContentId(string projectUuid, ulong nodeId, string? componentId)
    {
        if (string.IsNullOrWhiteSpace(projectUuid) || !Guid.TryParse(projectUuid, out var namespaceId))
            throw new ArgumentException($"Migration needs a valid project_uuid; got '{projectUuid}'.", nameof(projectUuid));
        if (nodeId == 0)
            throw new ArgumentException("Migration needs a nonzero node id.", nameof(nodeId));
        if (string.IsNullOrWhiteSpace(componentId))
            throw new ArgumentException("Migration needs a nonempty component id.", nameof(componentId));
        string name = $"{MigrationNamePrefix}{nodeId}/component:{componentId}";
        return ToUuidV5(namespaceId, name).ToString("D").ToLowerInvariant();
    }

    /// <summary>RFC 4122 §4.3 UUIDv5 (SHA-1 namespace hashing).</summary>
    public static Guid ToUuidV5(Guid namespaceId, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        byte[] namespaceBytes = namespaceId.ToByteArray();
        // Guid.ToByteArray uses mixed-endian layout; UUIDv5 hashes network order.
        SwapGuidByteOrder(namespaceBytes);
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        byte[] hash;
        using (var sha1 = SHA1.Create())
        {
            sha1.TransformBlock(namespaceBytes, 0, namespaceBytes.Length, null, 0);
            sha1.TransformFinalBlock(nameBytes, 0, nameBytes.Length);
            hash = sha1.Hash!;
        }
        var result = new byte[16];
        Array.Copy(hash, result, 16);
        result[6] = (byte)((result[6] & 0x0F) | 0x50); // version 5
        result[8] = (byte)((result[8] & 0x3F) | 0x80); // RFC 4122 variant
        SwapGuidByteOrder(result);
        return new Guid(result);
    }

    private static void SwapGuidByteOrder(byte[] guid)
    {
        (guid[0], guid[3]) = (guid[3], guid[0]);
        (guid[1], guid[2]) = (guid[2], guid[1]);
        (guid[4], guid[5]) = (guid[5], guid[4]);
        (guid[6], guid[7]) = (guid[7], guid[6]);
    }

    /// <summary>Outcome of <see cref="EnsureContentIds"/>.</summary>
    public sealed record MigrationResult(
        string ProjectUuid,
        int DialogueCount,
        int MigratedCount,
        int PreservedCount);

    /// <summary>
    /// Fills every empty dialogue <c>content_id</c> with its deterministic
    /// migration value. Existing ids are preserved untouched (and normalized
    /// to canonical form when they parse as UUIDs).
    /// </summary>
    /// <exception cref="ArgumentException">
    /// Thrown when <paramref name="projectUuid"/> is not a UUID.
    /// </exception>
    public static MigrationResult EnsureContentIds(
        IEnumerable<NodeViewModel> nodes, string projectUuid)
    {
        if (string.IsNullOrWhiteSpace(projectUuid) || !Guid.TryParse(projectUuid, out _))
            throw new ArgumentException($"Migration needs a valid project_uuid; got '{projectUuid}'.", nameof(projectUuid));
        int dialogueCount = 0, migrated = 0, preserved = 0;
        foreach (var node in nodes)
        {
            foreach (var dialogue in EnumerateDialogues(node))
            {
                dialogueCount++;
                if (string.IsNullOrWhiteSpace(dialogue.ContentId))
                {
                    dialogue.ContentId = MigrateContentId(projectUuid, node.Id, dialogue.ComponentId);
                    migrated++;
                }
                else
                {
                    string? normalized = Normalize(dialogue.ContentId);
                    if (normalized is not null)
                        dialogue.ContentId = normalized;
                    preserved++;
                }
            }
        }
        return new MigrationResult(projectUuid, dialogueCount, migrated, preserved);
    }

    internal static IEnumerable<DialogueComponentViewModel> EnumerateDialogues(NodeViewModel node)
    {
        foreach (var frameObject in node.Objects)
        {
            foreach (var component in frameObject.Components)
            {
                if (component is DialogueComponentViewModel dialogue)
                    yield return dialogue;
            }
        }
    }
}
