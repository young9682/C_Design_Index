#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

// ==========================================================================
// 【全局类型定义区 P1负责】枚举全部定义
// ==========================================================================
typedef enum { VIP_CLIENT, CORPORATE, ELDERLY, NORMAL } ClientType;

typedef enum {
    BIZ_CASH,        // 现金存取
    BIZ_TRANSFER,    // 转账汇款
    BIZ_ACCOUNT,     // 开户销户
    BIZ_FINANCE,     // 理财咨询
    BIZ_CORP_SETTLE  // 对公结算
} BusinessType;

typedef enum {
    STATUS_WAITING,      // 排队中
    STATUS_IN_SERVICE,   // 办理中
    STATUS_BALKED,       // 弃号离开
    STATUS_COMPLETED,    // 正常完成
    STATUS_ABANDONED     // 过号/关门未办结
} ClientStatus;

typedef enum {
    WIN_INDIVIDUAL,  // 个人业务窗口
    WIN_CORPORATE,   // 对公业务窗口
    WIN_PRIORITY     // 优先窗口(VIP/老年)
} WindowType;

typedef enum {
    WIN_IDLE,        // 空闲
    WIN_SERVING,     // 办理中
    WIN_SWITCHING,   // 切换中（弹性窗口）
    WIN_CLOSED       // 已关闭
} WindowStatus;

typedef enum {
    ARRIVAL,                // 单个客户到达
    BULK_ARRIVAL,           // 批量突发到达
    START_SERVICE,          // 开始办理
    END_SERVICE,            // 办理结束
    WINDOW_SWITCH,          // 窗口类型切换
    NO_RESPONSE_TIMEOUT,    // 过号超时
    SHUTDOWN                // 营业结束
} EventType;

// ==========================================================================
// 【全局结构体定义区 P1负责】
// ==========================================================================
// 随机数引擎：梅森旋转MT19937
typedef struct {
    unsigned int seed;
    unsigned int mt[624];
    int mt_index;
} RandomEngine;

// 时段到达率配置
typedef struct {
    int start_min;
    int end_min;
    double lambda;
} TimeSlot;

// 业务耗时：对数正态分布参数
typedef struct {
    double mu;
    double sigma;
} BizDurationParam;

// 客户结构体
typedef struct {
    int id;
    ClientType client_type;
    BusinessType business;
    ClientStatus status;

    double arrival_time;
    double start_time;
    double end_time;

    int priority_weight;
    int is_appointment;
    int queue_id;
} Client;

// 窗口结构体
typedef struct {
    int id;
    WindowType type;
    WindowStatus status;
    double staff_k;
    Client *current_client;

    double total_busy_time;
    int total_served;
    int total_errors;
} Window;

// 事件结构体（最小堆使用）
typedef struct {
    double timestamp;
    EventType type;
    union {
        Client *client;
        int window_id;
    } data;
} Event;

// 全局仿真配置
typedef struct {
    RandomEngine *rng;

    TimeSlot time_slots[16];
    int time_slot_count;

    double prob_vip;
    double prob_corporate;
    double prob_elderly;

    double biz_prob[4][5];
    BizDurationParam biz_duration[5];

    int window_count;
    int tidal_window_count;
    int tidal_start;
    int tidal_end;

    int balking_threshold;
    double balking_probability;
    int jockey_diff;
    double jockey_probability;
    double error_probability;
    double no_response_timeout;

    double bulk_time_start;
    double bulk_time_end;
    int bulk_count_min;
    int bulk_count_max;

    double open_time;
    double close_stop_time;
    double close_force_time;
    double max_satisfy_wait;
} SimConfig;

#define MAX_WINDOWS 10
// 统计结构体（P4实现，P1仅声明）
typedef struct {
    int total_arrivals;
    int total_completed;
    int total_balked;
    int total_jockeyed;
    int total_errors;

    double wait_time_sum[4];
    int wait_time_count[4];

    int window_served[MAX_WINDOWS];
    double window_busy_time[MAX_WINDOWS];
    int window_errors[MAX_WINDOWS];
    int win_count;

    int max_queue_length;
    double max_queue_time;

    double satisfaction_sum;
    int satisfaction_count;
} StatsCollector;

// 前置声明（P2结构体）
typedef struct EventList EventList;
typedef struct Queue Queue;

// 事件回调函数类型
typedef void (*EventHandler)(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);

// ==========================================================================
// P1 函数前置声明（全部接口）
// ==========================================================================
// 随机引擎
void rng_init(RandomEngine *rng, unsigned int seed);
void rng_set_seed(RandomEngine *rng, unsigned int seed);
double rng_uniform(RandomEngine *rng, double min, double max);
double rng_exponential(RandomEngine *rng, double lambda);
double rng_lognormal(RandomEngine *rng, double mu, double sigma);
int rng_random_client_type(RandomEngine *rng, const SimConfig *cfg);
int rng_random_business(RandomEngine *rng, const SimConfig *cfg, int ctype);

