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
// P2 函数前置声明
// ==========================================================================
// 事件表操作
EventList* event_list_create(void);
void       event_list_destroy(EventList *el);
int        event_list_insert(EventList *el, Event event);
int        event_list_pop(EventList *el, Event *out);
int        event_list_cancel_by_client(EventList *el, int client_id);
int        event_list_cancel_by_type(EventList *el, EventType type, double after_time);
int        event_list_empty(const EventList *el);
int        event_list_size(const EventList *el);

// 队列操作
Queue*   queue_create(int max_queues);
void     queue_destroy(Queue *q);
int      queue_enqueue(Queue *q, int queue_id, Client *c);
Client*  queue_dequeue(Queue *q, int queue_id);
Client*  queue_remove_by_id(Queue *q, int queue_id, int client_id);
int      queue_get_length(const Queue *q, int queue_id);
int      queue_get_min_length(const Queue *q, int *out_id);
Client*  queue_find_no_response(Queue *q, double current_time, double timeout);

// 窗口操作
Window*  window_create(int count, int tidal_count, double k_min, double k_max);
void     window_destroy(Window *wins);
int      window_switch_type(Window *w, WindowType new_type);
int      window_get_idle_count(const Window *wins, int count);
Window*  window_find_idle(Window *wins, int count, WindowType type);

// 仿真引擎
void sim_init(SimConfig *cfg, EventList *el, Queue *queue, Window *wins, int win_count);
void sim_run(SimConfig *cfg, EventList *el, Queue *queue, Window *wins,
             int win_count, EventHandler handlers[], StatsCollector *stats);
double sim_get_current_time(void);

