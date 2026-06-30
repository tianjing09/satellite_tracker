#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <pthread.h>  // 用于多线程并行

// ========== 错误码定义 ==========
#define SUCCESS 0
#define ERROR_IMAGE_LOAD 1001
#define ERROR_IMAGE_FORMAT 1002
#define ERROR_IMAGE_CORRUPT 1003
#define ERROR_STEP1_FAILED 2001
#define ERROR_STEP2_FAILED 2002
#define ERROR_STEP3_FAILED 3001
#define ERROR_STEP4_FAILED 4001
#define ERROR_STEP5_FAILED 5001
#define ERROR_FOLDER_OPEN 6001
#define ERROR_NO_IMAGES 6002
#define ERROR_UNKNOWN 9999


// ========== 回调函数类型 ==========
// step: 当前执行到第几步 (1-5)
// message: 描述信息
// data: step1=filtered路径, step2=enhanced路径, 其他=文件名
// current: 当前完成的数量
// total: 总数量
typedef void (*ProgressCallback)(int step, const char* message, int progress,
                                  int error_code, const char* error_msg, 
                                  const char* data, int current, int total);

// ========== 轨迹点结构 ==========
typedef struct {
    double x;
    double y;
} Point;

// ========== 单条轨迹结构 ==========
typedef struct {
    int id;
    int norad_id;
    char* name;
    double confidence;
    Point* points;
    int point_count;
} Track;

// ========== 全局结果 ==========
typedef struct {
    Track* tracks;
    int track_count;
    int total_images;
    int success_count;
} BatchResult;

// ========== 图片处理结果 ==========
typedef struct {
    char* original_path;
    char* filtered_path;
    char* enhanced_path;
    char* status;      // "ok" 或 "failed"
    char* error;
    int image_index;
} ImageResult;

// ========== 批量结果 ==========
typedef struct {
    ImageResult* results;
    int total;
    int success_count;
    int failed_count;
} ProcessResult;

// ========== 线程参数结构 ==========
typedef struct {
    int image_index;
    char* input_path;
    char* output_path;
    int step;
    int error_code;
    char error_msg[256];
} ThreadArgs;

// ========== 错误信息 ==========
const char* get_error_message(int error_code) {
    switch (error_code) {
        case ERROR_IMAGE_LOAD: return "无法加载图片文件";
        case ERROR_IMAGE_FORMAT: return "不支持的图片格式";
        case ERROR_IMAGE_CORRUPT: return "图片文件已损坏";
        case ERROR_STEP1_FAILED: return "步骤1失败: 去除恒星背景";
        case ERROR_STEP2_FAILED: return "步骤2失败: 提取候选移动光点";
        case ERROR_STEP3_FAILED: return "步骤3失败: 跨图片轨迹关联";
        case ERROR_STEP4_FAILED: return "步骤4失败: 过滤误检与噪声";
        case ERROR_STEP5_FAILED: return "步骤5失败: 生成卫星轨迹";
        case ERROR_FOLDER_OPEN: return "无法打开文件夹";
        case ERROR_NO_IMAGES: return "没有找到图片文件";
        default: return "未知错误";
    }
}

// ========== 随机错误配置 ==========
typedef struct {
    int step;
    int error_code;
    const char* error_msg;
    float probability;
} SimulatedError;

SimulatedError possible_errors[] = {
    {1, ERROR_STEP1_FAILED, "去除恒星背景失败", 0.08},
    {2, ERROR_STEP2_FAILED, "提取候选移动光点失败", 0.06},
    {3, ERROR_STEP3_FAILED, "跨图片轨迹关联失败", 0.05},
    {4, ERROR_STEP4_FAILED, "过滤误检与噪声失败", 0.05},
    {5, ERROR_STEP5_FAILED, "生成卫星轨迹失败", 0.04},
};

const int num_possible_errors = sizeof(possible_errors) / sizeof(possible_errors[0]);
static bool random_initialized = false;

int check_for_error(int current_step, int* out_error_code, const char** out_error_msg) {
    if (!random_initialized) {
        srand((unsigned int)time(NULL));
        random_initialized = true;
    }

    for (int i = 0; i < num_possible_errors; i++) {
        if (possible_errors[i].step == current_step) {
            int rand_val = rand() % 100;
            float threshold = possible_errors[i].probability * 100;
            if (rand_val < threshold) {
                *out_error_code = possible_errors[i].error_code;
                *out_error_msg = possible_errors[i].error_msg;
                return -1;
            }
        }
    }
    return 0;
}

