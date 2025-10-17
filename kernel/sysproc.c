#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// 前置声明辅助函数
static struct proc* find_next_runnable(struct proc *current);

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
  if (argint(1, &flags) < 0) return -1;
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
  int len = argstr(0, name, MAXPATH);
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
  
  // 1. 打印内核线程上下文保存的地址范围
  printf("Save the context of the process to the memory region from address %p to %p\n", 
         &p->context, (char*)&p->context + sizeof(p->context));
  
  // 2. 打印当前进程的pid和用户态pc值
  printf("Current running process pid is %d and user pc is %p\n", 
         p->pid, p->trapframe->epc);
  
  // 3. 查找下一个RUNNABLE进程并打印信息
  struct proc *next_proc = find_next_runnable(p);
  if (next_proc) {
    printf("Next runnable process pid is %d and user pc is %p\n", 
           next_proc->pid, next_proc->trapframe->epc);
    // 记得释放找到的进程的锁，在find_next_runnable中已经释放了
  } else {
    printf("Next runnable process not found\n");
  }
  
  // 4. 让出CPU
  yield();
  
  return 0;
}

// 辅助函数：查找下一个可运行进程
static struct proc* find_next_runnable(struct proc *current) {
  struct proc *p;
  struct proc *found = 0;
  int start_index = current - proc;
  int current_index;
  
  // 从当前进程的下一个开始环形搜索
  for (int i = 1; i < NPROC; i++) {
    current_index = (start_index + i) % NPROC;
    p = &proc[current_index];
    
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      found = p;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }
  
  return found;
}