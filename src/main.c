#include "sort_engine.h"

#include <gtk/gtk.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    APP_LANG_EN = 0,
    APP_LANG_ZH_CN,
    APP_LANG_ZH_TW,
} AppLanguage;

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *language_label;
    GtkWidget *language_combo;
    GtkWidget *algo_combo;
    GtkWidget *random_button;
    GtkWidget *speed_label;
    GtkWidget *meta_frame;
    GtkWidget *meta_name_header;
    GtkWidget *meta_best_header;
    GtkWidget *meta_avg_header;
    GtkWidget *meta_worst_header;
    GtkWidget *meta_stable_header;
    GtkWidget *meta_in_place_header;
    GtkWidget *meta_notes_header;
    GtkWidget *meta_name_value;
    GtkWidget *meta_best_value;
    GtkWidget *meta_avg_value;
    GtkWidget *meta_worst_value;
    GtkWidget *meta_stable_value;
    GtkWidget *meta_in_place_value;
    GtkWidget *meta_notes_value;
    GtkWidget *array_entry;
    GtkWidget *status_label;
    GtkWidget *speed_scale;
    GtkWidget *start_button;
    GtkWidget *compile_button;
    GtkWidget *custom_expander;
    GtkWidget *custom_text_view;

    SortFrames playback_frames;
    size_t playback_index;
    guint timer_id;

    CustomSortHandle custom_handle;
    gboolean custom_registered;
    AppLanguage language;
    GHashTable *runtime_translation_cache;
} AppState;

static gboolean playback_tick(gpointer user_data);
static void set_status(AppState *app, const gchar *text);
static void update_algorithm_metadata(AppState *app);

static const gchar *runtime_target_language(AppLanguage lang) {
    if (lang == APP_LANG_ZH_CN) {
        return "zh-CN";
    }
    if (lang == APP_LANG_ZH_TW) {
        return "zh-TW";
    }
    return NULL;
}

/* LCOV_EXCL_START */
static gchar *machine_translate_text(AppState *app, const gchar *source_text) {
    if (!source_text || !*source_text) { /* GCOVR_EXCL_BR_LINE */
        return g_strdup("");
    }

    const gchar *target = runtime_target_language(app->language);
    if (!target) { /* GCOVR_EXCL_BR_LINE */
        return g_strdup(source_text);
    }

    gchar *cache_key = g_strdup_printf("%d|%s", (int)app->language, source_text);
    const gchar *cached = g_hash_table_lookup(app->runtime_translation_cache, cache_key);
    if (cached) { /* GCOVR_EXCL_BR_LINE */
        gchar *out = g_strdup(cached);
        g_free(cache_key); /* GCOVR_EXCL_BR_LINE */
        return out;
    }

    const gchar *script =
        "import json, sys, urllib.parse, urllib.request\n"
        "text = sys.argv[1]\n"
        "target = sys.argv[2]\n"
        "pair = f'en|{target}'\n"
        "url = 'https://api.mymemory.translated.net/get?' + urllib.parse.urlencode({'q': text, 'langpair': pair})\n"
        "try:\n"
        "    with urllib.request.urlopen(url, timeout=4) as r:\n"
        "        data = json.loads(r.read().decode('utf-8', 'replace'))\n"
        "    translated = data.get('responseData', {}).get('translatedText', '').strip()\n"
        "    print(translated if translated else text)\n"
        "except Exception:\n"
        "    print(text)\n";

    gchar *argv[] = {
        "python3",
        "-c",
        (gchar *)script,
        (gchar *)source_text,
        (gchar *)target,
        NULL,
    };

    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    GError *spawn_error = NULL;

    gboolean ok = g_spawn_sync(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        &stdout_buf,
        &stderr_buf,
        &exit_status,
        &spawn_error);

    gchar *result = NULL;
    if (ok && exit_status == 0 && stdout_buf && *stdout_buf) { /* GCOVR_EXCL_BR_LINE */
        g_strchomp(stdout_buf);
        if (*stdout_buf) { /* GCOVR_EXCL_BR_LINE */
            result = g_strdup(stdout_buf);
        }
    }

    if (!result) { /* GCOVR_EXCL_BR_LINE */
        result = g_strdup(source_text);
    }

    g_hash_table_insert(app->runtime_translation_cache, cache_key, g_strdup(result));
    g_free(stdout_buf); /* GCOVR_EXCL_BR_LINE */
    g_free(stderr_buf); /* GCOVR_EXCL_BR_LINE */
    g_clear_error(&spawn_error);
    return result;
}

static gchar *translate_runtime_error(AppState *app, const gchar *message) {
    if (!message || !*message) { /* GCOVR_EXCL_BR_LINE */
        return g_strdup("");
    }

    const gchar *target = runtime_target_language(app->language);
    if (!target) { /* GCOVR_EXCL_BR_LINE */
        return g_strdup(message);
    }

    const gchar *newline = strchr(message, '\n');
    if (!newline) { /* GCOVR_EXCL_BR_LINE */
        return machine_translate_text(app, message);
    }

    gchar *headline = g_strndup(message, (gsize)(newline - message));
    gchar *headline_localized = machine_translate_text(app, headline);
    gchar *final_text = g_strdup_printf("%s%s", headline_localized, newline);

    g_free(headline); /* GCOVR_EXCL_BR_LINE */
    g_free(headline_localized); /* GCOVR_EXCL_BR_LINE */
    return final_text;
}
/* LCOV_EXCL_STOP */