char* make_error_response(int error_code, const char* error_msg, int step) {
    char* result = malloc(512);
    if (!result) return NULL;
    snprintf(result, 512,
        "{\"status\":\"error\",\"step\":%d,\"error_code\":%d,\"error_message\":\"%s\"}",
        step, error_code, error_msg);
    return result;
}

// ========== 辅助函数 ==========
const char* get_filename(const char* path) {
    const char* last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

char* remove_extension(const char* filename) {
    char* result = strdup(filename);
    char* dot = strrchr(result, '.');
    if (dot) *dot = '\0';
    return result;
}

char* build_output_path(const char* output_dir, const char* original_path, const char* suffix) {
    const char* filename = get_filename(original_path);
    char* basename = remove_extension(filename);
    
    char* output_path = malloc(1024);
    snprintf(output_path, 1024, "%s/%s_%s.jpg", output_dir, basename, suffix);
    
    free(basename);
    return output_path;
}

// ========== 模拟处理步骤（单张图片） ==========
int simulate_step_single(const char* input_path, const char* output_path, int step_num) {
    // 检查输入文件是否存在
    FILE* input = fopen(input_path, "rb");
    if (!input) {
        return 1;
    }
    fclose(input);
    
    // 模拟处理时间 (5-10 秒)
    int sleep_time = 5 + (rand() % 6);
    sleep(sleep_time);
    
    // 模拟写入输出文件
    FILE* output = fopen(output_path, "wb");
    if (!output) {
        return 1;
    }
    fprintf(output, "Simulated step %d output for: %s\n", step_num, input_path);
    fclose(output);
    
    return 0;  // 成功
}

// ========== 多线程执行步骤 ==========
void* process_step_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    
    int result = simulate_step_single(args->input_path, args->output_path, args->step);
    
    if (result != 0) {
        args->error_code = args->step == 1 ? ERROR_STEP1_FAILED : ERROR_STEP2_FAILED;
        snprintf(args->error_msg, sizeof(args->error_msg), 
                 "步骤%d处理失败", args->step);
    } else {
        args->error_code = 0;
        args->error_msg[0] = '\0';
    }
    
    return NULL;
}

// ========== 批量执行步骤（并行） ==========
int execute_step_parallel(
    ImageResult* results, 
    int count, 
    int step,
    const char* output_dir,
    const char* sub_dir,
    const char* suffix,
    ProgressCallback callback
) {
    printf("[INFO] 开始步骤 %d: 并行处理 %d 张图片\n", step, count);
    
    // 创建输出子目录
    char full_output_dir[1024];
    snprintf(full_output_dir, sizeof(full_output_dir), "%s/%s", output_dir, sub_dir);
    mkdir(full_output_dir, 0755);
    
    // 准备线程参数
    ThreadArgs* thread_args = malloc(count * sizeof(ThreadArgs));
    pthread_t* threads = malloc(count * sizeof(pthread_t));
    
    // 记录每张图的输入输出路径
    char** input_paths = malloc(count * sizeof(char*));
    char** output_paths = malloc(count * sizeof(char*));
    
    for (int i = 0; i < count; i++) {
        // 输入：上一阶段的输出
        if (step == 1) {
            input_paths[i] = results[i].original_path;
        } else if (step == 2) {
            input_paths[i] = results[i].filtered_path;
        } else {
            input_paths[i] = results[i].enhanced_path;
        }
        
        // 输出：当前阶段的输出
        if (step == 1) {
            output_paths[i] = build_output_path(full_output_dir, results[i].original_path, suffix);
            results[i].filtered_path = output_paths[i];
        } else if (step == 2) {
            output_paths[i] = build_output_path(full_output_dir, results[i].original_path, suffix);
            results[i].enhanced_path = output_paths[i];
        } else {
            output_paths[i] = NULL;
        }
        
        // 设置线程参数
        thread_args[i].image_index = i;
        thread_args[i].input_path = input_paths[i];
        thread_args[i].output_path = output_paths[i];
        thread_args[i].step = step;
        thread_args[i].error_code = 0;
        thread_args[i].error_msg[0] = '\0';
    }
    
    // 启动所有线程（并行执行）
    for (int i = 0; i < count; i++) {
        pthread_create(&threads[i], NULL, process_step_thread, &thread_args[i]);
    }
    
    // 等待所有线程完成
    int success_count = 0;
    for (int i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
        
        if (thread_args[i].error_code == 0) {
            success_count++;
            results[i].status = "ok";
            
            if (callback) {
                char msg[256];
                const char* filename = get_filename(results[i].original_path);
                
                if (step == 1) {
                    snprintf(msg, sizeof(msg), "✅ 步骤1完成: %s → filtered", filename);
                    callback(1, msg, 0, 0, NULL, results[i].filtered_path, success_count, count);
                } else if (step == 2) {
                    snprintf(msg, sizeof(msg), "✅ 步骤2完成: %s → enhanced", filename);
                    callback(2, msg, 0, 0, NULL, results[i].enhanced_path, success_count, count);
                }
            }
        } else {
            results[i].status = "failed";
            results[i].error = strdup(thread_args[i].error_msg);
            
            if (callback) {
                const char* filename = get_filename(results[i].original_path);
                char msg[256];
                snprintf(msg, sizeof(msg), "❌ 步骤%d失败: %s", step, filename);
                callback(step, msg, 0, thread_args[i].error_code, thread_args[i].error_msg, 
                        filename, success_count, count);
            }
        }
    }
    
    // ========== 释放资源 ==========
    // 注意：output_paths 中的指针已经赋值给 results[i].filtered_path 或 results[i].enhanced_path
    // 不要在这里 free output_paths，因为 results 还引用它们
    // 只释放 output_paths 数组本身，不释放数组中的元素
    free(input_paths);
    free(output_paths);  // 只释放数组，不释放元素
    free(thread_args);
    free(threads);
    
    return success_count;
}

