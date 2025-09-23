#include "kernel/types.h"
#include "user/user.h" 

int main(int argc, char *argv[]) {
    if (argc != 2) {
        
        printf("Sleep needs one argument!\n");
        exit(-1);
    }

    int ticks = atoi(argv[1]); // 将字符串参数转换为整数
    sleep(ticks); // 调用 sleep 系统调用
    printf("(nothing happens for a little while)\n");
    exit(0);
}
