#include<stdio.h>
#include<stdlib.h>
#include<time.h>

// 随机函数
int GenRandomInRange(int minVal, int maxVal){
    return (rand() % (maxVal - minVal + 1)) + minVal;
}

// 随机数分布统计
void TestRandom(){
    int count[101] = {0};
    for (int i = 0; i < 10000; i++){
        int num = GenRandomInRange(1, 100);
        count[num]++;
    }
    for (int i = 1; i <= 100; i++){
        printf("数字%-3d出现%-3d次\n", i, count[i]);
    }
    printf("\n");
}

// 小比例抽样
void SmallSampling(){
    int t = 10000;
    int n = 50;
    int selectedIDs[50];
    int count = 0;
    while(count < n){
        int id = GenRandomInRange(1, t);
        int flag = 0;
        for(int i = 0; i < count; i++){
            if(selectedIDs[i] == id){
                flag = 1;
                break;
            }
        }
        if(!flag){
            selectedIDs[count] = id;
            count++;
        }
    }
    printf("共选择%d位用户\n", count);
    for (int i = 0; i < n; i++){
        printf("被抽中的第%d位用户ID: %d\n", i + 1, selectedIDs[i]);
    }
}

// 通用抽样器
void ConductSurveySampling(int TotalPopulationSize, int SampleSize){
    int selectedIDs[SampleSize];
    int count = 0;
    while(count < SampleSize){
        int id = GenRandomInRange(1, TotalPopulationSize);
        int flag = 0;
        for(int i = 0; i < count; i++){
            if(selectedIDs[i] == id){
                flag = 1;
                break;
            }
        }
        if(!flag){
            selectedIDs[count] = id;
            count++;
        }
    }
    printf("共选择%d位用户\n", count);
    for (int i = 0; i < SampleSize; i++){
        printf("被抽中的第%d位用户ID: %d\n", i + 1, selectedIDs[i]);
    }
    int satisfiedCount = 0;
    for (int i = 0; i < SampleSize; i++){
        if (GenRandomInRange(1, 100) >= 60){
            satisfiedCount++;
        }
    }
    double satisfactionRate = (double)satisfiedCount / SampleSize * 100;
    printf("被调查人群中的满意率为: %.2f%%\n", satisfactionRate);
}

int main(){
    int N, n;
    scanf("%d %d", &N, &n);
    srand((unsigned int)time(NULL));
    TestRandom();
    SmallSampling();
    ConductSurveySampling(N, n);
    return 0;
}