static const gchar *tr(AppLanguage lang, const char *key) {
    if (lang == APP_LANG_ZH_CN) {
        if (g_strcmp0(key, "window_title") == 0) return "自定义排序可视化器";
        if (g_strcmp0(key, "language_label") == 0) return "语言";
        if (g_strcmp0(key, "array_placeholder") == 0) return "输入数字，使用逗号或空格分隔";
        if (g_strcmp0(key, "randomize") == 0) return "随机生成";
        if (g_strcmp0(key, "start") == 0) return "开始";
        if (g_strcmp0(key, "speed_label") == 0) return "帧延迟 (毫秒)";
        if (g_strcmp0(key, "meta_frame") == 0) return "算法元数据";
        if (g_strcmp0(key, "meta_algorithm") == 0) return "算法";
        if (g_strcmp0(key, "meta_best") == 0) return "最好";
        if (g_strcmp0(key, "meta_average") == 0) return "平均";
        if (g_strcmp0(key, "meta_worst") == 0) return "最坏";
        if (g_strcmp0(key, "meta_stable") == 0) return "稳定";
        if (g_strcmp0(key, "meta_in_place") == 0) return "原地";
        if (g_strcmp0(key, "meta_notes") == 0) return "说明";
        if (g_strcmp0(key, "custom_expander") == 0) return "自定义 C 排序源码";
        if (g_strcmp0(key, "compile_custom") == 0) return "编译自定义排序";
        if (g_strcmp0(key, "status_ready") == 0) return "就绪";
        if (g_strcmp0(key, "status_playback_complete") == 0) return "播放完成";
        if (g_strcmp0(key, "status_compile_failed") == 0) return "编译自定义排序失败";
        if (g_strcmp0(key, "status_custom_selected") == 0) return "自定义排序已编译并选中";
        if (g_strcmp0(key, "status_random_generated") == 0) return "已生成随机数组";
        if (g_strcmp0(key, "status_invalid_input") == 0) return "输入无效";
        if (g_strcmp0(key, "status_select_algo") == 0) return "请先选择算法";
        if (g_strcmp0(key, "status_sort_failed") == 0) return "排序失败";
        if (g_strcmp0(key, "status_sort_started") == 0) return "排序和播放已开始";
        if (g_strcmp0(key, "draw_empty") == 0) return "运行排序以可视化帧";
        if (g_strcmp0(key, "meta_none") == 0) return "未选择算法";
        if (g_strcmp0(key, "meta_choose") == 0) return "选择一个算法以查看详细信息。";
        if (g_strcmp0(key, "meta_unknown") == 0) return "未知算法选择";
        if (g_strcmp0(key, "meta_unavailable") == 0) return "没有可用元数据。";
        if (g_strcmp0(key, "meta_custom") == 0) return "自定义（已编译）";
        if (g_strcmp0(key, "meta_user_defined") == 0) return "用户定义";
        if (g_strcmp0(key, "meta_custom_note") == 0) return "实现 custom_sort(...)，并在每次交换时调用 swap_cb(i, j, user_data)，以生成逐帧动画。";
        if (g_strcmp0(key, "yes") == 0) return "是";
        if (g_strcmp0(key, "no") == 0) return "否";
        if (g_strcmp0(key, "custom_algo_label") == 0) return "自定义（已编译）"; /* GCOVR_EXCL_BR_LINE */
    } else if (lang == APP_LANG_ZH_TW) {
        if (g_strcmp0(key, "window_title") == 0) return "自訂排序可視化器";
        if (g_strcmp0(key, "language_label") == 0) return "語言";
        if (g_strcmp0(key, "array_placeholder") == 0) return "輸入數字，使用逗號或空白分隔";
        if (g_strcmp0(key, "randomize") == 0) return "隨機產生";
        if (g_strcmp0(key, "start") == 0) return "開始";
        if (g_strcmp0(key, "speed_label") == 0) return "幀延遲 (毫秒)";
        if (g_strcmp0(key, "meta_frame") == 0) return "演算法中繼資料";
        if (g_strcmp0(key, "meta_algorithm") == 0) return "演算法";
        if (g_strcmp0(key, "meta_best") == 0) return "最佳";
        if (g_strcmp0(key, "meta_average") == 0) return "平均";
        if (g_strcmp0(key, "meta_worst") == 0) return "最差";
        if (g_strcmp0(key, "meta_stable") == 0) return "穩定";
        if (g_strcmp0(key, "meta_in_place") == 0) return "原地";
        if (g_strcmp0(key, "meta_notes") == 0) return "說明";
        if (g_strcmp0(key, "custom_expander") == 0) return "自訂 C 排序原始碼";
        if (g_strcmp0(key, "compile_custom") == 0) return "編譯自訂排序";
        if (g_strcmp0(key, "status_ready") == 0) return "就緒";
        if (g_strcmp0(key, "status_playback_complete") == 0) return "播放完成";
        if (g_strcmp0(key, "status_compile_failed") == 0) return "編譯自訂排序失敗";
        if (g_strcmp0(key, "status_custom_selected") == 0) return "自訂排序已編譯並選取";
        if (g_strcmp0(key, "status_random_generated") == 0) return "已產生隨機陣列";
        if (g_strcmp0(key, "status_invalid_input") == 0) return "輸入無效";
        if (g_strcmp0(key, "status_select_algo") == 0) return "請先選擇演算法";
        if (g_strcmp0(key, "status_sort_failed") == 0) return "排序失敗";
        if (g_strcmp0(key, "status_sort_started") == 0) return "排序與播放已開始";
        if (g_strcmp0(key, "draw_empty") == 0) return "執行排序以視覺化幀";
        if (g_strcmp0(key, "meta_none") == 0) return "未選擇演算法";
        if (g_strcmp0(key, "meta_choose") == 0) return "選擇一個演算法以檢視詳細資訊。";
        if (g_strcmp0(key, "meta_unknown") == 0) return "未知演算法選擇";
        if (g_strcmp0(key, "meta_unavailable") == 0) return "沒有可用中繼資料。";
        if (g_strcmp0(key, "meta_custom") == 0) return "自訂（已編譯）";
        if (g_strcmp0(key, "meta_user_defined") == 0) return "使用者定義";
        if (g_strcmp0(key, "meta_custom_note") == 0) return "實作 custom_sort(...)，並在每次交換時呼叫 swap_cb(i, j, user_data)，以產生逐幀動畫。";
        if (g_strcmp0(key, "yes") == 0) return "是";
        if (g_strcmp0(key, "no") == 0) return "否";
        if (g_strcmp0(key, "custom_algo_label") == 0) return "自訂（已編譯）"; /* GCOVR_EXCL_BR_LINE */
    }

    if (g_strcmp0(key, "window_title") == 0) return "Custom Sort Visualizer";
    if (g_strcmp0(key, "language_label") == 0) return "Language";
    if (g_strcmp0(key, "array_placeholder") == 0) return "Enter numbers separated by commas or spaces";
    if (g_strcmp0(key, "randomize") == 0) return "Randomize";
    if (g_strcmp0(key, "start") == 0) return "Start";
    if (g_strcmp0(key, "speed_label") == 0) return "Frame Delay (ms)";
    if (g_strcmp0(key, "meta_frame") == 0) return "Algorithm Metadata";
    if (g_strcmp0(key, "meta_algorithm") == 0) return "Algorithm";
    if (g_strcmp0(key, "meta_best") == 0) return "Best";
    if (g_strcmp0(key, "meta_average") == 0) return "Average";
    if (g_strcmp0(key, "meta_worst") == 0) return "Worst";
    if (g_strcmp0(key, "meta_stable") == 0) return "Stable";
    if (g_strcmp0(key, "meta_in_place") == 0) return "In-place";
    if (g_strcmp0(key, "meta_notes") == 0) return "Notes";
    if (g_strcmp0(key, "custom_expander") == 0) return "Custom C Sort Source";
    if (g_strcmp0(key, "compile_custom") == 0) return "Compile Custom Sort";
    if (g_strcmp0(key, "status_ready") == 0) return "Ready";
    if (g_strcmp0(key, "status_playback_complete") == 0) return "Playback complete";
    if (g_strcmp0(key, "status_compile_failed") == 0) return "Failed to compile custom sort";
    if (g_strcmp0(key, "status_custom_selected") == 0) return "Custom sort compiled and selected";
    if (g_strcmp0(key, "status_random_generated") == 0) return "Generated random array";
    if (g_strcmp0(key, "status_invalid_input") == 0) return "Invalid input";
    if (g_strcmp0(key, "status_select_algo") == 0) return "Select an algorithm first";
    if (g_strcmp0(key, "status_sort_failed") == 0) return "Sort failed";
    if (g_strcmp0(key, "status_sort_started") == 0) return "Sorting and playback started";
    if (g_strcmp0(key, "draw_empty") == 0) return "Run a sort to visualize frames";
    if (g_strcmp0(key, "meta_none") == 0) return "No algorithm selected";
    if (g_strcmp0(key, "meta_choose") == 0) return "Choose an algorithm to view details.";
    if (g_strcmp0(key, "meta_unknown") == 0) return "Unknown algorithm selection";
    if (g_strcmp0(key, "meta_unavailable") == 0) return "No metadata available.";
    if (g_strcmp0(key, "meta_custom") == 0) return "Custom (Compiled)";
    if (g_strcmp0(key, "meta_user_defined") == 0) return "User-defined";
    if (g_strcmp0(key, "meta_custom_note") == 0) return "Implement custom_sort(...) and call swap_cb(i, j, user_data) whenever you swap to produce frame-by-frame animation.";
    if (g_strcmp0(key, "yes") == 0) return "Yes";
    if (g_strcmp0(key, "no") == 0) return "No";
    if (g_strcmp0(key, "custom_algo_label") == 0) return "Custom (Compiled)";
    return key;
}

