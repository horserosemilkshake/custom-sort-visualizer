#include "sort_engine.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>

static gboolean array_is_sorted(const int *arr, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        if (arr[i - 1] > arr[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void test_custom_sort_compile_and_run(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {10, -1, 7, 3, 2, 9};
    int input_second[] = {4, 4, -3, 12, 0, 8, -1};

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, custom_sort_template(), &error));
    g_assert_no_error(error);

    g_assert_true(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_no_error(error);

    g_assert_cmpuint(frames.frame_count, >, 0);
    g_assert_cmpuint(frames.n, ==, G_N_ELEMENTS(input));
    g_assert_cmpmem(frames.frames[0], sizeof(input), input, sizeof(input));
    g_assert_true(array_is_sorted(frames.frames[frames.frame_count - 1], frames.n));

    g_assert_true(custom_sort_run(&handle, input_second, G_N_ELEMENTS(input_second), &frames, &error));
    g_assert_no_error(error);

    g_assert_cmpuint(frames.frame_count, >, 0);
    g_assert_cmpuint(frames.n, ==, G_N_ELEMENTS(input_second));
    g_assert_cmpmem(frames.frames[0], sizeof(input_second), input_second, sizeof(input_second));
    g_assert_true(array_is_sorted(frames.frames[frames.frame_count - 1], frames.n));

    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_compile_error(void) {
    CustomSortHandle handle;
    GError *error = NULL;

    const char *invalid_code =
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    this_will_not_compile\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);

    g_assert_false(custom_sort_compile(&handle, invalid_code, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Custom sort compilation failed"));

    g_clear_error(&error);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_nonzero_result(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {5, 4, 3, 2, 1};
    const char *code =
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    return 7;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "custom_sort returned error code 7"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_missing_library(void) {
#ifdef G_OS_WIN32
    g_test_skip("Windows keeps loaded DLLs locked; runtime missing-library path is Unix-specific");
    return;
#else
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {9, 1, 8, 2};

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, custom_sort_template(), &error));
    g_assert_no_error(error);

    g_assert_nonnull(handle.library_path);
    g_assert_cmpint(g_remove(handle.library_path), ==, 0);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "cannot open shared object file"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
#endif
}

static void test_custom_sort_runtime_no_result_line(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stddef.h>\n"
        "#include <stdlib.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    _Exit(0);\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "without a result line"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_timeout(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {2, 1};
    const char *code =
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    for (;;) {}\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Custom sort timed out"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_missing_symbol(void) {
    CustomSortHandle handle;
    GError *error = NULL;

    const char *code =
        "#include <stddef.h>\n"
        "int not_custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);

    g_assert_false(custom_sort_compile(&handle, code, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Function custom_sort was not found"));

    g_clear_error(&error);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_invalid_frame_token(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 1, 2};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRAME not,a,number\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Worker emitted invalid frame token"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_invalid_result_line(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"RESULT not-a-number\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Worker emitted invalid result line"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_worker_error_line(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {8, 2, 5};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"ERROR injected worker message\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "injected worker message"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_too_many_frame_values(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {8, 2, 5};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRAME 1,2,3,4\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "too many values in frame"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_too_few_frame_values(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {8, 2, 5};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRAME 1,2\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "expected 3"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_ignores_unknown_worker_lines(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"NOTE ignored line\\n\");\n"
        "    printf(\"FRAME 1,,2,3\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_true(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(frames.frame_count, >, 0);

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_split_lines_across_reads(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRA\");\n"
        "    fflush(stdout);\n"
        "    printf(\"ME 1,2,3\\nRES\");\n"
        "    fflush(stdout);\n"
        "    printf(\"ULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_true(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(frames.frame_count, >=, 2);
    g_assert_cmpmem(frames.frames[frames.frame_count - 1], sizeof(input), input, sizeof(input));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_last_error_line_wins(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"ERROR first message\\n\");\n"
        "    printf(\"ERROR second message\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "second message"));
    g_assert_null(strstr(error->message, "first message"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_last_result_line_wins(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRAME 1,2,3\\n\");\n"
        "    printf(\"RESULT 7\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_true(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(frames.frame_count, >, 0);

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_custom_sort_runtime_error_after_result_fails(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;

    int input[] = {3, 2, 1};
    const char *code =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
        "    (void)arr; (void)n; (void)swap_cb; (void)user_data;\n"
        "    printf(\"FRAME 1,2,3\\n\");\n"
        "    printf(\"RESULT 0\\n\");\n"
        "    printf(\"ERROR late parser failure\\n\");\n"
        "    fflush(stdout);\n"
        "    return 0;\n"
        "}\n";

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_true(custom_sort_compile(&handle, code, &error));
    g_assert_no_error(error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "late parser failure"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/integration/custom_sort_compile_and_run", test_custom_sort_compile_and_run);
    g_test_add_func("/integration/custom_sort_compile_error", test_custom_sort_compile_error);
    g_test_add_func("/integration/custom_sort_runtime_nonzero_result", test_custom_sort_runtime_nonzero_result);
    g_test_add_func("/integration/custom_sort_runtime_missing_library", test_custom_sort_runtime_missing_library);
    g_test_add_func("/integration/custom_sort_runtime_no_result_line", test_custom_sort_runtime_no_result_line);
    g_test_add_func("/integration/custom_sort_timeout", test_custom_sort_timeout);
    g_test_add_func("/integration/custom_sort_missing_symbol", test_custom_sort_missing_symbol);
    g_test_add_func("/integration/custom_sort_runtime_invalid_frame_token", test_custom_sort_runtime_invalid_frame_token);
    g_test_add_func("/integration/custom_sort_runtime_invalid_result_line", test_custom_sort_runtime_invalid_result_line);
    g_test_add_func("/integration/custom_sort_runtime_worker_error_line", test_custom_sort_runtime_worker_error_line);
    g_test_add_func("/integration/custom_sort_runtime_too_many_frame_values", test_custom_sort_runtime_too_many_frame_values);
    g_test_add_func("/integration/custom_sort_runtime_too_few_frame_values", test_custom_sort_runtime_too_few_frame_values);
    g_test_add_func("/integration/custom_sort_runtime_ignores_unknown_worker_lines", test_custom_sort_runtime_ignores_unknown_worker_lines);
    g_test_add_func("/integration/custom_sort_runtime_split_lines_across_reads", test_custom_sort_runtime_split_lines_across_reads);
    g_test_add_func("/integration/custom_sort_runtime_last_error_line_wins", test_custom_sort_runtime_last_error_line_wins);
    g_test_add_func("/integration/custom_sort_runtime_last_result_line_wins", test_custom_sort_runtime_last_result_line_wins);
    g_test_add_func("/integration/custom_sort_runtime_error_after_result_fails", test_custom_sort_runtime_error_after_result_fails);

    return g_test_run();
}
