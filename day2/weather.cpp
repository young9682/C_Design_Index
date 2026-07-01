#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_DAYS 100
#define DATE_LEN 11
#define MONTH_FILE "WeatherMonth.txt"
#define DAY_FILE "WeatherDay.txt"

struct WeatherRecord{
    char date[11];
    float max_temp;
    float min_temp;
    char weatherType[20];
    float precipitation;
};

struct WeatherRecord_m{
    char ym[8];
    float ex_maxtemp;
    float ex_mintemp;
    float ave_maxtemp;
    float ave_mintemp;
    float sum_precipitation;
};

WeatherRecord days[MAX_DAYS];
int dayCount = 0;
WeatherRecord_m currentMonthStats; 

// 从WeatherDay.txt加载每日数据
int loadDailyData(){
    FILE *fp = fopen(DAY_FILE, "r");
    if(!fp){
        return -1;
    }
    dayCount = 0;
    char line[256];
    while(dayCount < MAX_DAYS && fgets(line, sizeof(line), fp)){
        if(sscanf(line, "%10s %f %f %19s %f",
                  days[dayCount].date,
                  &days[dayCount].max_temp,
                  &days[dayCount].min_temp,
                  days[dayCount].weatherType,
                  &days[dayCount].precipitation) == 5){
            dayCount++;
        }
    }
    fclose(fp);
    return dayCount;
}

// 计算指定年月的统计数据并存入currentMonthStats
void calcMonthlyStats(const char* ym){
    strncpy(currentMonthStats.ym, ym, 7);
    currentMonthStats.ex_maxtemp = -999.0f;
    currentMonthStats.ex_mintemp = 999.0f;
    currentMonthStats.sum_precipitation = 0.0f;
    float sumMax = 0.0f, sumMin = 0.0f;
    int validDays = 0;
    for (int i = 0; i < dayCount; i++){
        if (strncmp(days[i].date, ym, 7) == 0){
            if (days[i].max_temp > currentMonthStats.ex_maxtemp)
                currentMonthStats.ex_maxtemp = days[i].max_temp;
            if (days[i].min_temp < currentMonthStats.ex_mintemp)
                currentMonthStats.ex_mintemp = days[i].min_temp;
            sumMax += days[i].max_temp;
            sumMin += days[i].min_temp;
            currentMonthStats.sum_precipitation += days[i].precipitation;
            validDays++;
        }
    }
    if (validDays > 0){
        currentMonthStats.ave_maxtemp = sumMax / validDays;
        currentMonthStats.ave_mintemp = sumMin / validDays;
    }
    else{
        currentMonthStats.ave_maxtemp = 0.0f;
        currentMonthStats.ave_mintemp = 0.0f;
    }
}

// 极端最高气温
void HighestTemperature(){
    printf("\n[极端最高气温]:%.1f℃，日期为:\n", currentMonthStats.ex_maxtemp);
    for(int i = 0; i < dayCount; i++){
        if(fabs(days[i].max_temp - currentMonthStats.ex_maxtemp) < 0.01f){
            printf("  %s\n", days[i].date);
        }
    }
}

// 高温天气统计
void HighTempDays(float temp){
    int count = 0;
    for(int i = 0; i < dayCount; i++){
        if(days[i].max_temp >= temp){
            count++;
        }
    }
    printf("超过%.1f℃的日期为%d天\n", temp, count);
    for(int i = 0; i < dayCount; i++){
        if(days[i].max_temp >= temp){
            printf("%s    %.1f℃    %.1f℃    %s    %.1f\n",
                   days[i].date,
                   days[i].max_temp,
                   days[i].min_temp,
                   days[i].weatherType,
                   days[i].precipitation);
        }
    }
}

// 天气类型分类统计
void WeatherType(){
    const char* types[] = {"晴", "多云/阴", "雨", "雪"};
    int typeCount = sizeof(types) / sizeof(types[0]);
    for(int t = 0; t < typeCount; t++){
        int count = 0;
        for(int i = 0; i < dayCount; i++){
            if(strcmp(days[i].weatherType, types[t]) == 0){
                count++;
            }
        }
        printf("%s天气共%d天\n", types[t], count);
        for(int i = 0; i < dayCount; i++){
            if(strcmp(days[i].weatherType, types[t]) == 0){
                printf("%s    %.1f℃    %.1f℃    %s    %.1f\n",
                       days[i].date,
                       days[i].max_temp,
                       days[i].min_temp,
                       days[i].weatherType,
                       days[i].precipitation);
            }
        }
        printf("\n");
    }
}

