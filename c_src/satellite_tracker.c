#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>      // 用于扫描文件夹
#include <sys/stat.h>    // 用于文件信息

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
// 新增参数：current (当前处理第几张), total (总共几张)
typedef void (*ProgressCallback)(int step, const char* message, int progress,
                                  int error_code, const char* error_msg, 
                                  const char* data, int current, int total);

// ========== 错误信息 ==========
const char* get_error_message(int error_code) {
    switch (error_code) {
        case ERROR_IMAGE_LOAD: return "无法加载图片文件，请检查文件路径和权限";
        case ERROR_IMAGE_FORMAT: return "不支持的图片格式，请使用 FITS、JPG、PNG 或 TIFF 格式";
        case ERROR_IMAGE_CORRUPT: return "图片文件已损坏，无法读取";
        case ERROR_PREPROCESS: return "图像预处理失败，可能是内存不足";
        case ERROR_NOISE_REDUCTION: return "降噪算法失败，图片噪声过大";
        case ERROR_DETECTION: return "卫星目标检测算法异常";
        case ERROR_NO_SATELLITE: return "未检测到任何卫星目标，请检查图片是否包含卫星";
        case ERROR_TOO_MANY_TARGETS: return "检测到过多目标（>1000），请调整检测参数";
        case ERROR_FITTING: return "轨迹拟合算法失败";
        case ERROR_INSUFFICIENT_POINTS: return "有效轨迹点不足（<3个），无法拟合轨迹";
        case ERROR_RESULT_GEN: return "结果生成失败";
        case ERROR_FOLDER_OPEN: return "无法打开文件夹，请检查路径是否正确";
        case ERROR_NO_IMAGES: return "文件夹中没有找到任何图片文件";
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
    {1, ERROR_IMAGE_CORRUPT, "图片文件已损坏，无法读取", 0.05},
    {1, ERROR_IMAGE_LOAD, "无法加载图片文件", 0.05},
    {2, ERROR_PREPROCESS, "图像预处理失败，内存不足", 0.08},
    {2, ERROR_NOISE_REDUCTION, "降噪算法失败，图片噪声过大", 0.07},
    {3, ERROR_DETECTION, "卫星目标检测算法异常", 0.05},
    {3, ERROR_NO_SATELLITE, "未检测到任何卫星目标", 0.12},
    {3, ERROR_TOO_MANY_TARGETS, "检测到过多目标（>1000）", 0.03},
    {4, ERROR_FITTING, "轨迹拟合算法失败", 0.06},
    {4, ERROR_INSUFFICIENT_POINTS, "有效轨迹点不足（<3个）", 0.08},
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

// ========== 单张图片处理函数 ==========
char* extract_single_trajectory(const char* image_path, ProgressCallback callback, 
                                 int current_idx, int total_count) {
    int error_code = 0;
    const char* error_msg = NULL;

    printf("[INFO] 处理图片 %d/%d: %s\n", current_idx + 1, total_count, image_path);

    // Step 1: 加载图片
    if (callback) callback(1, "正在加载望远镜图片...", 5, 0, NULL, NULL, current_idx, total_count);
    sleep(1);

    // 检查文件是否存在
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
    if (callback) callback(2, "图像预处理：降噪、增强对比度...", 25, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(2, &error_code, &error_msg) != 0) {
        if (callback) callback(2, "图像预处理失败", 30, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 2);
    }
    if (callback) callback(2, "图像预处理完成", 30, 0, NULL, NULL, current_idx, total_count);

    // Step 3: 目标检测
    if (callback) callback(3, "卫星目标检测：识别候选目标...", 50, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(3, &error_code, &error_msg) != 0) {
        if (callback) callback(3, "卫星检测失败", 55, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 3);
    }
    if (callback) callback(3, "卫星检测完成", 55, 0, NULL, NULL, current_idx, total_count);

    // Step 4: 轨迹拟合
    if (callback) callback(4, "轨迹拟合：计算卫星运动轨迹...", 75, 0, NULL, NULL, current_idx, total_count);
    sleep(2);

    if (check_for_error(4, &error_code, &error_msg) != 0) {
        if (callback) callback(4, "轨迹拟合失败", 80, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 4);
    }
    if (callback) callback(4, "轨迹拟合完成", 80, 0, NULL, NULL, current_idx, total_count);

    // Step 5: 生成结果
    if (callback) callback(5, "正在生成轨迹数据...", 95, 0, NULL, NULL, current_idx, total_count);
    sleep(1);

    if (check_for_error(5, &error_code, &error_msg) != 0) {
        if (callback) callback(5, "结果生成失败", 100, error_code, error_msg, NULL, current_idx, total_count);
        return make_error_response(error_code, error_msg, 5);
    }

    // 单张图片的成功结果
    const char* success_json_template = 
        "{"
        "\"status\":\"success\","
        "\"total_tracks\":6,"
        "\"processing_time_ms\":7234,"
        "\"tracks\":["
        "{\"id\":1,\"norad_id\":25544,\"name\":\"国际空间站\",\"confidence\":0.96,\"points\":[[116.4,39.9],[117.2,40.5],[118.1,41.2]]},"
        "{\"id\":2,\"norad_id\":33591,\"name\":\"风云三号\",\"confidence\":0.92,\"points\":[[120.1,31.2],[121.0,31.8],[122.0,32.3]]},"
        "{\"id\":3,\"norad_id\":40336,\"name\":\"北斗卫星\",\"confidence\":0.89,\"points\":[[112.5,23.1],[113.2,23.7],[114.0,24.2]]},"
        "{\"id\":4,\"norad_id\":39444,\"name\":\"资源卫星\",\"confidence\":0.94,\"points\":[[108.3,34.0],[109.1,34.5],[110.0,35.1]]},"
        "{\"id\":5,\"norad_id\":42759,\"name\":\"吉林一号\",\"confidence\":0.91,\"points\":[[125.3,43.8],[126.0,44.2],[126.8,44.7]]},"
        "{\"id\":6,\"norad_id\":44804,\"name\":\"高分卫星\",\"confidence\":0.93,\"points\":[[100.5,28.6],[101.2,29.1],[102.0,29.6]]}"
        "]}";

    if (callback) callback(5, "单张图片处理完成！", 100, 0, NULL, success_json_template, current_idx, total_count);

    char* result = malloc(strlen(success_json_template) + 1);
    if (!result) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 5);
    }
    strcpy(result, success_json_template);
    return result;
}

