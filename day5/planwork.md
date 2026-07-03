针对这个高保真银行排队调度仿真系统，4人分工的核心原则是：**“接口先行、模块解耦、难度均衡”**。由于C语言没有面向对象特性，必须通过头文件（`.h`）严格定义数据结构与函数原型，避免后期联调时的“地狱模式”。

以下是推荐的4人分工方案，按照**数据/随机基础 -> 核心引擎 -> 业务逻辑 -> 统计输出**的依赖链进行切分：

### 📋 分工总览表

| 角色 | 模块名称 | 核心职责 | 关键交付物 (函数/结构体) | 难度 |
| :--- | :--- | :--- | :--- | :--- |
| **P1** | **基础设施与随机引擎** | 全局配置、客户/窗口结构体定义、随机数发生器、配置文件解析 | `RandomGen`, `Config`, `Client`, `Window` 结构体及初始化函数 | ⭐⭐⭐ |
| **P2** | **DES核心引擎与队列** | 事件表(最小堆)、优先队列实现、时间推进器、主循环框架 | `EventList`, `PriorityQueue`, `sim_run()`, `schedule_event()` | ⭐⭐⭐⭐⭐ |
| **P3** | **业务逻辑与调度策略** | 到达生成、服务处理、弹性窗口、弃号/换队/异常等复杂规则 | `handle_arrival()`, `handle_service()`, `dynamic_switch()` | ⭐⭐⭐⭐ |
| **P4** | **统计分析与日志持久化** | 实时指标计算、快照输出、日志文件写入、最终报告生成 | `StatsCollector`, `log_event()`, `print_snapshot()`, `write_report()` | ⭐⭐⭐ |

---

### 👤 P1：基础设施与随机引擎 (Foundation & RNG)

**定位**：项目的“地基”，所有其他模块都依赖你的头文件。

-   **具体任务**：
    1.  **全局结构体定义**：在 `types.h` 中定义 `Client`（含画像、业务类型、到达时间、优先级、状态）、`Window`（含熟练度k、类型、状态）、`SimConfig`（时段λ表、概率矩阵）。
    2.  **随机数引擎封装** (`rng.c/h`)：
        -   实现线性同余或梅森旋转算法作为底层均匀分布。
        -   封装 `rand_exponential(lambda)` 用于到达间隔。
        -   封装 `rand_lognormal(mu, sigma)` 用于业务时长（Box-Muller变换）。
        -   支持 `set_seed()` 保证可复现性。
    3.  **配置加载** (`config.c/h`)：解析宏定义或简易文本配置文件，填充 `SimConfig` 结构体（分时段λ、画像概率、业务耗时参数）。
    4.  **工具函数**：时间格式化（分钟转"HH:MM"字符串）、安全内存分配封装。

-   **验收标准**：随机数分布通过卡方检验；结构体字段完整覆盖需求文档；其他三人能直接 `#include` 并使用。

### 👤 P2：DES核心引擎与队列 (Engine & Data Structures)

**定位**：项目的“心脏”，最考验数据结构功底，是技术风险最高的模块。

-   **具体任务**：
    1.  **事件表实现** (`event_list.c/h`)：
        -   使用**最小堆**（数组实现）存储事件 `{timestamp, type, payload_ptr}`。
        -   提供 `insert_event()`, `pop_next_event()`, `cancel_event()` (用于过号/异常中断)。
    2.  **多级优先队列** (`queue.c/h`)：
        -   实现支持自定义比较函数的优先队列（排序权重 = FIFO序 + 画像权重 + 预约标识）。
        -   提供 `enqueue()`, `dequeue()`, `remove_by_id()` (用于Jockeying换队), `get_length()`。
    3.  **仿真主循环** (`sim_engine.c/h`)：
        -   `sim_init()`: 初始化事件表、队列、窗口状态。
        -   `sim_run()`: while循环 pop事件 -> 更新时间 -> 分发到P3的处理函数 -> 检查终止条件(20:00强制关门)。
    4.  **事件类型枚举**：定义 `ARRIVAL`, `START_SERVICE`, `END_SERVICE`, `WINDOW_SWITCH`, `BULK_ARRIVAL`, `SHUTDOWN`。

-   **验收标准**：事件表插入/弹出 O(log n)；优先队列支持动态删除；主循环能正确驱动空跑（无业务逻辑时不崩溃）。

