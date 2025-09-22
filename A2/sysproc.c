#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


int
sys_signal(void)
{
  sighandler_t handler;

  // Retrieve the first argument: the function pointer.
  if(argptr(0, (void*)&handler, sizeof(handler)) < 0)
    return -1;
  // Set the current process's custom_handler field.
  myproc()->custom_handler = handler;
  return 0;
}



int scheduler_start_handler(void);
int custom_fork(int start_later_flag,int exec_time);
int sys_scheduler_start(void){
  return scheduler_start_handler();
};
int sys_custom_fork(void){
  int start_later_flag,exec_time;
  if (argint(0, &start_later_flag) < 0 || argint(1, &exec_time) < 0)
    return -1;

  return custom_fork(start_later_flag, exec_time);
}