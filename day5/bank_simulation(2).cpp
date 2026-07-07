#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

// ================================================================
// bank_simulation.cpp — 所有类型定义、函数声明、实现均在此文件
// ================================================================
// 文件结构：
//   [文件顶部]  枚举定义 + 结构体定义 + 函数前向声明
//   [P1分区]    随机引擎实现 + 配置实现 + 工具函数实现
//   [P2分区]    事件表/队列/窗口实现 + 仿真引擎实现
//   [P3分区]    业务逻辑实现
//   [P4分区]    统计/日志实现
//   [文件末尾]  main() 主程序

// ==================== 类型定义区（全员共享）====================

// ---------- 枚举 ----------

typedef enum { VIP_CLIENT, CORPORATE, ELDERLY, NORMAL } ClientType;

typedef enum {
    BIZ_CASH,           // 现金存取
    BIZ_TRANSFER,       // 转账汇款
    BIZ_ACCOUNT,        // 开户销户
    BIZ_FINANCE,        // 理财咨询
    BIZ_CORP_SETTLE     // 对公结算
} BusinessType;

typedef enum {
    STATUS_WAITING,     // 排队中
    STATUS_IN_SERVICE,  // 办理中
    STATUS_BALKED,      // 弃号离开
    STATUS_COMPLETED,   // 正常完成
    STATUS_ABANDONED    // 过号/关门未办结
} ClientStatus;

typedef enum {
    WIN_INDIVIDUAL,     // 个人业务窗口
    WIN_CORPORATE,      // 对公业务窗口
    WIN_PRIORITY        // 优先窗口（VIP/老年）
} WindowType;

typedef enum {
    WIN_IDLE,           // 空闲
    WIN_SERVING,        // 办理中
    WIN_SWITCHING,      // 切换中（弹性窗口）
    WIN_CLOSED          // 已关闭
} WindowStatus;

typedef enum {
    ARRIVAL,            // 单个客户到达
    BULK_ARRIVAL,       // 批量突发到达
    START_SERVICE,      // 开始办理
    END_SERVICE,        // 办理结束
    WINDOW_SWITCH,      // 窗口类型切换
    NO_RESPONSE_TIMEOUT,// 过号超时
    SHUTDOWN            // 营业结束
} EventType;

// ---------- 结构体定义 ----------

// 随机数引擎（梅森旋转算法内部状态，P1实现）
typedef struct {
    unsigned int seed;          // 当前种子
    unsigned int mt[624];       // 梅森旋转状态数组
    int mt_index;               // 状态索引
} RandomEngine;

// 客户节点（到达时 malloc，完成/流失/关门时 free）
typedef struct {
    int id;                     // 全局唯一编号
    ClientType client_type;     // 画像
    BusinessType business;      // 业务类型
    ClientStatus status;        // 当前状态

    double arrival_time;        // 到达时间（分钟，从08:00起算）
    double start_time;          // 开始服务时间（-1表示未开始）
    double end_time;            // 结束服务时间（-1表示未结束）

    int priority_weight;        // 排队权重（越大越优先）
    int is_appointment;         // 是否预约客户 0/1
    int queue_id;               // 当前所在队列编号（-1表示未入队）
} Client;

// 窗口节点
typedef struct {
    int id;                     // 窗口编号 0~N-1
    WindowType type;            // 窗口类型
    WindowStatus status;        // 当前状态
    double staff_k;             // 柜员熟练度系数 [0.8, 1.5]
    Client *current_client;     // 当前办理的客户（NULL表示空闲）

    double total_busy_time;     // 累计服务时间（用于计算空闲率）
    int total_served;           // 累计接待人数
    int total_errors;           // 累计异常中断次数
} Window;

