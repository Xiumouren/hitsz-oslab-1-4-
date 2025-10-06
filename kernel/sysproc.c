#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0) return -1;
  if (argint(1, &flags) < 0) return -1;  // 获取 flags 参数
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, sizeof(name));
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_yield(void) {
    struct proc *p = myproc();
    struct proc *next_proc = 0;
    
    // 打印当前进程的内核线程上下文保存地址范围
    // 上下文通常保存在 p->context 中，大小为 sizeof(struct context)
    uint64 context_start = (uint64)&p->context;
    uint64 context_end = context_start + sizeof(struct context);
    printf("Save the context of the process to the memory region from address %p to %p\n", 
           context_start, context_end);
    
    // 打印当前进程的pid和用户态pc值
    // 用户态pc值保存在trapframe->epc中
    printf("Current running process pid is %d and user pc is %p\n", 
           p->pid, p->trapframe->epc);
    
    // 寻找下一个可运行的进程
    acquire(&p->lock); // 保护当前进程
    
    for (int i = 0; i < NPROC; i++) {
        int next_pid = (p->pid + i) % NPROC;
        if (next_pid == 0) continue; // 跳过pid为0的进程
        
        struct proc *np = &proc[next_pid];
        acquire(&np->lock);
        
        if (np->state == RUNNABLE && np != p) {
            next_proc = np;
            // 打印下一个进程的信息
            printf("Next runnable process pid is %d and user pc is %p\n", 
                   next_proc->pid, next_proc->trapframe->epc);
            release(&np->lock);
            break;
        }
        
        release(&np->lock);
    }
    
    release(&p->lock);
    
    // 调用内核的yield函数让出CPU
    yield();
    
    return 0;
}