// --- P3: 业务逻辑 ---
int  get_queue_for_window_type(WindowType wtype);
void try_call_next(SimConfig *cfg, Queue *queue, Window *wins, int win_count, EventList *el, StatsCollector *stats);
void start_service_for_client(Client *c, Window *win, SimConfig *cfg, EventList *el, StatsCollector *stats);
void schedule_next_arrival(SimConfig *cfg, EventList *el, double current_time);
void handle_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_bulk_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_start_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_end_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_window_switch(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void scheduler_check_tidal(SimConfig *cfg, Queue *queue, Window *wins, EventList *el, double current_time);
void scheduler_check_balking(Event *e, SimConfig *cfg, Queue *queue, StatsCollector *stats);
void scheduler_check_jockeying(SimConfig *cfg, Queue *queue, int queue_id, StatsCollector *stats);
void handle_no_response_timeout(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_shutdown(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);

// --- P4: 统计与日志 ---
void stats_record_arrival(StatsCollector *stats, Client *c);
void stats_record_service_start(StatsCollector *stats, Client *c, int window_id);
void stats_record_service_end(StatsCollector *stats, Client *c, int window_id, double duration);
void stats_record_wait(StatsCollector *stats, Client *c, double wait_time);
void stats_record_balk(StatsCollector *stats, Client *c);
void stats_record_jockey(StatsCollector *stats, Client *c, int from_q, int to_q);
void stats_record_error(StatsCollector *stats, int window_id);
void stats_record_queue_length(StatsCollector *stats, int length);
void stats_print_event(double time, const char *category, const char *fmt, ...);
double stats_get_max_queue_length(const StatsCollector *stats);
void stats_record_satisfaction(StatsCollector *stats, Client *c, double max_possible_wait);
void stats_write_report(const StatsCollector *stats, const char *filename, const Window *wins, int win_count);
void log_init(const char *filename);
void log_close(void);
void log_event(double time, const char *category, const char *fmt, ...);
void log_printf(const char *fmt, ...);

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
        {
            a ^= 0x9908B0DFU;
        }
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
    {
        mt_twist(rng);
    }
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
    if (p < acc)
    {
        return VIP_CLIENT;
    }
    acc += cfg->prob_corporate;
    if (p < acc)
    {
        return CORPORATE;
    }
    acc += cfg->prob_elderly;
    if (p < acc)
    {
        return ELDERLY;
    }
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
        {
            return i;
        }
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
    {
        memset(cfg->biz_prob[i], 0, sizeof(double) * 5);
    }
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
        {
            return slot.lambda;
        }
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

// ==========================================================================
// ====================== P2完整代码实现分区 ======================
// ==========================================================================

// --------------- 1. EventList 最小堆事件表 ---------------

// 事件表内部结构（不透明类型）
struct EventList {
    Event *heap;        // 动态数组（最小堆）
    int capacity;       // 当前容量
    int size;           // 当前元素数
};

// 上浮操作：维持最小堆性质（按timestamp排序）
static void event_list_sift_up(EventList *el, int idx)
{
    while (idx > 0)
    {
        int parent = (idx - 1) / 2;
        if (el->heap[parent].timestamp <= el->heap[idx].timestamp)
        {
            break;
        }
        // 交换
        Event tmp = el->heap[parent];
        el->heap[parent] = el->heap[idx];
        el->heap[idx] = tmp;
        idx = parent;
    }
}

// 下沉操作：维持最小堆性质
static void event_list_sift_down(EventList *el, int idx)
{
    while (1)
    {
        int smallest = idx;
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;

        if (left < el->size && el->heap[left].timestamp < el->heap[smallest].timestamp)
        {
            smallest = left;
        }
        if (right < el->size && el->heap[right].timestamp < el->heap[smallest].timestamp)
        {
            smallest = right;
        }
        if (smallest == idx)
        {
            break;
        }

        Event tmp = el->heap[idx];
        el->heap[idx] = el->heap[smallest];
        el->heap[smallest] = tmp;
        idx = smallest;
    }
}

EventList* event_list_create(void)
{
    EventList *el = (EventList*)safe_malloc(sizeof(EventList));
    el->capacity = 64;
    el->size = 0;
    el->heap = (Event*)safe_malloc(sizeof(Event) * el->capacity);
    return el;
}

void event_list_destroy(EventList *el)
{
    if (el)
    {
        free(el->heap);
        free(el);
    }
}

int event_list_insert(EventList *el, Event event)
{
    // 扩容
    if (el->size >= el->capacity)
    {
        el->capacity *= 2;
        el->heap = (Event*)realloc(el->heap, sizeof(Event) * el->capacity);
        if (!el->heap)
        {
            fprintf(stderr, "event_list扩容失败\n");
            exit(EXIT_FAILURE);
        }
    }
    el->heap[el->size] = event;
    event_list_sift_up(el, el->size);
    el->size++;
    return 1;
}

int event_list_pop(EventList *el, Event *out)
{
    if (el->size == 0)
    {
        return 0;
    }
    *out = el->heap[0];
    el->size--;
    if (el->size > 0)
    {
        el->heap[0] = el->heap[el->size];
        event_list_sift_down(el, 0);
    }
    return 1;
}

int event_list_cancel_by_client(EventList *el, int client_id)
{
    int removed = 0;
    // 从后往前遍历，避免跳过元素
    for (int i = el->size - 1; i >= 0; i--)
    {
        if ((el->heap[i].type == ARRIVAL || el->heap[i].type == START_SERVICE ||
             el->heap[i].type == END_SERVICE || el->heap[i].type == NO_RESPONSE_TIMEOUT) &&
            el->heap[i].data.client != NULL && el->heap[i].data.client->id == client_id)
        {
            // 用最后一个元素覆盖，然后下沉/上浮
            el->heap[i] = el->heap[el->size - 1];
            el->size--;
            if (i < el->size)
            {
                event_list_sift_down(el, i);
                event_list_sift_up(el, i);
            }
            removed++;
        }
    }
    return removed;
}

int event_list_cancel_by_type(EventList *el, EventType type, double after_time)
{
    int removed = 0;
    for (int i = el->size - 1; i >= 0; i--)
    {
        if (el->heap[i].type == type && el->heap[i].timestamp > after_time)
        {
            el->heap[i] = el->heap[el->size - 1];
            el->size--;
            if (i < el->size)
            {
                event_list_sift_down(el, i);
                event_list_sift_up(el, i);
            }
            removed++;
        }
    }
    return removed;
}

int event_list_empty(const EventList *el)
{
    return el->size == 0;
}

int event_list_size(const EventList *el)
{
    return el->size;
}

// --------------- 2. Queue 多级优先队列 ---------------

// 队列内部结构：每个队列是一个动态数组，按优先级排序
typedef struct {
    Client **items;     // 客户指针数组
    int length;         // 当前长度
    int capacity;       // 容量
} SubQueue;

struct Queue {
    SubQueue *queues;   // 各窗口队列数组
    int queue_count;    // 队列数量
};

// 比较两个客户的排队优先级：预约 > 画像权重 > 先到先得
static int client_priority(Client *a, Client *b)
{
    // 预约客户优先
    if (a->is_appointment != b->is_appointment)
    {
        return b->is_appointment - a->is_appointment;  // 预约的排前面
    }
    // 权重大的优先
    if (a->priority_weight != b->priority_weight)
    {
        return b->priority_weight - a->priority_weight;
    }
    // 同等条件下先到先得
    if (a->arrival_time != b->arrival_time)
    {
        return (a->arrival_time < b->arrival_time) ? -1 : 1;
    }
    return 0;
}

// 在SubQueue中找到正确插入位置（插入排序）
static void subqueue_insert_sorted(SubQueue *sq, Client *c)
{
    int pos = sq->length;
    // 找到插入位置（从后往前找第一个优先级不高于c的）
    while (pos > 0 && client_priority(c, sq->items[pos - 1]) < 0)
    {
        sq->items[pos] = sq->items[pos - 1];
        pos--;
    }
    sq->items[pos] = c;
    sq->length++;
}

Queue* queue_create(int max_queues)
{
    Queue *q = (Queue*)safe_malloc(sizeof(Queue));
    q->queue_count = max_queues;
    q->queues = (SubQueue*)safe_calloc(max_queues, sizeof(SubQueue));
    for (int i = 0; i < max_queues; i++)
    {
        q->queues[i].capacity = 32;
        q->queues[i].items = (Client**)safe_malloc(sizeof(Client*) * q->queues[i].capacity);
        q->queues[i].length = 0;
    }
    return q;
}

void queue_destroy(Queue *q)
{
    if (q)
    {
        for (int i = 0; i < q->queue_count; i++)
        {
            free(q->queues[i].items);
        }
        free(q->queues);
        free(q);
    }
}

int queue_enqueue(Queue *q, int queue_id, Client *c)
{
    if (queue_id < 0 || queue_id >= q->queue_count)
    {
        return 0;
    }
    SubQueue *sq = &q->queues[queue_id];
    // 扩容
    if (sq->length >= sq->capacity)
    {
        sq->capacity *= 2;
        sq->items = (Client**)realloc(sq->items, sizeof(Client*) * sq->capacity);
    }
    c->queue_id = queue_id;
    subqueue_insert_sorted(sq, c);
    return 1;
}

Client* queue_dequeue(Queue *q, int queue_id)
{
    if (queue_id < 0 || queue_id >= q->queue_count)
    {
        return NULL;
    }
    SubQueue *sq = &q->queues[queue_id];
    if (sq->length == 0)
    {
        return NULL;
    }
    Client *c = sq->items[0];
    // 前移
    for (int i = 0; i < sq->length - 1; i++)
    {
        sq->items[i] = sq->items[i + 1];
    }
    sq->length--;
    c->queue_id = -1;
    return c;
}

Client* queue_remove_by_id(Queue *q, int queue_id, int client_id)
{
    if (queue_id < 0 || queue_id >= q->queue_count)
    {
        return NULL;
    }
    SubQueue *sq = &q->queues[queue_id];
    for (int i = 0; i < sq->length; i++)
    {
        if (sq->items[i]->id == client_id)
        {
            Client *c = sq->items[i];
            for (int j = i; j < sq->length - 1; j++)
            {
                sq->items[j] = sq->items[j + 1];
            }
            sq->length--;
            c->queue_id = -1;
            return c;
        }
    }
    return NULL;
}

int queue_get_length(const Queue *q, int queue_id)
{
    if (queue_id < 0 || queue_id >= q->queue_count)
    {
        return 0;
    }
    return q->queues[queue_id].length;
}

// 返回最短队列的队列编号，*out_id = 队列编号
int queue_get_min_length(const Queue *q, int *out_id)
{
    int min_len = INT_MAX;
    int min_id = 0;
    for (int i = 0; i < q->queue_count; i++)
    {
        if (q->queues[i].length < min_len)
        {
            min_len = q->queues[i].length;
            min_id = i;
        }
    }
    if (out_id)
    {
        *out_id = min_id;
    }
    return min_len;
}

// 查找过号客户（等待时间超过timeout）
Client* queue_find_no_response(Queue *q, double current_time, double timeout)
{
    for (int i = 0; i < q->queue_count; i++)
    {
        SubQueue *sq = &q->queues[i];
        for (int j = 0; j < sq->length; j++)
        {
            Client *c = sq->items[j];
            // 有预约但已过号（等待超过timeout时间）
            if (c->is_appointment && (current_time - c->arrival_time) > timeout)
            {
                return c;
            }
        }
    }
    return NULL;
}

// --------------- 3. Window 窗口操作 ---------------

Window* window_create(int count, int tidal_count, double k_min, double k_max)
{
    Window *wins = (Window*)safe_calloc(count, sizeof(Window));
    for (int i = 0; i < count; i++)
    {
        wins[i].id = i;
        wins[i].status = WIN_IDLE;
        wins[i].current_client = NULL;
        wins[i].total_busy_time = 0;
        wins[i].total_served = 0;
        wins[i].total_errors = 0;

        // 分配窗口类型：前tidal_count个为潮汐窗口（默认个人），其余交替分配
        if (i < tidal_count)
        {
            wins[i].type = WIN_INDIVIDUAL;  // 潮汐窗口初始为个人业务
        }
        else if (i % 3 == 0)
        {
            wins[i].type = WIN_CORPORATE;
        }
        else if (i % 3 == 1)
        {
            wins[i].type = WIN_PRIORITY;
        }
        else
        {
            wins[i].type = WIN_INDIVIDUAL;
        }

        // 随机分配熟练度系数（由main调用时传入rng，这里用默认值）
        wins[i].staff_k = k_min + (k_max - k_min) * (0.5 + 0.5 * ((double)i / count));
    }
    return wins;
}

void window_destroy(Window *wins)
{
    if (wins)
    {
        free(wins);
    }
}

int window_switch_type(Window *w, WindowType new_type)
{
    if (w->status == WIN_SERVING || w->status == WIN_SWITCHING)
    {
        return 0;  // 办理中不能切换
    }
    w->type = new_type;
    return 1;
}

int window_get_idle_count(const Window *wins, int count)
{
    int idle = 0;
    for (int i = 0; i < count; i++)
    {
        if (wins[i].status == WIN_IDLE)
        {
            idle++;
        }
    }
    return idle;
}

Window* window_find_idle(Window *wins, int count, WindowType type)
{
    // 优先找同类型空闲窗口
    for (int i = 0; i < count; i++)
    {
        if (wins[i].status == WIN_IDLE && wins[i].type == type)
        {
            return &wins[i];
        }
    }
    // 找不到则找任意空闲窗口
    for (int i = 0; i < count; i++)
    {
        if (wins[i].status == WIN_IDLE)
        {
            return &wins[i];
        }
    }
    return NULL;
}

// --------------- 4. 仿真引擎 ---------------

static double g_current_time = 0;  // 全局仿真当前时间

double sim_get_current_time(void)
{
    return g_current_time;
}

void sim_init(SimConfig *cfg, EventList *el, Queue *queue, Window *wins, int win_count)
{
    g_current_time = cfg->open_time;

    // 插入SHUTDOWN事件（营业结束）
    Event shutdown_evt;
    shutdown_evt.timestamp = cfg->close_force_time;
    shutdown_evt.type = SHUTDOWN;
    shutdown_evt.data.client = NULL;
    event_list_insert(el, shutdown_evt);

    // 插入第一个ARRIVAL事件（第一次客户到达）
    schedule_next_arrival(cfg, el, g_current_time);

    // 插入第一个NO_RESPONSE_TIMEOUT检查事件
    Event timeout_check;
    timeout_check.timestamp = g_current_time + cfg->no_response_timeout;
    timeout_check.type = NO_RESPONSE_TIMEOUT;
    timeout_check.data.client = NULL;
    event_list_insert(el, timeout_check);

    // 插入第一个批量突发检查事件
    Event bulk_check;
    bulk_check.timestamp = cfg->bulk_time_start + rng_uniform(cfg->rng, 0, cfg->bulk_time_end - cfg->bulk_time_start);
    bulk_check.type = BULK_ARRIVAL;
    bulk_check.data.client = NULL;
    event_list_insert(el, bulk_check);

    printf("[仿真] 银行开门，时间 %s\n", time_format(g_current_time, (char[20]){0}, 20));
    printf("[仿真] 总窗口数: %d, 潮汐窗口: %d\n", win_count, cfg->tidal_window_count);
}

void sim_run(SimConfig *cfg, EventList *el, Queue *queue, Window *wins,
             int win_count, EventHandler handlers[], StatsCollector *stats)
{
    Event current;
    int running = 1;
    int event_count = 0;

    printf("[调试] 事件表初始大小: %d\n", event_list_size(el));

    while (running && !event_list_empty(el))
    {
        // 弹出最早事件
        if (!event_list_pop(el, &current))
        {
            break;
        }

        event_count++;
        if (event_count <= 10)
        {
            char buf[20];
            printf("[调试] 事件#%d: 时间=%s 类型=%d 剩余=%d\n",
                   event_count, time_format(current.timestamp, buf, 20),
                   current.type, event_list_size(el));
        }

        // 更新仿真时间
        g_current_time = current.timestamp;

        // 分发事件到对应处理函数
        if (current.type < 7 && handlers[current.type] != NULL)
        {
            handlers[current.type](&current, cfg, queue, wins, el, stats);
        }

        // 事件处理后再检查关门时间（确保同时间戳的END_SERVICE等事件先被处理）
        if (g_current_time >= cfg->close_force_time)
        {
            Event shutdown_evt;
            shutdown_evt.timestamp = g_current_time;
            shutdown_evt.type = SHUTDOWN;
            shutdown_evt.data.client = NULL;
            handlers[SHUTDOWN](&shutdown_evt, cfg, queue, wins, el, stats);
            running = 0;
            break;
        }
    }

    // 仿真结束统计
    printf("\n[调试] 共处理 %d 个事件\n", event_count);
    printf("[仿真] 银行关门，仿真结束\n");
    printf("[仿真] 最终时刻: %s\n", time_format(g_current_time, (char[20]){0}, 20));
}

// ==========================================================================
// ====================== P3 业务逻辑实现 ======================
// ==========================================================================
static int g_client_id_counter = 0;

// 窗口类型到队列ID的映射
int get_queue_for_window_type(WindowType wtype){
    switch (wtype) {
        case WIN_PRIORITY:  return 1;
        case WIN_CORPORATE: return 2;
        default:            return 0;
    }
}

// 叫号尝试
void try_call_next(SimConfig *cfg, Queue *queue, Window *wins, int win_count, EventList *el, StatsCollector *stats)
{
    for (int i = 0; i < win_count; i++)
    {
        if (wins[i].status != WIN_IDLE)
        {
            continue;
        }

        int qid = get_queue_for_window_type(wins[i].type);
        if (queue_get_length(queue, qid) > 0) {
            Client *next = queue_dequeue(queue, qid);
            if (next) {
                wins[i].status = WIN_SERVING;
                wins[i].current_client = next;
                next->status = STATUS_IN_SERVICE;
                next->start_time = g_current_time;

                Event start_ev;
                start_ev.timestamp = g_current_time;
                start_ev.type = START_SERVICE;
                start_ev.data.client = next;
                event_list_insert(el, start_ev);

                Event timeout_ev;
                timeout_ev.timestamp = g_current_time + cfg->no_response_timeout;
                timeout_ev.type = NO_RESPONSE_TIMEOUT;
                timeout_ev.data.client = next;
                event_list_insert(el, timeout_ev);

                stats_print_event(g_current_time, "CALL",
                                  "窗口 %d 叫号客户 %d", wins[i].id, next->id);
            }
        }
    }
}

// 客户开始服务
void start_service_for_client(Client *c, Window *win, SimConfig *cfg,
                              EventList *el, StatsCollector *stats)
{
    win->status = WIN_SERVING;
    win->current_client = c;
    c->status = STATUS_IN_SERVICE;
    c->start_time = g_current_time;
    stats_record_service_start(stats, c, win->id);

    BizDurationParam bp = cfg->biz_duration[c->business];
    double duration = rng_lognormal(cfg->rng, bp.mu, bp.sigma) * win->staff_k;
    if (duration < 0.5)
    {
        duration = 0.5;
    }

    Event end_evt;
    end_evt.timestamp = g_current_time + duration;
    end_evt.type = END_SERVICE;
    end_evt.data.client = c;
    event_list_insert(el, end_evt);

    stats_print_event(g_current_time, "SERVE",
                      "窗口 %d 开始服务客户 %d (业务%d, 耗时%.1f分)",
                      win->id, c->id, c->business, duration);
}

// 安排下个到达
void schedule_next_arrival(SimConfig *cfg, EventList *el, double current_time)
{
    double lambda = config_get_lambda(cfg, (int)current_time);
    if (lambda <= 0)
    {
        return;
    }
    double interval = rng_exponential(cfg->rng, lambda);
    double next_time = current_time + interval;
    if (next_time < cfg->close_stop_time) {
        Event e;
        e.timestamp = next_time;
        e.type = ARRIVAL;
        e.data.client = NULL;
        event_list_insert(el, e);
    }
}

// 单个客户到达
void handle_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    Client *c = (Client*)safe_malloc(sizeof(Client));
    c->id = ++g_client_id_counter;
    c->arrival_time = g_current_time;
    c->start_time = -1.0;
    c->end_time = -1.0;
    c->status = STATUS_WAITING;
    c->queue_id = -1;
    c->client_type = (ClientType)rng_random_client_type(cfg->rng, cfg);
    c->business = (BusinessType)rng_random_business(cfg->rng, cfg, c->client_type);
    c->is_appointment = (rng_uniform(cfg->rng, 0.0, 1.0) < 0.1) ? 1 : 0;

    switch (c->client_type)
    {
        case VIP_CLIENT:
            c->priority_weight = 3;
            break;
        case CORPORATE:
            c->priority_weight = 2;
            break;
        case ELDERLY:
            c->priority_weight = 2;
            break;
        default:
            c->priority_weight = 1;
            break;
    }

    // Balking 检查（基于事件的版本）
    e->data.client = c;
    scheduler_check_balking(e, cfg, queue, stats);
    if (e->data.client == NULL)
    {
        return;
    }

    // 正常入队（选择最短队列）
    int min_id;
    queue_get_min_length(queue, &min_id);
    queue_enqueue(queue, min_id, c);
    c->queue_id = min_id;
    stats_record_arrival(stats, c);
    stats_record_queue_length(stats, queue_get_length(queue, min_id));

    // 尝试立即开始服务
    WindowType pref = WIN_INDIVIDUAL;
    if (c->client_type == VIP_CLIENT || c->client_type == ELDERLY)
    {
        pref = WIN_PRIORITY;
    }
    else if (c->client_type == CORPORATE)
    {
        pref = WIN_CORPORATE;
    }

    Window *idle = window_find_idle(wins, cfg->window_count, pref);
    if (idle)
    {
        Client *svc = queue_dequeue(queue, min_id);
        if (svc)
        {
            Event no_resp;
            no_resp.timestamp = g_current_time + cfg->no_response_timeout;
            no_resp.type = NO_RESPONSE_TIMEOUT;
            no_resp.data.client = svc;
            event_list_insert(el, no_resp);
            event_list_cancel_by_client(el, svc->id);
            start_service_for_client(svc, idle, cfg, el, stats);
        }
    }

    // 安排下一次到达
    schedule_next_arrival(cfg, el, g_current_time);

    // 潮汐窗口检查
    scheduler_check_tidal(cfg, queue, wins, el, g_current_time);
}

// 批量突发到达
void handle_bulk_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    int count = (int)rng_uniform(cfg->rng, cfg->bulk_count_min, cfg->bulk_count_max + 1);
    stats_print_event(g_current_time, "BULK", "批量到达 %d 人", count);

    for (int i = 0; i < count; i++)
    {
        Client *c = (Client*)safe_malloc(sizeof(Client));
        c->id = ++g_client_id_counter;
        c->arrival_time = g_current_time;
        c->start_time = -1.0;
        c->end_time = -1.0;
        c->status = STATUS_WAITING;
        c->client_type = (ClientType)rng_random_client_type(cfg->rng, cfg);
        c->business = (BusinessType)rng_random_business(cfg->rng, cfg, c->client_type);
        c->is_appointment = 0;
        switch (c->client_type)
        {
            case VIP_CLIENT:
                c->priority_weight = 3;
                break;
            case CORPORATE:
                c->priority_weight = 2;
                break;
            case ELDERLY:
                c->priority_weight = 2;
                break;
            default:
                c->priority_weight = 1;
                break;
        }

        // 批量到达的Balking独立处理
        int min_len;
        int min_qid;
        queue_get_min_length(queue, &min_qid);
        min_len = queue_get_length(queue, min_qid);
        if (min_len >= cfg->balking_threshold &&
            rng_uniform(cfg->rng, 0.0, 1.0) < cfg->balking_probability)
        {
            c->status = STATUS_BALKED;
            stats_record_balk(stats, c);
            stats_record_arrival(stats, c);
            free(c);
            continue;
        }

        // 入队
        queue_enqueue(queue, min_qid, c);
        c->queue_id = min_qid;
        stats_record_arrival(stats, c);
        stats_record_queue_length(stats, queue_get_length(queue, min_qid));

        // 尝试立即服务
        WindowType pref = WIN_INDIVIDUAL;
        if (c->client_type == VIP_CLIENT || c->client_type == ELDERLY)
        {
            pref = WIN_PRIORITY;
        }
        else if (c->client_type == CORPORATE)
        {
            pref = WIN_CORPORATE;
        }
        Window *idle = window_find_idle(wins, cfg->window_count, pref);
        if (idle)
        {
            Client *svc = queue_dequeue(queue, min_qid);
            if (svc)
            {
                Event no_resp;
                no_resp.timestamp = g_current_time + cfg->no_response_timeout;
                no_resp.type = NO_RESPONSE_TIMEOUT;
                no_resp.data.client = svc;
                event_list_insert(el, no_resp);
                event_list_cancel_by_client(el, svc->id);
                start_service_for_client(svc, idle, cfg, el, stats);
            }
        }
    }
}