// 按日期查询天气
void QueryByDate(){
    char queryDate[11];
    printf("请输入要查询的日期(YYYY-MM-DD): ");
    scanf("%s", queryDate);
    if(strlen(queryDate) != 10 || queryDate[4] != '-' || queryDate[7] != '-'){
        printf("日期格式错误，请输入YYYY-MM-DD格式的日期。\n");
        return;
    }
    int found = 0;
    for(int i = 0; i < dayCount; i++){
        if(strcmp(days[i].date, queryDate) == 0){
            printf("%s    %.1f℃    %.1f℃    %s    %.1f\n",
                   days[i].date,
                   days[i].max_temp,
                   days[i].min_temp,
                   days[i].weatherType,
                   days[i].precipitation);
            found = 1;
            break;
        }
    }
    if(!found){
        printf("未找到该日期的天气记录\n");
    }
}

// 浮点数比较函数
int isFloatEqual(float a, float b){
    return fabs(a - b) < 0.01f;
}

// 更新每月天气记录
void updateMonthFile(){
    printf("\n正在检查 %s ...\n", MONTH_FILE);
    WeatherRecord_m records[MAX_DAYS];
    int recCount = 0;
    int existIdx = -1;
    FILE *fp = fopen(MONTH_FILE, "r");
    if(fp){
        while(recCount < MAX_DAYS && 
               fscanf(fp, "%7s %f %f %f %f %f",
                      records[recCount].ym,
                      &records[recCount].ex_maxtemp,
                      &records[recCount].ex_mintemp,
                      &records[recCount].ave_maxtemp,
                      &records[recCount].ave_mintemp,
                      &records[recCount].sum_precipitation) == 6){
            if(strcmp(records[recCount].ym, currentMonthStats.ym) == 0){
                existIdx = recCount;
            }
            recCount++;
        }
        fclose(fp);
    }
    if(existIdx >= 0){
        WeatherRecord_m *old = &records[existIdx];
        if(isFloatEqual(old->ex_maxtemp, currentMonthStats.ex_maxtemp) &&
            isFloatEqual(old->ex_mintemp, currentMonthStats.ex_mintemp) &&
            isFloatEqual(old->ave_maxtemp, currentMonthStats.ave_maxtemp) &&
            isFloatEqual(old->ave_mintemp, currentMonthStats.ave_mintemp) &&
            isFloatEqual(old->sum_precipitation, currentMonthStats.sum_precipitation)){
            printf("数据一致，无需更新！\n");
            return;
        }
        else{
            int confirm;
            printf("发现%s已存在但数据不一致！\n", currentMonthStats.ym);
            printf("是否覆盖？(1=是, 0=否): ");
            scanf("%d", &confirm);
            if (confirm != 1) {
                printf("已取消更新。\n");
                return;
            }
            records[existIdx] = currentMonthStats;
        }
    }
    else{
        records[recCount++] = currentMonthStats;
        printf("%s数据更新成功。\n", currentMonthStats.ym);
    }
    fp = fopen(MONTH_FILE, "w");
    if(!fp){
        printf("写入%s失败！\n", MONTH_FILE);
        return;
    }
    for(int i = 0; i < recCount; i++){
        fprintf(fp, "%s %.1f %.1f %.1f %.1f %.1f\n",
                records[i].ym,
                records[i].ex_maxtemp,
                records[i].ex_mintemp,
                records[i].ave_maxtemp,
                records[i].ave_mintemp,
                records[i].sum_precipitation);
    }
    fclose(fp);
    printf("%s更新成功！共%d条月度记录。\n", MONTH_FILE, recCount);
}

// 主函数
int main(){
    int choice;
    if(loadDailyData() <= 0){
        printf("无法读取%s或文件为空！\n", DAY_FILE);
        return 1;
    }
    calcMonthlyStats("2025-06");
    do{
        printf("欢迎进入气象数据统计系统！\n");
        printf("1. 输出极端最高气温对应日期\n");
        printf("2. 输出高温天气统计(>=35℃)\n");
        printf("3. 输出天气类型分类统计\n");
        printf("4. 按日期查询天气\n");
        printf("5. 年度每月天气记录更新\n");
        printf("0. 退出程序\n");
        printf("请选择操作(0-5): ");
        if(scanf("%d", &choice) != 1){
            while(getchar() != '\n');
            printf("输入无效，请输入数字！\n");
            continue;
        }
        switch(choice){
            case 1: HighestTemperature(); break;
            case 2: HighTempDays(35.0f); break;
            case 3: WeatherType(); break;
            case 4: QueryByDate(); break;
            case 5: updateMonthFile(); break;
            case 0: printf("程序已退出。\n"); break;
            default: printf("无效选项，请重新选择！\n");
        }
    }while(choice != 0);

    return 0;
}