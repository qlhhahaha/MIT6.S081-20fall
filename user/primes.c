#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void sieve(int pleft[2]){
    int num;
    read(pleft[0], &num, sizeof(num));
    if (num == -1){  // 遇到哨兵，所有数已遍历完
        exit(0);  
    }
    printf("prime is %d\n", num);

    int pright[2];
    pipe(pright);

    if (fork() == 0){  // 下一个stage的子进程
        close(pright[1]);
        sieve(pright);
    }
    else{  // 下一个stage的父进程
        int buf;
        while(read(pleft[0], &buf, sizeof(buf)) && buf != -1){
            // read读取的文件结束了的话会返回0
            if (buf % num != 0){
                write(pright[1], &buf, sizeof(buf));
            }
        }
        buf = -1;
        write(pright[1], &buf, sizeof(buf));
        wait(0);
        exit(0);
    }
}


int main(int argc, char** argv){
    int input_pipe[2];
    pipe(input_pipe);

    if (fork() == 0){  // 第一个stage的子进程
        close(input_pipe[1]);  // 子进程只用读管道不用写
        sieve(input_pipe);
        exit(0);
    }
    else{  // 父进程
        int i;
        for (i = 2; i <= 35; i++){
            write(input_pipe[1], &i, sizeof(i));
        }
        i = -1;  // 哨兵
        write(input_pipe[1], &i, sizeof(i));
    }

    // 等待第一个 stage 完成。注意：这里无法等待子进程的子进程，
    // 只能等待直接子进程，无法等待间接子进程。
    // 在 sieve() 中会为每个 stage 再各自执行 wait(0)，形成等待链。
    wait(0); 

    exit(0);
}