// 开始办理
void handle_start_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats)
{
    Client *c = e->data.client;
    if (!c)
    {
        return;
    }

    if (c->status != STATUS_IN_SERVICE)
    {
        return;
    }

    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++)
    {
        if (wins[i].current_client == c)
        {
            win = &wins[i];
            break;
        }
    }
    if (!win)
    {
        return;
    }

    event_list_cancel_by_client(el, c->id);
    start_service_for_client(c, win, cfg, el, stats);
}

// 结束办理
void handle_end_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats)
{
    Client *c = e->data.client;
    if (!c)
    {
        return;
    }

    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++)
    {
        if (wins[i].current_client == c)
        {
            win = &wins[i];
            break;
        }
    }
    if (!win)
    {
        return;
    }

    double duration = g_current_time - c->start_time;
    c->end_time = g_current_time;
    c->status = STATUS_COMPLETED;
    stats_record_service_end(stats, c, win->id, duration);
    stats_record_wait(stats, c, c->start_time - c->arrival_time);
    stats_record_satisfaction(stats, c, cfg->max_satisfy_wait);

    win->total_busy_time += duration;
    win->total_served++;
    win->current_client = NULL;

    // 异常中断检查
    if (rng_uniform(cfg->rng, 0.0, 1.0) < cfg->error_probability)
    {
        win->total_errors++;
        stats_record_error(stats, win->id);
        win->status = WIN_SWITCHING;
        Event switch_evt;
        switch_evt.timestamp = g_current_time + 2.0;
        switch_evt.type = WINDOW_SWITCH;
        switch_evt.data.window_id = win->id;
        event_list_insert(el, switch_evt);
        log_event(g_current_time, "ERROR", "窗口 %d 异常中断，2分钟后恢复", win->id);
    }
    // 正常情况：潮汐关闭检查与叫号
    else
    {
        int tidal_base = cfg->window_count - cfg->tidal_window_count;
        if (win->id >= tidal_base &&
            !(g_current_time >= cfg->tidal_start && g_current_time < cfg->tidal_end))
        {
            win->status = WIN_CLOSED;
        }
        else
        {
            win->status = WIN_IDLE;
            try_call_next(cfg, queue, wins, cfg->window_count, el, stats);
        }
    }

    free(c);

    // Jockeying 检查
    int qid = get_queue_for_window_type(win->type);
    scheduler_check_jockeying(cfg, queue, qid, stats);

    // 潮汐窗口全局调度检查
    scheduler_check_tidal(cfg, queue, wins, el, g_current_time);

    // 关门条件：停止取号且所有队列为空
    if (g_current_time >= cfg->close_stop_time)
    {
        int all_empty = 1;
        for (int i = 0; i < cfg->window_count; i++)
        {
            if (queue_get_length(queue, i) > 0)
            {
                all_empty = 0;
                break;
            }
        }
        if (all_empty)
        {
            Event shutdown_ev;
            shutdown_ev.timestamp = g_current_time;
            shutdown_ev.type = SHUTDOWN;
            event_list_insert(el, shutdown_ev);
        }
    }
}