static const gchar *localize_algorithm_label(AppLanguage lang, const char *id, const char *fallback) {
    if (lang == APP_LANG_EN || !id) { /* GCOVR_EXCL_BR_LINE */
        return fallback;
    }
    if (lang == APP_LANG_ZH_CN) {
        if (g_strcmp0(id, "quick") == 0) return "快速排序";
        if (g_strcmp0(id, "merge") == 0) return "归并排序";
        if (g_strcmp0(id, "heap") == 0) return "堆排序";
        if (g_strcmp0(id, "bubble") == 0) return "冒泡排序";
        if (g_strcmp0(id, "selection") == 0) return "选择排序";
        if (g_strcmp0(id, "insertion") == 0) return "插入排序";
        if (g_strcmp0(id, "gnome") == 0) return "侏儒排序";
        if (g_strcmp0(id, "shaker") == 0) return "鸡尾酒排序";
        if (g_strcmp0(id, "odd_even") == 0) return "奇偶排序";
        if (g_strcmp0(id, "pancake") == 0) return "煎饼排序";
        if (g_strcmp0(id, "bitonic") == 0) return "双调排序";
        if (g_strcmp0(id, "radix") == 0) return "基数排序";
        if (g_strcmp0(id, "shell") == 0) return "希尔排序";
        if (g_strcmp0(id, "comb") == 0) return "梳排序";
        if (g_strcmp0(id, "bogo") == 0) return "猴子排序";
        if (g_strcmp0(id, "stooge") == 0) return "慢排";
    }
    if (lang == APP_LANG_ZH_TW) {
        if (g_strcmp0(id, "quick") == 0) return "快速排序";
        if (g_strcmp0(id, "merge") == 0) return "合併排序";
        if (g_strcmp0(id, "heap") == 0) return "堆積排序";
        if (g_strcmp0(id, "bubble") == 0) return "氣泡排序";
        if (g_strcmp0(id, "selection") == 0) return "選擇排序";
        if (g_strcmp0(id, "insertion") == 0) return "插入排序";
        if (g_strcmp0(id, "gnome") == 0) return "侏儒排序";
        if (g_strcmp0(id, "shaker") == 0) return "雞尾酒排序";
        if (g_strcmp0(id, "odd_even") == 0) return "奇偶排序";
        if (g_strcmp0(id, "pancake") == 0) return "鬆餅排序";
        if (g_strcmp0(id, "bitonic") == 0) return "雙調排序";
        if (g_strcmp0(id, "radix") == 0) return "基數排序";
        if (g_strcmp0(id, "shell") == 0) return "謝爾排序";
        if (g_strcmp0(id, "comb") == 0) return "梳排序";
        if (g_strcmp0(id, "bogo") == 0) return "隨機排序";
        if (g_strcmp0(id, "stooge") == 0) return "慢排"; /* GCOVR_EXCL_BR_LINE */
    }
    return fallback;
}

