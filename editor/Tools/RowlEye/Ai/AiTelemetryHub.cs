using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Tools.RowlEye.Ai;

/// <summary>
/// Thread-safe, zero-dependency telemetry hub managing AI agent event broadcast,
/// history ring buffer, and active agent state.
/// </summary>
public sealed class AiTelemetryHub : IAsyncDisposable, IDisposable
{
    private static readonly Lazy<AiTelemetryHub> InstanceLazy = new(() => new AiTelemetryHub());
    public static AiTelemetryHub Instance => InstanceLazy.Value;

    private const int MaxHistoryEvents = 100;
    private const int MaxChannelCapacity = 1000;

    private readonly Channel<AiTelemetryEvent> _channel = Channel.CreateBounded<AiTelemetryEvent>(new BoundedChannelOptions(MaxChannelCapacity)
    {
        FullMode = BoundedChannelFullMode.DropOldest,
        SingleWriter = false,
        SingleReader = false
    });

    private readonly ConcurrentQueue<AiTelemetryEvent> _history = new();
    private readonly ConcurrentDictionary<Guid, Channel<AiTelemetryEvent>> _subscribers = new();
    private readonly object _stateLock = new();
    private readonly AiAgentState _currentState = new();
    private long _seqCounter = 0;
    private int _disposed = 0;

    public AiAgentState CurrentState
    {
        get
        {
            lock (_stateLock)
            {
                return new AiAgentState
                {
                    SessionId = _currentState.SessionId,
                    Target = _currentState.Target,
                    FocusedPath = _currentState.FocusedPath,
                    CurrentBounds = _currentState.CurrentBounds,
                    LastAction = _currentState.LastAction,
                    LastReasoning = _currentState.LastReasoning,
                    LastFinding = _currentState.LastFinding,
                    UpdatedAt = _currentState.UpdatedAt
                };
            }
        }
    }

    public AiTelemetryHub()
    {
    }

    /// <summary>
    /// Updates the active agent state and broadcasts a FocusChanged event.
    /// </summary>
    public AiTelemetryEvent RecordFocus(AiFocusRequest req)
    {
        ArgumentNullException.ThrowIfNull(req);

        lock (_stateLock)
        {
            _currentState.SessionId = req.SessionId;
            _currentState.Target = req.Target;
            _currentState.FocusedPath = req.ControlPath;
            _currentState.CurrentBounds = req.Bounds;
            _currentState.LastAction = req.Action;
            _currentState.LastReasoning = req.Reasoning;
            _currentState.LastFinding = req.Finding;
            _currentState.UpdatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        return Publish(AiTelemetryEventType.FocusChanged, req);
    }

    /// <summary>
    /// Publishes a general reasoning or observation step.
    /// </summary>
    public AiTelemetryEvent RecordReasoning(string sessionId, string reasoning, string target)
    {
        lock (_stateLock)
        {
            _currentState.SessionId = sessionId;
            _currentState.Target = target;
            _currentState.LastReasoning = reasoning;
            _currentState.UpdatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        return Publish(AiTelemetryEventType.Reasoning, new
        {
            sessionId,
            target,
            reasoning,
            timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
        });
    }

    /// <summary>
    /// Publishes an anomaly detection event.
    /// </summary>
    public AiTelemetryEvent RecordAnomaly(AiAnomalyDto anomaly)
    {
        ArgumentNullException.ThrowIfNull(anomaly);
        return Publish(AiTelemetryEventType.AnomalyDetected, anomaly);
    }

    /// <summary>
    /// Publishes a raw telemetry event to the history and active subscribers.
    /// </summary>
    public AiTelemetryEvent Publish(AiTelemetryEventType type, object payload)
    {
        if (Interlocked.CompareExchange(ref _disposed, 0, 0) == 1)
        {
            return new AiTelemetryEvent(0, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), type, payload);
        }

        long seq = Interlocked.Increment(ref _seqCounter);
        var evt = new AiTelemetryEvent(seq, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), type, payload);

        // Append to ring buffer
        _history.Enqueue(evt);
        while (_history.Count > MaxHistoryEvents && _history.TryDequeue(out _)) { }

        // Broadcast to all active subscribers
        foreach (var sub in _subscribers.Values)
        {
            sub.Writer.TryWrite(evt);
        }

        return evt;
    }

    /// <summary>
    /// Returns historical telemetry events, optionally starting from a sequence number.
    /// </summary>
    public IReadOnlyList<AiTelemetryEvent> GetHistory(long sinceSeq = 0)
    {
        return _history.Where(e => e.Seq > sinceSeq).OrderBy(e => e.Seq).ToList();
    }

    /// <summary>
    /// Subscribes an SSE consumer to real-time events.
    /// </summary>
    public (Guid SubscriptionId, ChannelReader<AiTelemetryEvent> Reader) Subscribe()
    {
        var id = Guid.NewGuid();
        var clientChannel = Channel.CreateBounded<AiTelemetryEvent>(new BoundedChannelOptions(200)
        {
            FullMode = BoundedChannelFullMode.DropOldest,
            SingleWriter = true,
            SingleReader = true
        });

        _subscribers[id] = clientChannel;
        return (id, clientChannel.Reader);
    }

    /// <summary>
    /// Unsubscribes an SSE consumer upon disconnection.
    /// </summary>
    public void Unsubscribe(Guid subscriptionId)
    {
        if (_subscribers.TryRemove(subscriptionId, out var ch))
        {
            ch.Writer.TryComplete();
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 1) return;

        foreach (var sub in _subscribers.Values)
        {
            sub.Writer.TryComplete();
        }
        _subscribers.Clear();
        _channel.Writer.TryComplete();
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }
}