### 👤 P3：业务逻辑与调度策略 (Business Logic & Scheduling)

**定位**：项目的“大脑”，将需求文档中的复杂规则转化为代码，依赖P1的配置和P2的引擎。

-   **具体任务**：
    1.  **到达处理** (`arrival.c/h`)：
        -   根据当前时间查表获取 λ(t)，调用P1的指数分布生成下一到达事件，通过回调参数 `EventList *el` 插入事件。
        -   按概率赋予客户画像，生成业务类型。
        -   实现**批量突发注入**：在随机时间点一次性插入一批客户事件（人数随机）。
        -   实现**Balking**：检查最短队列长度，超阈值则按概率丢弃并通知P4记录流失。
        -   每次入队后调用 `stats_record_queue_length()` 更新排队长度统计。
    2.  **服务处理** (`service.c/h`)：
        -   `start_service()`: 从队列取客户，计算实际时长(基准×k)，通过 `el` 插入 END_SERVICE 事件。
        -   `end_service()`: 释放窗口，触发下一位叫号；按2%概率触发异常中断，通过 `el` 插入 NO_RESPONSE_TIMEOUT 事件；每次出队后调用 `stats_record_queue_length()` 更新排队长度统计。
    3.  **动态调度** (`scheduler.c/h`)：
        -   **弹性窗口**：定期检查队列长度，满足条件时切换窗口类型并插入 WINDOW_SWITCH 事件。
        -   **Jockeying**：在服务开始/结束时检查相邻队列，触发换队操作（调用P2的remove+enqueue）。
        -   **过号重排**：超时未响应则降级重新入队。
        -   **营业时间控制**：17:00后停止生成ARRIVAL事件。

-   **验收标准**：VIP确实优先；高峰时段队列明显增长；弹性窗口能自动切换；异常中断后能恢复服务。

### 👤 P4：统计分析与日志持久化 (Statistics & Logging)

**定位**：项目的“脸面”和验收依据，确保数据正确性和日志规范性。

-   **具体任务**：
    1.  **统计收集器** (`stats.c/h`)：
        -   维护全局计数器：总到达、总完成、总流失、各画像等待时长累加、各窗口服务量/空闲时间/异常次数。
        -   提供 `record_arrival()`, `record_service_start()`, `record_service_end()`, `record_balk()` 等钩子函数供P3调用。
    2.  **实时控制台输出**：
        -   关键事件带时间戳打印（格式化对齐）。
    3.  **日志文件写入** (`logger.c/h`)：
        -   `log_event()`: 追加写入过程日志到 `simulation_log.txt`。
        -   `write_final_report()`: 仿真结束后计算平均值、满意度、均衡度偏差，格式化输出总结报告。
    4.  **数据校验**：编写简单的断言/检查函数，验证统计值非负、窗口服务量偏差<15%。

-   **验收标准**：日志文件格式清晰可读；统计数据与手动推算一致；控制台输出不刷屏且信息完整。

---

### 🔗 协作接口约定（开工前必须敲定！）

为避免联调灾难，**第一天**四人需共同确定以下接口（写入共享头文件）：

