# Locale-Aware Date/Time Formatting — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make clock widget and date formatting functions use locale-appropriate date order and translated day/month names, derived from the language setting.

**Architecture:** New `locale_formats.h/.cpp` provides locale-aware date formatting. On language change, it attempts `setlocale(LC_TIME, ...)` and probes whether `strftime` produces non-English output. If the system locale works (Pi with locales generated), `strftime` handles everything natively. If not (AD5M / Buildroot), falls back to built-in translation tables. The detection result is cached — no per-call overhead. No new settings UI — everything derives from the existing language setting.

**Tech Stack:** C++17, POSIX `setlocale`/`strftime`, LVGL subjects, SystemSettingsManager

---

### Task 1: Create locale format header and implementation

**Files:**
- Create: `include/locale_formats.h`
- Create: `src/ui/locale_formats.cpp`

**Step 1: Create the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

struct tm;

namespace helix::ui {

/// Call when language setting changes. Attempts setlocale(LC_TIME) for the
/// new language and probes strftime. Caches whether system locale is active
/// or fallback tables are needed.
void locale_set_language(const std::string& lang_code);

/// Format a date string using locale-appropriate order and translated names.
/// Uses cached locale state from locale_set_language().
/// Examples: "Fri, Feb 28" (en), "ven., 28 févr." (fr), "2/28 (金)" (ja)
std::string format_localized_date(const struct tm* tm_info);

/// Format a full date+time string for file modified timestamps.
/// Respects both language (date order/names) and time format (12h/24h).
/// Examples: "Feb 28 2:30 PM" (en), "28 févr. 14:30" (fr)
std::string format_localized_modified_date(const struct tm* tm_info);

/// Get the default time format for a language (true = 24h, false = 12h)
bool locale_default_24h(const std::string& lang_code);

} // namespace helix::ui
```

**Step 2: Create the implementation**

Key design:
- Language code → POSIX locale name mapping: `{"en", "en_US.UTF-8"}`, `{"de", "de_DE.UTF-8"}`, `{"fr", "fr_FR.UTF-8"}`, `{"es", "es_ES.UTF-8"}`, `{"ru", "ru_RU.UTF-8"}`, `{"pt", "pt_BR.UTF-8"}`, `{"it", "it_IT.UTF-8"}`, `{"zh", "zh_CN.UTF-8"}`, `{"ja", "ja_JP.UTF-8"}`
- `locale_set_language()`:
  1. Call `setlocale(LC_TIME, locale_name)`
  2. Probe: format a known date with `strftime("%a", ...)` — if result differs from C locale, system locale is active
  3. Cache: `static bool s_use_system_locale` and `static std::string s_current_lang`
  4. If probe fails, log and prepare fallback tables
- `format_localized_date()`:
  - If system locale active: `strftime` with locale-appropriate format string (still need per-language format patterns for date ORDER — `%a, %b %d` vs `%a, %d %b` vs `%m/%d (%a)`)
  - If fallback: manually substitute `%a`/`%b` with table lookups
- Fallback tables: `LocaleData` struct with `day_abbrev[7]`, `month_abbrev[12]`, format pattern enum

Date format pattern groups (needed for both paths — system locale handles names but NOT order):
- `en`: `"{day}, {mon} {dd}"` → "Fri, Feb 28"
- `de`: `"{day}, {dd}. {mon}"` → "Fr., 28. Feb."
- `fr`, `es`, `pt`, `it`, `ru`: `"{day}, {dd} {mon}"` → "ven., 28 févr."
- `zh`, `ja`: `"{mm}/{dd} ({day})"` → "2/28 (金)"

Modified date patterns (for file timestamps, used by `format_localized_modified_date`):
- `en`: `"{mon} {dd} {time}"` → "Feb 28 2:30 PM"
- `de`: `"{dd}. {mon} {time}"` → "28. Feb. 14:30"
- `fr`, `es`, `pt`, `it`, `ru`: `"{dd} {mon} {time}"` → "28 févr. 14:30"
- `zh`, `ja`: `"{mm}/{dd} {time}"` → "2/28 14:30"

For the time portion in `format_localized_modified_date`, call `helix::ui::format_time()` from `ui_format_utils.h`.

`locale_default_24h`: returns `false` only for `"en"`, `true` for all others. Unknown codes return `false`.

Null `tm_info` returns `"--"`.

**Step 3: Verify it compiles**

Run: `make -j 2>&1 | tail -20`

**Step 4: Commit**

```
feat(i18n): add locale format tables with system locale detection
```

---

### Task 2: Write tests for locale formatting

**Files:**
- Modify: `tests/unit/test_clock_widget.cpp`

**Step 1: Add includes and tests**

Add after line 177 (after "timer fires during LVGL processing" test), before the widget rebuild tests. New includes at top:

```cpp
#include "locale_formats.h"
#include "system_settings_manager.h"
```

Tests use a fixed `struct tm` (Friday, February 28, 2026 14:30). Since we can't predict whether system locales are installed in the test environment, test the fallback path explicitly by verifying the output format structure rather than exact translated strings for non-English. For English, exact match is safe.

```cpp
TEST_CASE("Locale date formatting", "[clock_widget][i18n]") {
    struct tm test_tm = {};
    test_tm.tm_year = 126;
    test_tm.tm_mon = 1;
    test_tm.tm_mday = 28;
    test_tm.tm_hour = 14;
    test_tm.tm_min = 30;
    test_tm.tm_wday = 5;

    SECTION("English: Day, Mon DD") {
        helix::SystemSettingsManager::instance().set_language("en");
        helix::ui::locale_set_language("en");
        REQUIRE(helix::ui::format_localized_date(&test_tm) == "Fri, Feb 28");
    }

    SECTION("German: Day, DD. Mon") {
        helix::SystemSettingsManager::instance().set_language("de");
        helix::ui::locale_set_language("de");
        auto result = helix::ui::format_localized_date(&test_tm);
        // Format: "XX., 28. XXX." — day abbrev, then "28.", then month abbrev
        REQUIRE(result.find("28.") != std::string::npos);
    }

    SECTION("French: Day, DD Mon") {
        helix::SystemSettingsManager::instance().set_language("fr");
        helix::ui::locale_set_language("fr");
        auto result = helix::ui::format_localized_date(&test_tm);
        // Format: "xxx., 28 xxx." — contains "28"
        REQUIRE(result.find(", 28 ") != std::string::npos);
    }

    SECTION("Japanese: MM/DD (Day)") {
        helix::SystemSettingsManager::instance().set_language("ja");
        helix::ui::locale_set_language("ja");
        auto result = helix::ui::format_localized_date(&test_tm);
        // Format: "2/28 (X)"
        REQUIRE(result.find("2/28") != std::string::npos);
    }

    SECTION("Unknown language falls back to English") {
        helix::SystemSettingsManager::instance().set_language("xx");
        helix::ui::locale_set_language("xx");
        REQUIRE(helix::ui::format_localized_date(&test_tm) == "Fri, Feb 28");
    }

    SECTION("nullptr tm returns unavailable") {
        REQUIRE(helix::ui::format_localized_date(nullptr) == "--");
    }

    // Restore default
    helix::SystemSettingsManager::instance().set_language("en");
    helix::ui::locale_set_language("en");
}