// 弹性窗口切换
void handle_window_switch(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats)
{
    int wid = e->data.window_id;
    if (wid < 0 || wid >= cfg->window_count)
    {
        return;
    }
    Window *win = &wins[wid];

    int in_tidal = (g_current_time >= cfg->tidal_start && g_current_time < cfg->tidal_end);
    int tidal_base = cfg->window_count - cfg->tidal_window_count;
    int is_tidal_win = (wid >= tidal_base);

    if (is_tidal_win)
    {
        if (in_tidal)
        {
            window_switch_type(win, WIN_INDIVIDUAL);
        }
        else
        {
            win->status = WIN_CLOSED;
            return;
        }
    }
    else
    {
        window_switch_type(win, win->type);
    }

    win->status = WIN_IDLE;
    try_call_next(cfg, queue, wins, cfg->window_count, el, stats);
}

// 潮汐窗口定时检查
void scheduler_check_tidal(SimConfig *cfg, Queue *queue, Window *wins, EventList *el, double current_time)
{
    if (cfg->tidal_window_count <= 0)
    {
        return;
    }

    int total_waiting = 0;
    for (int i = 0; i < cfg->window_count; i++)
    {
        total_waiting += queue_get_length(queue, i);
    }

    int in_tidal = (current_time >= cfg->tidal_start && current_time < cfg->tidal_end);
    int need_open = in_tidal && (total_waiting > cfg->window_count);

    int tidal_base = cfg->window_count - cfg->tidal_window_count;
    for (int i = tidal_base; i < cfg->window_count; i++)
    {
        if (need_open)
        {
            if (wins[i].status == WIN_CLOSED)
            {
                wins[i].status = WIN_SWITCHING;
                Event ev;
                ev.timestamp = current_time;
                ev.type = WINDOW_SWITCH;
                ev.data.window_id = i;
                event_list_insert(el, ev);
            }
        }
        else
        {
            if (wins[i].status == WIN_IDLE)
            {
                wins[i].status = WIN_CLOSED;
            }
        }
    }
}

