// xargs.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void run(char* program, char** args) {
    if (fork() == 0) {  // child exec
        exec(program, args);
        exit(0);
    }
    return;  // parent return
}

int main(int argc, char* argv[]) {
    char buf[2048];
    char* p = buf, * last_p = buf;  // 当前参数的结束、开始指针

    char* argsbuf[128]; 
    // 全部参数列表，字符串指针数组，包含 argv 传进来的参数和stdin 读入的参数

    char** args = argsbuf;  

    for (int i = 1; i < argc; i++) {
        // 将 argv 提供的参数加入到最终的参数列表中
        *args = argv[i];
        args++;
    }

    char** pa = args;  // 开始读入参数
    while (read(0, p, 1) != 0) {
        if (*p == ' ' || *p == '\n') {
            *p = '\0';
            // 将空格替换为 \0 分割开各个参数，这样可以直接使用内存池中的字符串作为参数字符串
            // 而不用额外开辟空间

            *(pa++) = last_p;
            last_p = p + 1;

            if (*p == '\n') {
                // 读入一行完成
                *pa = 0;                // 参数列表末尾用 null 标识列表结束
                run(argv[1], argsbuf);  // 执行最后一行指令
                pa = args;  // 重置读入参数指针，准备读入下一行
            }
        }
        p++;
    }

    if (pa != args) {  // 如果最后一行不是空行
        // 收尾最后一个参数
        *p = '\0';
        *(pa++) = last_p;
        // 收尾最后一行
        *pa = 0;  

        // 执行最后一行指令
        run(argv[1], argsbuf);
    }

    while (wait(0) != -1) {
    };  // 循环等待所有子进程完成，每一次 wait(0) 等待一个
    exit(0);
}
