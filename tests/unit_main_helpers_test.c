#include <glib.h>

#include <string.h>

static void test_gtk_main_quit(void);

#define gtk_main_quit test_gtk_main_quit
#define main sort_visualizer_real_main
#include "../src/main.c"
#undef main
#undef gtk_main_quit

static void test_gtk_main_quit(void) {
}

static gboolean ensure_gtk_ready(void) {
    static gboolean initialized = FALSE;
    if (!initialized) {
        int argc = 0;
        char **argv = NULL;
        initialized = gtk_init_check(&argc, &argv);
    }
    if (!initialized) {
        g_test_skip("GTK display backend unavailable");
    }
    return initialized;
}

static void setup_app_state(AppState *app) {
    memset(app, 0, sizeof(*app));

    app->language = APP_LANG_EN;
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->drawing_area = gtk_drawing_area_new();
    app->language_label = gtk_label_new(NULL);
    app->language_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "en", "English");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "zh_CN", "简体中文");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "zh_TW", "繁體中文");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->language_combo), "en");

    app->algo_combo = gtk_combo_box_text_new();
    app->random_button = gtk_button_new();
    app->speed_label = gtk_label_new(NULL);
    app->meta_frame = gtk_frame_new(NULL);
    app->meta_name_header = create_meta_header("-");
    app->meta_best_header = create_meta_header("-");
    app->meta_avg_header = create_meta_header("-");
    app->meta_worst_header = create_meta_header("-");
    app->meta_stable_header = create_meta_header("-");
    app->meta_in_place_header = create_meta_header("-");
    app->meta_notes_header = create_meta_header("-");
    app->meta_name_value = create_meta_value_label();
    app->meta_best_value = create_meta_value_label();
    app->meta_avg_value = create_meta_value_label();
    app->meta_worst_value = create_meta_value_label();
    app->meta_stable_value = create_meta_value_label();
    app->meta_in_place_value = create_meta_value_label();
    app->meta_notes_value = create_meta_value_label();
    app->array_entry = gtk_entry_new();
    app->status_label = gtk_label_new(NULL);
    app->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 2000, 1);
    gtk_range_set_value(GTK_RANGE(app->speed_scale), 10);
    app->start_button = gtk_button_new();
    app->compile_button = gtk_button_new();
    app->custom_expander = gtk_expander_new(NULL);
    app->custom_text_view = gtk_text_view_new();

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->custom_text_view));
    gtk_text_buffer_set_text(buf, custom_sort_template(), -1);

    sort_frames_init(&app->playback_frames);
    custom_sort_handle_init(&app->custom_handle);
    app->runtime_translation_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    refresh_algorithm_combo(app, "quick");
    apply_language(app);
    update_algorithm_metadata(app);
}

static void teardown_app_state(AppState *app) {
    clear_playback(app);
    sort_frames_clear(&app->playback_frames);
    custom_sort_handle_clear(&app->custom_handle);
    if (app->runtime_translation_cache) {
        g_hash_table_destroy(app->runtime_translation_cache);
        app->runtime_translation_cache = NULL;
    }
    if (app->window) {
        gtk_widget_destroy(app->window);
    }
}

static void test_language_helpers(void) {
    g_assert_null(runtime_target_language(APP_LANG_EN));
    g_assert_cmpstr(runtime_target_language(APP_LANG_ZH_CN), ==, "zh-CN");
    g_assert_cmpstr(runtime_target_language(APP_LANG_ZH_TW), ==, "zh-TW");

    g_assert_cmpstr(tr(APP_LANG_EN, "window_title"), ==, "Custom Sort Visualizer");
    g_assert_cmpstr(tr(APP_LANG_ZH_CN, "start"), ==, "开始");
    g_assert_cmpstr(tr(APP_LANG_ZH_TW, "start"), ==, "開始");
    g_assert_cmpstr(tr(APP_LANG_EN, "unknown_key_passthrough"), ==, "unknown_key_passthrough");
}