// Balking判断
void scheduler_check_balking(Event *e, SimConfig *cfg, Queue *queue, StatsCollector *stats)
{
    Client *c = e->data.client;
    if (!c)
    {
        return;
    }

    int min_len = 9999;
    for (int i = 0; i < cfg->window_count; i++)
    {
        int len = queue_get_length(queue, i);
        if (len < min_len)
        {
            min_len = len;
        }
    }

    if (min_len >= cfg->balking_threshold)
    {
        double r = rng_uniform(cfg->rng, 0.0, 1.0);
        if (r < cfg->balking_probability)
        {
            c->status = STATUS_BALKED;
            stats_record_balk(stats, c);
            stats_record_arrival(stats, c);
            free(c);
            e->data.client = NULL;
        }
    }
}

// Jockeying换队检查
void scheduler_check_jockeying(SimConfig *cfg, Queue *queue, int queue_id, StatsCollector *stats){
    for (int from = 0; from < cfg->window_count; from++) {
        if (from == queue_id) continue;
        int len_from = queue_get_length(queue, from);
        if (len_from == 0) continue;

        for (int to = 0; to < cfg->window_count; to++) {
            if (to == from) continue;
            int len_to = queue_get_length(queue, to);
            if (len_from - len_to >= cfg->jockey_diff) {
                double r = rng_uniform(cfg->rng, 0.0, 1.0);
                if (r < cfg->jockey_probability) {
                    Client *c = queue_dequeue(queue, from);
                    if (c) {
                        queue_enqueue(queue, to, c);
                        stats_record_jockey(stats, c, from, to);
                        c->queue_id = to;
                        stats_record_queue_length(stats, queue_get_length(queue, from));
                        stats_record_queue_length(stats, queue_get_length(queue, to));
                        break;
                    }
                }
            }
        }
    }
}

