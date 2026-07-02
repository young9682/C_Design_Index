#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>

#define MAX_LINE 1024
#define MAX_WORDS 256
#define MAX_WORD_LEN 64

// 词库结构
typedef struct{
    char words[MAX_WORDS][MAX_WORD_LEN];
    int count;
} WordDict;

// 加载词库文件
int loadDict(WordDict *dict, const char *filename){
    FILE *fp = fopen(filename, "r");
    if(!fp){
        printf("无法打开词库文件: %s\n", filename);
        return -1;
    }
    dict->count = 0;
    char line[MAX_LINE];
    while(fgets(line, sizeof(line), fp) && dict->count < MAX_WORDS){
        char *token = strtok(line, ",;\n\r");
        while(token && dict->count < MAX_WORDS){
            while(*token == ' ' || *token == '\t'){
                token++;
            }
            char *end = token + strlen(token) - 1;
            while(end > token && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')){
                end--;
            }
            *(end + 1) = '\0';
            if(strlen(token) > 0){
                strncpy(dict->words[dict->count], token, MAX_WORD_LEN - 1);
                dict->words[dict->count][MAX_WORD_LEN - 1] = '\0';
                dict->count++;
            }
            token = strtok(NULL, ",;\n\r");
        }
    }
    fclose(fp);
    return 0;
}

// 检查单词是否在词库中
int isInDict(const char *word, WordDict *dict){
    for(int i = 0; i < dict->count; i++){
        if(strcmp(word, dict->words[i]) == 0){
            return 1;
        }
    }
    return 0;
}

// 转小写
void toLowerStr(char *str){
    for(int i = 0; str[i]; i++){
        str[i] = tolower(str[i]);
    }
}

// 检查输入有效性
int isValidInput(const char *str){
    for(int i = 0; str[i]; i++){
        if(isalpha(str[i])){
            return 1;
        }
    }
    return 0;
}

// 主函数
int main(){
    WordDict positive, negative;
    if(loadDict(&positive, "positive.txt") != 0 || loadDict(&negative, "negative.txt") != 0){
        return 1;
    }
    char line[MAX_LINE];
    while(1){
        printf("请输入一行英文: ");
        if(fgets(line, sizeof(line), stdin) == NULL){
            break;
        }
        line[strcspn(line, "\n")] = '\0';
        if(!isValidInput(line)){
            printf("输入不符合规范，请重新输入。\n");
            continue;
        }
        break;
    }
    char words[MAX_WORDS][MAX_WORD_LEN];
    int wordCount = 0;
    int totalLen = 0;
    char *token = strtok(line, " ,.?!;:");
    while(token && wordCount < MAX_WORDS){
        toLowerStr(token);
        int len = strlen(token);
        if(len > 0){
            strncpy(words[wordCount], token, MAX_WORD_LEN - 1);
            words[wordCount][MAX_WORD_LEN - 1] = '\0';
            wordCount++;
            totalLen += len;
        }
        token = strtok(NULL, " ,.?!;:");
    }
    int posCount = 0, negCount = 0;
    for(int i = 0; i < wordCount; i++){
        if(isInDict(words[i], &positive)){
            posCount++;
        }
        if(isInDict(words[i], &negative)){
            negCount++;
        }
    }
    const char *sentiment;
    if(posCount > negCount){
        sentiment = "积极";
    }
    else if(negCount > posCount){
        sentiment = "消极";
    }
    else{
        sentiment = "中性";
    }
    double avgLen = wordCount > 0 ? (double)totalLen / wordCount : 0.0;
    printf("\n===== 文本分析结果 =====\n");
    printf("单词总数量：%d\n", wordCount);
    printf("单词平均长度：%.2f\n", avgLen);
    printf("积极词汇出现次数：%d\n", posCount);
    printf("消极词汇出现次数：%d\n", negCount);
    printf("文本情感倾向：%s\n", sentiment);
    return 0;
}
