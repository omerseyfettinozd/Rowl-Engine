#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace Rowl::Scripting {

class LuaSandbox {
public:
    LuaSandbox();
    ~LuaSandbox();

    bool initialize();
    bool executeString(const std::string& scriptCode);
    /// Loads a component script into an isolated environment. Global callback
    /// names inside one component cannot overwrite another component's names.
    bool loadModule(const std::string& moduleId, const std::string& scriptCode);
    /// Calls a lifecycle callback from one loaded component module. Missing
    /// callbacks are successful no-ops, matching callOptionalFunction().
    bool callOptionalModuleFunction(const std::string& moduleId,
                                    const std::string& functionName,
                                    double deltaTime = 0.0);
    /// Removes a loaded component module and releases its Lua registry entry.
    bool unloadModule(const std::string& moduleId);
    void clearModules();
    std::size_t getModuleCount() const { return m_modules.size(); }
    /// Calls a global lifecycle callback when it exists. Missing callbacks are successful no-ops.
    bool callOptionalFunction(const std::string& functionName, double deltaTime = 0.0);
    void shutdown();

    void setVariable(const std::string& key, const std::string& value);
    std::string getVariable(const std::string& key) const;
    void setGlobalNumber(const std::string& key, double value);
    double getGlobalNumber(const std::string& key, double defaultValue = 0.0) const;

    bool evaluateCondition(const std::string& conditionExpr);

    const std::unordered_map<std::string, std::string>& getAllVariables() const { return m_scriptVariables; }
    void clearVariables();

    bool isInitialized() const { return m_initialized; }

private:
    void bindEngineApis();

    lua_State* m_luaState = nullptr;
    std::unordered_map<std::string, std::string> m_scriptVariables;
    std::unordered_map<std::string, int> m_modules;
    bool m_initialized = false;
};

} // namespace Rowl::Scripting
