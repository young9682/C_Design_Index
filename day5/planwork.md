针对这个高保真银行排队调度仿真系统，4人分工的核心原则是：**”单文件协作、结构体封装、注释分区”**。所有代码写在同一个 `bank_simulation.cpp` 文件中，通过注释标记各人负责的区域，编译命令：`g++ -o bank_sim bank_simulation.cpp -lm`。代码风格保留C语言风格（结构体、函数、指针），使用.cpp扩展名仅用于编译便利。

## 架构设计：单文件结构体封装

采用**单一源文件**架构，所有类型定义、全局状态、模块函数均在 `bank_simulation.cpp` 中实现。模块间通过**结构体指针传递**进行通讯，每个模块用独立的结构体封装其内部状态，对外仅暴露操作函数。**不使用头文件**，所有声明和实现均在同一文件中，通过注释分区管理。

### 模块划分（结构体封装）

```
┌─────────────────────────────────────────────────────────────┐
│                   bank_simulation.cpp                       │
├─────────────────────────────────────────────────────────────┤
│  // ============ 类型定义区 (P1) ============               │
│  // 枚举、结构体定义                                        │
├─────────────────────────────────────────────────────────────┤
│  // ============ P1: 随机引擎与配置 ============            │
│  RandomEngine 结构体                                        │
│  SimConfig 结构体                                           │
│  rng_*() / config_*() 函数                                 │
├─────────────────────────────────────────────────────────────┤
│  // ============ P2: DES核心引擎 ============               │
│  EventList 结构体                                           │
│  Queue 结构体                                               │
│  Window 结构体                                              │
│  sim_run() 主循环                                           │
├─────────────────────────────────────────────────────────────┤
│  // ============ P3: 业务逻辑 ============                  │
│  handle_arrival() / handle_service() / scheduler_*()        │
├─────────────────────────────────────────────────────────────┤
│  // ============ P4: 统计与日志 ============                │
│  StatsCollector 结构体                                      │
│  Logger 结构体                                              │
│  stats_*() / log_*() 函数                                  │
├─────────────────────────────────────────────────────────────┤
│  // ============ 主程序 main() ============                 │
│  初始化各结构体 → 驱动仿真 → 输出报告                       │
└─────────────────────────────────────────────────────────────┘
```

### 模块间通讯机制

1. **结构体指针传递**：主程序创建各模块结构体实例，通过函数参数传递给各模块操作函数
2. **配置挂载引用**：`SimConfig` 内持有 `RandomEngine*` 指针，各模块通过 `cfg->rng` 访问随机引擎，避免全局变量
3. **回调函数**：事件处理器通过函数指针数组注册，解耦引擎与业务逻辑
4. **单文件内前向声明**：函数在文件顶部集中声明，实现放在对应分区，无需头文件

---

以下是推荐的4人分工方案，按照**数据/随机基础 -> 核心引擎 -> 业务逻辑 -> 统计输出**的依赖链进行切分：

### 📋 分工总览表

| 角色 | 模块名称 | 核心职责 | 关键交付物 (结构体/函数) | 难度 |
| :--- | :--- | :--- | :--- | :--- |
| **P1** | **基础设施与随机引擎** | 类型定义区所有结构体定义、随机数引擎实现、配置与工具函数 | `RandomEngine`, `SimConfig`, `Client`, `Window`, `Event` 结构体定义; `rng_*()`, `config_*()` | ⭐⭐⭐ |
| **P2** | **DES核心引擎与队列** | 事件表/队列结构体实现、窗口操作函数、仿真主循环 | `EventList`, `Queue` 结构体实现; `event_list_*()`, `queue_*()`, `window_*()`, `sim_run()` | ⭐⭐⭐⭐⭐ |
| **P3** | **业务逻辑与调度策略** | 到达/服务/调度函数，窗口-队列映射与叫号辅助，通过结构体指针访问全局状态 | `get_queue_for_window_type()`, `try_call_next()`, `start_service_for_client()`, `handle_arrival()`, `handle_end_service()`, `scheduler_*()` | ⭐⭐⭐⭐ |
| **P4** | **统计分析与日志持久化** | 统计收集器操作函数、日志结构体实现、报告生成 | `StatsCollector` 操作函数; `Logger` 结构体; `stats_*()`, `log_*()` | ⭐⭐⭐ |

---

### 👤 P1：基础设施与随机引擎 (Foundation & RNG)

**定位**：项目的”地基”，在 `bank_simulation.cpp` 文件顶部定义所有核心数据结构，并实现随机数引擎。