TEST_CASE("Locale default 24h", "[clock_widget][i18n]") {
    REQUIRE_FALSE(helix::ui::locale_default_24h("en"));
    REQUIRE(helix::ui::locale_default_24h("de"));
    REQUIRE(helix::ui::locale_default_24h("fr"));
    REQUIRE(helix::ui::locale_default_24h("ja"));
    REQUIRE_FALSE(helix::ui::locale_default_24h("xx"));
}
```

**Step 2: Run tests**

Run: `make test && ./build/bin/helix-tests "[i18n]" -v 2>&1 | tail -30`

**Step 3: Iterate until tests pass**

**Step 4: Commit**

```
test(i18n): add locale date formatting tests
```

---

### Task 3: Wire locale formatting into clock widget and format utils

**Files:**
- Modify: `src/ui/panel_widgets/clock_widget.cpp` (lines 240-243)
- Modify: `src/ui/ui_format_utils.cpp` (lines 99-121)

**Step 1: Update clock_widget.cpp**

Add include:
```cpp
#include "locale_formats.h"
```

Replace date formatting in `update_clock()` (lines 240-243):
```cpp
// Old:
char date_buf[32];
strftime(date_buf, sizeof(date_buf), "%a, %b %d", tm_info);
lv_subject_copy_string(&s_date_subject, date_buf);

