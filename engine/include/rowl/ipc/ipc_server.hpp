#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>

namespace Rowl::IPC {

enum class MessageType : uint16_t {
    HandshakeReq = 0,
    HandshakeResp = 1,
    UpdateNodeGraph = 2,
    SetActiveNode = 3,
    UpdateVariable = 4,
    TriggerEvent = 5,
    Heartbeat = 6
};

struct IpcPacket {
    MessageType type;
    uint32_t payloadSize;
    std::vector<uint8_t> payload;
};

using PacketCallback = std::function<void(const IpcPacket&)>;

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    bool start(const std::string& pipeName = "rowl_engine_ipc");
    void stop();

    void setPacketCallback(PacketCallback callback) { m_callback = callback; }
    bool isRunning() const { return m_running; }

private:
    void listenLoop(const std::string& pipeName);

    std::atomic<bool> m_running{false};
    std::thread m_listenThread;
    PacketCallback m_callback;
};

} // namespace Rowl::IPC