-   **具体任务**：
    1.  **核心结构体定义**（在文件顶部”类型定义区”）：
        -   `Client`：客户节点（id、画像、业务类型、状态、到达/开始/结束时间、优先级权重、预约标识、队列编号）
        -   `Window`：窗口节点（id、类型、状态、熟练度系数k、当前客户指针、统计计数器）
        -   `Event`：事件节点（时间戳、类型、联合体数据）
        -   `SimConfig`：仿真配置（时段λ表、画像概率、业务耗时参数、窗口配置、调度参数、营业时间）
    2.  **随机数引擎结构体** `RandomEngine`：
        -   内部状态：种子seed、状态数组（梅森旋转）
        -   底层函数：`rng_init()`、`rng_uniform()`、`rng_exponential()`、`rng_lognormal()`
        -   业务函数：`rng_random_client_type()`、`rng_random_business()`
    3.  **配置函数**：
        -   `config_init_default()`：填充默认配置到 `SimConfig` 结构体
        -   `config_get_lambda()`：查表获取当前时段 λ(t)
    4.  **工具函数**：`time_format()`（分钟转”HH:MM”）、`safe_malloc()`、`safe_calloc()`

-   **验收标准**：随机数分布通过卡方检验；结构体字段完整覆盖需求文档；其他三人能直接调用函数并使用结构体。

### 👤 P2：DES核心引擎与队列 (Engine & Data Structures)

**定位**：项目的”心脏”，实现事件驱动仿真引擎和数据结构。

-   **具体任务**：
    1.  **事件表结构体** `EventList`：
        -   内部封装最小堆（动态数组实现）
        -   操作函数：`event_list_create()`、`event_list_insert()`、`event_list_pop()`、`event_list_cancel_by_client()`、`event_list_cancel_by_type()`、`event_list_destroy()`
    2.  **队列结构体** `Queue`：
        -   内部封装多级优先队列（按 FIFO序 + 画像权重 + 预约标识 排序）
        -   操作函数：`queue_create()`、`queue_enqueue()`、`queue_dequeue()`、`queue_remove_by_id()`、`queue_get_length()`、`queue_get_min_length()`、`queue_find_no_response()`
    3.  **窗口管理结构体**：
        -   窗口数组由 `Window` 结构体直接表示（P1已定义）
        -   操作函数：`window_create()`、`window_destroy()`、`window_switch_type()`、`window_get_idle_count()`、`window_find_idle()`
    4.  **仿真主循环函数**：
        -   `sim_init()`：初始化事件表、队列、窗口状态（通过结构体指针操作）
        -   `sim_run()`：while循环 pop事件 -> 更新时间 -> 通过回调函数指针分发到P3处理函数（传递所有结构体指针包括 StatsCollector）-> 检查终止条件
    5.  **事件类型枚举**：`ARRIVAL`, `BULK_ARRIVAL`, `START_SERVICE`, `END_SERVICE`, `WINDOW_SWITCH`, `NO_RESPONSE_TIMEOUT`, `SHUTDOWN`

-   **验收标准**：事件表插入/弹出 O(log n)；优先队列支持动态删除；主循环能正确驱动空跑（无业务逻辑时不崩溃）。

### 👤 P3：业务逻辑与调度策略 (Business Logic & Scheduling)

**定位**：项目的”大脑”，将需求文档中的复杂规则转化为函数实现，通过参数接收所有结构体指针。

-   **具体任务**：
    1.  **窗口-队列映射与叫号辅助**：
        -   `get_queue_for_window_type(WindowType wtype)`：根据窗口类型返回对应队列ID（WIN_PRIORITY→1, WIN_CORPORATE→2, 其他→0）
        -   `try_call_next(cfg, queue, wins, win_count, el, stats)`：遍历所有空闲窗口，从对应队列取客户并开始服务
        -   `start_service_for_client(c, win, cfg, el, stats)`：为指定客户启动服务，计算时长(基准×k)，插入 END_SERVICE 事件
    2.  **到达处理函数**：
        -   `handle_arrival()`：接收 `(Event *e, SimConfig *cfg, Queue *queue, Window *wins, EventList *el, StatsCollector *stats)`
        -   根据当前时间查表获取 λ(t)，调用 `rng_exponential(cfg->rng, lambda)` 生成下一到达事件，插入 `EventList`
        -   按概率赋予客户画像，生成业务类型
        -   实现**批量突发注入** `handle_bulk_arrival()`：在随机时间点一次性插入一批客户事件
        -   实现**Balking**：检查最短队列长度，超阈值则按概率丢弃并调用 `stats_record_balk(stats, c)`
    3.  **服务处理函数**：
        -   `handle_start_service()`：验证客户状态，调用 `start_service_for_client()` 执行服务
        -   `handle_end_service()`：释放窗口，按概率触发异常中断（2分钟后 WINDOW_SWITCH 恢复），触发叫号；关门后检查是否所有队列清空
        -   每次入队/出队后调用 `stats_record_queue_length(stats, length)` 更新排队长度统计
    4.  **调度器函数**：
        -   `scheduler_check_tidal(cfg, queue, wins, el, current_time)`：定期检查队列总长度，满足条件时切换潮汐窗口开关状态
        -   `scheduler_check_jockeying(cfg, queue, queue_id, stats)`：在服务开始/结束时检查相邻队列，触发换队操作
        -   `handle_no_response_timeout()`：过号超时处理函数（客户降权后重新入队）
        -   `handle_shutdown()`：营业结束处理函数（强制结束服务、清空队列、记录未办结客户）