// 过号超时处理
void handle_no_response_timeout(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    Client *c = e->data.client;
    if (!c) return;

    if (c->status != STATUS_IN_SERVICE)
        return;

    // 释放窗口
    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++) {
        if (wins[i].current_client == c) {
            win = &wins[i];
            win->current_client = NULL;
            win->status = WIN_IDLE;
            break;
        }
    }

    // 客户降级
    c->priority_weight = (c->priority_weight > 500) ? c->priority_weight - 500 : 1;
    c->status = STATUS_WAITING;
    c->start_time = -1.0;

    // 重新入队
    int qid = get_queue_for_window_type(
        (c->client_type == VIP_CLIENT || c->client_type == ELDERLY) ? WIN_PRIORITY :
        (c->client_type == CORPORATE) ? WIN_CORPORATE : WIN_INDIVIDUAL
    );
    queue_enqueue(queue, qid, c);
    c->queue_id = qid;
    stats_record_queue_length(stats, queue_get_length(queue, qid));

    stats_print_event(g_current_time, "NO_RESP",
                      "客户 %d 过号超时，降权至 %d，重新入队 Q%d",
                      c->id, c->priority_weight, qid);

    // 立即为释放的窗口叫下一位
    try_call_next(cfg, queue, wins, cfg->window_count, el, stats);
}

