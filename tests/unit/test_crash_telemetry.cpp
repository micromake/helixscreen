// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_crash_telemetry.cpp
 * @brief Tests for crash-hardening: exception crash file writing and parsing
 *
 * Validates the write_exception_crash_file() pattern from main.cpp and the
 * read_crash_file() parsing of the "exception:" key from crash_handler.cpp.
 *
 * The write_exception_crash_file() function in main.cpp writes a minimal
 * crash.txt when a C++ exception escapes Application::run(). This file uses
 * the same key:value format as the signal handler so CrashReporter can parse
 * it uniformly on next startup.
 *
 * These tests FAIL if the exception field parsing is removed from read_crash_file().
 */

#include "system/crash_handler.h"

#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Helper: write an exception crash file using the same pattern as main.cpp
// ============================================================================

/**
 * Reimplements the write_exception_crash_file() logic from main.cpp
 * so we can test the format without depending on the static function.
 */
static void write_exception_crash_file_to(const std::string& path, const char* what) {
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }

    time_t now = time(nullptr);
    dprintf(fd, "signal:0\n");
    dprintf(fd, "name:EXCEPTION\n");
    dprintf(fd, "version:0.13.3-test\n");
    dprintf(fd, "timestamp:%ld\n", static_cast<long>(now));
    dprintf(fd, "uptime:0\n");
    if (what) {
        dprintf(fd, "exception:%s\n", what);
    }
    close(fd);
}

// ============================================================================
// Fixture
// ============================================================================

class CrashTelemetryFixture {
  public:
    CrashTelemetryFixture() {
        temp_dir_ = fs::temp_directory_path() /
                    ("helix_crash_telemetry_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
        crash_path_ = (temp_dir_ / "crash.txt").string();
    }

    ~CrashTelemetryFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    [[nodiscard]] std::string crash_path() const {
        return crash_path_;
    }
    [[nodiscard]] fs::path temp_dir() const {
        return temp_dir_;
    }

    void write_crash_file(const std::string& content) const {
        std::ofstream ofs(crash_path_);
        ofs << content;
        ofs.close();
    }

  private:
    fs::path temp_dir_;
    std::string crash_path_;
};

// ============================================================================
// write_exception_crash_file format tests
// ============================================================================

TEST_CASE_METHOD(CrashTelemetryFixture, "Exception crash file: writes valid key:value format",
                 "[crash_telemetry][crash_hardening]") {
    write_exception_crash_file_to(crash_path(), "segfault in observer callback");

    REQUIRE(crash_handler::has_crash_file(crash_path()));

    // Read raw content and verify format
    std::ifstream ifs(crash_path());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    REQUIRE(content.find("signal:0\n") != std::string::npos);
    REQUIRE(content.find("name:EXCEPTION\n") != std::string::npos);
    REQUIRE(content.find("version:") != std::string::npos);
    REQUIRE(content.find("timestamp:") != std::string::npos);
    REQUIRE(content.find("uptime:0\n") != std::string::npos);
    REQUIRE(content.find("exception:segfault in observer callback\n") != std::string::npos);
}

TEST_CASE_METHOD(CrashTelemetryFixture, "Exception crash file: null what omits exception field",
                 "[crash_telemetry][crash_hardening]") {
    write_exception_crash_file_to(crash_path(), nullptr);

    REQUIRE(crash_handler::has_crash_file(crash_path()));

    std::ifstream ifs(crash_path());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    REQUIRE(content.find("signal:0\n") != std::string::npos);
    REQUIRE(content.find("name:EXCEPTION\n") != std::string::npos);
    // No exception field when what is null
    REQUIRE(content.find("exception:") == std::string::npos);
}

// ============================================================================
// read_crash_file parsing of exception field
// ============================================================================

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: read_crash_file parses exception field",
                 "[crash_telemetry][crash_hardening]") {
    write_exception_crash_file_to(crash_path(), "std::runtime_error: out of memory");

    auto result = crash_handler::read_crash_file(crash_path());
    // Signal 0 + name EXCEPTION should parse successfully
    // (read_crash_file requires signal + signal_name)
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result["signal"] == 0);
    REQUIRE(result["signal_name"] == "EXCEPTION");
    REQUIRE(result.contains("exception"));
    REQUIRE(result["exception"] == "std::runtime_error: out of memory");
}

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: read_crash_file handles exception with special characters",
                 "[crash_telemetry][crash_hardening]") {
    // Exception messages can contain colons, quotes, etc.
    write_crash_file("signal:0\n"
                     "name:EXCEPTION\n"
                     "version:0.13.3\n"
                     "timestamp:1707350400\n"
                     "uptime:0\n"
                     "exception:std::bad_alloc: operator new(size_t): 4096 bytes\n");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("exception"));
    // The parser splits on first colon, so "std::bad_alloc: ..." is the value
    // (the "exception:" prefix is key, everything after first colon is value)
    std::string exc = result["exception"];
    REQUIRE(exc.find("std") != std::string::npos);
    REQUIRE(exc.find("bad_alloc") != std::string::npos);
}

TEST_CASE_METHOD(
    CrashTelemetryFixture,
    "Exception crash file: read_crash_file without exception field returns no exception key",
    "[crash_telemetry][crash_hardening]") {
    // Standard signal crash file — no exception field
    write_crash_file("signal:11\n"
                     "name:SIGSEGV\n"
                     "version:0.13.3\n"
                     "timestamp:1707350400\n"
                     "uptime:3600\n"
                     "bt:0x0040abcd\n");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result["signal"] == 11);
    REQUIRE_FALSE(result.contains("exception"));
}

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: non-std::exception message is captured",
                 "[crash_telemetry][crash_hardening]") {
    // Matches the catch(...) path in main.cpp
    write_exception_crash_file_to(crash_path(), "non-std::exception");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result["signal_name"] == "EXCEPTION");
    REQUIRE(result["exception"] == "non-std::exception");
}

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: timestamp is valid ISO 8601 after parsing",
                 "[crash_telemetry][crash_hardening]") {
    write_exception_crash_file_to(crash_path(), "test exception");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("timestamp"));

    std::string ts = result["timestamp"];
    // ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
    REQUIRE(ts.find('T') != std::string::npos);
    REQUIRE(ts.find('Z') != std::string::npos);
}

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: uptime is zero for exception crashes",
                 "[crash_telemetry][crash_hardening]") {
    write_exception_crash_file_to(crash_path(), "fatal error");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("uptime_sec"));
    REQUIRE(result["uptime_sec"] == 0);
}

TEST_CASE_METHOD(CrashTelemetryFixture, "Exception crash file: empty exception string is preserved",
                 "[crash_telemetry][crash_hardening]") {
    write_crash_file("signal:0\n"
                     "name:EXCEPTION\n"
                     "version:0.13.3\n"
                     "timestamp:1707350400\n"
                     "uptime:0\n"
                     "exception:\n");

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());
    REQUIRE(result.contains("exception"));
    REQUIRE(result["exception"] == "");
}

TEST_CASE_METHOD(CrashTelemetryFixture,
                 "Exception crash file: round-trip write then read preserves message",
                 "[crash_telemetry][crash_hardening]") {
    const char* original_msg = "Application::run() threw std::runtime_error: display init failed";
    write_exception_crash_file_to(crash_path(), original_msg);

    auto result = crash_handler::read_crash_file(crash_path());
    REQUIRE_FALSE(result.is_null());

    // The exception value should match exactly (after first colon split)
    // Since "exception:" is the key, and the message after the first colon
    // in the line "exception:Application::run()..." gives us the full message
    std::string parsed = result["exception"];
    REQUIRE(parsed == original_msg);
}
