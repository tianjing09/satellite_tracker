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

// ========== 全局结果 ==========
typedef struct {
    Track* tracks;
    int track_count;
    int total_images;
    int success_count;
} BatchResult;

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

// ========== 判断两个点是否相同（距离小于阈值） ==========
bool points_are_equal(Point p1, Point p2, double threshold) {
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

// ========== 去重：从现有轨迹中查找是否已存在相同点 ==========
bool point_exists_in_track(Track* track, Point new_point, double threshold) {
    for (int i = 0; i < track->point_count; i++) {
        if (points_are_equal(track->points[i], new_point, threshold)) {
            return true;
        }
    }
    return false;
}

// ========== 向轨迹中添加新点（自动去重） ==========
void add_point_to_track(Track* track, Point new_point, double threshold) {
    // 检查是否已存在
    if (point_exists_in_track(track, new_point, threshold)) {
        return;  // 已存在，不添加
    }
    
    // 添加新点
    track->point_count++;
    track->points = realloc(track->points, track->point_count * sizeof(Point));
    track->points[track->point_count - 1] = new_point;
}

// ========== 查找或创建轨道 ==========
Track* find_or_create_track(BatchResult* result, int norad_id, const char* name) {
    // 1. 查找是否已有该卫星的轨迹
    for (int i = 0; i < result->track_count; i++) {
        if (result->tracks[i].norad_id == norad_id) {
            return &result->tracks[i];
        }
    }
    
    // 2. 没有则创建新轨迹
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

// ========== 生成单张图片的模拟数据 ==========
void generate_simulated_tracks(int image_index, BatchResult* result) {
    // 模拟 6 颗卫星，每颗卫星在不同图片中位置有偏移
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
    double threshold = 0.01;  // 去重阈值（度）
    
    // 每张图片偏移量（模拟卫星移动）
    double offset_x = image_index * 0.15;
    double offset_y = image_index * 0.08;
    
    for (int i = 0; i < num_satellites; i++) {
        // 模拟该卫星在图片中的 3 个轨迹点（形成一条短轨迹）
        Point points[3];
        for (int j = 0; j < 3; j++) {
            points[j].x = satellites[i].base_x + offset_x + j * 0.02;
            points[j].y = satellites[i].base_y + offset_y + j * 0.015;
        }
        
        // 查找或创建轨迹（按 norad_id 去重）
        Track* track = find_or_create_track(result, satellites[i].norad_id, satellites[i].name);
        
        // 添加点（自动去重）
        for (int j = 0; j < 3; j++) {
            add_point_to_track(track, points[j], threshold);
        }
    }
}

// ========== 将 BatchResult 转换为 JSON ==========
char* batch_result_to_json(BatchResult* result) {
    // 估算总大小
    size_t total_size = 8192;
    char* json = malloc(total_size);
    if (!json) return NULL;
    
    snprintf(json, total_size,
        "{\"status\":\"success\",\"total_images\":%d,\"success_count\":%d,\"tracks\":[",
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
        
        // 追加到结果
        if (i > 0) {
            strcat(json, ",");
        }
        strcat(json, track_json);
    }
    
    strcat(json, "]}");
    
    // 重新分配精确大小
    char* final_json = malloc(strlen(json) + 1);
    if (final_json) {
        strcpy(final_json, json);
    }
    free(json);
    return final_json ? final_json : NULL;
}

// ========== 单张图片处理 ==========
char* extract_single_trajectory(const char* image_path, ProgressCallback callback, 
                                 int current_idx, int total_count) {
    int error_code = 0;
    const char* error_msg = NULL;

    printf("[INFO] 处理图片 %d/%d: %s\n", current_idx + 1, total_count, image_path);

    // Step 1: 加载图片
    if (callback) callback(1, "正在加载望远镜图片...", 5, 0, NULL, NULL, current_idx, total_count);
    sleep(1);

    FILE* test = fopen(image_path, "rb");
    if (!test) {
        error_code = ERROR_IMAGE_LOAD;
        error_msg = "无法打开图片文件";
        if (callback) callback(1, "图片加载失败", 10, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 1);
    }
    fclose(test);

    if (check_for_error(1, &error_code, &error_msg) != 0) {
        if (callback) callback(1, "图片加载失败", 10, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 1);
    }
    if (callback) callback(1, "图片加载成功", 10, 0, NULL, NULL, current_idx, total_count);

    // Step 2: 预处理
    if (callback) callback(2, "图像预处理...", 25, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(2, &error_code, &error_msg) != 0) {
        if (callback) callback(2, "预处理失败", 30, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 2);
    }
    if (callback) callback(2, "预处理完成", 30, 0, NULL, NULL, current_idx, total_count);

    // Step 3: 检测
    if (callback) callback(3, "卫星目标检测...", 50, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(3, &error_code, &error_msg) != 0) {
        if (callback) callback(3, "检测失败", 55, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 3);
    }
    if (callback) callback(3, "检测完成", 55, 0, NULL, NULL, current_idx, total_count);

    // Step 4: 拟合
    if (callback) callback(4, "轨迹拟合...", 75, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(4, &error_code, &error_msg) != 0) {
        if (callback) callback(4, "拟合失败", 80, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 4);
    }
    if (callback) callback(4, "拟合完成", 80, 0, NULL, NULL, current_idx, total_count);

    // Step 5: 生成结果
    if (callback) callback(5, "生成结果...", 95, 0, NULL, NULL, current_idx, total_count);
    sleep(1);

    if (check_for_error(5, &error_code, &error_msg) != 0) {
        if (callback) callback(5, "生成结果失败", 100, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 5);
    }

    // 生成模拟数据：当前图片的 3 个点（位置随图片索引偏移）
    // 注意：这里返回的是"一张图片中的 6 颗卫星，每颗 3 个点"
    // 用于后续批量处理时去重合并
    char result_json[4096];
    double offset_x = current_idx * 0.15;
    double offset_y = current_idx * 0.08;
    
    snprintf(result_json, sizeof(result_json),
        "{"
        "\"status\":\"success\","
        "\"frame\":%d,"
        "\"total_tracks\":6,"
        "\"tracks\":["
        "{"
        "\"norad_id\":25544,\"name\":\"国际空间站\",\"confidence\":0.96,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "},"
        "{"
        "\"norad_id\":33591,\"name\":\"风云三号\",\"confidence\":0.92,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "},"
        "{"
        "\"norad_id\":40336,\"name\":\"北斗卫星\",\"confidence\":0.89,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "},"
        "{"
        "\"norad_id\":39444,\"name\":\"资源卫星\",\"confidence\":0.94,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "},"
        "{"
        "\"norad_id\":42759,\"name\":\"吉林一号\",\"confidence\":0.91,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "},"
        "{"
        "\"norad_id\":44804,\"name\":\"高分卫星\",\"confidence\":0.93,"
        "\"points\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]]"
        "}"
        "]}",
        current_idx,
        // 国际空间站
        116.4 + offset_x, 39.9 + offset_y,
        117.2 + offset_x, 40.5 + offset_y,
        118.1 + offset_x, 41.2 + offset_y,
        // 风云三号
        120.1 + offset_x, 31.2 + offset_y,
        121.0 + offset_x, 31.8 + offset_y,
        122.0 + offset_x, 32.3 + offset_y,
        // 北斗
        112.5 + offset_x, 23.1 + offset_y,
        113.2 + offset_x, 23.7 + offset_y,
        114.0 + offset_x, 24.2 + offset_y,
        // 资源卫星
        108.3 + offset_x, 34.0 + offset_y,
        109.1 + offset_x, 34.5 + offset_y,
        110.0 + offset_x, 35.1 + offset_y,
        // 吉林一号
        125.3 + offset_x, 43.8 + offset_y,
        126.0 + offset_x, 44.2 + offset_y,
        126.8 + offset_x, 44.7 + offset_y,
        // 高分卫星
        100.5 + offset_x, 28.6 + offset_y,
        101.2 + offset_x, 29.1 + offset_y,
        102.0 + offset_x, 29.6 + offset_y
    );
    
    if (callback) callback(5, "单张处理完成", 100, 0, NULL, result_json, current_idx, total_count);

    char* result = malloc(strlen(result_json) + 1);
    if (!result) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 5);
    }
    strcpy(result, result_json);
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

