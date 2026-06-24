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

// ========== 错误码定义 ==========
#define SUCCESS 0
#define ERROR_IMAGE_LOAD 1001
#define ERROR_IMAGE_FORMAT 1002
#define ERROR_IMAGE_CORRUPT 1003
#define ERROR_PREPROCESS 2001
#define ERROR_NOISE_REDUCTION 2002
#define ERROR_DETECTION 3001
#define ERROR_NO_SATELLITE 3002
#define ERROR_TOO_MANY_TARGETS 3003
#define ERROR_FITTING 4001
#define ERROR_INSUFFICIENT_POINTS 4002
#define ERROR_RESULT_GEN 5001
#define ERROR_FOLDER_OPEN 6001
#define ERROR_NO_IMAGES 6002
#define ERROR_UNKNOWN 9999

// ========== 回调函数类型 ==========
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

// ========== 全局结果（必须在函数前定义） ==========
typedef struct {
    Track* tracks;
    int track_count;
    int total_images;
    int success_count;
} BatchResult;

// ========== 图片处理结果 ==========
typedef struct {
    char* original_path;
    char* output_path;
    char* status;  // "ok" 或 "failed"
    char* error;
    int point_count;
} ImageResult;

// ========== 批量结果 ==========
typedef struct {
    ImageResult* results;
    int total;
    int success_count;
    int failed_count;
} ProcessResult;

// ========== 函数声明（解决编译顺序问题） ==========
const char* get_error_message(int error_code);
int check_for_error(int current_step, int* out_error_code, const char** out_error_msg);
char* make_error_response(int error_code, const char* error_msg, int step);
const char* get_filename(const char* path);
char* remove_extension(const char* filename);
char* build_output_path(const char* output_dir, const char* original_path);
int apply_star_removal(const char* input_path, const char* output_path);
bool is_image_file(const char* filename);
Track* find_or_create_track(BatchResult* result, int norad_id, const char* name);
bool point_exists_in_track(Track* track, Point new_point, double threshold);
void add_point_to_track(Track* track, Point new_point, double threshold);
void generate_simulated_tracks(int image_index, BatchResult* result);
char* batch_result_to_json(BatchResult* result);
char* process_images(const char** image_paths, int count, const char* output_dir, ProgressCallback callback);


// ========== 随机错误配置 ==========
typedef struct {
    int step;
    int error_code;
    const char* error_msg;
    float probability;
} SimulatedError;

SimulatedError possible_errors[] = {
    {1, ERROR_IMAGE_FORMAT, "不支持的图片格式", 0.10},
    {1, ERROR_IMAGE_CORRUPT, "图片文件已损坏", 0.05},
    {1, ERROR_IMAGE_LOAD, "无法加载图片文件", 0.05},
    {2, ERROR_PREPROCESS, "图像预处理失败", 0.08},
    {2, ERROR_NOISE_REDUCTION, "降噪算法失败", 0.07},
    {3, ERROR_DETECTION, "卫星目标检测失败", 0.05},
    {3, ERROR_NO_SATELLITE, "未检测到任何卫星目标", 0.12},
    {3, ERROR_TOO_MANY_TARGETS, "检测到过多目标", 0.03},
    {4, ERROR_FITTING, "轨迹拟合失败", 0.06},
    {4, ERROR_INSUFFICIENT_POINTS, "有效轨迹点不足", 0.08},
    {5, ERROR_RESULT_GEN, "结果生成失败", 0.04},
};

const int num_possible_errors = sizeof(possible_errors) / sizeof(possible_errors[0]);
static bool random_initialized = false;

// ========== 以下是所有函数实现 ==========
// （把你之前的所有函数实现放在这里...）