// 事件节点（事件表最小堆使用）
typedef struct {
    double timestamp;           // 事件触发时间（分钟）
    EventType type;             // 事件类型
    union {
        Client *client;         // ARRIVAL: 新创建的客户; START_SERVICE/END_SERVICE: 正在服务的客户
        int window_id;          // WINDOW_SWITCH: 目标窗口编号
    } data;
    // 注意：BULK_ARRIVAL 不使用 data 字段，handler 内部批量创建客户
    //       NO_RESPONSE_TIMEOUT 不使用 data 字段，通过 queue_find_no_response 查找
    //       SHUTDOWN 不使用 data 字段
} Event;

// 时段配置项
typedef struct {
    int start_min;              // 时段起始（分钟）
    int end_min;                // 时段结束（分钟）
    double lambda;              // 到达率 λ（人/分钟）
} TimeSlot;

// 业务耗时参数（对数正态分布）
typedef struct {
    double mu;                  // 均值参数
    double sigma;               // 标准差参数
} BizDurationParam;

// 全局仿真配置
typedef struct {
    // 随机数引擎引用（由main初始化，各模块通过cfg->rng访问）
    RandomEngine *rng;

    // 时段到达率
    TimeSlot time_slots[16];    // 最多16个时段
    int time_slot_count;

    // 客户画像概率（累加到1.0）
    double prob_vip;            // 默认0.10
    double prob_corporate;      // 默认0.15
    double prob_elderly;        // 默认0.20
    // NORMAL = 1 - 以上三项

    // 各画像的业务类型概率矩阵 [ClientType][BusinessType]
    double biz_prob[4][5];

    // 各业务耗时分布参数
    BizDurationParam biz_duration[5];

    // 窗口配置
    int window_count;           // 总窗口数（默认4~6）
    int tidal_window_count;     // 潮汐窗口数
    int tidal_start;            // 潮汐开启时间（分钟）
    int tidal_end;              // 潮汐关闭时间（分钟）

    // 调度参数
    int balking_threshold;      // Balking队列长度阈值（默认10）
    double balking_probability; // Balking概率
    int jockey_diff;            // Jockeying换队差值（默认2）
    double jockey_probability;  // Jockeying概率
    double error_probability;   // 异常中断概率（默认0.02）
    double no_response_timeout; // 过号超时时间（分钟）

    // 批量突发配置（随机触发）
    double bulk_time_start;     // 突发最早可触发时间（分钟）
    double bulk_time_end;       // 突发最晚可触发时间（分钟）
    int bulk_count_min;         // 突发人数下限
    int bulk_count_max;         // 突发人数上限

    // 营业时间
    double open_time;           // 开门时间（分钟，默认0 = 08:00）
    double close_stop_time;     // 停止取号时间（分钟，默认540 = 17:00）
    double close_force_time;    // 强制关门时间（分钟，默认720 = 20:00）

    // 满意度估算基准
    double max_satisfy_wait;    // 满意度=0 的等待时长上限（分钟，默认60）
} SimConfig;

// 统计收集器（P4实现，max_windows由SimConfig.window_count决定）
#define MAX_WINDOWS 10  // 窗口数组上限，与SimConfig.window_count配合使用

typedef struct {
    int total_arrivals;             // 总到达人数
    int total_completed;            // 总完成人数
    int total_balked;               // 总弃号人数
    int total_jockeyed;             // 总换队次数
    int total_errors;               // 总异常中断次数

    double wait_time_sum[4];        // 各画像等待时长累加（按 ClientType 索引）
    int wait_time_count[4];         // 各画像等待人数

    int window_served[MAX_WINDOWS];     // 各窗口服务量
    double window_busy_time[MAX_WINDOWS]; // 各窗口累计服务时间
    int window_errors[MAX_WINDOWS];     // 各窗口异常次数
    int win_count;                      // 实际窗口数（初始化时设置，用于边界检查）

    int max_queue_length;           // 最大排队长度
    double max_queue_time;          // 最大排队长度发生时间

    double satisfaction_sum;        // 满意度累加
    int satisfaction_count;         // 满意度采样数
} StatsCollector;

// 不透明类型前向声明（必须在 EventHandler 之前）
typedef struct EventList EventList;
typedef struct Queue Queue;

