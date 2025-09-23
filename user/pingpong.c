#include "kernel/types.h"
#include "user/user.h"

#define READ 0
#define WRITE 1

int main(int argc, char *argv[]) {
    int f2c_pipe[2];  
    int c2f_pipe[2];  
    char buffer[1];   
    int father_pid = getpid(); 
    int pid;             

    if (pipe(f2c_pipe) < 0 || pipe(c2f_pipe) < 0) {
        fprintf(2, "pipe creation failed\n");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {  // 子进程
        close(f2c_pipe[WRITE]);  
        close(c2f_pipe[READ]);   

        read(f2c_pipe[READ], buffer, sizeof(buffer));
        printf("%d: received ping from pid %d\n", getpid(), father_pid);

        write(c2f_pipe[WRITE], buffer, sizeof(buffer));

        close(f2c_pipe[READ]);
        close(c2f_pipe[WRITE]);
        exit(0);

    } else {  // 父进程
        close(f2c_pipe[READ]);   
        close(c2f_pipe[WRITE]);  

        buffer[0] = 'X';  
        write(f2c_pipe[WRITE], buffer, sizeof(buffer));
        close(f2c_pipe[WRITE]);  

        read(c2f_pipe[READ], buffer, sizeof(buffer));
        printf("%d: received pong from pid %d\n", getpid(), pid);
        close(c2f_pipe[READ]);
        
        wait(0); 
        exit(0);
    }
}