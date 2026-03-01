// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_home.h"

#include "ui_callback_helpers.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_panel_ams.h"
#include "ui_update_queue.h"

#include "ams_state.h"
#include "app_globals.h"
#include "observer_factory.h"
#include "panel_widget_manager.h"
#include "panel_widgets/print_status_widget.h"
#include "panel_widgets/printer_image_widget.h"
#include "printer_image_manager.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <functional>
#include <memory>

using namespace helix;

/// Recursively set EVENT_BUBBLE on all descendants so touch events
/// (long_press, click, etc.) propagate up to the container.
static void set_event_bubble_recursive(lv_obj_t* obj) {
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        set_event_bubble_recursive(child);
    }
}

/// Recursively remove CLICKABLE flag from all descendants.
static void disable_widget_clicks_recursive(lv_obj_t* obj) {
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICKABLE);
        disable_widget_clicks_recursive(child);
    }
}

HomePanel::HomePanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Subscribe to printer image changes for immediate refresh
    image_changed_observer_ = helix::ui::observe_int_sync<HomePanel>(
        helix::PrinterImageManager::instance().get_image_changed_subject(), this,
        [](HomePanel* self, int /*ver*/) {
            // Clear cache so refresh_printer_image() actually applies the new image
            self->last_printer_image_path_.clear();
            self->refresh_printer_image();
        });
}

HomePanel::~HomePanel() {
    // Deinit subjects FIRST - disconnects observers before subject memory is freed
    deinit_subjects();

    // Gate observers watch external subjects (capabilities, klippy_state) that may
    // already be freed. Clear unconditionally.
    helix::PanelWidgetManager::instance().clear_gate_observers("home");
    helix::PanelWidgetManager::instance().unregister_rebuild_callback("home");

    // Detach active PanelWidget instances
    for (auto& w : active_widgets_) {
        w->detach();
    }
    active_widgets_.clear();
}

void HomePanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    spdlog::debug("[{}] Initializing subjects", get_name());

    // Register panel-level event callbacks BEFORE loading XML.
    // Widget-specific callbacks (LED, power, temp, network, fan, macro, etc.)
    // are self-registered by each widget in their attach() method.
    register_xml_callbacks({
        {"printer_status_clicked_cb", printer_status_clicked_cb},
        {"ams_clicked_cb", ams_clicked_cb},
        {"on_home_grid_long_press", on_home_grid_long_press},
        {"on_home_grid_clicked", on_home_grid_clicked},
        {"on_home_grid_pressing", on_home_grid_pressing},
        {"on_home_grid_released", on_home_grid_released},
    });

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticPanelRegistry::instance().register_destroy(
        "HomePanelSubjects", []() { get_global_home_panel().deinit_subjects(); });

    spdlog::debug("[{}] Registered subjects and event callbacks", get_name());
}

void HomePanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    // Release gate observers BEFORE subjects are freed
    helix::PanelWidgetManager::instance().clear_gate_observers("home");

    // SubjectManager handles all lv_subject_deinit() calls via RAII
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
}

void HomePanel::setup_widget_gate_observers() {
    auto& mgr = helix::PanelWidgetManager::instance();
    mgr.setup_gate_observers("home", [this]() { populate_widgets(); });
}

