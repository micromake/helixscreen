# Spoolman Server Setup Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow users to configure a Spoolman server from the touchscreen, using a shared `helixscreen.conf` Moonraker include file.

**Architecture:** A new `MoonrakerConfigManager` provides pure functions for INI-style section manipulation and async workflows for downloading/uploading config via Moonraker's file transfer API. The Spoolman settings panel gains server setup/status UI. The timelapse installer is refactored to use the same manager.

**Tech Stack:** C++17, LVGL 9.5, libhv HTTP client, Moonraker file transfer API, Catch2 tests

**Spec:** `docs/devel/plans/2026-03-14-spoolman-server-setup-design.md`

---

## Key Reference Files

| File | Purpose |
|------|---------|
| `include/moonraker_file_transfer_api.h` | `download_file()` / `upload_file()` signatures |
| `src/api/moonraker_file_transfer_api.cpp` | HTTP file operations via libhv `requests::` |
| `src/ui/ui_overlay_timelapse_install.cpp` | Current direct-to-moonraker.conf pattern (to refactor) |
| `src/ui/ui_spoolman_overlay.cpp` | Spoolman settings overlay (to extend) |
| `include/ui_spoolman_overlay.h` | SpoolmanOverlay class definition |
| `ui_xml/spoolman_settings.xml` | Settings panel XML layout |
| `src/api/moonraker_spoolman_api.cpp:120-146` | `get_spoolman_status()` — returns `(bool connected, int active_spool_id)` |
| `src/api/moonraker_discovery_sequence.cpp:212-238` | Spoolman detection via `server.info` components |
| `src/printer/printer_capabilities_state.cpp:39` | `printer_has_spoolman` subject (0/1) |
| `src/api/moonraker_api_internal.h:430` | 404 → `MoonrakerErrorType::FILE_NOT_FOUND` |

## Important Patterns

- **Makefile auto-discovers sources** — no manual registration needed for new `.cpp` files in `src/` subdirs
- **XML components require registration** — new XML files need `lv_xml_component_register_from_file()` in `main.cpp` (Lesson L014). Modifications to existing XML files do NOT need re-registration.
- **No XML rebuild needed** — XML loads at runtime, only rebuild for C++ changes (Lesson L031)
- **Event callbacks via XML** — `<event_cb trigger="clicked" callback="name"/>` + `lv_xml_register_event_cb()`
- **Thread safety** — use `helix::ui::queue_update()` for LVGL calls from async callbacks
- **Alive guard pattern** — capture `std::shared_ptr<bool>` to detect destroyed objects
- **Never wrap product names in lv_tr()** — "Spoolman" is a product name, do NOT translate (Lesson L070). Sentences containing product names ARE translatable.

---

## Chunk 1: MoonrakerConfigManager Pure Functions

### Task 1: `has_section()` — TDD

**Files:**
- Create: `include/moonraker_config_manager.h`
- Create: `src/system/moonraker_config_manager.cpp`
- Create: `tests/unit/test_moonraker_config_manager.cpp`

- [ ] **Step 1: Write failing tests for `has_section()`**

Create `tests/unit/test_moonraker_config_manager.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_config_manager.h"

#include "../catch_amalgamated.hpp"

using helix::MoonrakerConfigManager;

// ============================================================================
// has_section
// ============================================================================

TEST_CASE("has_section: detects existing section", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n[spoolman]\nserver: http://1.2.3.4:7912\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == true);
}

TEST_CASE("has_section: returns false when missing", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n[authorization]\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == false);
}

TEST_CASE("has_section: returns false for empty content", "[config_manager]") {
    REQUIRE(MoonrakerConfigManager::has_section("", "spoolman") == false);
}

TEST_CASE("has_section: ignores commented-out sections", "[config_manager]") {
    std::string content = "[server]\n# [spoolman]\n[authorization]\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == false);
}

TEST_CASE("has_section: handles section with spaces like update_manager timelapse",
          "[config_manager]") {
    std::string content = "[timelapse]\n\n[update_manager timelapse]\ntype: git_repo\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "update_manager timelapse") == true);
    REQUIRE(MoonrakerConfigManager::has_section(content, "timelapse") == true);
}

TEST_CASE("has_section: does not match partial names", "[config_manager]") {
    std::string content = "[spoolman_extra]\nfoo: bar\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == false);
}

TEST_CASE("has_section: handles trailing whitespace and Windows line endings", "[config_manager]") {
    std::string content = "[server]\r\n[spoolman]   \r\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == true);
}

TEST_CASE("has_section: handles leading whitespace", "[config_manager]") {
    std::string content = "  [spoolman]\nserver: http://1.2.3.4:7912\n";
    REQUIRE(MoonrakerConfigManager::has_section(content, "spoolman") == true);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`
Expected: Compilation error — `moonraker_config_manager.h` not found

- [ ] **Step 3: Write minimal header and implementation**

Create `include/moonraker_config_manager.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace helix {

/**
 * @brief Manages helixscreen.conf — a Moonraker include file for HelixScreen-managed config.
 *
 * Static pure functions for INI-style section manipulation (testable without async).
 * Multiple features (Spoolman, timelapse) share one helixscreen.conf file.
 */
class MoonrakerConfigManager {
  public:
    /// Check if a section header exists (e.g. "spoolman", "update_manager timelapse")
    static bool has_section(const std::string& content, const std::string& section_name);

    /// Add a section with key-value pairs. Returns modified content.
    /// If section already exists, returns content unchanged (idempotent).
    static std::string add_section(
        const std::string& content, const std::string& section_name,
        const std::vector<std::pair<std::string, std::string>>& entries,
        const std::string& comment = "");

    /// Remove a section and its entries (plus preceding comment line if present).
    static std::string remove_section(const std::string& content,
                                      const std::string& section_name);

    /// Check if moonraker.conf content has [include helixscreen.conf]
    static bool has_include_line(const std::string& moonraker_content);

    /// Add [include helixscreen.conf] line before first section. Returns modified content.
    static std::string add_include_line(const std::string& moonraker_content);

    /// Extract a key's value from a specific section. Returns empty string if not found.
    static std::string get_section_value(const std::string& content,
                                         const std::string& section_name,
                                         const std::string& key);
};

} // namespace helix
```

Create `src/system/moonraker_config_manager.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_config_manager.h"

#include <sstream>

namespace helix {

bool MoonrakerConfigManager::has_section(const std::string& content,
                                         const std::string& section_name) {
    std::string target = "[" + section_name + "]";
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        std::string trimmed = line.substr(start);
        size_t end = trimmed.find_last_not_of(" \t\r");
        if (end != std::string::npos)
            trimmed = trimmed.substr(0, end + 1);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;
        if (trimmed == target)
            return true;
    }
    return false;
}

} // namespace helix
```

- [ ] **Step 4: Run tests to verify `has_section()` passes**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`
Expected: All `has_section` tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/moonraker_config_manager.h src/system/moonraker_config_manager.cpp tests/unit/test_moonraker_config_manager.cpp
git commit -m "feat(config): add MoonrakerConfigManager::has_section with tests"
```

---

### Task 2: `add_section()` — TDD

**Files:**
- Modify: `tests/unit/test_moonraker_config_manager.cpp`
- Modify: `src/system/moonraker_config_manager.cpp`

- [ ] **Step 1: Write failing tests for `add_section()`**

Append to `tests/unit/test_moonraker_config_manager.cpp`:

```cpp
// ============================================================================
// add_section
// ============================================================================

TEST_CASE("add_section: appends section with entries", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n";
    auto result = MoonrakerConfigManager::add_section(
        content, "spoolman", {{"server", "http://192.168.1.100:7912"}},
        "Spoolman - added by HelixScreen");

    REQUIRE(result.find("[spoolman]") != std::string::npos);
    REQUIRE(result.find("server: http://192.168.1.100:7912") != std::string::npos);
    REQUIRE(result.find("# Spoolman - added by HelixScreen") != std::string::npos);
    REQUIRE(result.find("[server]") != std::string::npos);
}

TEST_CASE("add_section: idempotent — does not duplicate existing section", "[config_manager]") {
    std::string content = "[spoolman]\nserver: http://1.2.3.4:7912\n";
    auto result = MoonrakerConfigManager::add_section(
        content, "spoolman", {{"server", "http://5.6.7.8:7912"}});

    REQUIRE(result == content);
}

TEST_CASE("add_section: handles empty content", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_section(
        "", "spoolman", {{"server", "http://1.2.3.4:7912"}});

    REQUIRE(result.find("[spoolman]") != std::string::npos);
    REQUIRE(result.find("server: http://1.2.3.4:7912") != std::string::npos);
}

TEST_CASE("add_section: multiple entries preserved in order", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_section(
        "", "update_manager timelapse",
        {{"type", "git_repo"}, {"primary_branch", "main"}, {"path", "~/moonraker-timelapse"}});

    REQUIRE(result.find("[update_manager timelapse]") != std::string::npos);
    auto type_pos = result.find("type: git_repo");
    auto branch_pos = result.find("primary_branch: main");
    auto path_pos = result.find("path: ~/moonraker-timelapse");
    REQUIRE(type_pos != std::string::npos);
    REQUIRE(branch_pos != std::string::npos);
    REQUIRE(path_pos != std::string::npos);
    REQUIRE(type_pos < branch_pos);
    REQUIRE(branch_pos < path_pos);
}

TEST_CASE("add_section: no comment when comment is empty", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_section(
        "", "spoolman", {{"server", "http://1.2.3.4:7912"}}, "");

    auto section_pos = result.find("[spoolman]");
    REQUIRE(section_pos != std::string::npos);
    std::string before = result.substr(0, section_pos);
    REQUIRE(before.find('#') == std::string::npos);
}

TEST_CASE("add_section: section with no entries (header only)", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_section(
        "", "timelapse", {}, "Timelapse - added by HelixScreen");

    REQUIRE(result.find("[timelapse]") != std::string::npos);
    REQUIRE(result.find("# Timelapse - added by HelixScreen") != std::string::npos);
    REQUIRE(MoonrakerConfigManager::has_section(result, "timelapse") == true);
}

TEST_CASE("add_section: result passes has_section check", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_section(
        "[server]\n", "spoolman", {{"server", "http://1.2.3.4:7912"}});

    REQUIRE(MoonrakerConfigManager::has_section(result, "spoolman") == true);
    REQUIRE(MoonrakerConfigManager::has_section(result, "server") == true);
}
```

- [ ] **Step 2: Run tests — should fail (linker error, function not defined)**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`

- [ ] **Step 3: Implement `add_section()`**

Add to `src/system/moonraker_config_manager.cpp`:

```cpp
std::string MoonrakerConfigManager::add_section(
    const std::string& content, const std::string& section_name,
    const std::vector<std::pair<std::string, std::string>>& entries,
    const std::string& comment) {
    if (has_section(content, section_name)) {
        return content;
    }

    std::string result = content;
    if (!result.empty() && result.back() != '\n') {
        result += '\n';
    }
    result += '\n';

    if (!comment.empty()) {
        result += "# " + comment + '\n';
    }

    result += '[' + section_name + "]\n";

    for (const auto& [key, value] : entries) {
        result += key + ": " + value + '\n';
    }

    return result;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/system/moonraker_config_manager.cpp tests/unit/test_moonraker_config_manager.cpp
git commit -m "feat(config): add MoonrakerConfigManager::add_section with tests"
```

---

### Task 3: `remove_section()` — TDD

**Files:**
- Modify: `tests/unit/test_moonraker_config_manager.cpp`
- Modify: `src/system/moonraker_config_manager.cpp`

- [ ] **Step 1: Write failing tests for `remove_section()`**

Append to `tests/unit/test_moonraker_config_manager.cpp`:

```cpp
// ============================================================================
// remove_section
// ============================================================================

TEST_CASE("remove_section: removes section and its entries", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n"
                          "# Spoolman - added by HelixScreen\n"
                          "[spoolman]\nserver: http://1.2.3.4:7912\n";
    auto result = MoonrakerConfigManager::remove_section(content, "spoolman");

    REQUIRE(MoonrakerConfigManager::has_section(result, "spoolman") == false);
    REQUIRE(result.find("Spoolman - added by HelixScreen") == std::string::npos);
    REQUIRE(MoonrakerConfigManager::has_section(result, "server") == true);
}

TEST_CASE("remove_section: removes section between other sections", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n"
                          "[spoolman]\nserver: http://1.2.3.4:7912\n\n"
                          "[timelapse]\n";
    auto result = MoonrakerConfigManager::remove_section(content, "spoolman");

    REQUIRE(MoonrakerConfigManager::has_section(result, "spoolman") == false);
    REQUIRE(MoonrakerConfigManager::has_section(result, "server") == true);
    REQUIRE(MoonrakerConfigManager::has_section(result, "timelapse") == true);
}

TEST_CASE("remove_section: no-op when section does not exist", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n";
    auto result = MoonrakerConfigManager::remove_section(content, "spoolman");

    REQUIRE(result == content);
}

TEST_CASE("remove_section: handles section at end of file", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n[spoolman]\nserver: http://1.2.3.4:7912\n";
    auto result = MoonrakerConfigManager::remove_section(content, "spoolman");

    REQUIRE(MoonrakerConfigManager::has_section(result, "spoolman") == false);
    REQUIRE(MoonrakerConfigManager::has_section(result, "server") == true);
}

TEST_CASE("remove_section: removes section with spaces in name", "[config_manager]") {
    std::string content = "[timelapse]\n\n"
                          "[update_manager timelapse]\ntype: git_repo\npath: ~/timelapse\n";
    auto result = MoonrakerConfigManager::remove_section(content, "update_manager timelapse");

    REQUIRE(MoonrakerConfigManager::has_section(result, "update_manager timelapse") == false);
    REQUIRE(MoonrakerConfigManager::has_section(result, "timelapse") == true);
}

TEST_CASE("remove_section: add then remove returns to original-like state", "[config_manager]") {
    std::string original = "[server]\nhost: 0.0.0.0\n";
    auto added = MoonrakerConfigManager::add_section(
        original, "spoolman", {{"server", "http://1.2.3.4:7912"}}, "Spoolman");
    REQUIRE(MoonrakerConfigManager::has_section(added, "spoolman") == true);

    auto removed = MoonrakerConfigManager::remove_section(added, "spoolman");
    REQUIRE(MoonrakerConfigManager::has_section(removed, "spoolman") == false);
    REQUIRE(MoonrakerConfigManager::has_section(removed, "server") == true);
}
```

- [ ] **Step 2: Run tests — should fail**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`

- [ ] **Step 3: Implement `remove_section()`**

Add to `src/system/moonraker_config_manager.cpp`:

```cpp
std::string MoonrakerConfigManager::remove_section(const std::string& content,
                                                    const std::string& section_name) {
    if (!has_section(content, section_name)) {
        return content;
    }

    std::string target = "[" + section_name + "]";
    std::istringstream stream(content);
    std::string line;
    std::string result;
    bool skipping = false;
    std::string pending_comment;

    while (std::getline(stream, line)) {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos)
            trimmed = trimmed.substr(start);
        size_t end = trimmed.find_last_not_of(" \t\r");
        if (end != std::string::npos)
            trimmed = trimmed.substr(0, end + 1);

        if (trimmed == target) {
            skipping = true;
            pending_comment.clear();
            continue;
        }

        if (skipping) {
            if (!trimmed.empty() && trimmed[0] == '[') {
                skipping = false;
                result += line + '\n';
            }
            continue;
        }

        if (!trimmed.empty() && trimmed[0] == '#') {
            pending_comment += line + '\n';
            continue;
        }

        if (!pending_comment.empty()) {
            result += pending_comment;
            pending_comment.clear();
        }

        result += line + '\n';
    }

    if (!pending_comment.empty() && !skipping) {
        result += pending_comment;
    }

    // Collapse excessive trailing blank lines (max 1 trailing newline pair)
    while (result.size() >= 3 && result[result.size() - 1] == '\n' &&
           result[result.size() - 2] == '\n' && result[result.size() - 3] == '\n') {
        result.pop_back();
    }

    return result;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/system/moonraker_config_manager.cpp tests/unit/test_moonraker_config_manager.cpp
git commit -m "feat(config): add MoonrakerConfigManager::remove_section with tests"
```

---

### Task 4: Include Line + `get_section_value()` — TDD

**Files:**
- Modify: `tests/unit/test_moonraker_config_manager.cpp`
- Modify: `src/system/moonraker_config_manager.cpp`

- [ ] **Step 1: Write failing tests for include line functions and `get_section_value()`**

Append to `tests/unit/test_moonraker_config_manager.cpp`:

```cpp
// ============================================================================
// has_include_line / add_include_line
// ============================================================================

TEST_CASE("has_include_line: detects existing include", "[config_manager]") {
    std::string content = "[server]\n\n[include helixscreen.conf]\n\n[authorization]\n";
    REQUIRE(MoonrakerConfigManager::has_include_line(content) == true);
}

TEST_CASE("has_include_line: returns false when missing", "[config_manager]") {
    std::string content = "[server]\n[authorization]\n";
    REQUIRE(MoonrakerConfigManager::has_include_line(content) == false);
}

TEST_CASE("has_include_line: ignores commented include", "[config_manager]") {
    std::string content = "[server]\n# [include helixscreen.conf]\n";
    REQUIRE(MoonrakerConfigManager::has_include_line(content) == false);
}

TEST_CASE("add_include_line: adds include before first section", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n\n[authorization]\n";
    auto result = MoonrakerConfigManager::add_include_line(content);

    REQUIRE(MoonrakerConfigManager::has_include_line(result) == true);
    auto include_pos = result.find("[include helixscreen.conf]");
    auto server_pos = result.find("[server]");
    REQUIRE(include_pos < server_pos);
}

TEST_CASE("add_include_line: idempotent", "[config_manager]") {
    std::string content = "[include helixscreen.conf]\n\n[server]\n";
    auto result = MoonrakerConfigManager::add_include_line(content);
    REQUIRE(result == content);
}

TEST_CASE("add_include_line: handles empty content", "[config_manager]") {
    auto result = MoonrakerConfigManager::add_include_line("");
    REQUIRE(MoonrakerConfigManager::has_include_line(result) == true);
}

TEST_CASE("add_include_line: inserts after leading comments but before first section",
          "[config_manager]") {
    std::string content = "# Generated by Moonraker\n# Do not edit\n\n[server]\nhost: 0.0.0.0\n";
    auto result = MoonrakerConfigManager::add_include_line(content);

    REQUIRE(MoonrakerConfigManager::has_include_line(result) == true);
    // Include should appear after comments but before [server]
    auto include_pos = result.find("[include helixscreen.conf]");
    auto server_pos = result.find("[server]");
    auto comment_pos = result.find("# Generated");
    REQUIRE(comment_pos < include_pos);
    REQUIRE(include_pos < server_pos);
}

// ============================================================================
// get_section_value
// ============================================================================

TEST_CASE("get_section_value: extracts value from section", "[config_manager]") {
    std::string content = "[spoolman]\nserver: http://1.2.3.4:7912\n";
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "spoolman", "server") ==
            "http://1.2.3.4:7912");
}

TEST_CASE("get_section_value: returns empty for missing key", "[config_manager]") {
    std::string content = "[spoolman]\nserver: http://1.2.3.4:7912\n";
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "spoolman", "port").empty());
}

TEST_CASE("get_section_value: returns empty for missing section", "[config_manager]") {
    std::string content = "[server]\nhost: 0.0.0.0\n";
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "spoolman", "server").empty());
}

TEST_CASE("get_section_value: does not cross section boundaries", "[config_manager]") {
    std::string content = "[spoolman]\nserver: http://a\n\n[other]\nserver: http://b\n";
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "spoolman", "server") ==
            "http://a");
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "other", "server") == "http://b");
}

TEST_CASE("get_section_value: handles whitespace around colon", "[config_manager]") {
    std::string content = "[spoolman]\nserver :  http://1.2.3.4:7912  \n";
    REQUIRE(MoonrakerConfigManager::get_section_value(content, "spoolman", "server") ==
            "http://1.2.3.4:7912");
}
```

- [ ] **Step 2: Run tests — should fail**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`

- [ ] **Step 3: Implement include line functions and `get_section_value()`**

Add to `src/system/moonraker_config_manager.cpp`:

```cpp
bool MoonrakerConfigManager::has_include_line(const std::string& moonraker_content) {
    return has_section(moonraker_content, "include helixscreen.conf");
}

std::string MoonrakerConfigManager::add_include_line(const std::string& moonraker_content) {
    if (has_include_line(moonraker_content)) {
        return moonraker_content;
    }

    std::string include_line = "[include helixscreen.conf]\n\n";

    if (moonraker_content.empty()) {
        return include_line;
    }

    // Find the first non-comment section header and insert before it
    std::istringstream stream(moonraker_content);
    std::string line;
    size_t pos = 0;
    while (std::getline(stream, line)) {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos)
            trimmed = trimmed.substr(start);
        if (!trimmed.empty() && trimmed[0] == '[' && trimmed[0] != '#') {
            return moonraker_content.substr(0, pos) + include_line +
                   moonraker_content.substr(pos);
        }
        pos += line.size() + 1;
    }

    return include_line + moonraker_content;
}