-   **验收标准**：VIP确实优先；高峰时段队列明显增长；弹性窗口能自动切换；异常中断后能恢复服务。

### 👤 P4：统计分析与日志持久化 (Statistics & Logging)

**定位**：项目的”脸面”和验收依据，确保数据正确性和日志规范性。

-   **具体任务**：
    1.  **统计收集器结构体** `StatsCollector`（已在类型定义区定义）：
        -   内部封装：总到达、总完成、总流失、各画像等待时长累加、各窗口服务量/空闲时间/异常次数、最大排队长度、满意度累计等
        -   操作函数（均接收 `StatsCollector *stats` 作为第一个参数）：
            -   `stats_record_arrival(stats, c)`：记录客户到达
            -   `stats_record_service_start(stats, c, window_id)`：记录开始服务
            -   `stats_record_service_end(stats, c, window_id, duration)`：记录结束服务
            -   `stats_record_wait(stats, c, wait_time)`：记录等待时长
            -   `stats_record_balk(stats, c)`：记录弃号
            -   `stats_record_jockey(stats, c, from_q, to_q)`：记录换队
            -   `stats_record_error(stats, window_id)`：记录异常中断
            -   `stats_record_queue_length(stats, length)`：更新排队长度
    2.  **实时控制台输出**：
        -   `stats_print_event()`：关键事件带时间戳打印（格式化对齐）
    3.  **日志结构体** `Logger`：
        -   内部封装：文件指针、缓冲区
        -   操作函数：`log_init()`、`log_close()`、`log_event()`（追加写入过程日志）、`log_printf()`（控制台+日志双输出）
    4.  **报告生成函数**：
        -   `stats_write_report(stats, filename, wins, win_count)`：仿真结束后计算平均值、满意度、均衡度偏差，格式化输出总结报告到文件

-   **验收标准**：日志文件格式清晰可读；统计数据与手动推算一致；控制台输出不刷屏且信息完整。

---

### 🔗 单文件协作接口约定（开工前必须敲定！）

为避免联调灾难，**第一天**四人需共同确定以下接口。所有类型定义和函数声明均写在 `bank_simulation.cpp` 文件顶部，**不使用头文件**。

```cpp
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

void sim_init(SimConfig *cfg, EventList *el, Queue *queue, Window *wins, int win_count);
void sim_run(SimConfig *cfg, EventList *el, Queue *queue, Window *wins,
             int win_count, EventHandler handlers[], StatsCollector *stats);
double sim_get_current_time(void);  // 返回仿真当前时间（由sim_run内部维护的静态变量）

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
```

### ⚠️ 风险提示与应对

1.  **P2是瓶颈**：DES引擎和优先队列最难，建议P2由数据结构最强的同学担任。若进度滞后，P1在完成基础模块后应优先支援P2。
2.  **指针管理**：C风格代码手动管理内存，`Event.data.client` 指向的 `Client` 何时malloc/free需明确约定。**建议**：Client在到达时malloc，在服务完成/流失/关门时free；事件表只存指针不拷贝。
3.  **单文件协作**：所有代码在 `bank_simulation.cpp` 中，通过注释分区（`// ============ P1 ============` 等）标记各人负责区域。合并时使用Git分支，避免冲突。
4.  **调试策略**：不要等四个模块全写完再联调。
    -   P1+P2跑通”空事件循环”；P4写好日志框架。
    -   P3加入基础FIFO到达/服务，P4验证计数。
    -   叠加优先级、弹性窗口、异常等高级特性。
    -   压力测试、边界测试、日志美化。
5.  **Git规范**：每人一个分支（`feat/rng`, `feat/engine`, `feat/logic`, `feat/stats`），通过PR合并到`main`，禁止直接push main。合并后确保编译通过。

此分工确保了每个模块内聚、模块间耦合仅通过函数调用接口，且四人的工作量与难度相对均衡，符合综合实验的教学目标与工程实践要求。