// 营业结束处理
void handle_shutdown(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    log_event(g_current_time, "SHUTDOWN", "银行关门，强制结束所有服务并清空队列");

    // 强制结束正在进行中的服务
    for (int i = 0; i < cfg->window_count; i++) {
        if (wins[i].current_client) {
            Client *c = wins[i].current_client;
            c->end_time = g_current_time;
            c->status = STATUS_COMPLETED;
            double wait = c->start_time - c->arrival_time;
            stats_record_service_end(stats, c, i, g_current_time - c->start_time);
            stats_record_wait(stats, c, wait);
            stats_record_satisfaction(stats, c, cfg->max_satisfy_wait);
            wins[i].current_client = NULL;
            free(c);
        }
        wins[i].status = WIN_CLOSED;
    }

    // 清空所有排队客户
    for (int qid = 0; qid < cfg->window_count; qid++) {
        Client *c;
        while ((c = queue_dequeue(queue, qid)) != NULL) {
            c->status = STATUS_ABANDONED;
            free(c);
        }
    }

    log_event(g_current_time, "SHUTDOWN", "仿真结束");
}

// ==========================================================================
// ====================== P4 统计与日志实现 ======================
// ==========================================================================

// ---------- Logger 结构体与操作 ----------

#define LOG_BUF_SIZE 4096

typedef struct {
    FILE *fp;               // 日志文件指针
    char buf[LOG_BUF_SIZE]; // 写缓冲区
    int buf_len;            // 当前缓冲区长度
} Logger;

static Logger g_logger = { NULL, "", 0 };

// 刷新缓冲区到文件
static void log_flush(void)
{
    if (g_logger.fp && g_logger.buf_len > 0)
    {
        fwrite(g_logger.buf, 1, g_logger.buf_len, g_logger.fp);
        g_logger.buf_len = 0;
    }
}

// 追加内容到缓冲区
static void log_append(const char *str, int len)
{
    if (!g_logger.fp) return;
    if (g_logger.buf_len + len >= LOG_BUF_SIZE)
        log_flush();
    memcpy(g_logger.buf + g_logger.buf_len, str, len);
    g_logger.buf_len += len;
}

void log_init(const char *filename)
{
    g_logger.fp = fopen(filename, "w");
    g_logger.buf_len = 0;
    if (!g_logger.fp)
        fprintf(stderr, "[日志] 无法打开日志文件: %s\n", filename);
}

void log_close(void)
{
    log_flush();
    if (g_logger.fp)
    {
        fclose(g_logger.fp);
        g_logger.fp = NULL;
    }
}

void log_event(double time, const char *category, const char *fmt, ...)
{
    char timebuf[20];
    char line[512];

    // 格式化: [HH:MM][CATEGORY] message
    int off = snprintf(line, sizeof(line), "[%s][%s] ",
                       time_format(time, timebuf, 20), category);

    va_list args;
    va_start(args, fmt);
    off += vsnprintf(line + off, sizeof(line) - off, fmt, args);
    va_end(args);

    line[off++] = '\n';
    line[off] = '\0';

    // 同时输出到控制台和日志文件
    printf("%s", line);
    log_append(line, off);
}

void log_printf(const char *fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    int off = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    printf("%s", line);
    log_append(line, off);
}

// ---------- StatsCollector 操作函数 ----------

void stats_record_arrival(StatsCollector *stats, Client *c)
{
    stats->total_arrivals++;
}

void stats_record_service_start(StatsCollector *stats, Client *c, int window_id)
{
    if (window_id >= 0 && window_id < MAX_WINDOWS)
        stats->window_served[window_id]++;
}

void stats_record_service_end(StatsCollector *stats, Client *c, int window_id, double duration)
{
    stats->total_completed++;
    if (window_id >= 0 && window_id < MAX_WINDOWS)
        stats->window_busy_time[window_id] += duration;
}

void stats_record_wait(StatsCollector *stats, Client *c, double wait_time)
{
    if (c->client_type >= 0 && c->client_type < 4)
    {
        stats->wait_time_sum[c->client_type] += wait_time;
        stats->wait_time_count[c->client_type]++;
    }
}

void stats_record_balk(StatsCollector *stats, Client *c)
{
    stats->total_balked++;
}

void stats_record_jockey(StatsCollector *stats, Client *c, int from_q, int to_q)
{
    stats->total_jockeyed++;
}

void stats_record_error(StatsCollector *stats, int window_id)
{
    stats->total_errors++;
    if (window_id >= 0 && window_id < MAX_WINDOWS)
        stats->window_errors[window_id]++;
}

void stats_record_queue_length(StatsCollector *stats, int length)
{
    if (length > stats->max_queue_length)
    {
        stats->max_queue_length = length;
        stats->max_queue_time = g_current_time;
    }
}