std::string MoonrakerConfigManager::get_section_value(const std::string& content,
                                                       const std::string& section_name,
                                                       const std::string& key) {
    std::string target = "[" + section_name + "]";
    std::istringstream stream(content);
    std::string line;
    bool in_section = false;

    while (std::getline(stream, line)) {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        trimmed = trimmed.substr(start);
        size_t end = trimmed.find_last_not_of(" \t\r");
        if (end != std::string::npos)
            trimmed = trimmed.substr(0, end + 1);

        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        if (trimmed[0] == '[') {
            in_section = (trimmed == target);
            continue;
        }

        if (in_section) {
            auto colon_pos = trimmed.find(':');
            if (colon_pos != std::string::npos) {
                std::string k = trimmed.substr(0, colon_pos);
                size_t k_end = k.find_last_not_of(" \t");
                if (k_end != std::string::npos)
                    k = k.substr(0, k_end + 1);

                if (k == key) {
                    std::string v = trimmed.substr(colon_pos + 1);
                    size_t v_start = v.find_first_not_of(" \t");
                    if (v_start == std::string::npos)
                        return "";
                    v = v.substr(v_start);
                    size_t v_end = v.find_last_not_of(" \t");
                    if (v_end != std::string::npos)
                        v = v.substr(0, v_end + 1);
                    return v;
                }
            }
        }
    }

    return "";
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[config_manager]" -v`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/system/moonraker_config_manager.cpp tests/unit/test_moonraker_config_manager.cpp
git commit -m "feat(config): add include line management and get_section_value"
```

---

### Chunk 1 Review Checkpoint

**Review criteria:**
- All pure functions have comprehensive edge-case tests
- `add_section()` is idempotent
- `remove_section()` preserves other sections and cleans up comments
- `has_include_line()` / `add_include_line()` handle edge cases (empty, already present)
- `get_section_value()` respects section boundaries
- No async code or LVGL dependencies — pure string manipulation
- All tests pass: `make test && ./build/bin/helix-tests "[config_manager]" -v`

---

## Chunk 2: Spoolman Validation Helpers

### Task 5: SpoolmanSetup Static Helpers — TDD

**Files:**
- Create: `include/ui_spoolman_setup.h`
- Create: `src/ui/ui_spoolman_setup.cpp`
- Create: `tests/unit/test_spoolman_setup.cpp`

Pure validation and URL construction functions — no async, no LVGL.

- [ ] **Step 1: Write failing tests**

Create `tests/unit/test_spoolman_setup.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_setup.h"

#include "../catch_amalgamated.hpp"

using helix::ui::SpoolmanSetup;

// ============================================================================
// validate_port
// ============================================================================

TEST_CASE("validate_port: valid ports", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::validate_port("7912") == true);
    REQUIRE(SpoolmanSetup::validate_port("1") == true);
    REQUIRE(SpoolmanSetup::validate_port("65535") == true);
    REQUIRE(SpoolmanSetup::validate_port("80") == true);
}

TEST_CASE("validate_port: invalid ports", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::validate_port("") == false);
    REQUIRE(SpoolmanSetup::validate_port("0") == false);
    REQUIRE(SpoolmanSetup::validate_port("65536") == false);
    REQUIRE(SpoolmanSetup::validate_port("abc") == false);
    REQUIRE(SpoolmanSetup::validate_port("-1") == false);
    REQUIRE(SpoolmanSetup::validate_port("99999") == false);
}

// ============================================================================
// validate_host
// ============================================================================

