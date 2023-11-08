#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char** argv){
    // 注意，题目说pipe是单向的，所以要创建两个
    int pipe_p2c[2], pipe_c2p[2] = {0};
    pipe(pipe_p2c);
    pipe(pipe_c2p);

    int pid = fork();
    
    if (pid > 0){  // 父进程
        char buf;
        write(pipe_p2c[1], "x", 1);
        read(pipe_c2p[0], &buf, 1);
        printf("%d, received pong, it's %c\n", getpid(), buf);
    }
    else{  // 子进程
        char buf;
        read(pipe_p2c[0], &buf, 1);
        printf("%d, received ping, it's %c\n", getpid(), buf);
        write(pipe_c2p[1], "y", 1);
    }
    exit(0);
}