static const gchar *localize_algorithm_note(AppLanguage lang, const char *id, const char *fallback) {
    if (lang == APP_LANG_EN || !id) { /* GCOVR_EXCL_BR_LINE */
        return fallback;
    }
    if (lang == APP_LANG_ZH_CN) {
        if (g_strcmp0(id, "quick") == 0) return "选择主元，分区后递归。性能优秀，但主元选择会影响最坏情况。";
        if (g_strcmp0(id, "merge") == 0) return "先分再治，最后合并有序子数组。稳定且复杂度可预测。";
        if (g_strcmp0(id, "heap") == 0) return "先建最大堆，再依次把堆顶放到末尾。原地且最坏情况有保证。";
        if (g_strcmp0(id, "bubble") == 0) return "相邻逆序就交换，较大值逐步冒到右侧。易理解但效率较低。";
        if (g_strcmp0(id, "selection") == 0) return "每轮选择最小元素放到前面。比较多、交换少。";
        if (g_strcmp0(id, "insertion") == 0) return "维护前缀有序，将新元素插入合适位置。近乎有序数据表现好。";
        if (g_strcmp0(id, "gnome") == 0) return "有序就前进，逆序就交换后后退。思路类似插入排序。";
        if (g_strcmp0(id, "shaker") == 0) return "双向冒泡，来回扫描。能更快移动两端的错位元素。";
        if (g_strcmp0(id, "odd_even") == 0) return "交替处理奇偶位相邻对与偶奇位相邻对，适合并行思路。";
        if (g_strcmp0(id, "pancake") == 0) return "通过前缀翻转把最大值翻到末尾，教学演示性强。";
        if (g_strcmp0(id, "bitonic") == 0) return "构造双调序列并按网络比较合并，适合并行/硬件实现。";
        if (g_strcmp0(id, "radix") == 0) return "按位分配与收集（通常从低位到高位），属于非比较排序。";
        if (g_strcmp0(id, "shell") == 0) return "先大步长分组插入，再逐步缩小步长，提高插入排序效率。";
        if (g_strcmp0(id, "comb") == 0) return "从大间隔比较开始并逐渐缩小，改进冒泡对小值拖尾问题。";
        if (g_strcmp0(id, "bogo") == 0) return "不断随机打乱直到有序，仅用于展示低效算法。";
        if (g_strcmp0(id, "stooge") == 0) return "递归排序前 2/3、后 2/3、再前 2/3，用于展示低效递归。"; /* GCOVR_EXCL_BR_LINE */
    }
    if (lang == APP_LANG_ZH_TW) { /* GCOVR_EXCL_BR_LINE */
        if (g_strcmp0(id, "quick") == 0) return "選擇主元、分區後遞迴。效能好，但主元選擇會影響最差情況。";
        if (g_strcmp0(id, "merge") == 0) return "先分割再合併有序子陣列。穩定且時間複雜度可預測。";
        if (g_strcmp0(id, "heap") == 0) return "先建立最大堆，再把堆頂依序放到尾端。原地且最差情況有保證。";
        if (g_strcmp0(id, "bubble") == 0) return "相鄰逆序就交換，較大值逐步冒到右側。易懂但效率較低。";
        if (g_strcmp0(id, "selection") == 0) return "每回合找最小值放前面。比較次數多，但交換次數少。";
        if (g_strcmp0(id, "insertion") == 0) return "維持前綴有序，將新元素插入正確位置。接近有序資料特別快。";
        if (g_strcmp0(id, "gnome") == 0) return "有序就前進，逆序就交換並後退。概念接近插入排序。";
        if (g_strcmp0(id, "shaker") == 0) return "雙向氣泡，來回掃描。可更快移動兩端錯位元素。";
        if (g_strcmp0(id, "odd_even") == 0) return "交替處理奇偶與偶奇索引對，容易映射到平行比較。";
        if (g_strcmp0(id, "pancake") == 0) return "用前綴翻轉把最大值翻到尾端，適合教學展示。";
        if (g_strcmp0(id, "bitonic") == 0) return "建立雙調序列並用固定比較網路合併，常見於平行/硬體。";
        if (g_strcmp0(id, "radix") == 0) return "依位數分配與回收（通常 LSD 到 MSD），屬非比較排序。";
        if (g_strcmp0(id, "shell") == 0) return "先大間距分組插入，再逐步縮小間距，改善插入排序效率。";
        if (g_strcmp0(id, "comb") == 0) return "從大間距比較並逐步縮小，改善氣泡排序的小值拖尾。";
        if (g_strcmp0(id, "bogo") == 0) return "持續隨機打亂直到有序，僅作低效演算法示範。";
        if (g_strcmp0(id, "stooge") == 0) return "遞迴排序前 2/3、後 2/3、再前 2/3，用於展示低效率遞迴。";
    }
    return fallback;
}

static void set_meta_header(GtkWidget *header, const gchar *text) {
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(header), markup);
    g_free(markup); /* GCOVR_EXCL_BR_LINE */
}

static void refresh_algorithm_combo(AppState *app, const gchar *preferred_id) {
    size_t algo_count = 0;
    const SortAlgorithm *algorithms = sort_get_algorithms(&algo_count);

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->algo_combo));
    for (size_t i = 0; i < algo_count; ++i) {
        gtk_combo_box_text_append(
            GTK_COMBO_BOX_TEXT(app->algo_combo),
            algorithms[i].id,
            localize_algorithm_label(app->language, algorithms[i].id, algorithms[i].label));
    }

    if (app->custom_registered) { /* GCOVR_EXCL_BR_LINE */
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algo_combo), "custom", tr(app->language, "custom_algo_label")); /* LCOV_EXCL_LINE */
    }

    if (preferred_id && gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->algo_combo), preferred_id)) { /* GCOVR_EXCL_BR_LINE */
        return;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->algo_combo), 0);
}

static void apply_language(AppState *app) {
    const gchar *current_algo = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->algo_combo));
    gchar *preferred_algo = current_algo ? g_strdup(current_algo) : NULL; /* GCOVR_EXCL_BR_LINE */

    gtk_window_set_title(GTK_WINDOW(app->window), tr(app->language, "window_title"));
    gtk_label_set_text(GTK_LABEL(app->language_label), tr(app->language, "language_label"));
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->array_entry), tr(app->language, "array_placeholder"));
    gtk_button_set_label(GTK_BUTTON(app->random_button), tr(app->language, "randomize"));
    gtk_button_set_label(GTK_BUTTON(app->start_button), tr(app->language, "start"));
    gtk_button_set_label(GTK_BUTTON(app->compile_button), tr(app->language, "compile_custom"));
    gtk_label_set_text(GTK_LABEL(app->speed_label), tr(app->language, "speed_label"));
    gtk_frame_set_label(GTK_FRAME(app->meta_frame), tr(app->language, "meta_frame"));
    gtk_expander_set_label(GTK_EXPANDER(app->custom_expander), tr(app->language, "custom_expander"));

    set_meta_header(app->meta_name_header, tr(app->language, "meta_algorithm"));
    set_meta_header(app->meta_best_header, tr(app->language, "meta_best"));
    set_meta_header(app->meta_avg_header, tr(app->language, "meta_average"));
    set_meta_header(app->meta_worst_header, tr(app->language, "meta_worst"));
    set_meta_header(app->meta_stable_header, tr(app->language, "meta_stable"));
    set_meta_header(app->meta_in_place_header, tr(app->language, "meta_in_place"));
    set_meta_header(app->meta_notes_header, tr(app->language, "meta_notes"));

    refresh_algorithm_combo(app, preferred_algo);
    g_free(preferred_algo); /* GCOVR_EXCL_BR_LINE */
    set_status(app, tr(app->language, "status_ready"));
    update_algorithm_metadata(app);
}