TEST_CASE("validate_host: valid hosts", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::validate_host("192.168.1.100") == true);
    REQUIRE(SpoolmanSetup::validate_host("spoolman.local") == true);
    REQUIRE(SpoolmanSetup::validate_host("my-server") == true);
}

TEST_CASE("validate_host: invalid hosts", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::validate_host("") == false);
    REQUIRE(SpoolmanSetup::validate_host("   ") == false);
}

// ============================================================================
// build_url / build_probe_url
// ============================================================================

TEST_CASE("build_url: constructs correct URL", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::build_url("192.168.1.100", "7912") ==
            "http://192.168.1.100:7912");
}

TEST_CASE("build_probe_url: appends health endpoint", "[spoolman_setup]") {
    REQUIRE(SpoolmanSetup::build_probe_url("192.168.1.100", "7912") ==
            "http://192.168.1.100:7912/api/v1/health");
}

// ============================================================================
// build_spoolman_config_entries
// ============================================================================

TEST_CASE("build_spoolman_config_entries: returns correct entry", "[spoolman_setup]") {
    auto entries = SpoolmanSetup::build_spoolman_config_entries("192.168.1.100", "7912");
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].first == "server");
    REQUIRE(entries[0].second == "http://192.168.1.100:7912");
}

// ============================================================================
// parse_url_components
// ============================================================================

TEST_CASE("parse_url_components: extracts host and port", "[spoolman_setup]") {
    auto [host, port] = SpoolmanSetup::parse_url_components("http://192.168.1.100:7912");
    REQUIRE(host == "192.168.1.100");
    REQUIRE(port == "7912");
}

TEST_CASE("parse_url_components: handles URL without port", "[spoolman_setup]") {
    auto [host, port] = SpoolmanSetup::parse_url_components("http://spoolman.local");
    REQUIRE(host == "spoolman.local");
    REQUIRE(port == "7912");
}

TEST_CASE("parse_url_components: handles empty URL", "[spoolman_setup]") {
    auto [host, port] = SpoolmanSetup::parse_url_components("");
    REQUIRE(host.empty());
    REQUIRE(port == "7912");
}

TEST_CASE("parse_url_components: handles URL with trailing path", "[spoolman_setup]") {
    auto [host, port] = SpoolmanSetup::parse_url_components("http://192.168.1.100:7912/api/v1");
    REQUIRE(host == "192.168.1.100");
    REQUIRE(port == "7912");
}

// ============================================================================
// Round-trip: parse_url_components(build_url(host, port))
// ============================================================================

