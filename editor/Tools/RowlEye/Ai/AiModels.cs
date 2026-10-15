using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace RowlEngine.Editor.Tools.RowlEye.Ai;

/// <summary>
/// Common rectangle coordinates in logical DIP space.
/// </summary>
public sealed record RectDto(
    [property: JsonPropertyName("x")] double X,
    [property: JsonPropertyName("y")] double Y,
    [property: JsonPropertyName("w")] double W,
    [property: JsonPropertyName("h")] double H
);

/// <summary>
/// Types of telemetry events emitted by the AI agent inspection engine.
/// </summary>
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum AiTelemetryEventType
{
    FocusChanged,
    ActionExecuted,
    AnomalyDetected,
    Reasoning,
    Heartbeat,
    NativeLog
}

/// <summary>
/// Payload for native C++ engine core logs.
/// </summary>
public sealed record NativeLogPayloadDto(
    [property: JsonPropertyName("level")] int Level,
    [property: JsonPropertyName("levelStr")] string LevelStr,
    [property: JsonPropertyName("message")] string Message,
    [property: JsonPropertyName("timestamp")] string Timestamp
);

/// <summary>
/// Severity levels for UI visual anomalies.
/// </summary>
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum AnomalySeverity
{
    Info,
    Warning,
    High,
    Critical
}

/// <summary>
/// Represents a layout or accessibility anomaly detected on the UI.
/// </summary>
public sealed record AiAnomalyDto(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("severity")] AnomalySeverity Severity,
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("detail")] string Detail,
    [property: JsonPropertyName("bounds")] RectDto? Bounds = null
);

/// <summary>
/// Compact, semantic visual tree node optimized for AI agents and LLMs.
/// </summary>
public sealed record SemanticNodeDto(
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("role")] string Role,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("value")] string? Value,
    [property: JsonPropertyName("state")] string State,
    [property: JsonPropertyName("bounds")] RectDto Bounds,
    [property: JsonPropertyName("importance")] double Importance,
    [property: JsonPropertyName("actions")] IReadOnlyList<string> Actions,
    [property: JsonPropertyName("children")] IReadOnlyList<SemanticNodeDto>? Children = null
);

/// <summary>
/// Metadata for the AI context payload.
/// </summary>
public sealed record AiContextMetaDto(
    [property: JsonPropertyName("sessionId")] string SessionId,
    [property: JsonPropertyName("timestamp")] long Timestamp,
    [property: JsonPropertyName("target")] string Target,
    [property: JsonPropertyName("theme")] string Theme,
    [property: JsonPropertyName("scale")] double Scale,
    [property: JsonPropertyName("resolution")] IReadOnlyList<int> Resolution,
    [property: JsonPropertyName("nodeCount")] int NodeCount,
    [property: JsonPropertyName("version")] string Version = "1.0"
);

/// <summary>
/// Complete AI observation context payload containing semantic tree, active state, and anomalies.
/// </summary>
public sealed record AiContextDto(
    [property: JsonPropertyName("meta")] AiContextMetaDto Meta,
    [property: JsonPropertyName("activeState")] AiAgentState ActiveState,
    [property: JsonPropertyName("semanticTree")] SemanticNodeDto SemanticTree,
    [property: JsonPropertyName("anomalies")] IReadOnlyList<AiAnomalyDto> Anomalies
);

/// <summary>
/// Request payload sent by AI agents when inspecting or focusing on a specific UI element.
/// </summary>
public sealed record AiFocusRequest(
    [property: JsonPropertyName("sessionId")] string SessionId,
    [property: JsonPropertyName("target")] string Target,
    [property: JsonPropertyName("controlPath")] string ControlPath,
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("reasoning")] string Reasoning,
    [property: JsonPropertyName("bounds")] RectDto? Bounds = null,
    [property: JsonPropertyName("finding")] string? Finding = null,
    [property: JsonPropertyName("confidence")] double Confidence = 1.0
);

/// <summary>
/// Live snapshot of the AI Agent's current focus, action, and latest thought.
/// </summary>
public sealed class AiAgentState
{
    [JsonPropertyName("sessionId")]
    public string SessionId { get; set; } = "idle";

    [JsonPropertyName("target")]
    public string Target { get; set; } = "MainWindow";

    [JsonPropertyName("focusedPath")]
    public string? FocusedPath { get; set; }

    [JsonPropertyName("currentBounds")]
    public RectDto? CurrentBounds { get; set; }

    [JsonPropertyName("lastAction")]
    public string LastAction { get; set; } = "idle";

    [JsonPropertyName("lastReasoning")]
    public string LastReasoning { get; set; } = "Beklemede...";

    [JsonPropertyName("lastFinding")]
    public string? LastFinding { get; set; }

    [JsonPropertyName("updatedAt")]
    public long UpdatedAt { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
}

/// <summary>
/// Individual telemetry event broadcasted over SSE to observers.
/// </summary>
public sealed record AiTelemetryEvent(
    [property: JsonPropertyName("seq")] long Seq,
    [property: JsonPropertyName("timestamp")] long Timestamp,
    [property: JsonPropertyName("type")] AiTelemetryEventType Type,
    [property: JsonPropertyName("payload")] object Payload
);