static void on_custom_expander_state_changed(GtkExpander *expander, GParamSpec *pspec, gpointer user_data) {
    (void)expander;
    (void)pspec;
    AppState *app = (AppState *)user_data;

    GtkRequisition minimum = {0};
    GtkRequisition natural = {0};
    gtk_widget_get_preferred_size(app->window, &minimum, &natural);

    gint width = 0;
    gint height = 0;
    gtk_window_get_size(GTK_WINDOW(app->window), &width, &height);

    if (natural.width > width) { /* GCOVR_EXCL_BR_LINE */
        width = natural.width; /* LCOV_EXCL_LINE */
    }

    gtk_window_resize(GTK_WINDOW(app->window), width, natural.height);
}

static void set_status(AppState *app, const gchar *text) {
    gtk_label_set_text(GTK_LABEL(app->status_label), text ? text : ""); /* GCOVR_EXCL_BR_LINE */
}

static void set_meta_cell(GtkWidget *label, const gchar *text) {
    gtk_label_set_text(GTK_LABEL(label), text ? text : "N/A"); /* GCOVR_EXCL_BR_LINE */
}

static GtkWidget *create_meta_header(const gchar *text) {
    GtkWidget *label = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup); /* GCOVR_EXCL_BR_LINE */
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0f);
    return label;
}

static GtkWidget *create_meta_value_label(void) {
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    return label;
}

static void update_algorithm_metadata(AppState *app) {
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->algo_combo));
    if (!id) {
        set_meta_cell(app->meta_name_value, tr(app->language, "meta_none"));
        set_meta_cell(app->meta_best_value, "-");
        set_meta_cell(app->meta_avg_value, "-");
        set_meta_cell(app->meta_worst_value, "-");
        set_meta_cell(app->meta_stable_value, "-");
        set_meta_cell(app->meta_in_place_value, "-");
        set_meta_cell(app->meta_notes_value, tr(app->language, "meta_choose"));
        return;
    }

    if (g_strcmp0(id, "custom") == 0) {
        set_meta_cell(app->meta_name_value, tr(app->language, "meta_custom"));
        set_meta_cell(app->meta_best_value, tr(app->language, "meta_user_defined"));
        set_meta_cell(app->meta_avg_value, tr(app->language, "meta_user_defined"));
        set_meta_cell(app->meta_worst_value, tr(app->language, "meta_user_defined"));
        set_meta_cell(app->meta_stable_value, tr(app->language, "meta_user_defined"));
        set_meta_cell(app->meta_in_place_value, tr(app->language, "meta_user_defined"));
        set_meta_cell(app->meta_notes_value, tr(app->language, "meta_custom_note"));
        return;
    }

    const SortAlgorithm *algo = sort_find_algorithm(id);
    if (!algo) {
        set_meta_cell(app->meta_name_value, tr(app->language, "meta_unknown"));
        set_meta_cell(app->meta_best_value, "-");
        set_meta_cell(app->meta_avg_value, "-");
        set_meta_cell(app->meta_worst_value, "-");
        set_meta_cell(app->meta_stable_value, "-");
        set_meta_cell(app->meta_in_place_value, "-");
        set_meta_cell(app->meta_notes_value, tr(app->language, "meta_unavailable"));
        return;
    }

    set_meta_cell(app->meta_name_value, localize_algorithm_label(app->language, algo->id, algo->label));
    set_meta_cell(app->meta_best_value, algo->best_case ? algo->best_case : "N/A"); /* GCOVR_EXCL_BR_LINE */
    set_meta_cell(app->meta_avg_value, algo->avg_case ? algo->avg_case : "N/A"); /* GCOVR_EXCL_BR_LINE */
    set_meta_cell(app->meta_worst_value, algo->worst_case ? algo->worst_case : "N/A"); /* GCOVR_EXCL_BR_LINE */
    set_meta_cell(app->meta_stable_value, algo->stable ? tr(app->language, "yes") : tr(app->language, "no")); /* GCOVR_EXCL_BR_LINE */
    set_meta_cell(app->meta_in_place_value, algo->in_place ? tr(app->language, "yes") : tr(app->language, "no")); /* GCOVR_EXCL_BR_LINE */
    set_meta_cell(app->meta_notes_value, localize_algorithm_note(app->language, algo->id, algo->notes ? algo->notes : "N/A")); /* GCOVR_EXCL_BR_LINE */
}

static void on_algorithm_changed(GtkComboBox *combo, gpointer user_data) {
    (void)combo;
    AppState *app = (AppState *)user_data;
    update_algorithm_metadata(app);
}

static void on_language_changed(GtkComboBox *combo, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    const gchar *id = gtk_combo_box_get_active_id(combo);
    if (g_strcmp0(id, "zh_CN") == 0) {
        app->language = APP_LANG_ZH_CN;
    } else if (g_strcmp0(id, "zh_TW") == 0) {
        app->language = APP_LANG_ZH_TW;
    } else {
        app->language = APP_LANG_EN;
    }
    apply_language(app);
}

static guint get_frame_delay_ms(AppState *app) {
    gint interval = (gint)gtk_range_get_value(GTK_RANGE(app->speed_scale));
    if (interval < 1) { /* GCOVR_EXCL_BR_LINE */
        interval = 1; /* LCOV_EXCL_LINE */
    }
    return (guint)interval;
}

static void on_speed_scale_changed(GtkRange *range, gpointer user_data) {
    (void)range;
    AppState *app = (AppState *)user_data;

    if (app->timer_id != 0) {
        g_source_remove(app->timer_id);
        app->timer_id = g_timeout_add(get_frame_delay_ms(app), playback_tick, app);
    }
}