TEST_CASE("round-trip: build_url then parse_url_components", "[spoolman_setup]") {
    std::string host = "192.168.1.50";
    std::string port = "8080";
    auto url = SpoolmanSetup::build_url(host, port);
    auto [parsed_host, parsed_port] = SpoolmanSetup::parse_url_components(url);
    REQUIRE(parsed_host == host);
    REQUIRE(parsed_port == port);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[spoolman_setup]" -v`
Expected: Compilation error

- [ ] **Step 3: Implement SpoolmanSetup**

Create `include/ui_spoolman_setup.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace helix::ui {

static constexpr const char* DEFAULT_SPOOLMAN_PORT = "7912";

/**
 * @brief Spoolman server setup — validation, URL construction, config helpers.
 * Pure static functions for testability.
 */
class SpoolmanSetup {
  public:
    static bool validate_port(const std::string& port);
    static bool validate_host(const std::string& host);
    static std::string build_url(const std::string& host, const std::string& port);
    static std::string build_probe_url(const std::string& host, const std::string& port);
    static std::vector<std::pair<std::string, std::string>>
    build_spoolman_config_entries(const std::string& host, const std::string& port);
    static std::pair<std::string, std::string> parse_url_components(const std::string& url);
};

} // namespace helix::ui
```

Create `src/ui/ui_spoolman_setup.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_setup.h"

#include <charconv>

namespace helix::ui {

bool SpoolmanSetup::validate_port(const std::string& port) {
    if (port.empty()) return false;
    int value = 0;
    auto [ptr, ec] = std::from_chars(port.data(), port.data() + port.size(), value);
    if (ec != std::errc() || ptr != port.data() + port.size()) return false;
    return value >= 1 && value <= 65535;
}

bool SpoolmanSetup::validate_host(const std::string& host) {
    if (host.empty()) return false;
    return host.find_first_not_of(" \t") != std::string::npos;
}

std::string SpoolmanSetup::build_url(const std::string& host, const std::string& port) {
    return "http://" + host + ":" + port;
}

std::string SpoolmanSetup::build_probe_url(const std::string& host, const std::string& port) {
    return build_url(host, port) + "/api/v1/health";
}

std::vector<std::pair<std::string, std::string>>
SpoolmanSetup::build_spoolman_config_entries(const std::string& host, const std::string& port) {
    return {{"server", build_url(host, port)}};
}

std::pair<std::string, std::string>
SpoolmanSetup::parse_url_components(const std::string& url) {
    if (url.empty()) return {"", DEFAULT_SPOOLMAN_PORT};

    std::string stripped = url;
    auto proto_end = stripped.find("://");
    if (proto_end != std::string::npos)
        stripped = stripped.substr(proto_end + 3);

    auto path_start = stripped.find('/');
    if (path_start != std::string::npos)
        stripped = stripped.substr(0, path_start);

    auto colon = stripped.rfind(':');
    if (colon != std::string::npos) {
        std::string host = stripped.substr(0, colon);
        std::string port = stripped.substr(colon + 1);
        if (!host.empty() && validate_port(port))
            return {host, port};
    }

    return {stripped, DEFAULT_SPOOLMAN_PORT};
}

} // namespace helix::ui
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[spoolman_setup]" -v`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/ui_spoolman_setup.h src/ui/ui_spoolman_setup.cpp tests/unit/test_spoolman_setup.cpp
git commit -m "feat(spoolman): add server setup validation and URL helpers"
```

---

### Chunk 2 Review Checkpoint

**Review criteria:**
- Port validation covers edge cases (0, 65536, non-numeric, negative, empty)
- Host validation rejects empty/whitespace-only
- URL construction is correct
- `parse_url_components()` handles all URL formats (with/without protocol, port, path)
- Round-trip: `parse_url_components(build_url(host, port))` returns original values
- No LVGL or async dependencies
- All tests pass: `make test && ./build/bin/helix-tests "[spoolman_setup]" -v`

---

## Chunk 3: UI Integration

### Task 6: Spoolman Settings XML — Server Setup Section

**Files:**
- Modify: `ui_xml/spoolman_settings.xml`

- [ ] **Step 1: Add server setup card and server status card to XML**

The `printer_has_spoolman` subject (already registered globally at `src/printer/printer_capabilities_state.cpp:39`) controls visibility via `bind_flag_if_eq`:
- Setup card: visible when `printer_has_spoolman == 0`
- Status card: visible when `printer_has_spoolman == 1`
- Existing sync/interval cards: also hide when `printer_has_spoolman == 0`

Edit `ui_xml/spoolman_settings.xml` — insert after the `<text_small name="description">` and BEFORE the `sync_card`:

```xml
      <!-- ================================================================== -->
      <!-- Server Setup (shown when Spoolman NOT configured)                   -->
      <!-- ================================================================== -->
      <ui_card name="setup_card"
               width="100%" height="content" style_radius="#border_radius" style_pad_all="#space_md"
               flex_flow="column" style_pad_gap="#space_sm">
        <bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="1"/>
        <text_body text="Server Setup" translation_tag="Server Setup"/>
        <text_small text="Enter the IP address and port of your Spoolman server."
                    translation_tag="Enter the IP address and port of your Spoolman server."
                    style_text_color="#text_muted"/>
        <!-- Host input -->
        <lv_obj width="100%" height="content" style_pad_all="0" flex_flow="column"
                style_pad_gap="#space_xs" scrollable="false">
          <text_small text="IP Address / Hostname" translation_tag="IP Address / Hostname"/>
          <text_input name="spoolman_host_input"
                      width="100%" placeholder_text="192.168.1.100" max_length="64"/>
        </lv_obj>
        <!-- Port input -->
        <lv_obj width="100%" height="content" style_pad_all="0" flex_flow="column"
                style_pad_gap="#space_xs" scrollable="false">
          <text_small text="Port" translation_tag="Port"/>
          <text_input name="spoolman_port_input"
                      width="100%" placeholder_text="7912" max_length="5"
                      input_mode="number" keyboard_hint="numeric" text="7912"/>
        </lv_obj>
        <!-- Status text (for errors/progress) -->
        <text_small name="setup_status_text" width="100%" text=""
                    style_text_color="#text_muted"/>
        <!-- Connect button -->
        <ui_button name="connect_btn" variant="primary" width="100%"
                   label="Connect" label_tag="Connect">
          <event_cb trigger="clicked" callback="on_spoolman_connect_clicked"/>
        </ui_button>
      </ui_card>

      <!-- ================================================================== -->
      <!-- Server Status (shown when Spoolman IS configured)                   -->
      <!-- ================================================================== -->
      <ui_card name="status_card"
               width="100%" height="content" style_radius="#border_radius" style_pad_all="#space_md"
               flex_flow="column" style_pad_gap="#space_sm">
        <bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="0"/>
        <lv_obj width="100%" height="content" style_pad_all="0" flex_flow="row"
                style_flex_main_place="space_between" style_flex_cross_place="center" scrollable="false">
          <lv_obj height="content" style_pad_all="0" flex_flow="column"
                  style_pad_gap="#space_xs" scrollable="false" flex_grow="1">
            <text_body text="Spoolman Server"/>
            <text_small name="server_url_text" text="Connected" translation_tag="Connected"
                        style_text_color="#text_muted"/>
          </lv_obj>
          <icon src="check-circle" size="sm" color="#success"/>
        </lv_obj>
        <lv_obj width="100%" height="content" style_pad_all="0" flex_flow="row"
                style_pad_gap="#space_sm" scrollable="false">
          <ui_button name="change_btn" variant="outline" flex_grow="1"
                     label="Change" label_tag="Change">
            <event_cb trigger="clicked" callback="on_spoolman_change_clicked"/>
          </ui_button>
          <ui_button name="remove_btn" variant="outline" flex_grow="1"
                     label="Remove" label_tag="Remove">
            <event_cb trigger="clicked" callback="on_spoolman_remove_clicked"/>
          </ui_button>
        </lv_obj>
      </ui_card>
```

Also add `bind_flag_if_eq` as the first child of each existing card to hide when Spoolman is not configured:

- In `sync_card`: add `<bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="0"/>`
- In `interval_card`: add `<bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="0"/>`
- In `info_note`: add `<bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="0"/>`

Note: "Spoolman Server" text should NOT be wrapped in `translation_tag` — "Spoolman" is a product name (Lesson L070). The label text is set statically since it's a proper name.

- [ ] **Step 2: Test XML loads without errors (no rebuild needed — Lesson L031)**

Run: `./build/bin/helix-screen --test -vv` and check logs for XML parse errors around `spoolman_settings`.

- [ ] **Step 3: Commit**

```bash
git add ui_xml/spoolman_settings.xml
git commit -m "feat(spoolman): add server setup and status sections to settings XML"
```

---

### Task 7a: SpoolmanOverlay — Widget Setup + Connect Flow

**Files:**
- Modify: `include/ui_spoolman_overlay.h`
- Modify: `src/ui/ui_spoolman_overlay.cpp`

Header/member setup, widget lookups, callback registration, connect button + probe.

**Reference files to study before implementing:**
- `src/ui/ui_overlay_timelapse_install.cpp` — alive guard pattern, async flow, restart + verify
- `src/api/moonraker_file_transfer_api.cpp` — `requests::get()`, `requests::request()` patterns
- `include/ui_event_safety.h` — `LVGL_SAFE_EVENT_CB_BEGIN` / `END` / `RETURN` macros (already included in the .cpp)
- `include/ui_emergency_stop.h:136` — `suppress_recovery_dialog(uint32_t duration_ms)`
- `include/ui_toast_manager.h` — `ToastManager::instance().show(severity, msg, duration)`

- [ ] **Step 1: Add new includes to the .cpp file**

```cpp
#include "hv/requests.h"
#include "moonraker_config_manager.h"
#include "moonraker_types.h"
#include "ui_emergency_stop.h"
#include "ui_spoolman_setup.h"
#include "ui_toast_manager.h"

#include <thread>
```

- [ ] **Step 2: Add new members and method declarations to header**

Add to private section of `SpoolmanOverlay` in `include/ui_spoolman_overlay.h`:

```cpp
    // === Server Setup Methods ===
    void probe_spoolman_server(const std::string& host, const std::string& port);
    void configure_spoolman(const std::string& host, const std::string& port);
    void finish_configure(const std::string& helix_content,
                          const std::vector<std::pair<std::string, std::string>>& entries);
    void ensure_moonraker_include();
    void restart_and_verify();
    void verify_spoolman_connected();
    void remove_spoolman_config();
    void update_server_url_display();
    void set_setup_status(const char* text, bool is_error = false);

    // === Server Setup Callbacks ===
    static void on_connect_clicked(lv_event_t* e);
    static void on_change_clicked(lv_event_t* e);
    static void on_remove_clicked(lv_event_t* e);

    // === Server Setup Widgets ===
    lv_obj_t* host_input_ = nullptr;
    lv_obj_t* port_input_ = nullptr;
    lv_obj_t* setup_status_text_ = nullptr;
    lv_obj_t* server_url_text_ = nullptr;
    lv_obj_t* connect_btn_ = nullptr;
    lv_obj_t* setup_card_ = nullptr;
    lv_obj_t* status_card_ = nullptr;

    std::shared_ptr<bool> alive_guard_ = std::make_shared<bool>(true);
```

Add required includes to header:

```cpp
#include <memory>
#include <vector>
```

- [ ] **Step 3: Register new callbacks in `register_callbacks()`**

Add to `register_callbacks()`:

```cpp
    lv_xml_register_event_cb(nullptr, "on_spoolman_connect_clicked", on_connect_clicked);
    lv_xml_register_event_cb(nullptr, "on_spoolman_change_clicked", on_change_clicked);
    lv_xml_register_event_cb(nullptr, "on_spoolman_remove_clicked", on_remove_clicked);
```

- [ ] **Step 4: Find setup widgets in `create()`, update `on_ui_destroyed()`, invalidate alive guard in destructor**

Add after existing widget lookups in `create()`:

```cpp
    host_input_ = lv_obj_find_by_name(overlay_, "spoolman_host_input");
    port_input_ = lv_obj_find_by_name(overlay_, "spoolman_port_input");
    setup_status_text_ = lv_obj_find_by_name(overlay_, "setup_status_text");
    server_url_text_ = lv_obj_find_by_name(overlay_, "server_url_text");
    connect_btn_ = lv_obj_find_by_name(overlay_, "connect_btn");
    setup_card_ = lv_obj_find_by_name(overlay_, "setup_card");
    status_card_ = lv_obj_find_by_name(overlay_, "status_card");
```

Update `on_ui_destroyed()`:

```cpp
void SpoolmanOverlay::on_ui_destroyed() {
    sync_toggle_ = nullptr;
    interval_dropdown_ = nullptr;
    host_input_ = nullptr;
    port_input_ = nullptr;
    setup_status_text_ = nullptr;
    server_url_text_ = nullptr;
    connect_btn_ = nullptr;
    setup_card_ = nullptr;
    status_card_ = nullptr;
}
```

Add to `~SpoolmanOverlay()` before subject deinit:

```cpp
    if (alive_guard_)
        *alive_guard_ = false;
```

- [ ] **Step 5: Implement `set_setup_status()` helper**

```cpp
void SpoolmanOverlay::set_setup_status(const char* text, bool is_error) {
    if (setup_status_text_) {
        lv_label_set_text(setup_status_text_, text);
        lv_obj_set_style_text_color(setup_status_text_,
            ui_theme_get_color(is_error ? "danger" : "text_muted"), LV_PART_MAIN);
    }
}
```

- [ ] **Step 6: Implement `on_connect_clicked` — validate inputs and start probe**

Note: `ui_event_safety.h` is already included by the existing file.
Empty port defaults to 7912 (user requirement).

```cpp
void SpoolmanOverlay::on_connect_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SpoolmanOverlay] on_connect_clicked");

    auto& overlay = get_spoolman_overlay();
    const char* host_raw = overlay.host_input_ ? lv_textarea_get_text(overlay.host_input_) : "";
    const char* port_raw = overlay.port_input_ ? lv_textarea_get_text(overlay.port_input_) : "";

    std::string host(host_raw ? host_raw : "");
    std::string port(port_raw ? port_raw : "");

    // Trim whitespace
    auto trim = [](std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        size_t end = s.find_last_not_of(" \t");
        s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    };
    trim(host);
    trim(port);

    // Default to 7912 if port is empty
    if (port.empty()) port = helix::ui::DEFAULT_SPOOLMAN_PORT;

    if (!SpoolmanSetup::validate_host(host)) {
        overlay.set_setup_status(lv_tr("Please enter an IP address or hostname."), true);
        LVGL_SAFE_EVENT_CB_RETURN();
    }
    if (!SpoolmanSetup::validate_port(port)) {
        overlay.set_setup_status(lv_tr("Please enter a valid port (1-65535)."), true);
        LVGL_SAFE_EVENT_CB_RETURN();
    }

    overlay.set_setup_status(lv_tr("Checking Spoolman server..."));
    overlay.probe_spoolman_server(host, port);

    LVGL_SAFE_EVENT_CB_END();
}
```

- [ ] **Step 7: Implement `probe_spoolman_server()` — background HTTP GET**

```cpp
void SpoolmanOverlay::probe_spoolman_server(const std::string& host, const std::string& port) {
    auto alive = alive_guard_;
    std::string probe_url = SpoolmanSetup::build_probe_url(host, port);
    std::string host_copy = host;
    std::string port_copy = port;

    spdlog::info("[{}] Probing Spoolman at {}", get_name(), probe_url);

    std::thread([this, alive, probe_url, host_copy, port_copy]() {
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_GET;
        req->url = probe_url;
        req->timeout = 5;
        auto resp = requests::request(req);
        bool success = (resp && resp->status_code == 200);

        helix::ui::queue_update([this, alive, success, host_copy, port_copy]() {
            if (!alive || !*alive) return;
            if (success) {
                spdlog::info("[{}] Spoolman probe succeeded", get_name());
                set_setup_status(lv_tr("Spoolman found! Configuring..."));
                configure_spoolman(host_copy, port_copy);
            } else {
                spdlog::warn("[{}] Spoolman probe failed at {}:{}", get_name(),
                             host_copy, port_copy);
                auto msg = fmt::format("{} {}:{}",
                    lv_tr("Could not reach Spoolman at"), host_copy, port_copy);
                set_setup_status(msg.c_str(), true);
            }
        });
    }).detach();
}
```

- [ ] **Step 8: Build to verify compilation**

Run: `make -j`
Expected: Compiles (configure_spoolman etc. are declared but not yet defined — add stub implementations that log and return)

- [ ] **Step 9: Commit**

```bash
git add include/ui_spoolman_overlay.h src/ui/ui_spoolman_overlay.cpp
git commit -m "feat(spoolman): add widget setup, connect callback, and HTTP probe"
```

---

### Task 7b: SpoolmanOverlay — Config Write Chain

**Files:**
- Modify: `src/ui/ui_spoolman_overlay.cpp`

Implement the async chain: configure → finish_configure → ensure_moonraker_include → restart_and_verify → verify_spoolman_connected.

- [ ] **Step 1: Implement `configure_spoolman()`**

Downloads `helixscreen.conf` (handles 404 = start fresh), then calls `finish_configure()`.

```cpp
void SpoolmanOverlay::configure_spoolman(const std::string& host, const std::string& port) {
    if (!api_) {
        set_setup_status(lv_tr("Not connected to printer."), true);
        return;
    }

    auto alive = alive_guard_;
    auto entries = SpoolmanSetup::build_spoolman_config_entries(host, port);

    api_->transfers().download_file(
        "config", "helixscreen.conf",
        [this, alive, entries](const std::string& content) {
            if (!alive || !*alive) return;
            finish_configure(content, entries);
        },
        [this, alive, entries](const MoonrakerError& err) {
            if (!alive || !*alive) return;
            if (err.type == MoonrakerErrorType::FILE_NOT_FOUND) {
                finish_configure("", entries);
            } else {
                helix::ui::queue_update([this, alive]() {
                    if (!alive || !*alive) return;
                    set_setup_status(lv_tr("Failed to read config. Check connection."), true);
                });
            }
        });
}
```

- [ ] **Step 2: Implement `finish_configure()`**

Adds `[spoolman]` section, uploads `helixscreen.conf`, then calls `ensure_moonraker_include()`.

```cpp
void SpoolmanOverlay::finish_configure(
    const std::string& helix_content,
    const std::vector<std::pair<std::string, std::string>>& entries) {
    auto alive = alive_guard_;

    std::string modified = helix::MoonrakerConfigManager::add_section(
        helix_content, "spoolman", entries, "Spoolman - added by HelixScreen");

    api_->transfers().upload_file(
        "config", "helixscreen.conf", modified,
        [this, alive]() {
            if (!alive || !*alive) return;
            ensure_moonraker_include();
        },
        [this, alive](const MoonrakerError& err) {
            if (!alive || !*alive) return;
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                spdlog::error("[{}] Failed to upload helixscreen.conf: {}", get_name(),
                              err.message);
                set_setup_status(lv_tr("Failed to save config."), true);
            });
        });
}
```

- [ ] **Step 3: Implement `ensure_moonraker_include()`**

Downloads `moonraker.conf`, adds `[include helixscreen.conf]` if missing, uploads, then calls `restart_and_verify()`.

```cpp
void SpoolmanOverlay::ensure_moonraker_include() {
    auto alive = alive_guard_;

    api_->transfers().download_file(
        "config", "moonraker.conf",
        [this, alive](const std::string& content) {
            if (!alive || !*alive) return;

            if (helix::MoonrakerConfigManager::has_include_line(content)) {
                restart_and_verify();
                return;
            }

            std::string modified = helix::MoonrakerConfigManager::add_include_line(content);
            api_->transfers().upload_file(
                "config", "moonraker.conf", modified,
                [this, alive]() {
                    if (!alive || !*alive) return;
                    restart_and_verify();
                },
                [this, alive](const MoonrakerError&) {
                    if (!alive || !*alive) return;
                    helix::ui::queue_update([this, alive]() {
                        if (!alive || !*alive) return;
                        set_setup_status(lv_tr("Failed to update moonraker.conf."), true);
                    });
                });
        },
        [this, alive](const MoonrakerError&) {
            if (!alive || !*alive) return;
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                set_setup_status(lv_tr("Failed to read moonraker.conf."), true);
            });
        });
}
```

- [ ] **Step 4: Implement `restart_and_verify()`**

Suppresses recovery dialog, restarts Moonraker, waits 8s, then calls `verify_spoolman_connected()`.

```cpp
void SpoolmanOverlay::restart_and_verify() {
    auto alive = alive_guard_;

    helix::ui::queue_update([this, alive]() {
        if (!alive || !*alive) return;
        set_setup_status(lv_tr("Restarting Moonraker..."));
    });

    EmergencyStopOverlay::instance().suppress_recovery_dialog(15000);

    api_->restart_moonraker(
        [this, alive]() {
            if (!alive || !*alive) return;
            spdlog::info("[{}] Moonraker restart initiated", get_name());

            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                set_setup_status(lv_tr("Waiting for Moonraker..."));

                lv_timer_create(
                    [](lv_timer_t* timer) {
                        lv_timer_delete(timer);
                        auto& overlay = get_spoolman_overlay();
                        overlay.verify_spoolman_connected();
                    },
                    8000, nullptr);
            });
        },
        [this, alive](const MoonrakerError&) {
            if (!alive || !*alive) return;
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                set_setup_status(lv_tr("Failed to restart Moonraker."), true);
            });
        });
}
```

- [ ] **Step 5: Implement `verify_spoolman_connected()`**

```cpp
void SpoolmanOverlay::verify_spoolman_connected() {
    if (!api_) return;
    auto alive = alive_guard_;

    api_->spoolman().get_spoolman_status(
        [this, alive](bool connected, int /*spool_id*/) {
            helix::ui::queue_update([this, alive, connected]() {
                if (!alive || !*alive) return;
                if (connected) {
                    spdlog::info("[{}] Spoolman verified connected!", get_name());
                    set_setup_status("");
                    update_server_url_display();
                    ToastManager::instance().show(
                        ToastSeverity::SUCCESS, lv_tr("Spoolman connected!"), 3000);
                } else {
                    set_setup_status(
                        lv_tr("Moonraker restarted but Spoolman not connected. Check server."),
                        true);
                }
            });
        },
        [this, alive](const MoonrakerError&) {
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                set_setup_status(
                    lv_tr("Could not verify Spoolman status. Moonraker may still be restarting."),
                    true);
            });
        });
}
```

- [ ] **Step 6: Build and verify**

Run: `make -j`
Expected: Compiles without errors

- [ ] **Step 7: Commit**

```bash
git add src/ui/ui_spoolman_overlay.cpp
git commit -m "feat(spoolman): implement config write chain (configure → restart → verify)"
```

---

### Task 7c: SpoolmanOverlay — Change, Remove, and URL Display

**Files:**
- Modify: `src/ui/ui_spoolman_overlay.cpp`

- [ ] **Step 1: Implement `update_server_url_display()`**

Downloads `helixscreen.conf` to read the Spoolman URL, updates the status card label.

```cpp
void SpoolmanOverlay::update_server_url_display() {
    if (!server_url_text_ || !api_) return;
    auto alive = alive_guard_;

    api_->transfers().download_file(
        "config", "helixscreen.conf",
        [this, alive](const std::string& content) {
            auto url = helix::MoonrakerConfigManager::get_section_value(
                content, "spoolman", "server");
            helix::ui::queue_update([this, alive, url]() {
                if (!alive || !*alive || !server_url_text_) return;
                if (!url.empty()) {
                    auto text = fmt::format("{} {}", lv_tr("Connected to"), url);
                    lv_label_set_text(server_url_text_, text.c_str());
                } else {
                    lv_label_set_text(server_url_text_, lv_tr("Connected"));
                }
            });
        },
        [this, alive](const MoonrakerError&) {
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive || !server_url_text_) return;
                lv_label_set_text(server_url_text_, lv_tr("Connected"));
            });
        });
}
```

- [ ] **Step 2: Implement `on_change_clicked` — show setup card with pre-filled values**

```cpp
void SpoolmanOverlay::on_change_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SpoolmanOverlay] on_change_clicked");
    auto& overlay = get_spoolman_overlay();

    // Read current URL from helixscreen.conf to pre-fill inputs
    if (overlay.api_) {
        auto alive = overlay.alive_guard_;
        overlay.api_->transfers().download_file(
            "config", "helixscreen.conf",
            [&overlay, alive](const std::string& content) {
                auto url = helix::MoonrakerConfigManager::get_section_value(
                    content, "spoolman", "server");
                auto [host, port] = SpoolmanSetup::parse_url_components(url);
                helix::ui::queue_update([&overlay, alive, host, port]() {
                    if (!alive || !*alive) return;
                    if (overlay.host_input_)
                        lv_textarea_set_text(overlay.host_input_, host.c_str());
                    if (overlay.port_input_)
                        lv_textarea_set_text(overlay.port_input_, port.c_str());
                });
            },
            [](const MoonrakerError&) {});
    }

    // Show setup card, hide status card (imperative visibility is OK for this
    // temporary toggle — the cards will re-bind to subject on next panel open)
    if (overlay.setup_card_) lv_obj_remove_flag(overlay.setup_card_, LV_OBJ_FLAG_HIDDEN);
    if (overlay.status_card_) lv_obj_add_flag(overlay.status_card_, LV_OBJ_FLAG_HIDDEN);

    LVGL_SAFE_EVENT_CB_END();
}
```

- [ ] **Step 3: Implement `on_remove_clicked` — confirmation dialog**

```cpp
void SpoolmanOverlay::on_remove_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SpoolmanOverlay] on_remove_clicked");

    helix::ui::modal_show_confirmation(
        lv_tr("Remove Spoolman"),
        lv_tr("This will remove Spoolman integration from Moonraker. Continue?"),
        ConfirmSeverity::WARNING,
        lv_tr("Remove"),
        []() {
            auto& overlay = get_spoolman_overlay();
            overlay.remove_spoolman_config();
        },
        nullptr, nullptr);

    LVGL_SAFE_EVENT_CB_END();
}
```

- [ ] **Step 4: Implement `remove_spoolman_config()`**

```cpp
void SpoolmanOverlay::remove_spoolman_config() {
    if (!api_) return;
    auto alive = alive_guard_;

    api_->transfers().download_file(
        "config", "helixscreen.conf",
        [this, alive](const std::string& content) {
            if (!alive || !*alive) return;
            std::string modified = helix::MoonrakerConfigManager::remove_section(
                content, "spoolman");

            api_->transfers().upload_file(
                "config", "helixscreen.conf", modified,
                [this, alive]() {
                    if (!alive || !*alive) return;
                    helix::ui::queue_update([this, alive]() {
                        if (!alive || !*alive) return;
                        set_setup_status(lv_tr("Restarting Moonraker..."));
                    });

                    EmergencyStopOverlay::instance().suppress_recovery_dialog(15000);
                    api_->restart_moonraker(
                        [this, alive]() {
                            helix::ui::queue_update([this, alive]() {
                                if (!alive || !*alive) return;
                                ToastManager::instance().show(
                                    ToastSeverity::SUCCESS, lv_tr("Spoolman removed."), 3000);
                            });
                        },
                        [this, alive](const MoonrakerError&) {
                            helix::ui::queue_update([this, alive]() {
                                if (!alive || !*alive) return;
                                set_setup_status(lv_tr("Failed to restart Moonraker."), true);
                            });
                        });
                },
                [this, alive](const MoonrakerError&) {
                    helix::ui::queue_update([this, alive]() {
                        if (!alive || !*alive) return;
                        set_setup_status(lv_tr("Failed to save config."), true);
                    });
                });
        },
        [this, alive](const MoonrakerError&) {
            helix::ui::queue_update([this, alive]() {
                if (!alive || !*alive) return;
                set_setup_status(lv_tr("Failed to read config."), true);
            });
        });
}
```

- [ ] **Step 5: Call `update_server_url_display()` from `show()`**

After `update_ui_from_subjects()`:

```cpp
    update_server_url_display();
