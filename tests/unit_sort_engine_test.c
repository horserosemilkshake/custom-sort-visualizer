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

static void test_algorithm_registry(void) {
    size_t count = 0;
    const SortAlgorithm *algos = sort_get_algorithms(&count);

    g_assert_nonnull(algos);
    g_assert_cmpuint(count, >, 0);
    g_assert_nonnull(sort_find_algorithm("quick"));
    g_assert_nonnull(sort_find_algorithm("merge"));
    g_assert_null(sort_find_algorithm("does-not-exist"));
}

static void test_frame_capture_size_mismatch(void) {
    SortFrames frames;
    GError *error = NULL;

    int first[] = {3, 1, 2};
    int second[] = {9, 8, 7, 6};

    sort_frames_init(&frames);
    g_assert_true(sort_frames_capture(&frames, first, G_N_ELEMENTS(first), &error));
    g_assert_no_error(error);

    g_assert_false(sort_frames_capture(&frames, second, G_N_ELEMENTS(second), &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Frame size mismatch"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
}

static void test_run_deterministic_algorithms(void) {
    static const char *algorithm_ids[] = {
        "quick",
        "merge",
        "heap",
        "bubble",
        "selection",
        "insertion",
        "gnome",
        "shaker",
        "odd_even",
        "pancake",
        "bitonic",
        "radix",
        "shell",
        "comb",
        "stooge",
        /* Skip bogo here because it relies on randomness and can cause errors in CI timing windows. */
    };

    int input[] = {5, -1, 4, 2, 8, 0, 7, 3};

    for (size_t i = 0; i < G_N_ELEMENTS(algorithm_ids); ++i) {
        const SortAlgorithm *algo = sort_find_algorithm(algorithm_ids[i]);
        SortFrames frames;
        GError *error = NULL;
        gboolean ok = FALSE;

        g_assert_nonnull(algo);

        sort_frames_init(&frames);

        ok = sort_run_algorithm(algo, input, G_N_ELEMENTS(input), &frames, &error);
        if (!ok && error) {
            g_test_message("Algorithm '%s' failed: %s", algorithm_ids[i], error->message);
        }

        g_assert_true(ok);
        g_assert_no_error(error);
        g_assert_cmpuint(frames.frame_count, >, 0);
        g_assert_cmpuint(frames.n, ==, G_N_ELEMENTS(input));
        g_assert_cmpmem(frames.frames[0], sizeof(input), input, sizeof(input));
        g_assert_true(array_is_sorted(frames.frames[frames.frame_count - 1], frames.n));

        g_clear_error(&error);
        sort_frames_clear(&frames);
    }
}

static void test_bogo_limit_error(void) {
    SortFrames frames;
    GError *error = NULL;
    int input[11] = {0};

    const SortAlgorithm *algo = sort_find_algorithm("bogo");
    g_assert_nonnull(algo);

    sort_frames_init(&frames);

    g_assert_false(sort_run_algorithm(algo, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Bogo sort is limited"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
}

static void test_sort_run_algorithm_invalid_algorithm(void) {
    SortFrames frames;
    GError *error = NULL;
    int input[] = {3, 2, 1};

    sort_frames_init(&frames);
    g_assert_false(sort_run_algorithm(NULL, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Invalid algorithm"));

    g_clear_error(&error);
    sort_frames_clear(&frames);
}

static void test_custom_api_preconditions(void) {
    CustomSortHandle handle;
    SortFrames frames;
    GError *error = NULL;
    int input[] = {5, 1, 3};

    custom_sort_handle_init(&handle);
    sort_frames_init(&frames);

    g_assert_false(custom_sort_compile(&handle, "", &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "Custom code cannot be empty"));
    g_clear_error(&error);

    g_assert_false(custom_sort_run(&handle, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_error(error, g_quark_from_static_string("sort-visualizer-error"), 1);
    g_assert_nonnull(strstr(error->message, "No compiled custom sort available"));
    g_clear_error(&error);

    sort_frames_clear(&frames);
    custom_sort_handle_clear(&handle);
}

static void test_bogo_small_input_success(void) {
    SortFrames frames;
    GError *error = NULL;
    int input[] = {2, 1};

    const SortAlgorithm *algo = sort_find_algorithm("bogo");
    g_assert_nonnull(algo);

    sort_frames_init(&frames);

    g_assert_true(sort_run_algorithm(algo, input, G_N_ELEMENTS(input), &frames, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(frames.frame_count, >, 0);
    g_assert_true(array_is_sorted(frames.frames[frames.frame_count - 1], frames.n));

    sort_frames_clear(&frames);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/sort_engine/registry", test_algorithm_registry);
    g_test_add_func("/sort_engine/frame_size_mismatch", test_frame_capture_size_mismatch);
    g_test_add_func("/sort_engine/run_deterministic_algorithms", test_run_deterministic_algorithms);
    g_test_add_func("/sort_engine/bogo_limit", test_bogo_limit_error);
    g_test_add_func("/sort_engine/invalid_algorithm", test_sort_run_algorithm_invalid_algorithm);
    g_test_add_func("/sort_engine/custom_preconditions", test_custom_api_preconditions);
    g_test_add_func("/sort_engine/bogo_small_success", test_bogo_small_input_success);

    return g_test_run();
}
