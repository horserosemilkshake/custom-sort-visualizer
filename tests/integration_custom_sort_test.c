#include "sort_engine.h"

#include <glib.h>

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

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/integration/custom_sort_compile_and_run", test_custom_sort_compile_and_run);
    g_test_add_func("/integration/custom_sort_compile_error", test_custom_sort_compile_error);

    return g_test_run();
}
