#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<windows.h>

// 状态
typedef enum{
    STATE_AVAILABLE,
    STATE_RENTED,
    STATE_CHARGING
}State;

// 充电宝
typedef struct{
    int id;
    State state;
    float battery;
    time_t rental_start;
}PowerBank;

// 充电柜
typedef struct{
    PowerBank *banks;
    int total;
    float price;
}Cabinet;

// 清空输入缓冲区
void clear_input_buffer(){
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// 初始化充电柜
void init_cabinet(Cabinet *cab){
    printf("请输入充电宝槽位数量 N: ");
    while(scanf("%d", &cab->total) != 1 || cab->total <= 0){
        printf("输入无效，请输入正整数: ");
        clear_input_buffer();
    }
    printf("请输入费率（元/半小时）: ");
    while(scanf("%f", &cab->price) != 1 || cab->price <= 0){
        printf("输入无效，请输入正数: ");
        clear_input_buffer();
    }
    cab->banks = (PowerBank *)malloc(sizeof(PowerBank) * cab->total);
    for(int i = 0; i < cab->total; i++){
        cab->banks[i].id = i + 1;
        cab->banks[i].state = STATE_AVAILABLE;
        cab->banks[i].battery = 100.0f;
        cab->banks[i].rental_start = 0;
    }
    printf("初始化完成：%d 个充电宝，费率 %.1f 元/半小时\n\n", cab->total, cab->price);
}

// 显示状态
void display_status(Cabinet *cab){
    printf("\n===== 充电宝状态 =====\n");
    printf("编号 | 状态     | 电量\n");
    printf("--------------------\n");
    for(int i = 0; i < cab->total; i++){
        PowerBank *pb = &cab->banks[i];
        const char *state_str;
        char battery_str[8];
        switch(pb->state){
            case STATE_AVAILABLE:
                state_str = "空闲";
                sprintf(battery_str, "%.0f%%", pb->battery);
                break;
            case STATE_RENTED:
                state_str = "租借中";
                sprintf(battery_str, "--");
                break;
            case STATE_CHARGING:
                state_str = "充电中";
                sprintf(battery_str, "%.0f%%", pb->battery);
                break;
        }
        printf(" %2d  | %-8s | %s\n", pb->id, state_str, battery_str);
    }
    printf("======================\n");
}

// 更新充电状态
void update_charging(Cabinet *cab){
    for(int i = 0; i < cab->total; i++){
        PowerBank *pb = &cab->banks[i];
        if(pb->state == STATE_AVAILABLE || pb->state == STATE_CHARGING){
            if(pb->battery < 80.0f){
                pb->battery += 0.25f;
            }
            else{
                pb->battery += 0.20f;
            }
            if(pb->battery >= 100.0f){
                pb->battery = 100.0f;
                if(pb->state == STATE_CHARGING){
                    pb->state = STATE_AVAILABLE;
                    printf("充电宝%d充电完成，已恢复空闲状态\n", pb->id);
                }
            }
        }
    }
}

// 计算费用
float calculate_fee(Cabinet *cab, time_t start, time_t end){
    double diff = difftime(end, start);
    int cycles = (int)(diff / 1800);
    if(diff > cycles * 1800){
        cycles++;
    }
    return cycles * cab->price;
}

// 租借充电宝
int rent_power_bank(Cabinet *cab){
    int available_count = 0;
    for(int i = 0; i < cab->total; i++){
        if(cab->banks[i].state == STATE_AVAILABLE){
            available_count++;
        }
    }
    if(available_count == 0){
        printf("暂无可用电宝\n");
        return -1;
    }
    int target = rand() % available_count;
    int count = 0;
    for(int i = 0; i < cab->total; i++){
        if(cab->banks[i].state == STATE_AVAILABLE){
            if(count == target){
                cab->banks[i].state = STATE_RENTED;
                cab->banks[i].rental_start = time(NULL);
                printf("租借成功，充电宝编号%d\n", cab->banks[i].id);
                return cab->banks[i].id;
            }
            count++;
        }
    }
    return -1;
}
// 归还充电宝
int return_power_bank(Cabinet *cab){
    int id;
    printf("请输入要归还的充电宝编号: ");
    while(scanf("%d", &id) != 1){
        printf("输入无效，请输入数字: ");
        clear_input_buffer();
    }
    if(id < 1 || id > cab->total){
        printf("编号无效，有效范围是 1-%d\n", cab->total);
        return -1;
    }
    PowerBank *pb = &cab->banks[id - 1];
    if(pb->state != STATE_RENTED){
        printf("该充电宝未被租借\n");
        return -1;
    }
    time_t now = time(NULL);
    float fee = calculate_fee(cab, pb->rental_start, now);
    double duration = difftime(now, pb->rental_start);
    float return_battery = 20.0f + (rand() % 71);
    if(return_battery >= 50.0f){
        pb->state = STATE_AVAILABLE;
        pb->battery = return_battery;
        printf("充电宝%d已归还，租借%.0f秒，费用%.1f元，电量充足，已恢复空闲\n",
               pb->id, duration, fee);
    }
    else{
        pb->state = STATE_CHARGING;
        pb->battery = return_battery;
        printf("充电宝%d已归还，租借%.0f秒，费用%.1f元，电量不足，正在充电中\n",
               pb->id, duration, fee);
    }
    return 0;
}

// 主函数
int main(){
    srand((unsigned int)time(NULL));
    Cabinet cab;
    init_cabinet(&cab);
    int running = 1;
    while(running){
        printf("\n欢迎进入共享充电宝租借柜系统！\n");
        printf("1. 租借充电宝\n");
        printf("2. 归还充电宝\n");
        printf("3. 查看实时状态\n");
        printf("0. 退出系统\n");
        printf("请选择操作: ");
        int choice;
        while(scanf("%d", &choice) != 1){
            printf("输入无效，请输入数字: ");
            clear_input_buffer();
        }
        switch(choice){
            case 1:
                rent_power_bank(&cab);
                break;
            case 2:
                return_power_bank(&cab);
                break;
            case 3:
                display_status(&cab);
                break;
            case 0:
                running = 0;
                printf("感谢使用，再见！\n");
                break;
            default:
                printf("无效选项，请重新选择\n");
                break;
        }
        if(running){
            Sleep(1000);
            update_charging(&cab);
        }
    }
    free(cab.banks);
    return 0;
}