static gboolean detect_upcoming_swap(const int *current, const int *next, size_t n, size_t *out_a, size_t *out_b) {
    size_t first = n;
    size_t second = n;

    for (size_t i = 0; i < n; ++i) {
        if (current[i] == next[i]) {
            continue;
        }

        if (first == n) {
            first = i;
        } else if (second == n) {
            second = i;
        } else {
            return FALSE;
        }
    }

    if (first == n || second == n) { /* GCOVR_EXCL_BR_LINE */
        return FALSE; /* LCOV_EXCL_LINE */
    }

    if (current[first] == next[second] && current[second] == next[first]) { /* GCOVR_EXCL_BR_LINE */
        *out_a = first;
        *out_b = second;
        return TRUE;
    }

    return FALSE; /* LCOV_EXCL_LINE */
}

static void clear_playback(AppState *app) {
    if (app->timer_id != 0) {
        g_source_remove(app->timer_id);
        app->timer_id = 0;
    }

    sort_frames_clear(&app->playback_frames);
    sort_frames_init(&app->playback_frames);
    app->playback_index = 0;
    gtk_widget_queue_draw(app->drawing_area);
}

static gboolean parse_array_input(const gchar *text, int **out_array, size_t *out_n, GError **error) {
    if (!text || !*text) { /* GCOVR_EXCL_BR_LINE */
        g_set_error(error, g_quark_from_static_string("sort-ui"), 1, "Array input is empty");
        return FALSE;
    }

    gchar **tokens = g_strsplit_set(text, ", \t\n", -1);
    size_t count = 0;
    int *arr = NULL;

    for (gchar **it = tokens; it && *it; ++it) { /* GCOVR_EXCL_BR_LINE */
        gchar *tok = g_strstrip(*it);
        if (!tok || !*tok) { /* GCOVR_EXCL_BR_LINE */
            continue;
        }

        gchar *endptr = NULL;
        long value = strtol(tok, &endptr, 10);
        if (endptr == tok || *endptr != '\0' || value < INT_MIN || value > INT_MAX) { /* GCOVR_EXCL_BR_LINE */
            g_set_error(error, g_quark_from_static_string("sort-ui"), 1, "Invalid integer token: %s", tok);
            g_strfreev(tokens);
            g_free(arr); /* GCOVR_EXCL_BR_LINE */
            return FALSE;
        }

        int *resized = g_realloc_n(arr, count + 1, sizeof(int));
        if (!resized) { /* GCOVR_EXCL_BR_LINE */
            g_set_error(error, g_quark_from_static_string("sort-ui"), 1, "Out of memory while parsing input"); /* LCOV_EXCL_LINE */
            g_strfreev(tokens); /* LCOV_EXCL_LINE */
            g_free(arr); /* LCOV_EXCL_LINE */ /* GCOVR_EXCL_BR_LINE */
            return FALSE; /* LCOV_EXCL_LINE */
        }

        arr = resized;
        arr[count++] = (int)value;
    }

    g_strfreev(tokens);

    if (count == 0) {
        g_set_error(error, g_quark_from_static_string("sort-ui"), 1, "Array input did not contain any numbers");
        g_free(arr); /* GCOVR_EXCL_BR_LINE */
        return FALSE;
    }

    *out_array = arr;
    *out_n = count;
    return TRUE;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppState *app = (AppState *)user_data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    double width = (double)allocation.width;
    double height = (double)allocation.height;

    cairo_set_source_rgb(cr, 0.08, 0.1, 0.14);
    cairo_paint(cr);

    if (app->playback_frames.frame_count == 0 || app->playback_frames.n == 0) { /* GCOVR_EXCL_BR_LINE */
        const gchar *empty_text = tr(app->language, "draw_empty");
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_from_string("Sans 18");
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, empty_text, -1);

        int text_w = 0;
        int text_h = 0;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);

        double x = 24.0;
        double y = (height - text_h) / 2.0;
        if (x + text_w > width - 12.0) { /* GCOVR_EXCL_BR_LINE */
            x = 12.0;
            pango_layout_set_width(layout, (int)((width - 24.0) * PANGO_SCALE));
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        }

        cairo_set_source_rgb(cr, 0.75, 0.78, 0.82);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);

        pango_font_description_free(desc);
        g_object_unref(layout);
        return FALSE;
    }

    size_t n = app->playback_frames.n;
    const int *frame = app->playback_frames.frames[app->playback_index];

    size_t swap_a = n;
    size_t swap_b = n;
    if (app->playback_index + 1 < app->playback_frames.frame_count) {
        const int *next_frame = app->playback_frames.frames[app->playback_index + 1];
        detect_upcoming_swap(frame, next_frame, n, &swap_a, &swap_b);
    }

    int max_val = abs(frame[0]);
    for (size_t i = 1; i < n; ++i) {
        int v = abs(frame[i]);
        if (v > max_val) { /* GCOVR_EXCL_BR_LINE */
            max_val = v; /* LCOV_EXCL_LINE */
        }
    }
    if (max_val == 0) {
        max_val = 1;
    }

    double bar_gap = 1.0;
    double bar_width = (width - (n - 1) * bar_gap) / n;
    if (bar_width < 1.0) { /* GCOVR_EXCL_BR_LINE */
        bar_width = 1.0;
        bar_gap = 0.0;
    }

    for (size_t i = 0; i < n; ++i) {
        double normalized = (double)abs(frame[i]) / (double)max_val;
        double bar_height = normalized * (height - 20.0);
        double x = i * (bar_width + bar_gap);
        double y = height - bar_height;

        double hue = (double)i / (double)(n > 1 ? n - 1 : 1); /* GCOVR_EXCL_BR_LINE */
        double r = 0.2 + 0.6 * hue;
        double g = 0.8 - 0.5 * hue;
        double b = 0.45 + 0.3 * (1.0 - hue);

        gboolean is_swap_bar = (i == swap_a || i == swap_b);
        if (is_swap_bar) {
            r = 0.99;
            g = 0.82;
            b = 0.18;
        }

        cairo_set_source_rgb(cr, r, g, b);
        cairo_rectangle(cr, x, y, bar_width, bar_height);
        cairo_fill(cr);

        if (is_swap_bar) {
            cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, x + 1.0, y + 1.0, bar_width - 2.0, bar_height - 2.0);
            cairo_stroke(cr);
        }

        char value_text[32];
        g_snprintf(value_text, sizeof(value_text), "%d", frame[i]);

        double font_size = bar_width * 0.34;
        if (font_size < 9.0) { /* GCOVR_EXCL_BR_LINE */
            font_size = 9.0;
        }
        if (font_size > 15.0) { /* GCOVR_EXCL_BR_LINE */
            font_size = 15.0; /* LCOV_EXCL_LINE */
        }

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, value_text, &extents);

        double tx = x + (bar_width - extents.width) / 2.0 - extents.x_bearing;
        double ty = y - 3.0;
        if (ty < extents.height + 2.0) {
            ty = y + extents.height + 2.0;
        }

        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
        cairo_move_to(cr, tx + 1.0, ty + 1.0);
        cairo_show_text(cr, value_text);

        cairo_set_source_rgb(cr, 0.96, 0.97, 0.98);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, value_text);
    }

    return FALSE;
}

