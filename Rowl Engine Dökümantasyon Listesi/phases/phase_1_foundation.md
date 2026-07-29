# 🚀 PHASE 1 EXECUTION PLAN: C++20 CORE & VFS FOUNDATION (DETAILED BLUEPRINT)

> **Phase Objective:** Build the C++20 core engine runtime, CMake build system, SDL3 hardware windowing, and the Hybrid Zstd VFS storage engine with absolute precision.

---

## 🏗️ 1. DIRECTORY STRUCTURE & REPOSITORY SETUP

```text
Node-Oyun-Motoru/
├── CMakeLists.txt                 # Master CMake configuration
├── cmake/                         # CMake find modules & dependencies
├── engine/                        # C++20 Runtime Engine
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── rowl/
│   │       ├── core/              # Engine context, logger, config
│   │       ├── vfs/               # Virtual File System & Zstd reader
│   │       ├── render/            # SDL3 windowing & GPU context
│   │       └── utils/             # Ring buffers, hashing (MurmurHash3)
│   ├── src/
│   │   ├── core/
│   │   ├── vfs/
│   │   └── render/
│   └── tests/                     # GoogleTest suite
├── editor/                        # C# Avalonia Editor (Phases 2-3)
├── modules/                       # Community mods & script hooks
├── data/                          # Packed .rowlpkg assets & test scripts
└── docs/                          # Master Spec & Sub-Specs
```

---

## 🛠️ 2. CMAKE BUILD CONFIGURATION (`engine/CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.25)
project(RowlEngineCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dependencies via FetchContent or System Packages
include(FetchContent)

# 1. SDL3
find_package(SDL3 REQUIRED)

# 2. Zstd
find_package(zstd REQUIRED)

# 3. spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

add_library(RowlEngineCore
    src/core/logger.cpp
    src/vfs/vfs.cpp
    src/vfs/rowlpkg_reader.cpp
    src/render/window.cpp
    src/render/render_loop.cpp
)

target_include_directories(RowlEngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(RowlEngineCore PRIVATE
    SDL3::SDL3
    libzstd::libzstd
    spdlog::spdlog
)
```

---

## 💻 3. CORE COMPONENT INTERFACES & CODE BLUEPRINTS

### A. Thread-Safe Logger (`include/rowl/core/logger.hpp`)
```cpp
#pragma once
#include <spdlog/spdlog.h>
#include <memory>

namespace Rowl::Core {
    class Logger {
    public:
        static void init();
        static std::shared_ptr<spdlog::logger>& get_core_logger();
    };
}

#define ROWL_LOG_INFO(...)  ::Rowl::Core::Logger::get_core_logger()->info(__VA_ARGS__)
#define ROWL_LOG_WARN(...)  ::Rowl::Core::Logger::get_core_logger()->warn(__VA_ARGS__)
#define ROWL_LOG_ERROR(...) ::Rowl::Core::Logger::get_core_logger()->error(__VA_ARGS__)
```

### B. Hybrid VFS Resolver (`include/rowl/vfs/vfs.hpp`)
```cpp
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Rowl::VFS {

    class IDataSource {
    public:
        virtual ~IDataSource() = default;
        virtual bool exists(const std::string& path) = 0;
        virtual std::vector<uint8_t> read(const std::string& path) = 0;
    };

    class VFSManager {
    public:
        static VFSManager& instance();

        void mount_directory(const std::string& virtual_prefix, const std::string& physical_path);
        void mount_package(const std::string& virtual_prefix, const std::string& pkg_path);

        bool exists(const std::string& vfs_path);
        std::vector<uint8_t> read_bytes(const std::string& vfs_path);
        std::string read_string(const std::string& vfs_path);

    private:
        VFSManager() = default;
        std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>> m_mount_points;
    };
}
```

### C. Zstd `.rowlpkg` Reader (`include/rowl/vfs/rowlpkg_reader.hpp`)
```cpp
#pragma once
#include "vfs.hpp"
#include <unordered_map>
#include <fstream>

namespace Rowl::VFS {

    struct PackageEntry {
        uint64_t offset;
        uint64_t compressed_size;
        uint64_t uncompressed_size;
        uint32_t flags;
    };

    class RowlPkgDataSource : public IDataSource {
    public:
        explicit RowlPkgDataSource(const std::string& pkg_filepath);
        ~RowlPkgDataSource() override;

        bool exists(const std::string& path) override;
        std::vector<uint8_t> read(const std::string& path) override;

    private:
        void load_index_table();

        std::string m_filepath;
        std::ifstream m_file_stream;
        std::unordered_map<std::string, PackageEntry> m_index_table;
    };
}
```

---

## 🏃‍♂️ 4. STEP-BY-STEP EXECUTION & BUILD COMMANDS

1. **Configure CMake:**
   ```bash
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   ```
2. **Compile Engine Core:**
   ```bash
   cmake --build . --parallel 8
   ```
3. **Run Unit Tests:**
   ```bash
   ctest --output-on-failure
   ```

---

## ✅ PHASE 1 ACCEPTANCE CRITERIA
- [ ] CMake builds successfully on Linux CachyOS (`gcc 14+`) and Windows MSVC 2022.
- [ ] `VFSManager` resolves asset priority correctly (Mods > Loose > Package).
- [ ] Zstd decompression unpacks 100MB of test assets with zero memory leaks (verified via Valgrind / AddressSanitizer).
- [ ] SDL3 window opens cleanly at 1920x1080 virtual resolution.