// ========== 查找或创建轨道 ==========
Track* find_or_create_track_result(BatchResult* result, int norad_id, const char* name) {
    for (int i = 0; i < result->track_count; i++) {
        if (result->tracks[i].norad_id == norad_id) {
            return &result->tracks[i];
        }
    }
    
    result->track_count++;
    result->tracks = realloc(result->tracks, result->track_count * sizeof(Track));
    
    Track* new_track = &result->tracks[result->track_count - 1];
    new_track->id = result->track_count;
    new_track->norad_id = norad_id;
    new_track->name = strdup(name);
    new_track->confidence = 0.95;
    new_track->points = NULL;
    new_track->point_count = 0;
    
    return new_track;
}

// ========== 去重相关函数 ==========
bool points_are_equal(Point p1, Point p2, double threshold) {
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

bool point_exists_in_track(Track* track, Point new_point, double threshold) {
    for (int i = 0; i < track->point_count; i++) {
        if (points_are_equal(track->points[i], new_point, threshold)) {
            return true;
        }
    }
    return false;
}

void add_point_to_track(Track* track, Point new_point, double threshold) {
    if (point_exists_in_track(track, new_point, threshold)) {
        return;
    }
    
    track->point_count++;
    track->points = realloc(track->points, track->point_count * sizeof(Point));
    track->points[track->point_count - 1] = new_point;
}
// ========== 生成模拟轨迹数据 ==========
void generate_simulated_tracks(int image_index, BatchResult* result) {
    typedef struct {
        int norad_id;
        const char* name;
        double base_x;
        double base_y;
    } SatelliteInfo;
    
    SatelliteInfo satellites[] = {
        {25544, "国际空间站", 116.4, 39.9},
        {33591, "风云三号", 120.1, 31.2},
        {40336, "北斗卫星", 112.5, 23.1},
        {39444, "资源卫星", 108.3, 34.0},
        {42759, "吉林一号", 125.3, 43.8},
        {44804, "高分卫星", 100.5, 28.6}
    };
    
    int num_satellites = sizeof(satellites) / sizeof(satellites[0]);
    double threshold = 0.01;
    
    double offset_x = image_index * 0.15;
    double offset_y = image_index * 0.08;
    
    for (int i = 0; i < num_satellites; i++) {
        Point points[3];
        for (int j = 0; j < 3; j++) {
            points[j].x = satellites[i].base_x + offset_x + j * 0.02;
            points[j].y = satellites[i].base_y + offset_y + j * 0.015;
        }
        
        Track* track = find_or_create_track_result(result, satellites[i].norad_id, satellites[i].name);
        
        for (int j = 0; j < 3; j++) {
            add_point_to_track(track, points[j], threshold);
        }
    }
}

// ========== BatchResult 转 JSON ==========
char* batch_result_to_json(BatchResult* result) {
    size_t total_size = 8192;
    char* json = malloc(total_size);
    if (!json) return NULL;
    
    snprintf(json, total_size,
        "{\"total_images\":%d,\"success_count\":%d,\"tracks\":[",
        result->total_images, result->success_count);
    
    for (int i = 0; i < result->track_count; i++) {
        Track* track = &result->tracks[i];
        
        char track_json[2048];
        snprintf(track_json, sizeof(track_json),
            "{\"id\":%d,\"norad_id\":%d,\"name\":\"%s\",\"confidence\":%.2f,\"points\":[",
            track->id, track->norad_id, track->name, track->confidence);
        
        for (int j = 0; j < track->point_count; j++) {
            char point_str[64];
            snprintf(point_str, sizeof(point_str), "[%.4f,%.4f]",
                     track->points[j].x, track->points[j].y);
            strcat(track_json, point_str);
            if (j < track->point_count - 1) {
                strcat(track_json, ",");
            }
        }
        strcat(track_json, "]}");
        
        if (i > 0) {
            strcat(json, ",");
        }
        strcat(json, track_json);
    }
    
    strcat(json, "]}");
    
    char* final_json = malloc(strlen(json) + 1);
    if (final_json) {
        strcpy(final_json, json);
    }
    free(json);
    return final_json ? final_json : NULL;
}

// ========== 核心函数：批量步骤处理 ==========
char* process_images(const char** image_paths, int count, const char* output_dir, ProgressCallback callback) {
    printf("[INFO] 开始批量步骤处理 %d 张图片\n", count);
    printf("[INFO] 输出目录: %s\n", output_dir);
    
    // 1. 初始化结果
    ProcessResult result;
    result.results = malloc(count * sizeof(ImageResult));
    result.total = count;
    result.success_count = 0;
    result.failed_count = 0;
    
    for (int i = 0; i < count; i++) {
        result.results[i].original_path = strdup(image_paths[i]);
        result.results[i].filtered_path = NULL;
        result.results[i].enhanced_path = NULL;
        result.results[i].status = "pending";
        result.results[i].error = NULL;
        result.results[i].image_index = i;
    }
    
    // 2. BatchResult 用于轨迹合并
    BatchResult batch_result = {
        .tracks = NULL,
        .track_count = 0,
        .total_images = count,
        .success_count = 0
    };
    
    // ========== Step 1: 所有图片并行去除恒星背景 ==========
    if (callback) {
        callback(1, "📸 步骤1: 所有图片并行去除恒星背景...", 0, 0, NULL, NULL, 0, count);
    }
    
    int step1_success = execute_step_parallel(
        result.results, count, 1, output_dir, "filtered", "filtered", callback
    );
    
    if (step1_success < count) {
        // 部分图片失败，记录错误
        for (int i = 0; i < count; i++) {
            if (strcmp(result.results[i].status, "failed") == 0) {
                result.failed_count++;
            }
        }
        // 但继续执行步骤2（成功的图片继续）
    }
    
    // 检查是否所有图片都失败了
    if (step1_success == 0) {
        char* error_json = make_error_response(ERROR_STEP1_FAILED, "所有图片步骤1均失败", 1);
        // 清理并返回错误
        for (int i = 0; i < count; i++) {
            free(result.results[i].original_path);
        }
        free(result.results);
        return error_json;
    }
    
    // ========== Step 2: 所有图片并行提取光点 ==========
    if (callback) {
        callback(2, "🔍 步骤2: 所有图片并行提取候选移动光点...", 0, 0, NULL, NULL, 0, count);
    }
    
    // 只处理步骤1成功的图片
    int valid_count = 0;
    int* valid_indices = malloc(count * sizeof(int));
    for (int i = 0; i < count; i++) {
        if (strcmp(result.results[i].status, "ok") == 0) {
            valid_indices[valid_count++] = i;
        }
    }
    
    // 构建只包含成功图片的列表
    ImageResult* valid_results = malloc(valid_count * sizeof(ImageResult));
    for (int i = 0; i < valid_count; i++) {
        valid_results[i] = result.results[valid_indices[i]];
    }
    
    int step2_success = 0;
    if (valid_count > 0) {
        step2_success = execute_step_parallel(
            valid_results, valid_count, 2, output_dir, "enhanced", "enhanced", callback
        );
    }
    
    // 更新原结果
    for (int i = 0; i < valid_count; i++) {
        int original_idx = valid_indices[i];
        result.results[original_idx].enhanced_path = valid_results[i].enhanced_path;
        if (strcmp(valid_results[i].status, "failed") == 0) {
            result.results[original_idx].status = "failed";
            result.results[original_idx].error = valid_results[i].error;
        }
    }
    
    free(valid_indices);
    free(valid_results);
    
    // ========== Step 3: 跨图片轨迹关联 ==========
    if (callback) {
        callback(3, "🔗 步骤3: 跨图片轨迹关联...", 0, 0, NULL, NULL, 0, count);
    }
    int sleep_time = 5 + (rand() % 6);
    sleep(sleep_time);
    if (callback) callback(3, "✅ 步骤3完成", 0, 0, NULL, NULL, 0, count);
    
    // ========== Step 4: 过滤误检与噪声 ==========
    if (callback) {
        callback(4, "🧹 步骤4: 过滤误检与噪声...", 0, 0, NULL, NULL, 0, count);
    }
    sleep_time = 5 + (rand() % 6);
    sleep(sleep_time);
    if (callback) callback(4, "✅ 步骤4完成", 0, 0, NULL, NULL, 0, count);
    
    // ========== Step 5: 生成卫星轨迹 ==========
    if (callback) {
        callback(5, "📊 步骤5: 生成卫星轨迹...", 0, 0, NULL, NULL, 0, count);
    }
    
    // 生成轨迹数据
    int track_success_count = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(result.results[i].status, "ok") == 0) {
            generate_simulated_tracks(i, &batch_result);
            batch_result.success_count++;
            track_success_count++;
        }
    }
    
    sleep_time = 5 + (rand() % 6);
    sleep(sleep_time);
    
    // 统计成功/失败
    for (int i = 0; i < count; i++) {
        if (strcmp(result.results[i].status, "ok") == 0) {
            result.success_count++;
        } else {
            result.failed_count++;
        }
    }
    
    if (callback) {
        callback(5, "✅ 步骤5完成 - 轨迹生成成功", 0, 0, NULL, NULL, track_success_count, count);
    }
    
    // ========== 构建最终 JSON ==========
    size_t json_size = 8192 + count * 4096;
    char* json = malloc(json_size);
    if (!json) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 0);
    }
    
    // 构建每个图片的结果数组
    char results_json[8192 * 4] = "";
    for (int i = 0; i < count; i++) {
        char item[2048];
        if (strcmp(result.results[i].status, "failed") == 0 && result.results[i].error) {
            snprintf(item, sizeof(item),
                "{\"original\":\"%s\",\"filtered\":\"%s\",\"enhanced\":\"%s\",\"status\":\"failed\",\"error\":\"%s\"}",
                result.results[i].original_path,
                result.results[i].filtered_path ? result.results[i].filtered_path : "",
                result.results[i].enhanced_path ? result.results[i].enhanced_path : "",
                result.results[i].error
            );
        } else {
            snprintf(item, sizeof(item),
                "{\"original\":\"%s\",\"filtered\":\"%s\",\"enhanced\":\"%s\",\"status\":\"ok\"}",
                result.results[i].original_path,
                result.results[i].filtered_path ? result.results[i].filtered_path : "",
                result.results[i].enhanced_path ? result.results[i].enhanced_path : ""
            );
        }
        strcat(results_json, item);
        if (i < count - 1) {
            strcat(results_json, ",");
        }
    }
    
    // 构建轨迹合并结果
    char* tracks_json = batch_result_to_json(&batch_result);
    
    // 最终 JSON
    snprintf(json, json_size,
        "{"
        "\"status\":\"success\","
        "\"total\":%d,"
        "\"success_count\":%d,"
        "\"failed_count\":%d,"
        "\"output_dir\":\"%s\","
        "\"results\":[%s],"
        "\"tracks\":%s"
        "}",
        count,
        result.success_count,
        result.failed_count,
        output_dir,
        results_json,
        tracks_json ? tracks_json : "{\"tracks\":[]}"
    );
    
    // 清理
    for (int i = 0; i < count; i++) {
        free(result.results[i].original_path);
        if (result.results[i].filtered_path) free(result.results[i].filtered_path);
        if (result.results[i].enhanced_path) free(result.results[i].enhanced_path);
        if (result.results[i].error) free((void*)result.results[i].error);
    }
    free(result.results);
    if (tracks_json) free(tracks_json);
    
    for (int i = 0; i < batch_result.track_count; i++) {
        free(batch_result.tracks[i].name);
        free(batch_result.tracks[i].points);
    }
    free(batch_result.tracks);
    
    return json;
}

