#include "rowl/core/runtime_context.hpp"
#include "rowl/vfs/vfs.hpp"

namespace Rowl::Core {

RuntimeContext::RuntimeContext()
    : m_vfs(std::make_shared<Rowl::VFS::VFSManager>()),
      m_lastResult(RuntimeResult::ok("init")) {}

RuntimeContext::RuntimeContext(std::shared_ptr<Rowl::VFS::VFSManager> vfs)
    : m_vfs(vfs ? std::move(vfs) : std::make_shared<Rowl::VFS::VFSManager>()),
      m_lastResult(RuntimeResult::ok("init")) {}

RuntimeContext::~RuntimeContext() = default;

std::shared_ptr<Rowl::VFS::VFSManager> RuntimeContext::getVfs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_vfs;
}

void RuntimeContext::setVfs(std::shared_ptr<Rowl::VFS::VFSManager> vfs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vfs = vfs ? std::move(vfs) : std::make_shared<Rowl::VFS::VFSManager>();
}

void RuntimeContext::setResult(const RuntimeResult& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastResult = result;
}

void RuntimeContext::setSuccess(const std::string& operation, const std::string& target) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastResult = RuntimeResult::ok(operation, target);
}

void RuntimeContext::setError(RuntimeErrorCode code, const std::string& message,
                              const std::string& operation, const std::string& target) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastResult = RuntimeResult::error(code, message, operation, target);
}

RuntimeResult RuntimeContext::getLastResult() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastResult;
}

void RuntimeContext::clearResult() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastResult = RuntimeResult::ok();
}

} // namespace Rowl::Core
