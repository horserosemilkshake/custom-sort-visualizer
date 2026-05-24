#include <glib.h>

#include <string.h>

#define main sort_visualizer_real_main
#include "../src/main.c"
#undef main

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
    size_t a = 0;
    size_t b = 0;

    g_assert_true(detect_upcoming_swap(current, next_swap, G_N_ELEMENTS(current), &a, &b));
    g_assert_cmpuint(a, ==, 1);
    g_assert_cmpuint(b, ==, 2);

    g_assert_false(detect_upcoming_swap(current, next_no_swap, G_N_ELEMENTS(current), &a, &b));
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

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ui_helpers/language", test_language_helpers);
    g_test_add_func("/ui_helpers/algorithm_localization", test_algorithm_localization_fallbacks);
    g_test_add_func("/ui_helpers/swap_detection", test_swap_detection);
    g_test_add_func("/ui_helpers/parse_array_input", test_parse_array_input_paths);
    g_test_add_func("/ui_helpers/runtime_translation_passthrough", test_runtime_translation_passthrough);

    return g_test_run();
}
