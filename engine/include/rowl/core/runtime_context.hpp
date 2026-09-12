#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <mutex>

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Platform {
class PlatformHost;
}

namespace Rowl::Core {

enum class RuntimeErrorCode : int32_t {
    Ok = 0,
    InvalidHandle = 1,
    InvalidArgument = 2,
    FileNotFound = 3,
    FileTooLarge = 4,
    ParseError = 5,
    ValidationError = 6,
    IoError = 7,
    ScriptSyntaxError = 8,
    ScriptRuntimeError = 9,
    AudioDecodeError = 10,
    StateError = 11,
    UnknownError = 99
};

struct RuntimeResult {
    RuntimeErrorCode code = RuntimeErrorCode::Ok;
    std::string operation;
    std::string message;
    std::string target;

    bool isOk() const { return code == RuntimeErrorCode::Ok; }
    int32_t rawCode() const { return static_cast<int32_t>(code); }

    static RuntimeResult ok(std::string op = "", std::string tgt = "") {
        return {RuntimeErrorCode::Ok, std::move(op), "Success", std::move(tgt)};
    }
    static RuntimeResult error(RuntimeErrorCode c, std::string msg,
                              std::string op = "", std::string tgt = "") {
        return {c, std::move(op), std::move(msg), std::move(tgt)};
    }
};

class RuntimeContext {
public:
    RuntimeContext();
    explicit RuntimeContext(std::shared_ptr<Rowl::VFS::VFSManager> vfs);
    RuntimeContext(std::shared_ptr<Rowl::VFS::VFSManager> vfs,
                   std::shared_ptr<Rowl::Platform::PlatformHost> platformHost);
    ~RuntimeContext();

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;

    std::shared_ptr<Rowl::VFS::VFSManager> getVfs() const;
    void setVfs(std::shared_ptr<Rowl::VFS::VFSManager> vfs);
    std::shared_ptr<Rowl::Platform::PlatformHost> getPlatformHost() const;
    void setPlatformHost(std::shared_ptr<Rowl::Platform::PlatformHost> platformHost);

    void setResult(const RuntimeResult& result);
    void setSuccess(const std::string& operation = "", const std::string& target = "");
    void setError(RuntimeErrorCode code, const std::string& message,
                  const std::string& operation = "", const std::string& target = "");

    RuntimeResult getLastResult() const;
    void clearResult();

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<Rowl::VFS::VFSManager> m_vfs;
    std::shared_ptr<Rowl::Platform::PlatformHost> m_platformHost;
    RuntimeResult m_lastResult;
};

} // namespace Rowl::Core