void HomePanel::populate_widgets() {
    if (populating_widgets_) {
        spdlog::debug("[{}] populate_widgets: already in progress, skipping", get_name());
        return;
    }
    populating_widgets_ = true;

    lv_obj_t* container = lv_obj_find_by_name(panel_, "widget_container");
    if (!container) {
        spdlog::error("[{}] widget_container not found", get_name());
        populating_widgets_ = false;
        return;
    }

    // Detach active PanelWidget instances before clearing
    for (auto& w : active_widgets_) {
        w->detach();
    }

    // Flush any deferred observer callbacks that captured raw widget pointers.
    // observe_int_sync / observe_string defer via ui_queue_update(), so lambdas
    // may already be queued with a `self` pointer to a widget we're about to
    // destroy.  Draining now ensures they run while the C++ objects still exist
    // (detach() cleared widget_obj_ so the guards will skip the work).
    helix::ui::UpdateQueue::instance().drain();

    // Destroy LVGL children BEFORE destroying C++ widget instances.
    lv_obj_clean(container);
    active_widgets_.clear();

    // Delegate generic widget creation to the manager
    active_widgets_ = helix::PanelWidgetManager::instance().populate_widgets("home", container);

    // Enable event bubbling on the entire widget subtree so touch events
    // (long_press, click, etc.) propagate from deeply-nested clickable
    // elements up to the widget_container, where the grid edit mode
    // handlers are registered via XML.
    set_event_bubble_recursive(container);

    // If edit mode is active (e.g. rebuild triggered during editing),
    // disable clickability so widget click handlers don't fire.
    if (grid_edit_mode_.is_active()) {
        disable_widget_clicks_recursive(container);
    }

    populating_widgets_ = false;
}

void HomePanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up...", get_name());

    // Dynamically populate grid widgets from PanelWidgetConfig
    populate_widgets();

    // Observe hardware gate subjects so widgets appear/disappear when
    // capabilities change (e.g. power devices discovered after startup).
    setup_widget_gate_observers();

    // Register rebuild callback so settings overlay toggle changes take effect immediately
    helix::PanelWidgetManager::instance().register_rebuild_callback(
        "home", [this]() { populate_widgets(); });

    // Widgets handle their own initialization via version observers
    // (no explicit config reload needed)

    spdlog::debug("[{}] Setup complete!", get_name());
}

void HomePanel::on_activate() {
    // Notify all widgets that the panel is visible
    for (auto& w : active_widgets_) {
        w->on_activate();
    }

    // Start Spoolman polling for AMS mini status updates
    AmsState::instance().start_spoolman_polling();
}

void HomePanel::on_deactivate() {
    // Exit grid edit mode if active, UNLESS the widget catalog overlay is open
    // (push_overlay triggers on_deactivate, but edit mode must survive)
    if (grid_edit_mode_.is_active() && !grid_edit_mode_.is_catalog_open()) {
        grid_edit_mode_.exit();
    }

    // Notify all widgets that the panel is going offscreen
    for (auto& w : active_widgets_) {
        w->on_deactivate();
    }

    AmsState::instance().stop_spoolman_polling();
}

void HomePanel::apply_printer_config() {
    // Widgets use version observers for auto-binding (LED, power, etc.)
    // Just refresh the printer image (delegated to PrinterImageWidget)
    refresh_printer_image();
}

void HomePanel::refresh_printer_image() {
    for (auto& w : active_widgets_) {
        if (auto* piw = dynamic_cast<helix::PrinterImageWidget*>(w.get())) {
            piw->refresh_printer_image();
            return;
        }
    }
}

void HomePanel::trigger_idle_runout_check() {
    for (auto& w : active_widgets_) {
        if (auto* psw = dynamic_cast<helix::PrintStatusWidget*>(w.get())) {
            psw->trigger_idle_runout_check();
            return;
        }
    }
    spdlog::debug("[{}] PrintStatusWidget not active - skipping runout check", get_name());
}

// ============================================================================
// Panel-level click handlers
// ============================================================================

void HomePanel::handle_printer_status_clicked() {
    spdlog::info("[{}] Printer status icon clicked - navigating to advanced settings", get_name());
    NavigationManager::instance().set_active(PanelId::Advanced);
}

void HomePanel::handle_ams_clicked() {
    spdlog::info("[{}] AMS indicator clicked - opening AMS panel overlay", get_name());

    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    lv_obj_t* panel_obj = ams_panel.get_panel();
    if (panel_obj) {
        NavigationManager::instance().push_overlay(panel_obj);
    }
}