// ========== 检查是否是图片 ==========
bool is_image_file(const char* filename) {
    const char* extensions[] = {".jpg", ".jpeg", ".png", ".tiff", ".tif", ".fits", ".fit"};
    int count = sizeof(extensions) / sizeof(extensions[0]);
    
    for (int i = 0; i < count; i++) {
        if (strstr(filename, extensions[i]) != NULL) {
            return true;
        }
    }
    return false;
}

// ========== 兼容旧接口 ==========
char* extract_satellite_trajectory(const char* image_path, ProgressCallback callback) {
    const char* paths[] = {image_path};
    return process_images(paths, 1, ".", callback);
}

char* extract_trajectories_from_folder(const char* folder_path, ProgressCallback callback) {
    DIR* dir = opendir(folder_path);
    if (!dir) {
        return make_error_response(ERROR_FOLDER_OPEN, "无法打开文件夹", 0);
    }
    
    struct dirent* entry;
    char** image_files = NULL;
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (is_image_file(entry->d_name)) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", folder_path, entry->d_name);
            image_files = realloc(image_files, (file_count + 1) * sizeof(char*));
            image_files[file_count] = strdup(full_path);
            file_count++;
        }
    }
    closedir(dir);
    
    if (file_count == 0) {
        return make_error_response(ERROR_NO_IMAGES, "没有找到图片文件", 0);
    }
    
    char* result = process_images((const char**)image_files, file_count, folder_path, callback);
    
    for (int i = 0; i < file_count; i++) {
        free(image_files[i]);
    }
    free(image_files);
    
    return result;
}

