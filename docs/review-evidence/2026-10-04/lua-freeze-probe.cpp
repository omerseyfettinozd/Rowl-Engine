#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"
#include <iostream>
int main() {
  Rowl::Core::Logger::setLogLevel(Rowl::Core::LogLevel::Critical);
  Rowl::Scripting::LuaSandbox s;
  if (!s.initialize()) return 2;
  std::cout << "probe-start" << std::endl;
  const bool ok = s.executeString("while true do pcall(function() while true do end end) end");
  std::cout << "probe-return ok=" << ok << " error=" << s.getLastError() << std::endl;
}