// ========== 批量处理（核心：去重合并） ==========
char* extract_trajectories_from_folder(const char* folder_path, ProgressCallback callback) {
    // 1. 打开文件夹
    DIR* dir = opendir(folder_path);
    if (!dir) {
        printf("[ERROR] 无法打开文件夹: %s\n", folder_path);
        return make_error_response(ERROR_FOLDER_OPEN, "无法打开文件夹", 0);
    }
    
    // 2. 收集所有图片
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
    
    printf("[INFO] 找到 %d 张图片\n", file_count);
    
    // 3. 创建全局结果容器
    BatchResult batch_result = {
        .tracks = NULL,
        .track_count = 0,
        .total_images = file_count,
        .success_count = 0
    };
    
    // 4. 逐个处理图片
    for (int i = 0; i < file_count; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "处理 %d/%d", i + 1, file_count);
        if (callback) {
            callback(0, msg, (i * 100) / file_count, 0, NULL, NULL, i, file_count);
        }
        
        // 调用单张处理
        char* result_str = extract_single_trajectory(image_files[i], callback, i, file_count);
        
        // 解析结果（简化版：我们不需要解析，直接用生成函数）
        // 因为 extract_single_trajectory 返回的是 JSON，但我们想要的是合并到 BatchResult
        // 为了简化，我们直接用 generate_simulated_tracks 生成数据
        // 注意：真实场景中，这里应该解析 JSON 并合并
        
        free(result_str);
        
        // 生成模拟数据（用当前图片索引）
        generate_simulated_tracks(i, &batch_result);
        batch_result.success_count++;
    }
    
    // 5. 转换为 JSON
    char* final_json = batch_result_to_json(&batch_result);
    
    // 6. 清理
    for (int i = 0; i < file_count; i++) {
        free(image_files[i]);
    }
    free(image_files);
    
    // 清理 tracks
    for (int i = 0; i < batch_result.track_count; i++) {
        free(batch_result.tracks[i].name);
        free(batch_result.tracks[i].points);
    }
    free(batch_result.tracks);
    
    if (callback) {
        callback(5, "批量处理完成！", 100, 0, NULL, final_json, file_count, file_count);
    }
    
    return final_json;
}

// ========== 兼容旧接口 ==========
char* extract_satellite_trajectory(const char* image_path, ProgressCallback callback) {
    return extract_single_trajectory(image_path, callback, 0, 1);
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