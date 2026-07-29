#pragma once

#include <string>
#include <memory>
#include <unordered_map>

struct lua_State;

namespace Rowl::Scripting {

class LuaSandbox {
public:
    LuaSandbox();
    ~LuaSandbox();

    bool initialize();
    bool executeString(const std::string& scriptCode);
    void shutdown();

    void setVariable(const std::string& key, const std::string& value);
    std::string getVariable(const std::string& key) const;

    bool isInitialized() const { return m_initialized; }

private:
    void bindEngineApis();

    lua_State* m_luaState = nullptr;
    std::unordered_map<std::string, std::string> m_scriptVariables;
    bool m_initialized = false;
};

} // namespace Rowl::Scripting
