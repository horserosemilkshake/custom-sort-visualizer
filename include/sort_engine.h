#ifndef SORT_ENGINE_H
#define SORT_ENGINE_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

G_BEGIN_DECLS

/**
 * @brief Captured snapshots produced while a sort runs.
 *
 * Each entry in @p frames points to an array of @p n integers.
 */
typedef struct {
    /** Captured frame buffers. */
    int **frames;
    /** Number of valid snapshots in @p frames. */
    size_t frame_count;
    /** Allocated capacity of @p frames. */
    size_t frame_capacity;
    /** Element count in each frame. */
    size_t n;
} SortFrames;

/**
 * @brief Description and runner for a built-in sorting algorithm.
 */
typedef struct {
    /** Stable internal identifier used for lookup (for example quick). */
    const char *id;
    /** Display name shown in the UI. */
    const char *label;
    /** Best-case complexity text. */
    const char *best_case;
    /** Average-case complexity text. */
    const char *avg_case;
    /** Worst-case complexity text. */
    const char *worst_case;
    /** Whether relative order of equal keys is preserved. */
    gboolean stable;
    /** Whether the algorithm sorts in place. */
    gboolean in_place;
    /** Human-readable explanation of behavior and tradeoffs. */
    const char *notes;
    /** Algorithm execution callback used by the engine. */
    gboolean (*run)(int *arr, size_t n, SortFrames *frames, GError **error);
} SortAlgorithm;

/**
 * @brief Lifecycle state for user-provided custom sort code.
 */
typedef struct {
    /** Temporary directory where custom source and binary are stored. */
    gchar *build_dir;
    /** Path to generated custom C source file. */
    gchar *source_path;
    /** Path to compiled dynamic library. */
    gchar *library_path;
    /** Dynamic loader handle returned by GModule. */
    void *dl_handle;
    /** Resolved custom_sort symbol from the compiled library. */
    int (*custom_sort_fn)(int *arr, size_t n, void (*swap_cb)(size_t, size_t, void *), void *user_data);
} CustomSortHandle;

/**
 * @brief Initialize a SortFrames container.
 * @param frames Destination frame container.
 */
void sort_frames_init(SortFrames *frames);

/**
 * @brief Release all memory associated with captured frames.
 * @param frames Frame container to clear.
 */
void sort_frames_clear(SortFrames *frames);

/**
 * @brief Append a snapshot of the current array state.
 * @param frames Destination frame container.
 * @param arr Source array to copy.
 * @param n Number of elements in @p arr.
 * @param error Optional error output.
 * @return TRUE on success, FALSE on validation or allocation failure.
 */
gboolean sort_frames_capture(SortFrames *frames, const int *arr, size_t n, GError **error);

/**
 * @brief Get the immutable built-in algorithm list.
 * @param count Optional output for algorithm count.
 * @return Pointer to static array of SortAlgorithm entries.
 */
const SortAlgorithm *sort_get_algorithms(size_t *count);

/**
 * @brief Find a built-in algorithm by id.
 * @param id Internal algorithm identifier.
 * @return Matching algorithm or NULL when not found.
 */
const SortAlgorithm *sort_find_algorithm(const char *id);

/**
 * @brief Execute a built-in algorithm and capture all animation frames.
 * @param algorithm Algorithm descriptor to run.
 * @param input Input array values.
 * @param n Number of elements in @p input.
 * @param frames Destination frame container.
 * @param error Optional error output.
 * @return TRUE on success, FALSE on error.
 */
gboolean sort_run_algorithm(const SortAlgorithm *algorithm, const int *input, size_t n, SortFrames *frames, GError **error);

/**
 * @brief Initialize a custom sort handle.
 * @param handle Handle to initialize.
 */
void custom_sort_handle_init(CustomSortHandle *handle);

/**
 * @brief Free resources associated with a custom sort handle.
 * @param handle Handle to clear.
 */
void custom_sort_handle_clear(CustomSortHandle *handle);

/**
 * @brief Compile user-provided C code into a loadable custom sort module.
 * @param handle Custom handle that receives compiled module state.
 * @param user_code C source implementing custom_sort.
 * @param error Optional error output.
 * @return TRUE on successful compile and symbol resolution.
 */
gboolean custom_sort_compile(CustomSortHandle *handle, const char *user_code, GError **error);

/**
 * @brief Execute previously compiled custom sort code and capture frames.
 * @param handle Compiled custom sort handle.
 * @param input Input array values.
 * @param n Number of elements in @p input.
 * @param frames Destination frame container.
 * @param error Optional error output.
 * @return TRUE on success, FALSE when runtime errors occur.
 */
gboolean custom_sort_run(CustomSortHandle *handle, const int *input, size_t n, SortFrames *frames, GError **error);

/**
 * @brief Return starter source code for implementing custom_sort.
 * @return Null-terminated template string.
 */
const char *custom_sort_template(void);

G_END_DECLS

#endif