static void test_algorithm_localization_fallbacks(void) {
    g_assert_cmpstr(localize_algorithm_label(APP_LANG_EN, "quick", "Quick Sort"), ==, "Quick Sort");
    g_assert_cmpstr(localize_algorithm_label(APP_LANG_ZH_CN, "quick", "Quick Sort"), ==, "快速排序");
    g_assert_cmpstr(localize_algorithm_label(APP_LANG_ZH_TW, "merge", "Merge Sort"), ==, "合併排序");
    g_assert_cmpstr(localize_algorithm_label(APP_LANG_ZH_CN, "not_real", "Fallback"), ==, "Fallback");

    g_assert_cmpstr(localize_algorithm_note(APP_LANG_EN, "quick", "Note"), ==, "Note");
    g_assert_cmpstr(localize_algorithm_note(APP_LANG_ZH_CN, "quick", "Note"), ==, "选择主元，分区后递归。性能优秀，但主元选择会影响最坏情况。");
    g_assert_cmpstr(localize_algorithm_note(APP_LANG_ZH_TW, "quick", "Note"), ==, "選擇主元、分區後遞迴。效能好，但主元選擇會影響最差情況。");
    g_assert_cmpstr(localize_algorithm_note(APP_LANG_ZH_TW, "not_real", "Fallback"), ==, "Fallback");
}

static void test_swap_detection(void) {
    int current[] = {1, 2, 3, 4};
    int next_swap[] = {1, 3, 2, 4};
    int next_no_swap[] = {1, 3, 4, 2};
    int next_single_diff[] = {1, 2, 3, 5};
    size_t a = 0;
    size_t b = 0;

    g_assert_true(detect_upcoming_swap(current, next_swap, G_N_ELEMENTS(current), &a, &b));
    g_assert_cmpuint(a, ==, 1);
    g_assert_cmpuint(b, ==, 2);

    g_assert_false(detect_upcoming_swap(current, next_no_swap, G_N_ELEMENTS(current), &a, &b));
    g_assert_false(detect_upcoming_swap(current, next_single_diff, G_N_ELEMENTS(current), &a, &b));
}

static void test_parse_array_input_paths(void) {
    int *arr = NULL;
    size_t n = 0;
    GError *error = NULL;

    g_assert_true(parse_array_input("1, 2 3\n4", &arr, &n, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(n, ==, 4);
    g_assert_cmpint(arr[0], ==, 1);
    g_assert_cmpint(arr[1], ==, 2);
    g_assert_cmpint(arr[2], ==, 3);
    g_assert_cmpint(arr[3], ==, 4);
    g_free(arr);
    arr = NULL;

    g_assert_false(parse_array_input("", &arr, &n, &error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error->message, "Array input is empty"));
    g_clear_error(&error);

    g_assert_false(parse_array_input("1, x", &arr, &n, &error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error->message, "Invalid integer token"));
    g_clear_error(&error);

    g_assert_false(parse_array_input(",,   ,", &arr, &n, &error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error->message, "did not contain any numbers"));
    g_clear_error(&error);

    g_assert_true(parse_array_input(" 10, ,\t20\n", &arr, &n, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(n, ==, 2);
    g_assert_cmpint(arr[0], ==, 10);
    g_assert_cmpint(arr[1], ==, 20);
    g_free(arr);
}

static void test_runtime_translation_passthrough(void) {
    AppState app = {0};

    app.language = APP_LANG_EN;
    app.runtime_translation_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    gchar *same = machine_translate_text(&app, "hello");
    g_assert_cmpstr(same, ==, "hello");
    g_free(same);

    gchar *translated_error = translate_runtime_error(&app, "line one\nline two");
    g_assert_cmpstr(translated_error, ==, "line one\nline two");
    g_free(translated_error);

    g_hash_table_destroy(app.runtime_translation_cache);
}

static void test_translation_tables_exhaustive(void) {
    const char *keys[] = {
        "window_title", "language_label", "array_placeholder", "randomize", "start", "speed_label",
        "meta_frame", "meta_algorithm", "meta_best", "meta_average", "meta_worst", "meta_stable",
        "meta_in_place", "meta_notes", "custom_expander", "compile_custom", "status_ready",
        "status_playback_complete", "status_compile_failed", "status_custom_selected", "status_random_generated",
        "status_invalid_input", "status_select_algo", "status_sort_failed", "status_sort_started",
        "draw_empty", "meta_none", "meta_choose", "meta_unknown", "meta_unavailable", "meta_custom",
        "meta_user_defined", "meta_custom_note", "yes", "no", "custom_algo_label",
    };

    const char *algo_ids[] = {
        "quick", "merge", "heap", "bubble", "selection", "insertion", "gnome", "shaker",
        "odd_even", "pancake", "bitonic", "radix", "shell", "comb", "bogo", "stooge",
    };

    for (size_t i = 0; i < G_N_ELEMENTS(keys); ++i) {
        const char *k = keys[i];
        g_assert_cmpstr(tr(APP_LANG_EN, k), !=, "");
        g_assert_cmpstr(tr(APP_LANG_ZH_CN, k), !=, "");
        g_assert_cmpstr(tr(APP_LANG_ZH_TW, k), !=, "");
    }

    for (size_t i = 0; i < G_N_ELEMENTS(algo_ids); ++i) {
        const char *id = algo_ids[i];
        g_assert_cmpstr(localize_algorithm_label(APP_LANG_ZH_CN, id, "fallback"), !=, "fallback");
        g_assert_cmpstr(localize_algorithm_label(APP_LANG_ZH_TW, id, "fallback"), !=, "fallback");
        g_assert_cmpstr(localize_algorithm_note(APP_LANG_ZH_CN, id, "fallback"), !=, "fallback");
        g_assert_cmpstr(localize_algorithm_note(APP_LANG_ZH_TW, id, "fallback"), !=, "fallback");
    }
}

static void test_event_language_algorithm_and_metadata(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.language_combo), "zh_CN");
    on_language_changed(GTK_COMBO_BOX(app.language_combo), &app);
    g_assert_cmpint(app.language, ==, APP_LANG_ZH_CN);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.start_button)), ==, "开始");

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "quick");
    on_algorithm_changed(GTK_COMBO_BOX(app.algo_combo), &app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.meta_name_value)), ==, "快速排序");

    ensure_custom_option(&app);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "custom");
    update_algorithm_metadata(&app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.meta_name_value)), ==, "自定义（已编译）");

    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.algo_combo), "unknown", "Unknown");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "unknown");
    update_algorithm_metadata(&app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.meta_name_value)), ==, "未知算法选择");

    teardown_app_state(&app);
}