void stats_print_event(double time, const char *category, const char *fmt, ...)
{
    char buf[20];
    printf("[%s][%s] ", time_format(time, buf, 20), category);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

double stats_get_max_queue_length(const StatsCollector *stats)
{
    return stats->max_queue_length;
}

void stats_record_satisfaction(StatsCollector *stats, Client *c, double max_possible_wait)
{
    double wait = (c->start_time > 0) ? c->start_time - c->arrival_time : max_possible_wait;
    double sat = (wait < max_possible_wait) ? 1.0 - wait / max_possible_wait : 0.0;
    stats->satisfaction_sum += sat;
    stats->satisfaction_count++;
}

// ---------- 报告生成 ----------

void stats_write_report(const StatsCollector *stats, const char *filename,
                        const Window *wins, int win_count)
{
    FILE *f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "========================================\n");
    fprintf(f, "       银行排队调度仿真报告\n");
    fprintf(f, "========================================\n\n");

    // 1. 总体统计
    fprintf(f, "一、总体统计\n");
    fprintf(f, "  总到达人数:     %d\n", stats->total_arrivals);
    fprintf(f, "  总完成人数:     %d\n", stats->total_completed);
    fprintf(f, "  总弃号人数:     %d\n", stats->total_balked);
    fprintf(f, "  总换队次数:     %d\n", stats->total_jockeyed);
    fprintf(f, "  总异常中断:     %d\n", stats->total_errors);
    fprintf(f, "\n");

    // 2. 各画像等待时间
    const char *type_names[] = { "VIP", "企业", "老年", "普通" };
    fprintf(f, "二、各画像等待时间\n");
    fprintf(f, "  %-6s  %-10s  %-10s  %-10s\n", "画像", "人数", "总等待(分)", "平均等待(分)");
    fprintf(f, "  %-6s  %-10s  %-10s  %-10s\n", "------", "--------", "----------", "----------");
    for (int i = 0; i < 4; i++)
    {
        double avg = (stats->wait_time_count[i] > 0)
            ? stats->wait_time_sum[i] / stats->wait_time_count[i] : 0;
        fprintf(f, "  %-6s  %-10d  %-10.1f  %-10.2f\n",
                type_names[i], stats->wait_time_count[i],
                stats->wait_time_sum[i], avg);
    }
    fprintf(f, "\n");

    // 3. 各窗口统计
    fprintf(f, "三、各窗口统计\n");
    fprintf(f, "  %-8s  %-8s  %-12s  %-10s  %-8s\n",
            "窗口", "类型", "服务时间(分)", "服务人数", "异常次数");
    fprintf(f, "  %-8s  %-8s  %-12s  %-10s  %-8s\n",
            "------", "------", "------------", "--------", "--------");
    const char *wtype_names[] = { "个人", "对公", "优先" };
    for (int i = 0; i < win_count && i < MAX_WINDOWS; i++)
    {
        fprintf(f, "  W%-7d  %-8s  %-12.1f  %-10d  %-8d\n",
                i, wtype_names[wins[i].type],
                stats->window_busy_time[i],
                stats->window_served[i],
                stats->window_errors[i]);
    }
    fprintf(f, "\n");

    // 4. 排队与满意度
    fprintf(f, "四、排队与满意度\n");
    fprintf(f, "  最大排队长度:   %d (时刻 %s)\n",
            stats->max_queue_length,
            time_format(stats->max_queue_time, (char[20]){0}, 20));
    double avg_sat = (stats->satisfaction_count > 0)
        ? stats->satisfaction_sum / stats->satisfaction_count : 0;
    fprintf(f, "  平均满意度:     %.2f%%\n", avg_sat * 100);
    fprintf(f, "  满意度采样数:   %d\n", stats->satisfaction_count);
    fprintf(f, "\n");

    // 5. 均衡度偏差
    if (win_count > 0)
    {
        double avg_served = (double)stats->total_completed / win_count;
        double deviation = 0;
        for (int i = 0; i < win_count && i < MAX_WINDOWS; i++)
        {
            double diff = stats->window_served[i] - avg_served;
            deviation += diff * diff;
        }
        deviation = sqrt(deviation / win_count);
        fprintf(f, "五、均衡度\n");
        fprintf(f, "  平均每窗口服务: %.1f 人\n", avg_served);
        fprintf(f, "  服务量标准差:   %.2f\n", deviation);
    }

    fprintf(f, "\n========================================\n");
    fprintf(f, "             报告结束\n");
    fprintf(f, "========================================\n");

    fclose(f);
}

// ==========================================================================
// ====================== 主程序 main() ======================
// ==========================================================================
int main(int argc, char *argv[])
{
    SimConfig cfg;
    StatsCollector stats;
    EventList *el;
    Queue *queue;
    Window *wins;
    RandomEngine rng;

    config_init_default(&cfg);
    cfg.close_force_time = 540;  // 测试用：只跑540分钟，(8点到17点)
    cfg.close_stop_time = 480;   // 停止取号时间(16点)
    rng_init(&rng, 12345);
    cfg.rng = &rng;
    log_init("simulation.log");
    queue = queue_create(cfg.window_count);
    wins = window_create(cfg.window_count, cfg.tidal_window_count, 0.8, 1.5);
    el = event_list_create();

    memset(&stats, 0, sizeof(StatsCollector));
    stats.win_count = cfg.window_count;

    EventHandler handlers[7];
    handlers[ARRIVAL]          = handle_arrival;
    handlers[BULK_ARRIVAL]     = handle_bulk_arrival;
    handlers[START_SERVICE]    = handle_start_service;
    handlers[END_SERVICE]      = handle_end_service;
    handlers[WINDOW_SWITCH]    = handle_window_switch;
    handlers[NO_RESPONSE_TIMEOUT] = handle_no_response_timeout;
    handlers[SHUTDOWN]         = handle_shutdown;

    config_print(&cfg);
    sim_init(&cfg, el, queue, wins, cfg.window_count);
    sim_run(&cfg, el, queue, wins, cfg.window_count, handlers, &stats);

    stats_write_report(&stats, "report.txt", wins, cfg.window_count);
    log_close();
    event_list_destroy(el);
    queue_destroy(queue);
    window_destroy(wins);

    return 0;
}