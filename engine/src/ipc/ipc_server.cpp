#include "rowl/ipc/ipc_server.hpp"
#include "rowl/core/logger.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace Rowl::IPC {

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const std::string& pipeName) {
    if (m_running) return true;

    m_running = true;
    m_listenThread = std::thread(&IpcServer::listenLoop, this, pipeName);

    ROWL_LOG_INFO("IPC Server started background thread for pipe endpoint: " + pipeName);
    return true;
}

void IpcServer::stop() {
    if (!m_running) return;

    m_running = false;
    if (m_listenThread.joinable()) {
        m_listenThread.join();
    }
    ROWL_LOG_INFO("IPC Server stopped.");
}

void IpcServer::listenLoop(const std::string& pipeName) {
    // We use Unix Domain Socket on Linux for ultra-fast zero-copy IPC
    std::string socketPath = "/tmp/" + pipeName + ".sock";
    unlink(socketPath.c_str()); // Remove old socket if exists

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        ROWL_LOG_ERROR("IPC Server failed to create Unix socket.");
        return;
    }

    // Allow socket reuse to avoid "Address already in use" after crash/restart
    int optval = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ROWL_LOG_ERROR("IPC Server failed to bind socket: " + socketPath);
        close(serverFd);
        return;
    }

    if (listen(serverFd, 5) < 0) {
        ROWL_LOG_ERROR("IPC Server listen failed.");
        close(serverFd);
        return;
    }

    ROWL_LOG_INFO("IPC Server listening on Unix Domain Socket: " + socketPath);

    // Timeout for non-blocking accept
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms
    setsockopt(serverFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (m_running) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) {
            continue; // Timeout or no connection yet
        }

        ROWL_LOG_INFO("IPC Client connected to Editor bridge!");

        while (m_running) {
            // Read 12-byte header: 4-byte Magic 'ROWL' + 2-byte type + 2-byte reserved + 4-byte payload size
            uint8_t headerBuf[12];
            ssize_t bytesRead = recv(clientFd, headerBuf, 12, MSG_WAITALL);
            if (bytesRead <= 0) {
                if (bytesRead < 0) {
                    ROWL_LOG_WARN("IPC Client connection error: " + std::string(strerror(errno)));
                } else {
                    ROWL_LOG_WARN("IPC Client disconnected.");
                }
                break;
            }

            if (bytesRead < 12 || std::memcmp(headerBuf, "ROWL", 4) != 0) {
                ROWL_LOG_WARN("IPC Invalid framing header received.");
                continue;
            }

            uint16_t msgTypeRaw = *reinterpret_cast<uint16_t*>(&headerBuf[4]);
            uint32_t payloadSize = *reinterpret_cast<uint32_t*>(&headerBuf[8]);

            IpcPacket packet;
            packet.type = static_cast<MessageType>(msgTypeRaw);
            packet.payloadSize = payloadSize;
            packet.payload.resize(payloadSize);

            if (payloadSize > 0) {
                ssize_t totalPayloadRead = 0;
                while (totalPayloadRead < static_cast<ssize_t>(payloadSize)) {
                    ssize_t n = recv(clientFd, packet.payload.data() + totalPayloadRead, payloadSize - totalPayloadRead, 0);
                    if (n <= 0) break;
                    totalPayloadRead += n;
                }
            }

            ROWL_LOG_INFO("IPC Server received packet! Type: " + std::to_string(static_cast<int>(packet.type)) + ", Size: " + std::to_string(payloadSize) + " bytes");

            if (m_callback) {
                m_callback(packet);
            }
        }

        close(clientFd);
    }

    close(serverFd);
    unlink(socketPath.c_str());
}

} // namespace Rowl::IPC