// 事件分发回调函数类型
typedef void (*EventHandler)(Event *e, SimConfig *cfg,
                             Queue *queue, Window *wins,
                             EventList *el, StatsCollector *stats);

// ==================== 函数前向声明区 ====================
// （按模块分区声明，实现放在对应分区）

// --- P1: 随机引擎与配置 ---
// 所有rng_*函数第一个参数均为RandomEngine*，显式传递引擎状态（无全局变量）
void   rng_init(RandomEngine *rng, unsigned int seed);  // 初始化梅森旋转引擎
void   rng_set_seed(RandomEngine *rng, unsigned int seed);  // 重新设置种子
double rng_uniform(RandomEngine *rng, double min, double max);
double rng_exponential(RandomEngine *rng, double lambda);
double rng_lognormal(RandomEngine *rng, double mu, double sigma);
int    rng_random_client_type(RandomEngine *rng, const SimConfig *cfg);
int    rng_random_business(RandomEngine *rng, const SimConfig *cfg, int ctype);
void   config_init_default(SimConfig *cfg);
double config_get_lambda(const SimConfig *cfg, int time_min);
void   config_print(const SimConfig *cfg);
char*  time_format(double minutes, char *buf, int buf_size);
void*  safe_malloc(size_t size);
void*  safe_calloc(size_t nmemb, size_t size);

// --- P2: DES核心引擎 ---
EventList* event_list_create(void);
void       event_list_destroy(EventList *el);
int        event_list_insert(EventList *el, Event event);
int        event_list_pop(EventList *el, Event *out);
int        event_list_cancel_by_client(EventList *el, int client_id);  // 取消指定客户的所有待处理事件
int        event_list_cancel_by_type(EventList *el, EventType type, double after_time);  // 取消某时间之后的指定类型事件
int        event_list_empty(const EventList *el);
int        event_list_size(const EventList *el);

Queue*   queue_create(int max_queues);
void     queue_destroy(Queue *q);
int      queue_enqueue(Queue *q, int queue_id, Client *c);
Client*  queue_dequeue(Queue *q, int queue_id);
Client*  queue_remove_by_id(Queue *q, int queue_id, int client_id);
int      queue_get_length(const Queue *q, int queue_id);
int      queue_get_min_length(const Queue *q, int *out_id);
Client*  queue_find_no_response(Queue *q, double current_time, double timeout);

Window*  window_create(int count, int tidal_count, double k_min, double k_max);
void     window_destroy(Window *wins);
int      window_switch_type(Window *w, WindowType new_type);
int      window_get_idle_count(const Window *wins, int count);
Window*  window_find_idle(Window *wins, int count, WindowType type);

static double g_current_time = 0.0;  // 仿真当前时间（分钟），由sim_run内部维护
void sim_init(SimConfig *cfg, EventList *el, Queue *queue, Window *wins, int win_count);
void sim_run(SimConfig *cfg, EventList *el, Queue *queue, Window *wins,
             int win_count, EventHandler handlers[], StatsCollector *stats);
double sim_get_current_time(void);  // 返回仿真当前时间（由sim_run内部维护的静态变量）

// --- P3: 业务逻辑 ---
static int g_client_id_counter = 0;
int  get_queue_for_window_type(WindowType wtype);
void try_call_next(SimConfig *cfg, Queue *queue, Window *wins, int win_count, EventList *el, StatsCollector *stats);
void schedule_next_arrival(SimConfig *cfg, EventList *el, double current_time);  // 由handle_arrival内部调用，安排下一次到达事件
void handle_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void handle_bulk_arrival(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats);
void start_service_for_client(Client *c, Window *win, SimConfig *cfg, EventList *el, StatsCollector *stats);
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
void stats_print_event(double time, const char *category, const char *fmt, ...);  // 关键事件带时间戳打印到控制台
double stats_get_max_queue_length(const StatsCollector *stats);
void stats_record_satisfaction(StatsCollector *stats, Client *c, double max_possible_wait);
void stats_write_report(const StatsCollector *stats, const char *filename, const Window *wins, int win_count);
void log_init(const char *filename);
void log_close(void);
void log_event(double time, const char *category, const char *fmt, ...);
void log_printf(const char *fmt, ...);