```

- [ ] **Step 6: Build and test interactively**

Run: `make -j && ./build/bin/helix-screen --test -vv`

Verify:
- Navigate to Settings → Spoolman
- When `printer_has_spoolman == 0`: setup card visible with IP/port inputs, sync/interval hidden
- Enter invalid values → validation errors shown
- Enter valid values → "Checking..." status
- When `printer_has_spoolman == 1`: status card visible with URL, Change/Remove buttons, sync/interval visible
- Change button shows setup card pre-filled
- Remove button shows confirmation dialog

- [ ] **Step 7: Commit**

```bash
git add include/ui_spoolman_overlay.h src/ui/ui_spoolman_overlay.cpp
git commit -m "feat(spoolman): add change, remove, and server URL display"
```

---

### Chunk 3 Review Checkpoint

**Review criteria:**
- All async callbacks capture alive guard and check `!alive || !*alive` before proceeding
- No direct LVGL calls from background threads — all go through `queue_update()`
- Error handling at every async step with user-visible feedback (no silent failures)
- `EmergencyStopOverlay::suppress_recovery_dialog(15000)` called before every Moonraker restart
- No `lv_obj_add_event_cb()` — all callbacks via XML `event_cb` + `lv_xml_register_event_cb()`
- Product name "Spoolman" not wrapped in `lv_tr()` (Lesson L070)
- Empty port input defaults to 7912 (user requirement)
- Setup card visibility bound to `printer_has_spoolman` subject
- `on_ui_destroyed()` nulls ALL widget pointers (old + new)
- Alive guard invalidated in destructor
- Build succeeds: `make -j`
- Interactive test: settings panel shows correct cards based on Spoolman state
- Change button pre-fills host/port from current config
- Remove button shows confirmation dialog before acting

---

## Chunk 4: Timelapse Refactor

### Task 8: Refactor Timelapse Installer to Use MoonrakerConfigManager

**Files:**
- Modify: `src/ui/ui_overlay_timelapse_install.cpp`
- Modify: `include/ui_overlay_timelapse_install.h`
- Modify: `tests/unit/test_timelapse_install.cpp`

- [ ] **Step 1: Add new test verifying config manager produces valid timelapse config**

Append to `tests/unit/test_timelapse_install.cpp`:

```cpp
#include "moonraker_config_manager.h"