void HomePanel::ensure_led_observers() {
    using helix::ui::observe_int_sync;

    if (!led_state_observer_) {
        led_state_observer_ = observe_int_sync<HomePanel>(
            printer_state_.get_led_state_subject(), this,
            [](HomePanel* self, int state) { self->on_led_state_changed(state); });
    }
    if (!led_brightness_observer_) {
        led_brightness_observer_ = observe_int_sync<HomePanel>(
            printer_state_.get_led_brightness_subject(), this,
            [](HomePanel* self, int /*brightness*/) { self->update_light_icon(); });
    }
}

void HomePanel::on_led_state_changed(int state) {
    auto& led_ctrl = helix::led::LedController::instance();
    if (led_ctrl.light_state_trackable()) {
        light_on_ = (state != 0);
        spdlog::debug("[{}] LED state changed: {} (from PrinterState)", get_name(),
                      light_on_ ? "ON" : "OFF");
        update_light_icon();
    } else {
        spdlog::debug("[{}] LED state changed but not trackable (TOGGLE macro mode)", get_name());
    }
}

void HomePanel::update_light_icon() {
    if (!light_icon_) {
        return;
    }

    // Get current brightness
    int brightness = lv_subject_get_int(printer_state_.get_led_brightness_subject());

    // Set icon based on brightness level
    const char* icon_name = ui_brightness_to_lightbulb_icon(brightness);
    ui_icon_set_source(light_icon_, icon_name);

    // Calculate icon color from LED RGBW values
    if (brightness == 0) {
        // OFF state - use muted gray from design tokens
        ui_icon_set_color(light_icon_, theme_manager_get_color("light_icon_off"), LV_OPA_COVER);
    } else {
        // Get RGB values from PrinterState
        int r = lv_subject_get_int(printer_state_.get_led_r_subject());
        int g = lv_subject_get_int(printer_state_.get_led_g_subject());
        int b = lv_subject_get_int(printer_state_.get_led_b_subject());
        int w = lv_subject_get_int(printer_state_.get_led_w_subject());

        lv_color_t icon_color;
        // If white channel dominant or RGB near white, use gold from design tokens
        if (w > std::max({r, g, b}) || (r > 200 && g > 200 && b > 200)) {
            icon_color = theme_manager_get_color("light_icon_on");
        } else {
            // Use actual LED color, boost if too dark for visibility
            int max_val = std::max({r, g, b});
            if (max_val < 128 && max_val > 0) {
                float scale = 128.0f / static_cast<float>(max_val);
                icon_color =
                    lv_color_make(static_cast<uint8_t>(std::min(255, static_cast<int>(r * scale))),
                                  static_cast<uint8_t>(std::min(255, static_cast<int>(g * scale))),
                                  static_cast<uint8_t>(std::min(255, static_cast<int>(b * scale))));
            } else {
                icon_color = lv_color_make(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                           static_cast<uint8_t>(b));
            }
        }

        ui_icon_set_color(light_icon_, icon_color, LV_OPA_COVER);
    }

    spdlog::trace("[{}] Light icon: {} at {}%", get_name(), icon_name, brightness);
}

void HomePanel::flash_light_icon() {
    if (!light_icon_)
        return;

    // Flash gold briefly then fade back to muted
    ui_icon_set_color(light_icon_, theme_manager_get_color("light_icon_on"), LV_OPA_COVER);

    if (!DisplaySettingsManager::instance().get_animations_enabled()) {
        // No animations -- the next status update will restore the icon naturally
        return;
    }

    // Animate opacity 255 -> 0 then restore to muted on completion
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, light_icon_);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, 300);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
    });
    lv_anim_set_completed_cb(&anim, [](lv_anim_t* a) {
        auto* icon = static_cast<lv_obj_t*>(a->var);
        lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
        ui_icon_set_color(icon, theme_manager_get_color("light_icon_off"), LV_OPA_COVER);
    });
    lv_anim_start(&anim);

    spdlog::debug("[{}] Flash light icon (TOGGLE macro, state unknown)", get_name());
}