// ==================== 函数实现区（按分区编写）====================
// 注：以下为示例框架，具体实现由各人负责
// --- P3: 业务逻辑 ---
// 窗口类型到队列ID的映射
int get_queue_for_window_type(WindowType wtype){
    switch (wtype) {
        case WIN_PRIORITY:  return 1;
        case WIN_CORPORATE: return 2;
        default:            return 0;
    }
}

// 叫号尝试
void try_call_next(SimConfig *cfg, Queue *queue, Window *wins, int win_count, EventList *el, StatsCollector *stats){
    for (int i = 0; i < win_count; i++) {
        if (wins[i].status != WIN_IDLE)
            continue;

        int qid = get_queue_for_window_type(wins[i].type);
        if (queue_get_length(queue, qid) > 0) {
            Client *next = queue_dequeue(queue, qid);
            if (next) {
                // 窗口立即占用，客户标记为“已叫号”
                wins[i].status = WIN_SERVING;
                wins[i].current_client = next;
                next->status = STATUS_IN_SERVICE;     // 视为服务中（等待窗口响应）
                next->start_time = g_current_time;    // 记录叫号时间（后续会被真实服务开始覆盖）

                // 1. 插入“客户到达窗口并开始服务”事件（立即）
                Event start_ev;
                start_ev.timestamp = g_current_time;
                start_ev.type = START_SERVICE;
                start_ev.data.client = next;
                event_list_insert(el, start_ev);

                // 2. 插入过号超时事件
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

// 安排下个到达
void schedule_next_arrival(SimConfig *cfg, EventList *el, double current_time){
    double lambda = config_get_lambda(cfg, (int)current_time);
    if (lambda <= 0) return;
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
    // 创建新客户
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

    switch (c->client_type) {
        case VIP_CLIENT: c->priority_weight = 3; break;
        case CORPORATE:  c->priority_weight = 2; break;
        case ELDERLY:    c->priority_weight = 2; break;
        default:         c->priority_weight = 1; break;
    }

    // Balking 检查（基于事件的版本）
    e->data.client = c;
    scheduler_check_balking(e, cfg, queue, stats);
    if (e->data.client == NULL) {
        // 客户已弃号离开
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
        pref = WIN_PRIORITY;
    else if (c->client_type == CORPORATE)
        pref = WIN_CORPORATE;

    Window *idle = window_find_idle(wins, cfg->window_count, pref);
    if (idle) {
        Client *svc = queue_dequeue(queue, min_id);
        if (svc) {
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

    for (int i = 0; i < count; i++) {
        Client *c = (Client*)safe_malloc(sizeof(Client));
        c->id = ++g_client_id_counter;
        c->arrival_time = g_current_time;
        c->start_time = -1.0;
        c->end_time = -1.0;
        c->status = STATUS_WAITING;
        c->client_type = (ClientType)rng_random_client_type(cfg->rng, cfg);
        c->business = (BusinessType)rng_random_business(cfg->rng, cfg, c->client_type);
        c->is_appointment = 0;
        switch (c->client_type) {
            case VIP_CLIENT: c->priority_weight = 3; break;
            case CORPORATE:  c->priority_weight = 2; break;
            case ELDERLY:    c->priority_weight = 2; break;
            default:         c->priority_weight = 1; break;
        }

        // 批量到达的Balking独立处理（基于Client*）
        int min_len;
        int min_qid;
        queue_get_min_length(queue, &min_qid);
        min_len = queue_get_length(queue, min_qid);
        if (min_len >= cfg->balking_threshold &&
            rng_uniform(cfg->rng, 0.0, 1.0) < cfg->balking_probability) {
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
            pref = WIN_PRIORITY;
        else if (c->client_type == CORPORATE)
            pref = WIN_CORPORATE;
        Window *idle = window_find_idle(wins, cfg->window_count, pref);
        if (idle) {
            Client *svc = queue_dequeue(queue, min_qid);
            if (svc) {
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

// 客户开始服务
void start_service_for_client(Client *c, Window *win, SimConfig *cfg,
                              EventList *el, StatsCollector *stats)
{
    win->status = WIN_SERVING;
    win->current_client = c;
    c->status = STATUS_IN_SERVICE;
    c->start_time = g_current_time;          // 真正服务开始时间（覆盖叫号时间）
    stats_record_service_start(stats, c, win->id);

    BizDurationParam bp = cfg->biz_duration[c->business];
    double duration = rng_lognormal(cfg->rng, bp.mu, bp.sigma) * win->staff_k;
    if (duration < 0.5) duration = 0.5;

    Event end_evt;
    end_evt.timestamp = g_current_time + duration;
    end_evt.type = END_SERVICE;
    end_evt.data.client = c;
    event_list_insert(el, end_evt);

    stats_print_event(g_current_time, "SERVE",
                      "窗口 %d 开始服务客户 %d (业务%d, 耗时%.1f分)",
                      win->id, c->id, c->business, duration);
}

// 开始办理：取号→计算时长→插入END
void handle_start_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    Client *c = e->data.client;
    if (!c) return;

    // 若状态已不是 IN_SERVICE，说明已被过号处理，忽略本次事件
    if (c->status != STATUS_IN_SERVICE)
        return;

    // 找到窗口
    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++) {
        if (wins[i].current_client == c) {
            win = &wins[i];
            break;
        }
    }
    if (!win) return;

    // ★ 取消超时事件，防止后续误触发
    event_list_cancel_by_client(el, c->id);

    // 实际开始服务（计算服务时长、插入 END_SERVICE 等）
    start_service_for_client(c, win, cfg, el, stats);
}

// 结束办理：释放窗口→叫号→检查异常
void handle_end_service(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    Client *c = e->data.client;
    if (!c) return;

    // 找到服务窗口
    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++) {
        if (wins[i].current_client == c) {
            win = &wins[i];
            break;
        }
    }
    if (!win) return;

    // 计算服务时长并记录统计
    double duration = g_current_time - c->start_time;
    c->end_time = g_current_time;
    c->status = STATUS_COMPLETED;
    stats_record_service_end(stats, c, win->id, duration);
    stats_record_wait(stats, c, c->start_time - c->arrival_time);
    stats_record_satisfaction(stats, c, cfg->max_satisfy_wait);

    win->total_busy_time += duration;
    win->total_served++;
    win->current_client = NULL;

    // ---------- 异常中断检查 ----------
    if (rng_uniform(cfg->rng, 0.0, 1.0) < cfg->error_probability) {
        win->total_errors++;
        stats_record_error(stats, win->id);
        // 窗口进入切换状态（故障），2分钟后恢复
        win->status = WIN_SWITCHING;
        Event switch_evt;
        switch_evt.timestamp = g_current_time + 2.0;
        switch_evt.type = WINDOW_SWITCH;
        switch_evt.data.window_id = win->id;
        event_list_insert(el, switch_evt);
        log_event(g_current_time, "ERROR", "窗口 %d 异常中断，2分钟后恢复", win->id);
        // 发生异常时不叫号，也不执行潮汐关闭检查
    }
    // ---------- 正常情况：潮汐关闭检查与叫号 ----------
    else {
        int tidal_base = cfg->window_count - cfg->tidal_window_count;
        // 潮汐窗口且当前不在潮汐时段 → 直接关闭
        if (win->id >= tidal_base &&
            !(g_current_time >= cfg->tidal_start && g_current_time < cfg->tidal_end)) {
            win->status = WIN_CLOSED;
            // 不调用 try_call_next
        }
        // 非潮汐窗口，或潮汐窗口在时段内 → 正常空闲并叫号
        else {
            win->status = WIN_IDLE;
            try_call_next(cfg, queue, wins, cfg->window_count, el, stats);
        }
    }

    free(c);  // 释放已完成客户

    // Jockeying 检查（使用窗口对应的队列）
    int qid = get_queue_for_window_type(win->type);
    scheduler_check_jockeying(cfg, queue, qid, stats);

    // 潮汐窗口全局调度检查（可能开启/关闭其他窗口）
    scheduler_check_tidal(cfg, queue, wins, el, g_current_time);

    // 关门条件：停止取号且所有队列为空
    if (g_current_time >= cfg->close_stop_time) {
        int all_empty = 1;
        for (int i = 0; i < cfg->window_count; i++) {
            if (queue_get_length(queue, i) > 0) {
                all_empty = 0;
                break;
            }
        }
        if (all_empty) {
            Event shutdown_ev;
            shutdown_ev.timestamp = g_current_time;
            shutdown_ev.type = SHUTDOWN;
            event_list_insert(el, shutdown_ev);
        }
    }
}

// 弹性窗口切换
void handle_window_switch(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats){
    int wid = e->data.window_id;
    if (wid < 0 || wid >= cfg->window_count) return;
    Window *win = &wins[wid];

    // 判断潮汐状态
    int in_tidal = (g_current_time >= cfg->tidal_start && g_current_time < cfg->tidal_end);
    int tidal_base = cfg->window_count - cfg->tidal_window_count;
    int is_tidal_win = (wid >= tidal_base);

    if (is_tidal_win) {
        if (in_tidal) {
            // 潮汐时段内：开启或恢复为个人业务窗口
            window_switch_type(win, WIN_INDIVIDUAL);
        } else {
            // 潮汐时段外：该窗口应关闭，切换事件变为“关闭窗口”
            win->status = WIN_CLOSED;
            return;   // 关闭后不叫号
        }
    } else {
        // 非潮汐窗口（如异常中断恢复）：保持原类型不变
        window_switch_type(win, win->type);   // win->type 仍是切换前的业务类型
    }

    win->status = WIN_IDLE;                   // 切换完成，置为空闲
    try_call_next(cfg, queue, wins, cfg->window_count, el, stats);
}

// 潮汐窗口定时检查
void scheduler_check_tidal(SimConfig *cfg, Queue *queue, Window *wins, EventList *el, double current_time){
    if (cfg->tidal_window_count <= 0) return;

    int total_waiting = 0;
    for (int i = 0; i < cfg->window_count; i++) {
        total_waiting += queue_get_length(queue, i);
    }

    int in_tidal = (current_time >= cfg->tidal_start && current_time < cfg->tidal_end);
    int need_open = in_tidal && (total_waiting > cfg->window_count);

    int tidal_base = cfg->window_count - cfg->tidal_window_count;
    for (int i = tidal_base; i < cfg->window_count; i++) {
        if (need_open) {
            if (wins[i].status == WIN_CLOSED) {
                wins[i].status = WIN_SWITCHING;          // 进入切换状态
                Event ev;
                ev.timestamp = current_time;
                ev.type = WINDOW_SWITCH;
                ev.data.window_id = i;
                event_list_insert(el, ev);
            }
        } else {
            // 潮汐时段外或无排队压力：若窗口空闲则直接关闭
            if (wins[i].status == WIN_IDLE) {
                wins[i].status = WIN_CLOSED;
            }
            // 若正在服务，将在服务结束时再次检查并关闭
        }
    }
}

// Balking判断
void scheduler_check_balking(Event *e, SimConfig *cfg, Queue *queue, StatsCollector *stats){
    Client *c = e->data.client;
    if (!c) return;

    int min_len;
    queue_get_min_length(queue, NULL);  // 仅获取长度，不关心ID
    // 注：queue_get_min_length 需要返回最小长度，我们改用直接计算
    min_len = 9999;
    for (int i = 0; i < cfg->window_count; i++) {
        int len = queue_get_length(queue, i);
        if (len < min_len) min_len = len;
    }

    if (min_len >= cfg->balking_threshold) {
        double r = rng_uniform(cfg->rng, 0.0, 1.0);
        if (r < cfg->balking_probability) {
            c->status = STATUS_BALKED;
            stats_record_balk(stats, c);
            stats_record_arrival(stats, c);
            free(c);
            e->data.client = NULL; // 标记为无效，调用方检查此字段
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

    // 只有仍在“已叫号”状态的客户才处理超时
    if (c->status != STATUS_IN_SERVICE)
        return;

    // 1. 释放窗口
    Window *win = NULL;
    for (int i = 0; i < cfg->window_count; i++) {
        if (wins[i].current_client == c) {
            win = &wins[i];
            win->current_client = NULL;
            win->status = WIN_IDLE;
            break;
        }
    }

    // 2. 客户降级（标准惩罚：权重 -500，不低于1）
    c->priority_weight = (c->priority_weight > 500) ? c->priority_weight - 500 : 1;
    c->status = STATUS_WAITING;       // 退回等待状态
    c->start_time = -1.0;             // 重置服务开始时间

    // 3. 重新入队（根据画像分配队列）
    int qid = get_queue_for_window_type(
        (c->client_type == VIP_CLIENT || c->client_type == ELDERLY) ? WIN_PRIORITY :
        (c->client_type == CORPORATE) ? WIN_CORPORATE : WIN_INDIVIDUAL
    );
    queue_enqueue(queue, qid, c);
    c->queue_id = qid;
    stats_record_queue_length(stats, queue_get_length(queue, qid));

    // 4. 记录过号日志与统计（可选，此处用通用事件打印）
    stats_print_event(g_current_time, "NO_RESP",
                      "客户 %d 过号超时，降权至 %d，重新入队 Q%d",
                      c->id, c->priority_weight, qid);

    // 5. 立即为释放的窗口叫下一位
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


// ==================== 主程序 main()（文件末尾）====================
// P2/P3共同编写

// 模块初始化顺序：config → rng → logger → queue → window → event_list → stats → sim_engine
// 模拟运行：sim_run() 驱动所有事件
// 退出清理：stats_write_report() → log_close() → 各子系统 destroy
int main(int argc, char *argv[]) {
    // 创建全局上下文
    SimConfig cfg;
    StatsCollector stats;       // 统计收集器（栈上分配，手动初始化）
    EventList *el;
    Queue *queue;
    Window *wins;
    RandomEngine rng;           // 随机数引擎实例

    // 初始化各模块
    config_init_default(&cfg);
    rng_init(&rng, 12345);      // 初始化梅森旋转引擎（或从命令行参数获取种子）
    cfg.rng = &rng;              // 将引擎引用挂载到配置，各模块通过cfg->rng访问
    log_init("simulation.log");
    queue = queue_create(cfg.window_count);
    wins = window_create(cfg.window_count, cfg.tidal_window_count, 0.8, 1.5);
    el = event_list_create();

    // 初始化统计收集器（设置实际窗口数，用于数组边界检查）
    memset(&stats, 0, sizeof(StatsCollector));
    stats.win_count = cfg.window_count;

    // 注册事件处理器
    EventHandler handlers[7];
    handlers[ARRIVAL]          = handle_arrival;
    handlers[BULK_ARRIVAL]     = handle_bulk_arrival;
    handlers[START_SERVICE]    = handle_start_service;
    handlers[END_SERVICE]      = handle_end_service;
    handlers[WINDOW_SWITCH]    = handle_window_switch;
    handlers[NO_RESPONSE_TIMEOUT] = handle_no_response_timeout;
    handlers[SHUTDOWN]         = handle_shutdown;

    // 运行仿真
    sim_init(&cfg, el, queue, wins, cfg.window_count);
    sim_run(&cfg, el, queue, wins, cfg.window_count, handlers, &stats);

    // 清理资源
    stats_write_report(&stats, "report.txt", wins, cfg.window_count);
    log_close();
    event_list_destroy(el);
    queue_destroy(queue);
    window_destroy(wins);

    return 0;
}