TEST_CASE("MoonrakerConfigManager produces valid timelapse config", "[timelapse][config_manager]") {
    std::string content = "";
    auto result = helix::MoonrakerConfigManager::add_section(
        content, "timelapse", {}, "Timelapse - added by HelixScreen");
    result = helix::MoonrakerConfigManager::add_section(
        result, "update_manager timelapse",
        {{"type", "git_repo"},
         {"primary_branch", "main"},
         {"path", "~/moonraker-timelapse"},
         {"origin", "https://github.com/mainsail-crew/moonraker-timelapse.git"},
         {"managed_services", "klipper moonraker"}});

    REQUIRE(helix::MoonrakerConfigManager::has_section(result, "timelapse") == true);
    REQUIRE(helix::MoonrakerConfigManager::has_section(result, "update_manager timelapse") == true);
    REQUIRE(result.find("type: git_repo") != std::string::npos);
    REQUIRE(result.find("path: ~/moonraker-timelapse") != std::string::npos);
    REQUIRE(result.find("# Timelapse - added by HelixScreen") != std::string::npos);
}

TEST_CASE("Timelapse config can coexist with spoolman config", "[timelapse][config_manager]") {
    std::string content = "";
    // Add spoolman first
    content = helix::MoonrakerConfigManager::add_section(
        content, "spoolman", {{"server", "http://1.2.3.4:7912"}}, "Spoolman");
    // Add timelapse
    content = helix::MoonrakerConfigManager::add_section(
        content, "timelapse", {}, "Timelapse");
    content = helix::MoonrakerConfigManager::add_section(
        content, "update_manager timelapse",
        {{"type", "git_repo"}, {"path", "~/moonraker-timelapse"}});

    REQUIRE(helix::MoonrakerConfigManager::has_section(content, "spoolman") == true);
    REQUIRE(helix::MoonrakerConfigManager::has_section(content, "timelapse") == true);
    REQUIRE(helix::MoonrakerConfigManager::has_section(content, "update_manager timelapse") == true);

    // Remove spoolman should not affect timelapse
    auto without_spoolman = helix::MoonrakerConfigManager::remove_section(content, "spoolman");
    REQUIRE(helix::MoonrakerConfigManager::has_section(without_spoolman, "spoolman") == false);
    REQUIRE(helix::MoonrakerConfigManager::has_section(without_spoolman, "timelapse") == true);
    REQUIRE(helix::MoonrakerConfigManager::has_section(without_spoolman, "update_manager timelapse") == true);
}
```

- [ ] **Step 2: Run new tests to verify they pass (using already-implemented ConfigManager)**

Run: `make test && ./build/bin/helix-tests "[timelapse][config_manager]" -v`
Expected: PASS (these use already-implemented pure functions)

- [ ] **Step 3: Refactor `download_and_modify_config()` to use `MoonrakerConfigManager`**

Add include to `ui_overlay_timelapse_install.cpp`:

```cpp
#include "moonraker_config_manager.h"
```

Replace the `download_and_modify_config()` implementation. The new flow:
1. Download `moonraker.conf` — check if `[timelapse]` already exists (migration: leave user config alone)
2. If not in `moonraker.conf`, download `helixscreen.conf` (handle 404 = start fresh)
3. Check if `helixscreen.conf` already has `[timelapse]` (idempotent)
4. Add `[timelapse]` + `[update_manager timelapse]` sections via `MoonrakerConfigManager::add_section()`
5. Upload `helixscreen.conf`
6. Ensure `[include helixscreen.conf]` in `moonraker.conf` via `MoonrakerConfigManager::add_include_line()`
7. Upload `moonraker.conf` if modified
8. Proceed to `step_restart_moonraker()`

Add a new private method `write_timelapse_config()` to the header for the second half of the flow.

- [ ] **Step 4: Verify existing timelapse tests still pass**

Run: `make test && ./build/bin/helix-tests "[timelapse]" -v`
Expected: All existing `has_timelapse_section` and `append_timelapse_config` tests still PASS (static methods remain for backward compatibility)

- [ ] **Step 5: Build full project and run all tests**

Run: `make -j && make test-run`
Expected: All tests PASS, no build warnings

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_overlay_timelapse_install.cpp include/ui_overlay_timelapse_install.h tests/unit/test_timelapse_install.cpp
git commit -m "refactor(timelapse): use MoonrakerConfigManager for helixscreen.conf"
```

