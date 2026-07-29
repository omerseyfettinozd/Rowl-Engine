# 🚀 PHASE 2 EXECUTION PLAN: C# AVALONIA EDITOR & FLATBUFFERS IPC

> **Phase Objective:** Setup the cross-platform C# .NET 8 Avalonia UI editor interface, compile the FlatBuffers serialization schema, and build the real-time Local IPC (Named Pipes / Unix Domain Sockets) bridge between the Editor and C++ Runtime.

---

## 🏗️ 1. DIRECTORY STRUCTURE (EDITOR & IPC CONTROLLERS)

```text
Node-Oyun-Motoru/
├── editor/                        # C# .NET 8 Avalonia Editor
│   ├── RowlEngine.Editor.sln
│   └── Src/
│       ├── App.axaml
│       ├── ViewModels/            # MainWindowViewModel, IpcViewModel
│       ├── Views/                 # MainWindow.axaml, CanvasView.axaml
│       ├── Ipc/                   # C# Named Pipe client, FlatBuffers binders
│       └── Models/                # Graph model definitions
├── shared/
│   └── rowl_ipc.fbs               # Shared FlatBuffers schema definition
└── engine/
    └── src/
        └── ipc/                   # C++ Named Pipe server receiver
```

---

## 📜 2. FLATBUFFERS COMPILATION PIPELINE

To compile the shared serialization schemas into both C++ and C# source files:

1. **Schema File (`shared/rowl_ipc.fbs`):** This is the master contract file we defined in `sub-specs/01_architecture_and_ipc.md`.
2. **Compilation Command (Automated via script or CMake/MSBuild):**
   ```bash
   # Compile into C++ headers (placed in engine include folder)
   flatc --cpp -o engine/include/rowl/ipc/ shared/rowl_ipc.fbs

   # Compile into C# classes (placed in editor source folder)
   flatc --csharp -o editor/Src/Ipc/ shared/rowl_ipc.fbs
   ```

---

## 💻 3. CROSS-PLATFORM IPC IMPLEMENTATION SKELETONS

### A. C# Named Pipe Client (`editor/Src/Ipc/IpcClient.cs`)
```csharp
using System;
using System.IO.Pipes;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Ipc
{
    public class IpcClient
    {
        private NamedPipeClientStream? _pipeClient;
        private readonly string _pipeName;

        public IpcClient(string pipeName = "rowl_engine_ipc")
        {
            _pipeName = pipeName;
        }

        public async Task ConnectAsync(CancellationToken cancellationToken = default)
        {
            _pipeClient = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
            await _pipeClient.ConnectAsync(cancellationToken);
            // Begin message reading loop or status pinging
        }

        public async Task SendMessageAsync(byte[] payload)
        {
            if (_pipeClient == null || !_pipeClient.IsConnected) return;

            // Frame message: magic bytes + payload length + payload
            byte[] magic = { 0x52, 0x4F, 0x57, 0x4C }; // "ROWL"
            byte[] size = BitConverter.GetBytes((uint)payload.Length);

            await _pipeClient.WriteAsync(magic, 0, 4);
            await _pipeClient.WriteAsync(size, 0, 4);
            await _pipeClient.WriteAsync(payload, 0, payload.Length);
            await _pipeClient.FlushAsync();
        }
    }
}
```

### B. C++ Named Pipe Server (`engine/src/ipc/ipc_server.cpp`)
```cpp
#include <iostream>
#include <vector>
#include <thread>
#include <asio.hpp> // Using standalone header-only ASIO for network/pipes

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Rowl::IPC {
    class IpcServer {
    public:
        void start(const std::string& pipe_name) {
            std::thread([this, pipe_name]() {
                #if defined(_WIN32)
                std::string full_pipe_path = "\\\\.\\pipe\\" + pipe_name;
                while (m_running) {
                    HANDLE hPipe = CreateNamedPipeA(
                        full_pipe_path.c_str(),
                        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                        1, 1024 * 16, 1024 * 16, 0, NULL
                    );
                    if (hPipe == INVALID_HANDLE_VALUE) continue;
                    
                    if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                        process_pipe_stream(hPipe);
                    }
                    CloseHandle(hPipe);
                }
                #else
                // Unix Domain Sockets on Linux/macOS
                asio::io_context io_context;
                asio::local::stream_protocol::endpoint ep("/tmp/" + pipe_name + ".sock");
                asio::local::stream_protocol::acceptor acceptor(io_context, ep);
                
                while (m_running) {
                    asio::local::stream_protocol::socket socket(io_context);
                    acceptor.accept(socket);
                    process_socket_stream(socket);
                }
                #endif
            }).detach();
        }

    private:
        bool m_running = true;
        void process_pipe_stream(void* pipe_handle);
    };
}
```

---

## 🔁 4. LIVE PREVIEW SYNCHRONIZATION CYCLE

During active design sessions, the live preview sync loop acts as the instantaneous bridge between editing and executing:

```
[ Property Changed in Avalonia Slider ]
                  │
                  ▼
[ ViewModel Builds Delta FlatBuffer Payload ]
                  │
                  ▼
[ Pipe Client Sends Packet (framed with "ROWL" header) ]
                  │
                  ▼
[ C++ Server Thread Reads Header + Body ]
                  │
                  ▼
[ Decode FlatBuffer Payload & Push to SPSC Queue ]
                  │
                  ▼
[ Main Render Loop Pops Queue & Applies Update Instantly ]
```

---

## ✅ PHASE 2 ACCEPTANCE CRITERIA
- [ ] `flatc` compiler runs without error and generates identical schema classes for C# and C++.
- [ ] Avalonia Editor initializes its shell with dockable windows using `Dock.Avalonia`.
- [ ] C# client successfully opens and connects to Named Pipes (Windows) and Unix Domain Sockets (Linux).
- [ ] Sending a dummy FlatBuffer packet (e.g., node change event) from the Editor updates the printed output in the running C++ engine console with under **1 millisecond** latency.