```c
// ================================================================
// types.h — P1主导，全员确认，所有模块共享
// ================================================================

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

// ---------- 结构体 ----------

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
        Client *client;         // ARRIVAL / BULK_ARRIVAL / END_SERVICE 等
        int window_id;          // WINDOW_SWITCH 时使用
    } data;
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


// ================================================================
// rng.h — P1实现，P2/P3调用
// ================================================================

void   rng_set_seed(unsigned int seed);
double rng_uniform(double min, double max);                    // [min, max) 均匀分布
double rng_exponential(double lambda);                         // 指数分布，用于到达间隔
double rng_lognormal(double mu, double sigma);                 // 对数正态分布，用于业务时长
int    rng_random_client_type(const SimConfig *cfg);           // 按概率随机返回 ClientType
int    rng_random_business(const SimConfig *cfg, int ctype);  // 按画像概率随机返回 BusinessType


// ================================================================
// config.h — P1实现，P3调用
// ================================================================

void config_init_default(SimConfig *cfg);                      // 填充默认配置
double config_get_lambda(const SimConfig *cfg, int time_min);  // 查表获取当前时段 λ(t)
void config_print(const SimConfig *cfg);                       // 打印当前配置（调试用）


// ================================================================
// utils.h — P1实现，全员调用
// ================================================================

char*  time_format(double minutes, char *buf, int buf_size);   // 分钟 → "HH:MM"
void*  safe_malloc(size_t size);                               // malloc 封装，失败则退出
void*  safe_calloc(size_t nmemb, size_t size);                 // calloc 封装


// ================================================================
// event_list.h — P2实现，P3/P4调用
// ================================================================

typedef struct EventList EventList;                            // 不透明类型

EventList* event_list_create(void);
void       event_list_destroy(EventList *el);
int        event_list_insert(EventList *el, Event event);      // O(log n)，返回0成功
int        event_list_pop(EventList *el, Event *out);          // 弹出最小事件，返回0成功，1空
int        event_list_cancel(EventList *el, double ts,
                             EventType type, void *ptr);       // 按条件取消事件
int        event_list_empty(const EventList *el);              // 1=空，0=非空
int        event_list_size(const EventList *el);               // 当前事件数


// ================================================================
// queue.h — P2实现，P3调用
// ================================================================

typedef struct Queue Queue;                                    // 不透明类型

Queue*   queue_create(int max_queues);
void     queue_destroy(Queue *q);
int      queue_enqueue(Queue *q, int queue_id, Client *c);    // 返回0成功，-1队列满
Client*  queue_dequeue(Queue *q, int queue_id);               // 返回NULL则队列空
Client*  queue_remove_by_id(Queue *q, int queue_id,
                            int client_id);                   // Jockeying: 移除指定客户
int      queue_get_length(const Queue *q, int queue_id);      // 获取指定队列长度
int      queue_get_min_length(const Queue *q, int *out_id);   // 返回最短队列长度，out_id写入其编号
Client*  queue_find_no_response(Queue *q, double current_time,
                               double timeout);               // 过号检测：返回超时未响应的客户


// ================================================================
// window.h — P2实现，P3调用
// ================================================================

Window*  window_create(int count, int tidal_count,
                       double k_min, double k_max);           // 创建窗口数组
void     window_destroy(Window *wins);
int      window_switch_type(Window *w, WindowType new_type);   // 切换窗口类型，返回0成功
int      window_get_idle_count(const Window *wins, int count); // 空闲窗口数
Window*  window_find_idle(Window *wins, int count,
                          WindowType type);                    // 按类型查找空闲窗口


// ================================================================
// sim_engine.h — P2实现，驱动P3的回调
// ================================================================

// 事件分发回调函数类型（EventList 传入，使 P3 能插入新事件）
typedef void (*EventHandler)(Event *e, SimConfig *cfg,
                             Queue *queue, Window *wins,
                             EventList *el);

void sim_init(SimConfig *cfg, EventList *el,
              Queue *queue, Window *wins);                     // 初始化所有子系统
void sim_run(SimConfig *cfg, EventList *el,
             Queue *queue, Window *wins,
             EventHandler handlers[]);                         // 主循环
double sim_get_current_time(void);                             // 获取当前仿真时间


// ================================================================
// arrival.h — P3实现，被 sim_engine 回调
// ================================================================

void handle_arrival(Event *e, SimConfig *cfg,
                    Queue *queue, Window *wins,
                    EventList *el);                            // 单个客户到达
void handle_bulk_arrival(Event *e, SimConfig *cfg,
                         Queue *queue, Window *wins,
                         EventList *el);                       // 批量突发到达
void schedule_next_arrival(SimConfig *cfg, EventList *el,
                           double current_time);               // 安排下一次到达事件


// ================================================================
// service.h — P3实现，被 sim_engine 回调
// ================================================================

void handle_start_service(Event *e, SimConfig *cfg,
                          Queue *queue, Window *wins,
                          EventList *el);                      // 开始办理：取号→计算时长→插入END
void handle_end_service(Event *e, SimConfig *cfg,
                        Queue *queue, Window *wins,
                        EventList *el);                        // 结束办理：释放窗口→叫号→检查异常


// ================================================================
// scheduler.h — P3实现，被 sim_engine 回调
// ================================================================

void handle_window_switch(Event *e, SimConfig *cfg,
                          Queue *queue, Window *wins,
                          EventList *el);                      // 弹性窗口切换
void scheduler_check_tidal(SimConfig *cfg, Window *wins,
                           double current_time);               // 潮汐窗口定时检查
void scheduler_check_balking(Event *e, SimConfig *cfg,
                             Queue *queue);                    // Balking判断
void scheduler_check_jockeying(Queue *queue,
                               int queue_id);                  // Jockeying换队检查
void scheduler_check_no_response(Queue *queue, EventList *el,
                                 double current_time);         // 过号重排检查
void handle_no_response_timeout(Event *e, SimConfig *cfg,
                                Queue *queue, Window *wins,
                                EventList *el);                // 过号超时处理：降级重入队
void handle_shutdown(Event *e, SimConfig *cfg,
                     Queue *queue, Window *wins,
                     EventList *el);                           // 营业结束：记录未办结客户并释放


// ================================================================
// stats.h — P4实现，P3在各关键点调用
// ================================================================

void stats_init(void);
void stats_record_arrival(Client *c);                          // 记录客户到达
void stats_record_service_start(Client *c, int window_id);     // 记录开始服务
void stats_record_service_end(Client *c, int window_id,
                              double duration);                // 记录结束服务
void stats_record_wait(Client *c, double wait_time);           // 记录等待时长
void stats_record_balk(Client *c);                             // 记录弃号
void stats_record_jockey(Client *c, int from_q, int to_q);    // 记录换队
void stats_record_error(int window_id);                        // 记录异常中断
void stats_record_queue_length(int length);                    // 更新当前排队长度（每次 enqueue/dequeue 后调用）
double stats_get_max_queue_length(void);                       // 获取最大排队长度
void stats_record_satisfaction(Client *c, double max_possible_wait); // 记录满意度估算
void stats_write_report(const char *filename,
                        const Window *wins, int win_count);    // 写入最终报告


// ================================================================
// logger.h — P4实现，P3/P2调用
// ================================================================

void log_init(const char *filename);                           // 打开日志文件
void log_close(void);                                          // 关闭日志文件
void log_event(double time, const char *category,
               const char *fmt, ...);                          // 追加一条事件日志
void log_printf(const char *fmt, ...);                         // 控制台+日志双输出


// ================================================================
// 主程序 main.c — P2/P3共同编写
// ================================================================

// 模块初始化顺序：config → rng → stats → logger → queue → window → event_list → sim_engine
// 模拟运行：sim_run() 驱动所有事件
// 退出清理：stats_write_report() → log_close() → 各子系统 destroy
int main(int argc, char *argv[]);

// EventHandler 注册表（传给 sim_run）
// 每个 handler 签名统一为：(Event*, SimConfig*, Queue*, Window*, EventList*)
// handlers[ARRIVAL]         = handle_arrival
// handlers[BULK_ARRIVAL]    = handle_bulk_arrival
// handlers[START_SERVICE]   = handle_start_service
// handlers[END_SERVICE]     = handle_end_service
// handlers[WINDOW_SWITCH]   = handle_window_switch
// handlers[NO_RESPONSE_TIMEOUT] = handle_no_response_timeout
// handlers[SHUTDOWN]        = handle_shutdown
```

### ⚠️ 风险提示与应对

1.  **P2是瓶颈**：DES引擎和优先队列最难，建议P2由数据结构最强的同学担任。若进度滞后，P1在完成基础模块后应优先支援P2。
2.  **指针管理**：C语言手动管理内存，`Event.payload` 指向的 `Client` 何时malloc/free需明确约定。**建议**：Client在到达时malloc，在服务完成/流失/关门时free；事件表只存指针不拷贝。
3.  **调试策略**：不要等四个模块全写完再联调。
    -   P1+P2跑通“空事件循环”；P4写好日志框架。
    -   P3加入基础FIFO到达/服务，P4验证计数。
    -   叠加优先级、弹性窗口、异常等高级特性。
    -   压力测试、边界测试、日志美化。
4.  **Git规范**：每人一个分支（`feat/rng`, `feat/engine`, `feat/logic`, `feat/stats`），通过PR合并到`main`，禁止直接push main。

此分工确保了每个模块内聚、模块间耦合仅通过头文件接口，且四人的工作量与难度相对均衡，符合综合实验的教学目标与工程实践要求。