void HomePanel::on_extruder_temp_changed(int temp_centi) {
    int temp_deg = centi_to_degrees(temp_centi);

    // Format temperature for display and update the string subject
    // Guard: Observer callback fires during constructor before init_subjects()
    helix::ui::temperature::format_temperature(temp_deg, temp_buffer_, sizeof(temp_buffer_));
    if (subjects_initialized_) {
        lv_subject_copy_string(&temp_subject_, temp_buffer_);
    }

    // Update cached value and animator (animator expects centidegrees)
    cached_extruder_temp_ = temp_centi;
    update_temp_icon_animation();

    spdlog::trace("[{}] Extruder temperature updated: {}°C", get_name(), temp_deg);
}

void HomePanel::on_extruder_target_changed(int target_centi) {
    // Animator expects centidegrees
    cached_extruder_target_ = target_centi;
    update_temp_icon_animation();
    spdlog::trace("[{}] Extruder target updated: {}°C", get_name(), centi_to_degrees(target_centi));
}

void HomePanel::update_temp_icon_animation() {
    temp_icon_animator_.update(cached_extruder_temp_, cached_extruder_target_);
}

void HomePanel::reload_from_config() {
    using helix::ui::observe_int_sync;

    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[{}] reload_from_config: Config not available", get_name());
        return;
    }

    // Reload LED configuration from LedController (single source of truth)
    // LED visibility is controlled by printer_has_led subject set via set_printer_capabilities()
    // which is called by the on_discovery_complete_ callback after hardware discovery
    {
        auto& led_ctrl = helix::led::LedController::instance();
        const auto& strips = led_ctrl.selected_strips();
        if (!strips.empty()) {
            // Set up tracked LED and observers (idempotent)
            printer_state_.set_tracked_led(strips.front());
            ensure_led_observers();
            spdlog::info("[{}] Reloaded LED config: {} LED(s)", get_name(), strips.size());
        } else {
            // No LED configured - clear tracking
            printer_state_.set_tracked_led("");
            spdlog::debug("[{}] LED config cleared", get_name());
        }
    }

    // Update printer type in PrinterState (triggers capability cache refresh)
    std::string printer_type =
        config->get<std::string>(config->df() + helix::wizard::PRINTER_TYPE, "");
    printer_state_.set_printer_type_sync(printer_type);

    // Update printer image
    refresh_printer_image();

    // Update printer type/host overlay
    // Always visible (even for localhost) to maintain consistent flex layout.
    // Hidden flag removes elements from flex, causing printer image to scale differently.
    std::string host = config->get<std::string>(config->df() + helix::wizard::MOONRAKER_HOST, "");

    if (host.empty() || host == "127.0.0.1" || host == "localhost") {
        // Space keeps the text_small at its font height for consistent layout
        std::strncpy(printer_type_buffer_, " ", sizeof(printer_type_buffer_) - 1);
        lv_subject_copy_string(&printer_type_subject_, printer_type_buffer_);
        lv_subject_set_int(&printer_info_visible_, 1);
    } else {
        std::strncpy(printer_type_buffer_, printer_type.empty() ? "Printer" : printer_type.c_str(),
                     sizeof(printer_type_buffer_) - 1);
        std::strncpy(printer_host_buffer_, host.c_str(), sizeof(printer_host_buffer_) - 1);
        lv_subject_copy_string(&printer_type_subject_, printer_type_buffer_);
        lv_subject_copy_string(&printer_host_subject_, printer_host_buffer_);
        lv_subject_set_int(&printer_info_visible_, 1);
    }
}