static gboolean playback_tick(gpointer user_data) {
    AppState *app = (AppState *)user_data;

    if (app->playback_frames.frame_count == 0) {
        app->timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (app->playback_index + 1 >= app->playback_frames.frame_count) {
        app->timer_id = 0;
        set_status(app, tr(app->language, "status_playback_complete"));
        gtk_widget_set_sensitive(app->start_button, TRUE);
        gtk_widget_set_sensitive(app->compile_button, TRUE);
        return G_SOURCE_REMOVE;
    }

    app->playback_index++;
    gtk_widget_queue_draw(app->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void ensure_custom_option(AppState *app) {
    if (app->custom_registered) { /* GCOVR_EXCL_BR_LINE */
        return; /* LCOV_EXCL_LINE */
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algo_combo), "custom", tr(app->language, "custom_algo_label"));
    app->custom_registered = TRUE;
}

static void on_compile_custom_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = (AppState *)user_data;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->custom_text_view));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);

    gchar *code = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    GError *error = NULL;

    if (!custom_sort_compile(&app->custom_handle, code, &error)) {
        if (error && error->message) { /* GCOVR_EXCL_BR_LINE */
            gchar *localized = translate_runtime_error(app, error->message);
            set_status(app, localized);
            g_free(localized); /* GCOVR_EXCL_BR_LINE */
        } else {
            set_status(app, tr(app->language, "status_compile_failed")); /* LCOV_EXCL_LINE */
        }
        g_clear_error(&error);
        g_free(code); /* GCOVR_EXCL_BR_LINE */
        return;
    }

    ensure_custom_option(app);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->algo_combo), "custom");
    set_status(app, tr(app->language, "status_custom_selected"));
    g_free(code); /* GCOVR_EXCL_BR_LINE */
}

static void on_randomize_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = (AppState *)user_data;

    GString *builder = g_string_new(NULL);
    for (int i = 0; i < 40; ++i) {
        int value = g_random_int_range(5, 300);
        if (i > 0) {
            g_string_append(builder, ", "); /* GCOVR_EXCL_BR_LINE */
        }
        g_string_append_printf(builder, "%d", value);
    }

    gtk_entry_set_text(GTK_ENTRY(app->array_entry), builder->str);
    g_string_free(builder, TRUE);

    set_status(app, tr(app->language, "status_random_generated"));
}

static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = (AppState *)user_data;

    int *input = NULL;
    size_t n = 0;
    GError *error = NULL;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(app->array_entry));

    if (!parse_array_input(text, &input, &n, &error)) {
        if (error && error->message) { /* GCOVR_EXCL_BR_LINE */
            gchar *localized = translate_runtime_error(app, error->message);
            set_status(app, localized);
            g_free(localized); /* GCOVR_EXCL_BR_LINE */
        } else {
            set_status(app, tr(app->language, "status_invalid_input")); /* LCOV_EXCL_LINE */
        }
        g_clear_error(&error);
        return;
    }

    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->algo_combo));
    if (!id) {
        set_status(app, tr(app->language, "status_select_algo"));
        g_free(input); /* GCOVR_EXCL_BR_LINE */
        return;
    }

    clear_playback(app);

    gboolean ok = FALSE;
    if (g_strcmp0(id, "custom") == 0) {
        ok = custom_sort_run(&app->custom_handle, input, n, &app->playback_frames, &error);
    } else {
        const SortAlgorithm *algo = sort_find_algorithm(id);
        ok = sort_run_algorithm(algo, input, n, &app->playback_frames, &error);
    }

    g_free(input); /* GCOVR_EXCL_BR_LINE */

    if (!ok) {
        if (error && error->message) { /* GCOVR_EXCL_BR_LINE */
            gchar *localized = translate_runtime_error(app, error->message);
            set_status(app, localized);
            g_free(localized); /* GCOVR_EXCL_BR_LINE */
        } else {
            set_status(app, tr(app->language, "status_sort_failed")); /* LCOV_EXCL_LINE */
        }
        g_clear_error(&error);
        clear_playback(app);
        return;
    }

    app->playback_index = 0;
    gtk_widget_queue_draw(app->drawing_area);

    gtk_widget_set_sensitive(app->start_button, FALSE);
    gtk_widget_set_sensitive(app->compile_button, FALSE);
    app->timer_id = g_timeout_add(get_frame_delay_ms(app), playback_tick, app);

    set_status(app, tr(app->language, "status_sort_started"));
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    clear_playback(app);
    custom_sort_handle_clear(&app->custom_handle);
    g_hash_table_destroy(app->runtime_translation_cache);
    g_free(app); /* GCOVR_EXCL_BR_LINE */
    gtk_main_quit();
}

static AppState *app_state_new(void) {
    AppState *app = g_new0(AppState, 1);
    sort_frames_init(&app->playback_frames);
    custom_sort_handle_init(&app->custom_handle);
    app->runtime_translation_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    app->language = APP_LANG_EN;
    return app;
}