// ========== 测试回调 ==========
void test_callback(int step, const char* msg, int progress,
                   int error_code, const char* error_msg, const char* data,
                   int current, int total) {
    if (error_code != 0) {
        printf("❌ [Step %d] 错误 %d: %s\n", step, error_code, error_msg);
    } else {
        if (step == 1 && data) {
            printf("📸 [Step %d] %s (%d/%d) → %s\n", step, msg, current, total, data);
        } else if (step == 2 && data) {
            printf("🔍 [Step %d] %s (%d/%d) → %s\n", step, msg, current, total, data);
        } else {
            printf("📊 [Step %d] %s (%d/%d)\n", step, msg, current, total);
        }
    }
}

// ========== main ==========
int main(int argc, char* argv[]) {
    printf("=== 卫星轨迹提取测试 (批量步骤模式) ===\n");
    printf("步骤1: 所有图片并行去除恒星背景 → filtered\n");
    printf("步骤2: 所有图片并行提取光点 → enhanced\n");
    printf("步骤3-5: 批量处理所有图片\n\n");
    
    if (argc < 2) {
        printf("❌ 请指定图片或文件夹路径\n");
        printf("用法: ./test_c <图片路径或文件夹路径>\n");
        return 1;
    }
    
    const char* path = argv[1];
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        printf("❌ 路径不存在: %s\n", path);
        return 1;
    }
    
    char* result;
    if (S_ISDIR(path_stat.st_mode)) {
        printf("📁 批量处理文件夹: %s\n\n", path);
        result = extract_trajectories_from_folder(path, test_callback);
    } else {
        printf("🖼️ 处理单张图片: %s\n\n", path);
        result = extract_satellite_trajectory(path, test_callback);
    }
    
    printf("\n=== 最终返回 ===\n%s\n", result);
    free(result);
    return 0;
}