// ========== 错误信息 ==========
const char* get_error_message(int error_code) {
    switch (error_code) {
        case ERROR_IMAGE_LOAD: return "无法加载图片文件";
        case ERROR_IMAGE_FORMAT: return "不支持的图片格式";
        case ERROR_IMAGE_CORRUPT: return "图片文件已损坏";
        case ERROR_PREPROCESS: return "图像预处理失败";
        case ERROR_NOISE_REDUCTION: return "降噪算法失败";
        case ERROR_DETECTION: return "卫星目标检测失败";
        case ERROR_NO_SATELLITE: return "未检测到任何卫星目标";
        case ERROR_TOO_MANY_TARGETS: return "检测到过多目标";
        case ERROR_FITTING: return "轨迹拟合失败";
        case ERROR_INSUFFICIENT_POINTS: return "有效轨迹点不足";
        case ERROR_RESULT_GEN: return "结果生成失败";
        case ERROR_FOLDER_OPEN: return "无法打开文件夹";
        case ERROR_NO_IMAGES: return "没有找到图片文件";
        default: return "未知错误";
    }
}

int check_for_error(int current_step, int* out_error_code, const char** out_error_msg) {
    if (!random_initialized) {
        srand((unsigned int)time(NULL));
        random_initialized = true;
    }

    for (int i = 0; i < num_possible_errors; i++) {
        if (possible_errors[i].step == current_step) {
            int rand_val = rand() % 100;
            float threshold = 0;//possible_errors[i].probability * 100;
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

// ========== 提取文件名 ==========
const char* get_filename(const char* path) {
    const char* last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

// ========== 移除扩展名 ==========
char* remove_extension(const char* filename) {
    char* result = strdup(filename);
    char* dot = strrchr(result, '.');
    if (dot) *dot = '\0';
    return result;
}

// ========== 构建输出路径 ==========
char* build_output_path(const char* output_dir, const char* original_path) {
    const char* filename = get_filename(original_path);
    char* basename = remove_extension(filename);
    
    char* output_path = malloc(1024);
    snprintf(output_path, 1024, "%s/%s_filtered.jpg", output_dir, basename);
    
    free(basename);
    return output_path;
}

// ========== 模拟去恒星处理 ==========
// 返回值: 0=成功, 1=失败
int apply_star_removal(const char* input_path, const char* output_path) {
    // 模拟处理：只是简单地复制文件或创建空文件
    // 真实场景中，这里会调用图像处理库
    
    // 检查输入文件是否存在
    FILE* input = fopen(input_path, "rb");
    if (!input) {
        return 1;
    }
    fclose(input);
    
    // 模拟处理时间
    usleep(500000);  // 0.5秒
    
    // 创建一个空文件模拟输出（真实场景中这里会写入处理后的图片）
    FILE* output = fopen(output_path, "wb");
    if (!output) {
        return 1;
    }
    fclose(output);
    
    return 0;  // 成功
}

// ========== 核心函数：处理图片列表 ==========
char* process_images(const char** image_paths, int count, const char* output_dir, ProgressCallback callback) {
    printf("[INFO] 开始处理 %d 张图片\n", count);
    printf("[INFO] 输出目录: %s\n", output_dir);
    
    // 1. 创建输出目录
    mkdir(output_dir, 0755);
    
    // 2. 初始化结果
    ProcessResult result;
    result.results = malloc(count * sizeof(ImageResult));
    result.total = count;
    result.success_count = 0;
    result.failed_count = 0;
    
    // 3. 用于去重合并的 BatchResult
    BatchResult batch_result = {
        .tracks = NULL,
        .track_count = 0,
        .total_images = count,
        .success_count = 0
    };
    
    // 4. 逐张处理图片
    for (int i = 0; i < count; i++) {
        const char* input_path = image_paths[i];
        const char* filename = get_filename(input_path);
        
        // 构建输出路径
        char* output_path = build_output_path(output_dir, input_path);
        
        // 进度回调
        if (callback) {
            char msg[256];
            snprintf(msg, sizeof(msg), "正在处理: %s", filename);
            callback(1, msg, (i * 100) / count, 0, NULL, filename, i, count);
        }
        
        // 检查文件是否存在
        FILE* test = fopen(input_path, "rb");
        if (!test) {
            result.results[i].original_path = strdup(input_path);
            result.results[i].output_path = output_path;
            result.results[i].status = "failed";
            result.results[i].error = "文件不存在";
            result.results[i].point_count = 0;
            result.failed_count++;
            
            if (callback) {
                callback(2, "文件不存在", (i * 100) / count, ERROR_IMAGE_LOAD, "无法打开图片", filename, i, count);
            }
            continue;
        }
        fclose(test);
        
        // 模拟处理步骤
        bool has_error = false;
        int error_code = 0;
        const char* error_msg = NULL;
        
        // Step 2: 预处理
        if (callback) callback(2, "图像预处理...", (i * 100) / count + 10, 0, NULL, filename, i, count);
        sleep(1);
        
        if (check_for_error(2, &error_code, &error_msg) != 0) {
            has_error = true;
            if (callback) callback(2, "预处理失败", (i * 100) / count + 15, error_code, error_msg, filename, i, count);
        }
        
        // Step 3: 检测
        if (!has_error) {
            if (callback) callback(3, "卫星目标检测...", (i * 100) / count + 30, 0, NULL, filename, i, count);
            sleep(1);
            
            if (check_for_error(3, &error_code, &error_msg) != 0) {
                has_error = true;
                if (callback) callback(3, "检测失败", (i * 100) / count + 35, error_code, error_msg, filename, i, count);
            }
        }
        
        // Step 4: 拟合
        if (!has_error) {
            if (callback) callback(4, "轨迹拟合...", (i * 100) / count + 60, 0, NULL, filename, i, count);
            sleep(1);
            
            if (check_for_error(4, &error_code, &error_msg) != 0) {
                has_error = true;
                if (callback) callback(4, "拟合失败", (i * 100) / count + 65, error_code, error_msg, filename, i, count);
            }
        }
        
        // Step 5: 生成结果
        if (!has_error) {
            if (callback) callback(5, "生成结果...", (i * 100) / count + 85, 0, NULL, filename, i, count);
            sleep(1);
            
            if (check_for_error(5, &error_code, &error_msg) != 0) {
                has_error = true;
                if (callback) callback(5, "结果生成失败", (i * 100) / count + 90, error_code, error_msg, filename, i, count);
            }
        }
        
        if (!has_error) {
            // 模拟应用去恒星处理
            int process_result = apply_star_removal(input_path, output_path);
            
            if (process_result == 0) {
                result.results[i].original_path = strdup(input_path);
                result.results[i].output_path = output_path;
                result.results[i].status = "ok";
                result.results[i].error = NULL;
                result.results[i].point_count = 3;  // 模拟每个图片有3个点
                result.success_count++;
                
                // 生成模拟轨迹数据（用于去重合并）
                generate_simulated_tracks(i, &batch_result);
                batch_result.success_count++;
                
                if (callback) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "✅ 处理完成: %s", filename);
                    callback(5, msg, (i * 100) / count + 100, 0, NULL, output_path, i, count);
                }
            } else {
                result.results[i].original_path = strdup(input_path);
                result.results[i].output_path = output_path;
                result.results[i].status = "failed";
                result.results[i].error = "去恒星处理失败";
                result.results[i].point_count = 0;
                result.failed_count++;
            }
        } else {
            result.results[i].original_path = strdup(input_path);
            result.results[i].output_path = output_path;
            result.results[i].status = "failed";
            result.results[i].error = (char*)error_msg;
            result.results[i].point_count = 0;
            result.failed_count++;
        }
    }
    
    // 5. 构建最终 JSON
    size_t json_size = 8192 + count * 2048;
    char* json = malloc(json_size);
    if (!json) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 0);
    }
    
    // 构建每个图片的结果数组
    char results_json[8192 * 4] = "";
    for (int i = 0; i < count; i++) {
        char item[1024];
        snprintf(item, sizeof(item), 
            "{\"original\":\"%s\",\"output\":\"%s\",\"status\":\"%s\"%s}",
            result.results[i].original_path,
            result.results[i].output_path,
            result.results[i].status,
            result.results[i].error ? ", \"error\":\"" : ""
        );
        if (result.results[i].error) {
            strcat(item, result.results[i].error);
            strcat(item, "\"");
        }
        // strcat(item, "}");
        if (i < count - 1) {
            strcat(item, ",");
        }
        strcat(results_json, item);
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
        // 从 tracks_json 中提取 tracks 部分
        // 简化处理：直接使用 tracks_json 中的 tracks 数组
        tracks_json ? tracks_json : "\"tracks\":[]"
    );
    
    // 6. 清理
    for (int i = 0; i < count; i++) {
        free(result.results[i].original_path);
        if (result.results[i].output_path) {
            free(result.results[i].output_path);
        }
    }
    free(result.results);
    if (tracks_json) free(tracks_json);
    
    // 清理 BatchResult 的 tracks
    for (int i = 0; i < batch_result.track_count; i++) {
        free(batch_result.tracks[i].name);
        free(batch_result.tracks[i].points);
    }
    free(batch_result.tracks);
    
    return json;
}