// ========== 检查是否是图片文件 ==========
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

// ========== 批量处理文件夹 ==========
char* extract_trajectories_from_folder(const char* folder_path, ProgressCallback callback) {
    // 1. 打开文件夹
    DIR* dir = opendir(folder_path);
    if (!dir) {
        printf("[ERROR] 无法打开文件夹: %s\n", folder_path);
        return make_error_response(ERROR_FOLDER_OPEN, "无法打开文件夹", 0);
    }
    
    // 2. 收集所有图片文件
    struct dirent* entry;
    char** image_files = NULL;
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (is_image_file(entry->d_name)) {
            // 构建完整路径
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", folder_path, entry->d_name);
            
            image_files = realloc(image_files, (file_count + 1) * sizeof(char*));
            image_files[file_count] = strdup(full_path);
            file_count++;
        }
    }
    closedir(dir);
    
    if (file_count == 0) {
        printf("[ERROR] 文件夹中没有图片: %s\n", folder_path);
        return make_error_response(ERROR_NO_IMAGES, "没有找到图片文件", 0);
    }
    
    printf("[INFO] 找到 %d 张图片\n", file_count);
    
    // 3. 逐个处理图片
    char** all_results = malloc(file_count * sizeof(char*));
    int success_count = 0;
    
    for (int i = 0; i < file_count; i++) {
        char progress_msg[256];
        snprintf(progress_msg, sizeof(progress_msg), "正在处理 %d/%d", i + 1, file_count);
        
        if (callback) {
            callback(0, progress_msg, (i * 100) / file_count, 0, NULL, NULL, i, file_count);
        }
        
        // 调用单张处理
        char* result = extract_single_trajectory(image_files[i], callback, i, file_count);
        all_results[i] = result;
        
        // 检查是否成功
        if (result && strstr(result, "\"status\":\"success\"") != NULL) {
            success_count++;
        }
    }
    
    // 4. 合并所有结果为一个 JSON
    size_t total_size = 1024;  // 初始大小
    char* merged = malloc(total_size);
    if (!merged) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 0);
    }
    strcpy(merged, "{\"status\":\"success\",\"total_images\":");
    char num_str[32];
    snprintf(num_str, sizeof(num_str), "%d", file_count);
    strcat(merged, num_str);
    strcat(merged, ",\"success_count\":");
    snprintf(num_str, sizeof(num_str), "%d", success_count);
    strcat(merged, num_str);
    strcat(merged, ",\"results\":[");
    
    for (int i = 0; i < file_count; i++) {
        if (all_results[i]) {
            strcat(merged, all_results[i]);
            if (i < file_count - 1) {
                strcat(merged, ",");
            }
        }
    }
    strcat(merged, "]}");
    
    // 5. 清理
    for (int i = 0; i < file_count; i++) {
        free(image_files[i]);
        if (all_results[i]) {
            free(all_results[i]);
        }
    }
    free(image_files);
    free(all_results);
    
    if (callback) {
        callback(5, "批量处理完成！", 100, 0, NULL, merged, file_count, file_count);
    }
    
    return merged;
}

// ========== 兼容旧接口（单张处理） ==========
char* extract_satellite_trajectory(const char* image_path, ProgressCallback callback) {
    // 调用单张处理函数，current=0, total=1
    return extract_single_trajectory(image_path, callback, 0, 1);
}

// ========== 测试回调函数 ==========
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
    if (data && error_code == 0 && step == 5) {
        printf("📊 结果: %s\n", data);
    }
}

// ========== main 函数 ==========
int main(int argc, char* argv[]) {
    printf("=== 卫星轨迹提取测试 ===\n");
    printf("用法: ./test_c <图片路径或文件夹路径>\n");
    printf("   - 单张: ./test_c test.jpg\n");
    printf("   - 批量: ./test_c ./images/\n\n");
    
    if (argc < 2) {
        printf("❌ 请指定图片或文件夹路径\n");
        return 1;
    }
    
    const char* path = argv[1];
    
    // 判断是文件还是文件夹
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        printf("❌ 路径不存在: %s\n", path);
        return 1;
    }
    
    char* result;
    
    if (S_ISDIR(path_stat.st_mode)) {
        // 处理文件夹（批量）
        printf("📁 批量处理文件夹: %s\n", path);
        result = extract_trajectories_from_folder(path, test_callback);
    } else {
        // 处理单张图片
        printf("🖼️ 处理单张图片: %s\n", path);
        result = extract_satellite_trajectory(path, test_callback);
    }
    
    printf("\n=== 最终返回 ===\n%s\n", result);
    free(result);
    return 0;
}