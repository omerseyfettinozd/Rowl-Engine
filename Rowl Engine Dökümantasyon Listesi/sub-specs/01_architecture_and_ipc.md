# 🔬 SUB-SPEC 01: ENGINE ARCHITECTURE & IPC PROTOCOL

> **Subsystem Target:** Decoupled C# Avalonia Editor & C++20 Runtime Inter-Process Communication (IPC) via FlatBuffers.

---

## 1. ARCHITECTURAL OVERVIEW

Rowl Engine enforces a strict decoupling between the **Visual Authoring Environment (Editor)** and the **Execution Environment (Runtime Player)**.

```
+------------------------------------+        IPC Bridge        +------------------------------------+
|        C# .NET 8 AVALONIA          |   (Named Pipes / UDS)    |          C++20 RUNTIME             |
|              EDITOR                | <======================> |             ENGINE                 |
|                                    |       FlatBuffers        |                                    |
|  - Node Graph Management           |   Zero-Copy Binary Payload|  - SDL3 / SFML Render Loop          |
|  - Asset Inspector & Preview       |                          |  - MSDF Font & Layout Engine       |
|  - Project File Management         |                          |  - Sandboxed Lua State Machine     |
+------------------------------------+                          +------------------------------------+
```

### Key Principles:
1. **Decoupled Lifecycle:** The Runtime can run standalone as a exported game or be spawned in child process/IPC mode by the Editor for Live Preview.
2. **Zero-Copy Serialization:** Uses Google FlatBuffers to eliminate JSON parsing overhead during real-time state synchronization.
3. **Cross-Platform IPC Transport:**
   - **Windows:** Named Pipes (`\\.\pipe\rowl_engine_ipc`)
   - **Linux / macOS:** Unix Domain Sockets (`/tmp/rowl_engine.sock`)
   - **Fallback / Remote Debug:** Local Loopback TCP Socket (`127.0.0.1:9099`)

---

## 2. IPC TRANSPORT LAYER & HANDSHAKE PROTOCOL

### Handshake Sequence:
1. **Editor Spawns Runtime:** Editor launches C++ engine binary with flag `--ipc-mode --pipe-id <unique_id>`.
2. **Connection Establishment:** Runtime connects to the pipe endpoint opened by the Editor.
3. **Protocol Handshake (`HandshakeReq` / `HandshakeResp`):**
   - Version match check (e.g., `v1.0.0`).
   - Feature flags verification.
   - Screen geometry / target viewport handle exchange.

### Message Framing Header (Fixed 8-byte Binary Header):
```
[ 4 Bytes: Magic Cookie (0x524F574C = "ROWL") ]
[ 4 Bytes: Payload Size (UInt32 Little-Endian) ]
[ N Bytes: FlatBuffer Binary Payload            ]
```

---

## 3. FLATBUFFERS SCHEMA SPECIFICATION (`rowl_ipc.fbs`)

```fbs
namespace Rowl.IPC;

enum MessageType : byte {
    HandshakeReq = 0,
    HandshakeResp = 1,
    UpdateNodeGraph = 2,
    SetActiveNode = 3,
    UpdateVariable = 4,
    TriggerEvent = 5,
    Heartbeat = 6,
    RuntimeStateReport = 7
}

table HandshakeReq {
    editor_version: string;
    protocol_version: uint;
}

table HandshakeResp {
    engine_version: string;
    status_code: uint; // 0 = OK, 1 = Version Mismatch
}

table NodeData {
    id: ulong;
    node_type: string;
    payload_json: string; // Fast attribute dump
}

table UpdateNodeGraph {
    nodes: [NodeData];
    active_node_id: ulong;
}

table SetActiveNode {
    node_id: ulong;
    instant_jump: bool;
}

table MessageEnvelope {
    msg_type: MessageType;
    sequence_id: ulong;
    timestamp_ms: uint64;
    payload: [ubyte]; // FlatBuffer table payload
}

root_type MessageEnvelope;
```

---

## 4. LIVE PREVIEW & HOT-RELOAD SYNCHRONIZATION

When a creator changes a property in the Editor (e.g., changing background sprite or typewriter text speed):
1. **Delta Patching:** Editor does NOT resend the entire graph. It constructs a targeted `UpdateNodeData` or `SetActiveNode` FlatBuffer envelope.
2. **Lock-Free Queue Processing:** C++ Runtime receives the packet on an asynchronous IO thread (using `asio` / non-blocking socket poll) and pushes it to a lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
3. **Tick Sync:** At the beginning of the next frame loop, C++ Runtime consumes the queue and applies state updates instantly before render execution.

---

## 5. ERROR HANDLING & DISCONNECTION RECOVERY

- **Heartbeat Mechanism:** A bi-directional ping/pong is sent every 1000ms.
- **Graceful Editor Detach:** If the C++ Runtime loses IPC connection while in `--ipc-mode`, it gracefully pauses execution, surfaces an overlay message *"IPC Disconnected — Waiting for Editor"*, and attempts non-blocking reconnection for 10 seconds before terminating cleanly.
- **Panic Protection:** Exceptions in the C++ Runtime during Live Preview send an `ErrorAlert` packet back to the Editor with a stack trace rather than crashing silently.