static void test_event_compile_start_playback_and_speed(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    on_compile_custom_clicked(NULL, &app);
    g_assert_true(app.custom_registered);
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.algo_combo)), ==, "custom");

    gtk_entry_set_text(GTK_ENTRY(app.array_entry), "9, 3, 7, 1");
    on_start_clicked(NULL, &app);
    g_assert_cmpuint(app.playback_frames.frame_count, >, 0);
    g_assert_cmpuint(app.timer_id, !=, 0);

    guint tick_guard = 0;
    while (app.timer_id != 0 && tick_guard++ < 10000) {
        playback_tick(&app);
    }
    g_assert_cmpuint(app.timer_id, ==, 0);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.status_label)), ==, tr(app.language, "status_playback_complete"));

    app.timer_id = g_timeout_add(100, playback_tick, &app);
    gtk_range_set_value(GTK_RANGE(app.speed_scale), 20);
    on_speed_scale_changed(GTK_RANGE(app.speed_scale), &app);
    g_assert_cmpuint(app.timer_id, !=, 0);

    teardown_app_state(&app);
}

static void test_event_invalid_input_randomize_expander_and_draw(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    gtk_entry_set_text(GTK_ENTRY(app.array_entry), "bad,input");
    on_start_clicked(NULL, &app);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status_label)), "Invalid"));

    on_randomize_clicked(NULL, &app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.status_label)), ==, tr(app.language, "status_random_generated"));
    g_assert_cmpuint(strlen(gtk_entry_get_text(GTK_ENTRY(app.array_entry))), >, 0);

    on_custom_expander_state_changed(GTK_EXPANDER(app.custom_expander), NULL, &app);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 240);
    cairo_t *cr = cairo_create(surface);

    on_draw(app.drawing_area, cr, &app);

    clear_playback(&app);
    int zeros[] = {0, 0, 0};
    g_assert_true(sort_frames_capture(&app.playback_frames, zeros, G_N_ELEMENTS(zeros), NULL));
    app.playback_index = 0;
    on_draw(app.drawing_area, cr, &app);

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "quick");
    gtk_entry_set_text(GTK_ENTRY(app.array_entry), "3,1,2");
    on_start_clicked(NULL, &app);
    app.playback_index = 0;
    on_draw(app.drawing_area, cr, &app);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    teardown_app_state(&app);
}

static void test_event_language_fallback_and_no_algo_metadata(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.language_combo), "zh_TW");
    on_language_changed(GTK_COMBO_BOX(app.language_combo), &app);
    g_assert_cmpint(app.language, ==, APP_LANG_ZH_TW);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.start_button)), ==, "開始");

    GtkWidget *fake_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(fake_combo), "xx", "Unknown");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(fake_combo), "xx");
    on_language_changed(GTK_COMBO_BOX(fake_combo), &app);
    g_assert_cmpint(app.language, ==, APP_LANG_EN);
    gtk_widget_destroy(fake_combo);

    gtk_combo_box_set_active(GTK_COMBO_BOX(app.algo_combo), -1);
    update_algorithm_metadata(&app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.meta_name_value)), ==, tr(app.language, "meta_none"));

    teardown_app_state(&app);
}