void HomePanel::refresh_printer_image() {
    if (!panel_)
        return;

    // Free old snapshot — image source is about to change
    if (cached_printer_snapshot_) {
        lv_obj_t* img = lv_obj_find_by_name(panel_, "printer_image");
        if (img) {
            // Clear source before destroying buffer it points to
            // Note: must use NULL, not "" — empty string byte 0x00 gets misclassified
            // as LV_IMAGE_SRC_VARIABLE by lv_image_src_get_type
            lv_image_set_src(img, nullptr);
            // Restore contain alignment so the original image scales correctly
            // during the ~50ms gap before the new snapshot is taken
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
        }
        lv_draw_buf_destroy(cached_printer_snapshot_);
        cached_printer_snapshot_ = nullptr;
    }

    lv_display_t* disp = lv_display_get_default();
    int screen_width = disp ? lv_display_get_horizontal_resolution(disp) : 800;

    // Check for user-selected printer image (custom or shipped override)
    auto& pim = helix::PrinterImageManager::instance();
    std::string custom_path = pim.get_active_image_path(screen_width);
    if (!custom_path.empty()) {
        lv_obj_t* img = lv_obj_find_by_name(panel_, "printer_image");
        if (img) {
            lv_image_set_src(img, custom_path.c_str());
            spdlog::debug("[{}] User-selected printer image: '{}'", get_name(), custom_path);
        }
        schedule_printer_image_snapshot();
        return;
    }

    // Auto-detect from printer type using PrinterImages
    Config* config = Config::get_instance();
    std::string printer_type =
        config ? config->get<std::string>(config->df() + helix::wizard::PRINTER_TYPE, "") : "";
    std::string image_path = PrinterImages::get_best_printer_image(printer_type);
    lv_obj_t* img = lv_obj_find_by_name(panel_, "printer_image");
    if (img) {
        lv_image_set_src(img, image_path.c_str());
        spdlog::debug("[{}] Printer image: '{}' for '{}'", get_name(), image_path, printer_type);
    }
    schedule_printer_image_snapshot();
}

void HomePanel::schedule_printer_image_snapshot() {
    // Cancel any pending snapshot timer
    if (snapshot_timer_) {
        lv_timer_delete(snapshot_timer_);
        snapshot_timer_ = nullptr;
    }

    // Defer snapshot until after layout resolves (~50ms)
    snapshot_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<HomePanel*>(lv_timer_get_user_data(timer));
            if (self) {
                self->snapshot_timer_ = nullptr; // Timer is one-shot, about to be deleted
                self->take_printer_image_snapshot();
            }
            lv_timer_delete(timer);
        },
        50, this);
    lv_timer_set_repeat_count(snapshot_timer_, 1);
}

void HomePanel::take_printer_image_snapshot() {
    if (!panel_)
        return;

    lv_obj_t* img = lv_obj_find_by_name(panel_, "printer_image");
    if (!img)
        return;

    // Only snapshot if the widget has resolved to a non-zero size
    int32_t w = lv_obj_get_width(img);
    int32_t h = lv_obj_get_height(img);
    if (w <= 0 || h <= 0) {
        spdlog::debug("[{}] Printer image not laid out yet ({}x{}), skipping snapshot", get_name(),
                      w, h);
        return;
    }

    lv_draw_buf_t* snapshot = lv_snapshot_take(img, LV_COLOR_FORMAT_ARGB8888);
    if (!snapshot) {
        spdlog::warn("[{}] Failed to take printer image snapshot", get_name());
        return;
    }

    // Free previous snapshot if any
    if (cached_printer_snapshot_) {
        lv_draw_buf_destroy(cached_printer_snapshot_);
    }
    cached_printer_snapshot_ = snapshot;

    // Diagnostic: verify snapshot header before setting as source
    uint32_t snap_w = snapshot->header.w;
    uint32_t snap_h = snapshot->header.h;
    uint32_t snap_magic = snapshot->header.magic;
    uint32_t snap_cf = snapshot->header.cf;
    spdlog::debug("[{}] Snapshot header: magic=0x{:02x} cf={} {}x{} data={}", get_name(),
                  snap_magic, snap_cf, snap_w, snap_h, fmt::ptr(snapshot->data));

    // Swap image source to the pre-scaled snapshot buffer — LVGL blits 1:1, no scaling
    lv_image_set_src(img, cached_printer_snapshot_);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);

    spdlog::debug("[{}] Printer image snapshot cached ({}x{}, {} bytes)", get_name(), snap_w,
                  snap_h, snap_w * snap_h * 4);
}

