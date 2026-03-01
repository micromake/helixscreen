// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform_info.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <sys/utsname.h>

namespace helix {

// -1 = use compile-time default, 0 = force non-Android, 1 = force Android
static int s_platform_override = -1;

bool is_android_platform() {
    if (s_platform_override >= 0) {
        return s_platform_override != 0;
    }
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

void set_platform_override(int override_value) {
    s_platform_override = override_value;
}

void log_platform_info() {
    struct utsname uts {};
    if (uname(&uts) == 0) {
        spdlog::info("[Application] Platform: {} {} {} ({})", uts.sysname, uts.release, uts.machine,
                     uts.nodename);
    }

    // Total RAM from /proc/meminfo (Linux only)
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        unsigned long mem_total_kb = 0;
        if (fscanf(f, "MemTotal: %lu kB", &mem_total_kb) == 1 && mem_total_kb > 0) {
            spdlog::info("[Application] Memory: {} MB", mem_total_kb / 1024);
        }
        fclose(f);
    }

    // Display backend env var (if forced)
    const char* backend_env = std::getenv("HELIX_DISPLAY_BACKEND");
    if (backend_env && backend_env[0] != '\0') {
        spdlog::info("[Application] Display backend (env): {}", backend_env);
    }
}

} // namespace helix