// 配置模块
void config_init_default(SimConfig *cfg);
double config_get_lambda(const SimConfig *cfg, int time_min);
void config_print(const SimConfig *cfg);

// 工具函数
char* time_format(double minutes, char *buf, int buf_size);
void* safe_malloc(size_t size);
void* safe_calloc(size_t nmemb, size_t size);

// ==========================================================================
// ====================== P1完整代码实现分区 ======================
// ==========================================================================

// --------------- 1.梅森旋转随机引擎实现 ---------------
static void mt_twist(RandomEngine *rng)
{
    unsigned int a;
    unsigned int upper_mask = 0x80000000;
    unsigned int lower_mask = 0x7FFFFFFF;
    unsigned int mt[624];
    memcpy(mt, rng->mt, sizeof(rng->mt));

    for (int i = 0; i < 624; i++)
    {
        unsigned int x = (mt[i] & upper_mask) + (mt[(i + 1) % 624] & lower_mask);
        a = x >> 1;
        if (x & 1)
            a ^= 0x9908B0DFU;
        mt[i] = mt[(i + 397) % 624] ^ a;
    }
    memcpy(rng->mt, mt, sizeof(rng->mt));
    rng->mt_index = 0;
}

void rng_init(RandomEngine *rng, unsigned int seed)
{
    rng->seed = seed;
    rng->mt[0] = seed;
    for (int i = 1; i < 624; i++)
    {
        rng->mt[i] = 1812433253U * (rng->mt[i - 1] ^ (rng->mt[i - 1] >> 30)) + i;
    }
    rng->mt_index = 624;
    mt_twist(rng);
}

void rng_set_seed(RandomEngine *rng, unsigned int seed)
{
    rng_init(rng, seed);
}

// 获取原始32位随机整数
static unsigned int mt_rand_uint(RandomEngine *rng)
{
    if (rng->mt_index >= 624)
        mt_twist(rng);
    unsigned int y = rng->mt[rng->mt_index++];
    // 梅森旋转提纯
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    y ^= y >> 18;
    return y;
}

// 均匀分布 [min,max]
double rng_uniform(RandomEngine *rng, double min, double max)
{
    unsigned int raw = mt_rand_uint(rng);
    double val = raw / 4294967296.0;
    return min + val * (max - min);
}

// 指数分布（到达间隔，泊松过程）
double rng_exponential(RandomEngine *rng, double lambda)
{
    double u = rng_uniform(rng, 1e-9, 1.0);
    return -log(u) / lambda;
}

// 对数正态分布（业务耗时）
double rng_lognormal(RandomEngine *rng, double mu, double sigma)
{
    // Box-Muller生成标准正态，规避u=0对数报错
    double u1 = rng_uniform(rng, 1e-9, 1.0);
    double u2 = rng_uniform(rng, 1e-9, 1.0);
    double z = sqrt(-2 * log(u1)) * cos(2 * M_PI * u2);
    double normal = mu + z * sigma;
    return exp(normal);
}

// 随机生成客户类型
int rng_random_client_type(RandomEngine *rng, const SimConfig *cfg)
{
    double p = rng_uniform(rng, 0, 1);
    double acc = 0;
    acc += cfg->prob_vip;
    if (p < acc) return VIP_CLIENT;
    acc += cfg->prob_corporate;
    if (p < acc) return CORPORATE;
    acc += cfg->prob_elderly;
    if (p < acc) return ELDERLY;
    return NORMAL;
}

// 根据客户类型随机业务类型
int rng_random_business(RandomEngine *rng, const SimConfig *cfg, int ctype)
{
    double p = rng_uniform(rng, 0, 1);
    double acc = 0;
    for (int i = 0; i < 5; i++)
    {
        acc += cfg->biz_prob[ctype][i];
        if (p < acc)
            return i;
    }
    return 4;
}

