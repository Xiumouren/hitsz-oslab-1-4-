#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

/**
 * 从路径中提取文件名部分
 */
char*
fmt_name(char *path)
{
    static char buf[DIRSIZ+1];
    char *p;
    
    // 从字符串末尾向前查找最后一个'/'
    for(p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;
    
    // 如果文件名长度超过DIRSIZ，直接返回指针
    if(strlen(p) >= DIRSIZ)
        return p;
    
    // 复制文件名到缓冲区
    memmove(buf, p, strlen(p));
    buf[strlen(p)] = 0;
    return buf;
}

/**
 * 在指定路径中递归查找文件或目录
 */
void
find(char *path, char *target_name)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    // 无论是文件还是目录，都先检查名称是否匹配
    if(strcmp(fmt_name(path), target_name) == 0){
        printf("%s\n", path);
    }
    
    // 如果是目录，进行递归查找
    if(st.type == T_DIR){
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("find: path too long\n");
            close(fd);
            return;
        }
        
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            // 跳过无效目录项
            if(de.inum == 0)
                continue;
            
            // 跳过特殊目录，但要确保这些目录本身能被查找
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            
            find(buf, target_name);
        }
    }
    
    close(fd);
}

int
main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(2, "Usage: find <path> <filename>\n");
        fprintf(2, "Example: find . b\n");
        exit(1);
    }
    
    find(argv[1], argv[2]);
    exit(0);
}