static void test_frame_delay_and_playback_empty_paths(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    gtk_range_set_value(GTK_RANGE(app.speed_scale), 0);
    g_assert_cmpuint(get_frame_delay_ms(&app), ==, 1);

    app.timer_id = 0;
    on_speed_scale_changed(GTK_RANGE(app.speed_scale), &app);
    g_assert_cmpuint(app.timer_id, ==, 0);

    app.timer_id = 123;
    g_assert_false(playback_tick(&app));
    g_assert_cmpuint(app.timer_id, ==, 0);

    teardown_app_state(&app);
}

static void test_event_compile_and_start_failure_paths(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState app;
    setup_app_state(&app);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.custom_text_view));
    gtk_text_buffer_set_text(buf, "int custom_sort(", -1);
    on_compile_custom_clicked(NULL, &app);
    g_assert_false(app.custom_registered);
    g_assert_cmpuint(strlen(gtk_label_get_text(GTK_LABEL(app.status_label))), >, 0);

    ensure_custom_option(&app);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "custom");
    gtk_entry_set_text(GTK_ENTRY(app.array_entry), "3, 2, 1");
    on_start_clicked(NULL, &app);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status_label)), "No compiled custom sort available"));
    g_assert_cmpuint(app.timer_id, ==, 0);

    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.algo_combo), "missing_algo", "Missing");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.algo_combo), "missing_algo");
    gtk_entry_set_text(GTK_ENTRY(app.array_entry), "4, 1, 2");
    on_start_clicked(NULL, &app);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status_label)), "Invalid algorithm"));
    g_assert_cmpuint(app.timer_id, ==, 0);

    gtk_combo_box_set_active(GTK_COMBO_BOX(app.algo_combo), -1);
    on_start_clicked(NULL, &app);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app.status_label)), ==, tr(app.language, "status_select_algo"));

    teardown_app_state(&app);
}

static void test_app_builder_bootstrap_ui(void) {
    if (!ensure_gtk_ready()) {
        return;
    }

    AppState *app = app_state_new();
    g_assert_nonnull(app);

    app_build_ui(app);

    g_assert_nonnull(app->window);
    g_assert_nonnull(app->drawing_area);
    g_assert_nonnull(app->language_combo);
    g_assert_nonnull(app->algo_combo);
    g_assert_nonnull(app->array_entry);
    g_assert_nonnull(app->speed_scale);
    g_assert_nonnull(app->custom_text_view);
    g_assert_nonnull(app->status_label);

    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->language_combo)), ==, "en");
    g_assert_cmpstr(gtk_entry_get_placeholder_text(GTK_ENTRY(app->array_entry)), ==, tr(app->language, "array_placeholder"));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(app->status_label)), ==, tr(app->language, "status_ready"));

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->custom_text_view));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    g_assert_nonnull(strstr(text, "custom_sort"));
    g_free(text);

    on_window_destroy(app->window, app);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ui_helpers/language", test_language_helpers);
    g_test_add_func("/ui_helpers/algorithm_localization", test_algorithm_localization_fallbacks);
    g_test_add_func("/ui_helpers/swap_detection", test_swap_detection);
    g_test_add_func("/ui_helpers/parse_array_input", test_parse_array_input_paths);
    g_test_add_func("/ui_helpers/runtime_translation_passthrough", test_runtime_translation_passthrough);
    g_test_add_func("/ui_helpers/translation_tables_exhaustive", test_translation_tables_exhaustive);
    g_test_add_func("/ui_helpers/events_language_algorithm_metadata", test_event_language_algorithm_and_metadata);
    g_test_add_func("/ui_helpers/events_compile_start_playback_speed", test_event_compile_start_playback_and_speed);
    g_test_add_func("/ui_helpers/events_invalid_randomize_expander_draw", test_event_invalid_input_randomize_expander_and_draw);
    g_test_add_func("/ui_helpers/events_language_fallback_and_no_algo_metadata", test_event_language_fallback_and_no_algo_metadata);
    g_test_add_func("/ui_helpers/frame_delay_and_playback_empty", test_frame_delay_and_playback_empty_paths);
    g_test_add_func("/ui_helpers/events_compile_and_start_failure_paths", test_event_compile_and_start_failure_paths);
    g_test_add_func("/ui_helpers/app_builder_bootstrap_ui", test_app_builder_bootstrap_ui);

    return g_test_run();
}