// ========== 生成单张图片的模拟数据 ==========
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
    
    // 每张图片偏移量（模拟卫星移动）
    double offset_x = image_index * 0.15;
    double offset_y = image_index * 0.08;
    
    for (int i = 0; i < num_satellites; i++) {
        Point points[3];
        for (int j = 0; j < 3; j++) {
            points[j].x = satellites[i].base_x + offset_x + j * 0.02;
            points[j].y = satellites[i].base_y + offset_y + j * 0.015;
        }
        
        Track* track = find_or_create_track(result, satellites[i].norad_id, satellites[i].name);
        
        for (int j = 0; j < 3; j++) {
            add_point_to_track(track, points[j], threshold);
        }
    }
}

// ========== 查找或创建轨道 ==========
Track* find_or_create_track(BatchResult* result, int norad_id, const char* name) {
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

// ========== 向轨迹中添加新点（自动去重） ==========
void add_point_to_track(Track* track, Point new_point, double threshold) {
    if (point_exists_in_track(track, new_point, threshold)) {
        return;
    }
    
    track->point_count++;
    track->points = realloc(track->points, track->point_count * sizeof(Point));
    track->points[track->point_count - 1] = new_point;
}

// ========== 判断两个点是否相同 ==========
bool points_are_equal(Point p1, Point p2, double threshold) {
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

// ========== 点是否存在于轨迹中 ==========
bool point_exists_in_track(Track* track, Point new_point, double threshold) {
    for (int i = 0; i < track->point_count; i++) {
        if (points_are_equal(track->points[i], new_point, threshold)) {
            return true;
        }
    }
    return false;
}

// ========== 将 BatchResult 转换为 JSON ==========
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

// ========== 兼容旧接口 ==========
char* extract_satellite_trajectory(const char* image_path, ProgressCallback callback) {
    const char* paths[] = {image_path};
    return process_images(paths, 1, ".", callback);
}

char* extract_trajectories_from_folder(const char* folder_path, ProgressCallback callback) {
    // 收集文件夹内所有图片
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
    
    // 调用 process_images
    char* result = process_images((const char**)image_files, file_count, folder_path, callback);
    
    // 清理
    for (int i = 0; i < file_count; i++) {
        free(image_files[i]);
    }
    free(image_files);
    
    return result;
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

// ========== 测试回调 ==========
void test_callback(int step, const char* msg, int progress,
                   int error_code, const char* error_msg, const char* data,
                   int current, int total) {
    if (error_code != 0) {
        printf("❌ [Step %d] 错误 %d: %s\n", step, error_code, error_msg);
    } else {
        if (total > 1) {
            printf("✅ [%d/%d] %s (%d%%)\n", current + 1, total, msg, progress);
        } else {
            printf("✅ [Step %d] %s (%d%%)\n", step, msg, progress);
        }
    }
    if (data && error_code == 0 && (step == 5 || step == 0)) {
        printf("📊 结果: %s\n", data);
    }
}

// ========== main ==========
int main(int argc, char* argv[]) {
    printf("=== 卫星轨迹提取测试 ===\n");
    printf("用法: ./test_c <图片路径或文件夹路径>\n\n");
    
    if (argc < 2) {
        printf("❌ 请指定图片或文件夹路径\n");
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