---

### Task 9: Translation Strings + Final Polish

**Files:**
- Modify: `translations/en.yml`

- [ ] **Step 1: Add translation strings for all new `lv_tr()` calls**

Check all `lv_tr()` calls added in Tasks 6-8 and add corresponding entries to `translations/en.yml`.

Strings to add (do NOT translate "Spoolman" — it's a product name, Lesson L070):
- "Server Setup"
- "Enter the IP address and port of your Spoolman server."
- "IP Address / Hostname"
- "Port"
- "Connect"
- "Connected"
- "Change"
- "Remove"
- "Please enter an IP address or hostname."
- "Please enter a valid port (1-65535)."
- "Checking Spoolman server..."
- "Could not reach Spoolman at"
- "Spoolman found! Configuring..."
- "Not connected to printer."
- "Failed to read config. Check connection."
- "Failed to save config."
- "Failed to update moonraker.conf."
- "Restarting Moonraker..."
- "Waiting for Moonraker..."
- "Spoolman connected!"
- "Moonraker restarted but Spoolman not connected. Check server."
- "Could not verify Spoolman status. Moonraker may still be restarting."
- "Remove Spoolman"
- "This will remove Spoolman integration from Moonraker. Continue?"
- "Spoolman removed."
- "Failed to restart Moonraker."
- "Failed to read config."
- "Connected to"
- "Configure Spoolman integration and weight sync settings."

Note: Some of these may already exist. Check before adding duplicates.

- [ ] **Step 2: Regenerate translation artifacts (Lesson L064)**

Run: `make -j` (build regenerates translation artifacts automatically)
Then stage the generated files:

```bash
git add translations/en.yml src/generated/lv_i18n_translations.c src/generated/lv_i18n_translations.h ui_xml/translations/translations.xml
```

- [ ] **Step 3: Run full test suite**

Run: `make test-run`
Expected: All tests pass

- [ ] **Step 4: Interactive smoke test**

Run: `./build/bin/helix-screen --test -vv`

Verify end-to-end:
1. Settings → Spoolman shows setup card (mock mode has no Spoolman)
2. Input fields accept text, port defaults to 7912
3. Empty host shows validation error
4. Sync/interval cards are hidden when Spoolman not configured

- [ ] **Step 5: Commit**

```bash
git add translations/en.yml src/generated/lv_i18n_translations.c src/generated/lv_i18n_translations.h ui_xml/translations/translations.xml
git commit -m "feat(spoolman): add translation strings for server setup"
```

---

### Chunk 4 Review Checkpoint

**Review criteria:**
- Timelapse refactor preserves existing behavior (migration: leaves existing moonraker.conf sections alone)
- Timelapse and Spoolman can coexist in the same `helixscreen.conf` without conflict
- `add_section()` idempotency prevents duplicate sections
- Existing timelapse tests still pass unchanged
- New coexistence test validates section isolation
- All translation strings added (Lesson L064: generated artifacts committed)
- No "Spoolman" wrapped in `lv_tr()` (Lesson L070)
- Full test suite passes: `make test-run`
- Interactive smoke test confirms UI works in mock mode

---

## Task Dependencies

```
Task 1 (has_section) ──┐
Task 2 (add_section) ──┤
Task 3 (remove_section)┤── Chunk 1: ConfigManager pure functions
Task 4 (include+get)  ─┘
                        │
Task 5 (SpoolmanSetup) ─── Chunk 2: Validation helpers
                        │
Task 6  (XML layout) ──┐
Task 7a (widget+probe)─┤
Task 7b (config chain) ┤── Chunk 3: UI integration (depends on Chunks 1+2)
Task 7c (change/remove)┘
                        │
Task 8  (Timelapse) ────┤
Task 9  (Translations) ─┘── Chunk 4: Refactor + polish (depends on Chunks 1+3)
```

Tasks within Chunk 1 are sequential (each builds on prior).
Chunk 2 can run in parallel with Chunk 1.
Chunks 3 and 4 depend on Chunks 1 and 2.
Within Chunk 3: Task 6 → 7a → 7b → 7c (sequential).
Task 8 can run in parallel with Chunk 3 (both depend on Chunk 1 only).