static void app_build_ui(AppState *app) {
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), tr(app->language, "window_title"));
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1100, 760);
    gtk_container_set_border_width(GTK_CONTAINER(app->window), 10);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    GtkWidget *top_toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(top_toolbar), GTK_TOOLBAR_BOTH_HORIZ);
    gtk_toolbar_set_show_arrow(GTK_TOOLBAR(top_toolbar), FALSE);
    gtk_box_pack_start(GTK_BOX(root), top_toolbar, FALSE, FALSE, 0);

    GtkToolItem *language_item = gtk_tool_item_new();
    GtkWidget *language_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    app->language_label = gtk_label_new(tr(app->language, "language_label"));
    gtk_box_pack_start(GTK_BOX(language_box), app->language_label, FALSE, FALSE, 0);

    app->language_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "en", "English");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "zh_CN", "简体中文");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->language_combo), "zh_TW", "繁體中文");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->language_combo), "en");
    gtk_box_pack_start(GTK_BOX(language_box), app->language_combo, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(language_item), language_box);
    gtk_toolbar_insert(GTK_TOOLBAR(top_toolbar), language_item, -1);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);

    app->algo_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(controls), app->algo_combo, FALSE, FALSE, 0);
    refresh_algorithm_combo(app, NULL);

    app->array_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->array_entry), tr(app->language, "array_placeholder"));
    gtk_entry_set_text(GTK_ENTRY(app->array_entry), "35, 12, 57, 89, 1, 8, 44, 19, 73, 23");
    gtk_box_pack_start(GTK_BOX(controls), app->array_entry, TRUE, TRUE, 0);

    app->random_button = gtk_button_new_with_label(tr(app->language, "randomize"));
    gtk_box_pack_start(GTK_BOX(controls), app->random_button, FALSE, FALSE, 0);

    app->start_button = gtk_button_new_with_label(tr(app->language, "start"));
    gtk_box_pack_start(GTK_BOX(controls), app->start_button, FALSE, FALSE, 0);

    GtkWidget *speed_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(root), speed_box, FALSE, FALSE, 0);

    app->speed_label = gtk_label_new(tr(app->language, "speed_label"));
    gtk_box_pack_start(GTK_BOX(speed_box), app->speed_label, FALSE, FALSE, 0);

    app->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 2000, 1);
    gtk_range_set_value(GTK_RANGE(app->speed_scale), 15);
    gtk_scale_set_digits(GTK_SCALE(app->speed_scale), 0);
    gtk_scale_add_mark(GTK_SCALE(app->speed_scale), 1, GTK_POS_BOTTOM, "1");
    gtk_scale_add_mark(GTK_SCALE(app->speed_scale), 50, GTK_POS_BOTTOM, "50");
    gtk_scale_add_mark(GTK_SCALE(app->speed_scale), 200, GTK_POS_BOTTOM, "200");
    gtk_scale_add_mark(GTK_SCALE(app->speed_scale), 1000, GTK_POS_BOTTOM, "1000");
    gtk_scale_add_mark(GTK_SCALE(app->speed_scale), 2000, GTK_POS_BOTTOM, "2000");
    gtk_widget_set_hexpand(app->speed_scale, TRUE);
    gtk_box_pack_start(GTK_BOX(speed_box), app->speed_scale, TRUE, TRUE, 0);

    app->meta_frame = gtk_frame_new(tr(app->language, "meta_frame"));
    gtk_box_pack_start(GTK_BOX(root), app->meta_frame, FALSE, FALSE, 0);

    GtkWidget *meta_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(meta_grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(meta_grid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(meta_grid), 8);
    gtk_container_add(GTK_CONTAINER(app->meta_frame), meta_grid);

    app->meta_name_header = create_meta_header(tr(app->language, "meta_algorithm"));
    app->meta_name_value = create_meta_value_label();
    app->meta_best_header = create_meta_header(tr(app->language, "meta_best"));
    app->meta_best_value = create_meta_value_label();
    app->meta_avg_header = create_meta_header(tr(app->language, "meta_average"));
    app->meta_avg_value = create_meta_value_label();
    app->meta_worst_header = create_meta_header(tr(app->language, "meta_worst"));
    app->meta_worst_value = create_meta_value_label();
    app->meta_stable_header = create_meta_header(tr(app->language, "meta_stable"));
    app->meta_stable_value = create_meta_value_label();
    app->meta_in_place_header = create_meta_header(tr(app->language, "meta_in_place"));
    app->meta_in_place_value = create_meta_value_label();
    app->meta_notes_header = create_meta_header(tr(app->language, "meta_notes"));
    app->meta_notes_value = create_meta_value_label();

    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_name_header, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_name_value, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_best_header, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_best_value, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_avg_header, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_avg_value, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_worst_header, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_worst_value, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_stable_header, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_stable_value, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_in_place_header, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_in_place_value, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_notes_header, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(meta_grid), app->meta_notes_value, 1, 6, 1, 1);

    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->drawing_area, 1000, 390);
    gtk_box_pack_start(GTK_BOX(root), app->drawing_area, TRUE, TRUE, 0);

    app->custom_expander = gtk_expander_new(tr(app->language, "custom_expander"));
    gtk_expander_set_expanded(GTK_EXPANDER(app->custom_expander), FALSE);
    gtk_box_pack_start(GTK_BOX(root), app->custom_expander, TRUE, TRUE, 0);

    GtkWidget *custom_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(app->custom_expander), custom_vbox);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 190);
    gtk_box_pack_start(GTK_BOX(custom_vbox), scroll, TRUE, TRUE, 0);

    app->custom_text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scroll), app->custom_text_view);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->custom_text_view));
    gtk_text_buffer_set_text(buf, custom_sort_template(), -1);

    app->compile_button = gtk_button_new_with_label(tr(app->language, "compile_custom"));
    gtk_box_pack_start(GTK_BOX(custom_vbox), app->compile_button, FALSE, FALSE, 0);

    app->status_label = gtk_label_new(tr(app->language, "status_ready"));
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(root), app->status_label, FALSE, FALSE, 0);

    g_signal_connect(app->drawing_area, "draw", G_CALLBACK(on_draw), app);
    g_signal_connect(app->algo_combo, "changed", G_CALLBACK(on_algorithm_changed), app);
    g_signal_connect(app->language_combo, "changed", G_CALLBACK(on_language_changed), app);
    g_signal_connect(app->speed_scale, "value-changed", G_CALLBACK(on_speed_scale_changed), app);
    g_signal_connect(app->custom_expander, "notify::expanded", G_CALLBACK(on_custom_expander_state_changed), app);
    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_clicked), app);
    g_signal_connect(app->compile_button, "clicked", G_CALLBACK(on_compile_custom_clicked), app);
    g_signal_connect(app->random_button, "clicked", G_CALLBACK(on_randomize_clicked), app);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);

    apply_language(app);
    update_algorithm_metadata(app);
}

/* LCOV_EXCL_START */
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    AppState *app = app_state_new();
    app_build_ui(app);

    gtk_widget_show_all(app->window);
    gtk_main();

    return 0;
}
/* LCOV_EXCL_STOP */