// New:
std::string date_str = helix::ui::format_localized_date(tm_info);
lv_subject_copy_string(&s_date_subject, date_str.c_str());
```

**Step 2: Update format_modified_date() in ui_format_utils.cpp**

Add include:
```cpp
#include "locale_formats.h"
```

Replace `format_modified_date()` body (lines 99-121):
```cpp
std::string format_modified_date(time_t timestamp) {
    struct tm tm_buf;
    struct tm* timeinfo = localtime_r(&timestamp, &tm_buf);
    if (!timeinfo) {
        return "Unknown";
    }
    return format_localized_modified_date(timeinfo);
}
```

**Step 3: Hook locale_set_language() into language change**

In `system_settings_manager.cpp`, in `set_language()` — after calling `lv_translation_set_language()`, add:
```cpp
helix::ui::locale_set_language(lang);
```

Also call it during `init_subjects()` after loading the language from config, so the locale is set on startup.

Add include:
```cpp
#include "locale_formats.h"
```

**Step 4: Build and run all tests**

Run: `make -j && make test-run`

**Step 5: Commit**

```
feat(i18n): wire locale-aware formatting into clock widget and format utils
```

---

### Task 4: Visual verification and cleanup

**Step 1: Run the app**

Run: `./build/bin/helix-screen --test -vv`

Verify clock shows correct date format.

**Step 2: Run full test suite**

Run: `make test-run`

**Step 3: Final fixup commit if needed**

---

## Locale Reference Data

### POSIX locale names

| Code | Locale | 24h default |
|------|--------|-------------|
| en | en_US.UTF-8 | No |
| de | de_DE.UTF-8 | Yes |
| fr | fr_FR.UTF-8 | Yes |
| es | es_ES.UTF-8 | Yes |
| ru | ru_RU.UTF-8 | Yes |
| pt | pt_BR.UTF-8 | Yes |
| it | it_IT.UTF-8 | Yes |
| zh | zh_CN.UTF-8 | Yes |
| ja | ja_JP.UTF-8 | Yes |

### Fallback day abbreviations (Sun=0 → Sat=6)

| Lang | Sun | Mon | Tue | Wed | Thu | Fri | Sat |
|------|-----|-----|-----|-----|-----|-----|-----|
| en | Sun | Mon | Tue | Wed | Thu | Fri | Sat |
| de | So. | Mo. | Di. | Mi. | Do. | Fr. | Sa. |
| fr | dim. | lun. | mar. | mer. | jeu. | ven. | sam. |
| es | dom. | lun. | mar. | mié. | jue. | vie. | sáb. |
| ru | вс | пн | вт | ср | чт | пт | сб |
| pt | dom. | seg. | ter. | qua. | qui. | sex. | sáb. |
| it | dom. | lun. | mar. | mer. | gio. | ven. | sab. |
| zh | 日 | 一 | 二 | 三 | 四 | 五 | 六 |
| ja | 日 | 月 | 火 | 水 | 木 | 金 | 土 |

### Fallback month abbreviations (Jan=0 → Dec=11)

| Lang | Jan | Feb | Mar | Apr | May | Jun | Jul | Aug | Sep | Oct | Nov | Dec |
|------|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| en | Jan | Feb | Mar | Apr | May | Jun | Jul | Aug | Sep | Oct | Nov | Dec |
| de | Jan. | Feb. | Mär. | Apr. | Mai | Jun. | Jul. | Aug. | Sep. | Okt. | Nov. | Dez. |
| fr | janv. | févr. | mars | avr. | mai | juin | juil. | août | sept. | oct. | nov. | déc. |
| es | ene. | feb. | mar. | abr. | may. | jun. | jul. | ago. | sept. | oct. | nov. | dic. |
| ru | янв. | февр. | мар. | апр. | мая | июн. | июл. | авг. | сент. | окт. | нояб. | дек. |
| pt | jan. | fev. | mar. | abr. | mai. | jun. | jul. | ago. | set. | out. | nov. | dez. |
| it | gen. | feb. | mar. | apr. | mag. | giu. | lug. | ago. | set. | ott. | nov. | dic. |
| zh | 1月 | 2月 | 3月 | 4月 | 5月 | 6月 | 7月 | 8月 | 9月 | 10月 | 11月 | 12月 |
| ja | 1月 | 2月 | 3月 | 4月 | 5月 | 6月 | 7月 | 8月 | 9月 | 10月 | 11月 | 12月 |