void HomePanel::light_toggle_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] light_toggle_cb");
    (void)e;
    // XML-registered callbacks don't have user_data set to 'this'
    // Use the global instance via legacy API bridge
    // This will be fixed when main.cpp switches to class-based instantiation
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_light_toggle();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::light_long_press_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] light_long_press_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_light_long_press();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::power_toggle_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] power_toggle_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_power_toggle();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::power_long_press_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] power_long_press_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_power_long_press();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::print_card_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] print_card_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_print_card_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::tip_text_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] tip_text_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_tip_text_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Static callback trampolines
// ============================================================================

void HomePanel::printer_status_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] printer_status_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_printer_status_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::ams_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] ams_clicked_cb");
    (void)e;
    extern HomePanel& get_global_home_panel();
    get_global_home_panel().handle_ams_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::on_home_grid_long_press(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] on_home_grid_long_press");
    extern HomePanel& get_global_home_panel();
    auto& panel = get_global_home_panel();
    if (!panel.grid_edit_mode_.is_active()) {
        // Cancel the in-progress press to prevent the widget's click action
        // from firing on release. Also clears PRESSED state from tracked objects.
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_indev_reset(indev, nullptr);
        }
        // Clear PRESSED state from all descendants — the pressed button
        // may be deeply nested inside a widget (e.g., print_status card).
        auto* wc = lv_obj_find_by_name(panel.panel_, "widget_container");
        if (wc) {
            std::function<void(lv_obj_t*)> clear_pressed = [&](lv_obj_t* obj) {
                lv_obj_remove_state(obj, LV_STATE_PRESSED);
                uint32_t count = lv_obj_get_child_count(obj);
                for (uint32_t i = 0; i < count; ++i) {
                    clear_pressed(lv_obj_get_child(obj, static_cast<int32_t>(i)));
                }
            };
            clear_pressed(wc);
        }

        // Enter edit mode on first long-press
        auto* container = lv_obj_find_by_name(panel.panel_, "widget_container");
        auto& config = helix::PanelWidgetManager::instance().get_widget_config("home");
        panel.grid_edit_mode_.set_rebuild_callback([&panel]() { panel.populate_widgets(); });
        panel.grid_edit_mode_.enter(container, &config);
        // Select the widget under the finger and start dragging immediately.
        panel.grid_edit_mode_.handle_click(e);
        if (panel.grid_edit_mode_.selected_widget()) {
            panel.grid_edit_mode_.handle_drag_start(e);
        }
    } else {
        // Already in edit mode — start drag if a widget is selected
        panel.grid_edit_mode_.handle_long_press(e);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::on_home_grid_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] on_home_grid_clicked");
    extern HomePanel& get_global_home_panel();
    auto& panel = get_global_home_panel();
    if (panel.grid_edit_mode_.is_active()) {
        panel.grid_edit_mode_.handle_click(e);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::on_home_grid_pressing(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] on_home_grid_pressing");
    extern HomePanel& get_global_home_panel();
    auto& panel = get_global_home_panel();
    if (panel.grid_edit_mode_.is_active()) {
        panel.grid_edit_mode_.handle_pressing(e);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::on_home_grid_released(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[HomePanel] on_home_grid_released");
    extern HomePanel& get_global_home_panel();
    auto& panel = get_global_home_panel();
    if (panel.grid_edit_mode_.is_active()) {
        panel.grid_edit_mode_.handle_released(e);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void HomePanel::exit_grid_edit_mode() {
    if (grid_edit_mode_.is_active()) {
        grid_edit_mode_.exit();
    }
}

void HomePanel::open_widget_catalog() {
    if (grid_edit_mode_.is_active() && parent_screen_) {
        grid_edit_mode_.open_widget_catalog(parent_screen_);
    }
}

// ============================================================================
// Global instance
// ============================================================================

static std::unique_ptr<HomePanel> g_home_panel;

HomePanel& get_global_home_panel() {
    if (!g_home_panel) {
        g_home_panel = std::make_unique<HomePanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("HomePanel",
                                                         []() { g_home_panel.reset(); });
    }
    return *g_home_panel;
}