// --------------- 2.配置文件默认初始化 ---------------
void config_init_default(SimConfig *cfg)
{
    memset(cfg, 0, sizeof(SimConfig));

    // 时段配置：8:00~17:00 按小时划分，分钟为单位
    cfg->time_slot_count = 9;
    // 0~60 8-9点高峰 λ=8
    cfg->time_slots[0] = (TimeSlot){0, 60, 8.0};
    cfg->time_slots[1] = (TimeSlot){60, 120, 4.0};
    cfg->time_slots[2] = (TimeSlot){120, 180, 3.0};
    cfg->time_slots[3] = (TimeSlot){180, 240, 3.0};
    cfg->time_slots[4] = (TimeSlot){240, 300, 3.5};
    cfg->time_slots[5] = (TimeSlot){300, 360, 6.0};
    cfg->time_slots[6] = (TimeSlot){360, 420, 4.0};
    cfg->time_slots[7] = (TimeSlot){420, 480, 3.0};
    cfg->time_slots[8] = (TimeSlot){480, 540, 3.0};

    // 客户画像概率
    cfg->prob_vip = 0.10;
    cfg->prob_corporate = 0.15;
    cfg->prob_elderly = 0.20;

    // 业务概率矩阵初始化
    for (int i = 0; i < 4; i++)
        memset(cfg->biz_prob[i], 0, sizeof(double) * 5);
    // VIP：理财、优先存取款
    cfg->biz_prob[VIP_CLIENT][0] = 0.2;
    cfg->biz_prob[VIP_CLIENT][3] = 0.8;
    // 企业：对公结算、转账
    cfg->biz_prob[CORPORATE][1] = 0.3;
    cfg->biz_prob[CORPORATE][4] = 0.7;
    // 老人：存取款、开户
    cfg->biz_prob[ELDERLY][0] = 0.7;
    cfg->biz_prob[ELDERLY][2] = 0.3;
    // 普通客户均衡分配
    cfg->biz_prob[NORMAL][0] = 0.4;
    cfg->biz_prob[NORMAL][1] = 0.3;
    cfg->biz_prob[NORMAL][2] = 0.2;
    cfg->biz_prob[NORMAL][3] = 0.1;

    // 业务耗时参数
    cfg->biz_duration[0] = (BizDurationParam){2.0, 0.4};
    cfg->biz_duration[1] = (BizDurationParam){2.5, 0.5};
    cfg->biz_duration[2] = (BizDurationParam){3.0, 0.6};
    cfg->biz_duration[3] = (BizDurationParam){3.5, 0.5};
    cfg->biz_duration[4] = (BizDurationParam){4.0, 0.6};

    // 窗口基础配置
    cfg->window_count = 5;
    cfg->tidal_window_count = 2;
    cfg->tidal_start = 0;
    cfg->tidal_end = 540;

    // 调度参数
    cfg->balking_threshold = 10;
    cfg->balking_probability = 0.3;
    cfg->jockey_diff = 2;
    cfg->jockey_probability = 0.25;
    cfg->error_probability = 0.02;
    cfg->no_response_timeout = 3.0;

    // 批量突发
    cfg->bulk_time_start = 60;
    cfg->bulk_time_end = 480;
    cfg->bulk_count_min = 3;
    cfg->bulk_count_max = 8;

    // 营业时间
    cfg->open_time = 0;
    cfg->close_stop_time = 540;
    cfg->close_force_time = 720;
    cfg->max_satisfy_wait = 60.0;
}

// 根据当前分钟查找对应时段到达率λ
double config_get_lambda(const SimConfig *cfg, int time_min)
{
    for (int i = 0; i < cfg->time_slot_count; i++)
    {
        TimeSlot slot = cfg->time_slots[i];
        if (time_min >= slot.start_min && time_min < slot.end_min)
            return slot.lambda;
    }
    return 0.0;
}

// 打印配置信息
void config_print(const SimConfig *cfg)
{
    printf("=====仿真基础配置=====\n");
    printf("总窗口数:%d 潮汐窗口:%d\n", cfg->window_count, cfg->tidal_window_count);
    printf("VIP概率:%.2f 企业:%.2f 老人:%.2f\n",
           cfg->prob_vip, cfg->prob_corporate, cfg->prob_elderly);
    printf("弃号队列阈值:%d 异常概率:%.2f\n\n",
           cfg->balking_threshold, cfg->error_probability);
}

// --------------- 3.通用工具函数 ---------------
// 分钟转换 HH:MM时间格式
char* time_format(double minutes, char *buf, int buf_size)
{
    int total = (int)minutes;
    int hour = 8 + total / 60;
    int min = total % 60;
    snprintf(buf, buf_size, "%02d:%02d", hour, min);
    return buf;
}

// 安全malloc，失败终止
void* safe_malloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
    {
        fprintf(stderr, "malloc内存分配失败\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

// 安全calloc，清零初始化
void* safe_calloc(size_t nmemb, size_t size)
{
    void *p = calloc(nmemb, size);
    if (!p)
    {
        fprintf(stderr, "calloc内存分配失败\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

// ===================== P1测试主程序（可选）=====================
int main(void)
{
    // 初始化随机引擎与配置
    RandomEngine rng;
    SimConfig cfg;
    rng_init(&rng, (unsigned)time(NULL));
    config_init_default(&cfg);
    cfg.rng = &rng;

    // 打印配置
    config_print(&cfg);

    // 随机功能测试
    double uni = rng_uniform(&rng, 0, 1);
    double exp = rng_exponential(&rng, 5);
    double logn = rng_lognormal(&rng, 2, 0.4);
    int cType = rng_random_client_type(&rng, &cfg);

    char timeBuf[20];
    time_format(75, timeBuf, sizeof(timeBuf));

    printf("均匀随机数：%.4f\n", uni);
    printf("指数间隔：%.4f min\n", exp);
    printf("对数正态业务耗时：%.4f min\n", logn);
    printf("模拟时刻：%s\n", timeBuf);
    return 0;
}