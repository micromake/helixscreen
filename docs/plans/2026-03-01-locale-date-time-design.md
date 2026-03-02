# Locale-Aware Date/Time Formatting

**Date:** 2026-03-01
**Status:** Approved

## Summary

Derive date/time formatting from the language setting — no new settings UI. Translate day/month names and use locale-appropriate date order for all 9 supported languages.

## Design

### Locale Table (`locale_formats.h/.cpp`)

Per-language data:
- **Date format pattern** (order + separator):
  - `en` → `"%a, %b %d"` (Mon, Jan 5)
  - `de`, `fr`, `es`, `pt`, `it`, `ru` → `"%a, %d %b"` (Mon, 5 Jan)
  - `ja`, `zh` → `"%m/%d (%a)"` (1/5 (月))
- **Abbreviated day names** (7 per language)
- **Abbreviated month names** (12 per language)
- **Default 12h/24h** — `en` defaults 12h; `de`, `fr`, `ja`, etc. default 24h

### Format Function

`format_localized_date(tm*, lang_code)`:
1. Look up locale table for `lang_code`
2. Substitute `%a` and `%b` with translated names
3. Apply locale's date order pattern

Update `format_time()` and `format_modified_date()` in `ui_format_utils.cpp` similarly.

### Integration

1. **Clock widget** — use `format_localized_date()` instead of raw `strftime`
2. **Format utils** — update `format_time()`, `format_modified_date()`
3. **Language change** — clock updates on next 1s timer tick (already reactive)
4. **12h/24h default** — when language changes and user hasn't explicitly overridden, update default

### Files

| File | Change |
|------|--------|
| `include/locale_formats.h` (new) | Locale table struct + lookup |
| `src/ui/locale_formats.cpp` (new) | Translation tables for 9 languages |
| `src/ui/panel_widgets/clock_widget.cpp` | Use localized formatting |
| `src/ui/ui_format_utils.cpp` | Use localized formatting |
| `include/ui_format_utils.h` | Updated signatures |
| `tests/unit/test_clock_widget.cpp` | Test localized output |

### Unchanged

- No new settings controls or subjects
- Settings persistence unchanged
- XML unchanged
