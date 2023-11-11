// find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char* path, char* target) {
    // printf("1:path is %s\n", path);
    char buf[512], * p;
    int fd;
    struct dirent de;
    struct stat st;

    if (((fd = open(path, 0)) < 0)) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // printf("2:path is %s\n", path);

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        // 返回0表示文件名结尾匹配输入的<target>
        if (strcmp(path + strlen(path) - strlen(target), target) == 0) {
            // printf("target: %s\n", target);
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0) {
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            // 不要进入 `.` 和 `..`
            if (strcmp(buf + strlen(buf) - 2, "/.") != 0 &&
                strcmp(buf + strlen(buf) - 3, "/..") != 0) {
                find(buf, target); // 递归查找
            }
        }
        break;
    }
    close(fd);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(2, "Usage: find <path> <file>\n");
        exit(0);
    }

    char target[512];
    target[0] = '/'; // 为查找的文件名添加 / 在开头
    strcpy(target + 1, argv[2]);
    find(argv[1], target);

    exit(0);
}
