#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>

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
#define ERROR_UNKNOWN 9999

// ========== 回调函数类型 ==========
typedef void (*ProgressCallback)(int step, const char* message, int progress,
                                  int error_code, const char* error_msg, const char* data);

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
    {1, ERROR_IMAGE_FORMAT, "不支持的图片格式，请使用 FITS、JPG、PNG 或 TIFF 格式", 0.10},
    {1, ERROR_IMAGE_CORRUPT, "图片文件已损坏，无法读取", 0.05},
    {1, ERROR_IMAGE_LOAD, "无法加载图片文件，文件可能不存在", 0.05},
    {2, ERROR_PREPROCESS, "图像预处理失败，内存不足", 0.08},
    {2, ERROR_NOISE_REDUCTION, "降噪算法失败，图片噪声过大", 0.07},
    {3, ERROR_DETECTION, "卫星目标检测算法异常", 0.05},
    {3, ERROR_NO_SATELLITE, "未检测到任何卫星目标，请检查图片是否包含卫星", 0.12},
    {3, ERROR_TOO_MANY_TARGETS, "检测到过多目标（>1000），请调整检测参数", 0.03},
    {4, ERROR_FITTING, "轨迹拟合算法失败", 0.06},
    {4, ERROR_INSUFFICIENT_POINTS, "有效轨迹点不足（<3个），无法拟合轨迹", 0.08},
    {5, ERROR_RESULT_GEN, "结果生成失败，无法序列化轨迹数据", 0.04},
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

// ========== 核心算法 ==========
char* extract_satellite_trajectory(const char* image_path, ProgressCallback callback) {
    int error_code = 0;
    const char* error_msg = NULL;

    printf("[INFO] 开始处理图片: %s\n", image_path);

    // Step 1: 加载图片
    if (callback) callback(1, "正在加载望远镜图片...", 5, 0, NULL, NULL);
    sleep(1);

    // 检查文件是否存在
    FILE* test = fopen(image_path, "rb");
    if (!test) {
        error_code = ERROR_IMAGE_LOAD;
        error_msg = "无法打开图片文件";
        if (callback) callback(1, "图片加载失败", 10, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 1);
    }
    fclose(test);

    if (check_for_error(1, &error_code, &error_msg) != 0) {
        if (callback) callback(1, "图片加载失败", 10, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 1);
    }
    if (callback) callback(1, "图片加载成功", 10, 0, NULL, NULL);

    // Step 2: 预处理
    if (callback) callback(2, "图像预处理：降噪、增强对比度...", 25, 0, NULL, NULL);
    sleep(2);

    if (check_for_error(2, &error_code, &error_msg) != 0) {
        if (callback) callback(2, "图像预处理失败", 30, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 2);
    }
    if (callback) callback(2, "图像预处理完成", 30, 0, NULL, NULL);

    // Step 3: 目标检测
    if (callback) callback(3, "卫星目标检测：识别候选目标...", 50, 0, NULL, NULL);
    sleep(2);

    if (check_for_error(3, &error_code, &error_msg) != 0) {
        if (callback) callback(3, "卫星检测失败", 55, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 3);
    }
    if (callback) callback(3, "卫星检测完成", 55, 0, NULL, NULL);

    // Step 4: 轨迹拟合
    if (callback) callback(4, "轨迹拟合：计算卫星运动轨迹...", 75, 0, NULL, NULL);
    sleep(2);

    if (check_for_error(4, &error_code, &error_msg) != 0) {
        if (callback) callback(4, "轨迹拟合失败", 80, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 4);
    }
    if (callback) callback(4, "轨迹拟合完成", 80, 0, NULL, NULL);

    // Step 5: 生成结果
    if (callback) callback(5, "正在生成轨迹数据...", 95, 0, NULL, NULL);
    sleep(1);

    if (check_for_error(5, &error_code, &error_msg) != 0) {
        if (callback) callback(5, "结果生成失败", 100, error_code, error_msg, NULL);
        return make_error_response(error_code, error_msg, 5);
    }

    // 成功结果
    const char* success_json =
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

    if (callback) callback(5, "轨迹提取完成！", 100, 0, NULL, success_json);

    char* result = malloc(strlen(success_json) + 1);
    if (!result) {
        return make_error_response(ERROR_UNKNOWN, "内存分配失败", 5);
    }
    strcpy(result, success_json);
    return result;
}

// ========== 回调函数（移到 main 外部） ==========
void test_callback(int step, const char* msg, int progress,
                   int error_code, const char* error_msg, const char* data) {
    if (error_code != 0) {
        printf("❌ [Step %d] 错误 %d: %s\n", step, error_code, error_msg);
    } else {
        printf("✅ [Step %d] %s (%d%%)\n", step, msg, progress);
    }
    if (data && error_code == 0) {
        printf("📊 结果: %s\n", data);
    }
}

// ========== main 函数 ==========
int main(int argc, char* argv[]) {
    printf("=== 卫星轨迹提取测试 ===\n");
    printf("注意：每次运行随机产生错误，多试几次可以看到不同错误\n\n");

    const char* test_path = argc > 1 ? argv[1] : "test.jpg";
    char* result = extract_satellite_trajectory(test_path, test_callback);

    printf("\n=== 最终返回 ===\n%s\n", result);
    free(result);
    return 0;
}