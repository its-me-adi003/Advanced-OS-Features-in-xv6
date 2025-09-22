#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
int yield_on_timer =0 ;

struct proc *prev_proc=0;
static struct proc *initproc;
uint last_ticks =0;
int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}
void helper1(struct proc *np){
  np->state = RUNNABLE;
  np->creation_time = ticks;
  np->context_switches = 0;
  np->first_run_time = -1;
  np->finish_time = 0;
  np->rtime = 0; 
  np->initial_priority = INIT_PRIORITY;
  np->cpu_ticks = 0;
  np->wait_ticks = 0;
  np->wait_ticks2=0;
}
// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  np->start_later_flag=0;
  np->scheduling_enabled=0;
  np->exec_time=-1;
  pid = np->pid;

  acquire(&ptable.lock);

  // np->state = RUNNABLE;
  // np->creation_time = ticks;
  // np->context_switches = 0;
  // np->first_run_time = -1;
  // np->finish_time = 0;
  // np->rtime = 0; 
  // np->initial_priority = INIT_PRIORITY;
  // np->cpu_ticks = 0;
  // np->wait_ticks = 0;
  // np->wait_ticks2=0;
  helper1(np);
  np->last_scheduled = ticks;
  np->dyn_priority = np->initial_priority;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void helper2(struct proc *curproc ){
  curproc->state = ZOMBIE;
}
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);
  curproc->finish_time=ticks;
  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  
  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  
  int turnaround_time = curproc->finish_time - curproc->creation_time;
  //int waiting_time = turnaround_time - curproc->rtime;
  int response_time;
  if(curproc->first_run_time == -1){
    response_time=-1;
  }
  else{
    response_time = curproc->first_run_time - curproc->creation_time;
  }
 // int response_time = (curproc->first_run_time == -1) ? -1 : (curproc->first_run_time - curproc->creation_time);

  cprintf("PID: %d\n", curproc->pid);
  cprintf("TAT: %d\n", turnaround_time);
  cprintf("WT: %d\n",curproc->wait_ticks2);
  cprintf("RT: %d\n", response_time);
  cprintf("#CS: %d\n", curproc->context_switches);
  // Jump into the scheduler, never to return.
  helper2(curproc);
  // if(curproc==prev_proc){
  //   cprintf("hello\n");
  // }
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //  cprintf("%d %d hi3",p->state,p->pid);
      if(p->parent != curproc)
        continue;
      if(p->state!=SUSPENDED)
        havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void helper3(struct proc *p){
  if(p->dyn_priority<0){
    p->dyn_priority=0;
  }
}
void helper4(struct proc *p,struct cpu *c ){
  p->wait_ticks = 0;         
     // p->cpu_ticks++;           
     // p->context_switches++; 
      p->last_scheduled=ticks;   
      c->proc = p;
}
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE){
        p->dyn_priority = p->initial_priority - (ALPHA * p->cpu_ticks) + (BETA * p->wait_ticks);
        helper3(p);
      }

    }
    
    struct proc *highest = 0;

    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid==4){
    //    cprintf("%d %d %d %d hi1\n",p->state,p->wait_ticks2,ticks,last_ticks);
      }
      if(p->state != RUNNABLE)
        continue;
      if(p->scheduling_enabled == 1 ){
      if(p->start_later_flag == 1){
        continue;
      }
      }
      if(highest == 0 || p->dyn_priority > highest->dyn_priority ||
         (p->dyn_priority == highest->dyn_priority && p->pid < highest->pid)){
        highest = p;
      }
    }

    if(highest){
      
      p = highest;
    //   p->wait_ticks = 0;         
    //  // p->cpu_ticks++;           
    //  // p->context_switches++; 
    //   p->last_scheduled=ticks;   
    //   c->proc = p;
      helper4(p,c);
      switchuvm(p);
      p->state = RUNNING;
      if(p->first_run_time == -1)
        p->first_run_time = ticks;
      
      swtch(&(c->scheduler), p->context);
      
      switchkvm();
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
 // cprintf("%d %d %d %d hi0\n",myproc()->pid,prev_proc->pid,prev_proc->context_switches,myproc()->context_switches);
  if(prev_proc->state!=5){
    
  
  swtch(&p->context, mycpu()->scheduler);
  //cprintf("%d %d %d hi1\n",myproc()->pid,prev_proc->pid,prev_proc->context_switches);
      if(prev_proc){
        if(prev_proc->pid!=myproc()->pid){
          prev_proc->context_switches++;
        }
      }
      
      prev_proc=myproc();
      
     // prev_proc = myproc();
    //  cprintf("%d %d %d %d hi2\n",myproc()->pid,prev_proc->pid,prev_proc->context_switches,myproc()->context_switches);

  // if()
  // struct proc *p1 ;
  // acquire(&ptable.lock);
  //   for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
  //     if (p1->pid=)
  //   }
  //   release(&ptable.lock);
  mycpu()->intena = intena;
 //   cprintf("fdfsd\n");
  }
  else{
    prev_proc=0;
  //int a = myproc()->pid;
  swtch(&p->context, mycpu()->scheduler);
  //cprintf("%d %d %d hi1\n",myproc()->pid,prev_proc->pid,prev_proc->context_switches);
      if(prev_proc){
        if(prev_proc->pid!=myproc()->pid){
          prev_proc->context_switches++;
        }
      }
      
      prev_proc=myproc();
      
     // prev_proc = myproc();
    //  cprintf("%d %d %d %d hi2\n",myproc()->pid,prev_proc->pid,prev_proc->context_switches,myproc()->context_switches);

  // if()
  // struct proc *p1 ;
  // acquire(&ptable.lock);
  //   for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
  //     if (p1->pid=)
  //   }
  //   release(&ptable.lock);
  mycpu()->intena = intena;
    }
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
void helper5(void){
  if(myproc()->state != SUSPENDED)
    myproc()->state = RUNNABLE;
  sched();
}
void
yield2(void)
{
  suspend_user_procs();
  acquire(&ptable.lock);  //DOC: yieldlock
  helper5();
  release(&ptable.lock);
}
 

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
     // last_ticks=ticks ;
    //   if(p->pid==4){
    //   cprintf("hi2 %d %d \n",ticks,last_ticks);
    // }

    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}



void
kill_user_procs(void)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // if(p->pid > 2) cprintf("PID: %d StATE: %s Name: %s\n", p->pid, p->state, p->name);
    if(p->pid > 2 && p->state != UNUSED){
      if(p->state == SUSPENDED){
        p->state = RUNNABLE;
      }
      if(p->state != ZOMBIE) kill(p->pid);
      // kill(p->pid);
    }
    extern int ctrlC_pressed;
    if(ctrlC_pressed){
      ctrlC_pressed=0;
    }
  }
}
void suspend_user_procs(void)
{
  struct proc *p;
  
  acquire(&ptable.lock);
 // cprintf("size of ptable.proc: %d\n", sizeof(ptable.proc));
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // Skip processes that are already suspended or not in a state to be run.
    // Also skip init (pid 1) and the shell (pid 2).
    // if(p->pid != 0) cprintf("Process %d (%s) state: %d\n", p->pid, p->name, p->state);
    if(p->pid != 2 && p->pid != 1 && p->pid!=0){
      p->state = SUSPENDED;
     // cprintf("Suspending process %d (%s)\n", p->pid, p->name);
    }
    else if (p->state == SUSPENDED) {
      extern int ctrlB_pressed;
      if(ctrlB_pressed){
        ctrlB_pressed=0;
      }
    
    
      // cprintf("Process %d (%s) is already suspended\n", p->pid, p->name);
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->pid == 2 && p->state == SLEEPING) {
          p->state = RUNNABLE;
      }
  }
  release(&ptable.lock);
}
void resume_user_procs(void)
{
  struct proc *p;

  acquire(&ptable.lock);
  int num_procs=0;
  // Iterate over all processes in the process table.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // Resume only those processes that were suspended.
    // (We assume that processes suspended by SIGBG are in state SUSPENDED.)
    if(p->state == SUSPENDED){
      p->state = RUNNABLE;
    //  cprintf("Resuming process %d (%s)\n", p->pid, p->name);
      // If the process is sleeping on a particular wait channel (such as input),
      // you might also want to call wakeup() on that channel here.
    }
    num_procs++;
  }
  if(num_procs){
    extern int ctrlF_pressed;
    if(ctrlF_pressed){
      ctrlF_pressed=0;
    }
  }
  release(&ptable.lock);
}
 
void helper6(struct proc *np){
  np->scheduling_enabled=1;
  np->cpu_ticks=0;
  np->creation_time=ticks;
  np->context_switches = 0;
  np->first_run_time = -1;
  np->finish_time = 0;
  np->rtime = 0; 
  np->initial_priority = INIT_PRIORITY;
  np->wait_ticks = 0;
  np->wait_ticks2=0;
  
  np->last_scheduled = ticks;
}


int custom_fork(int start_later_flag ,int exec_time){
  
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  
  pid = np->pid;
  enum procstate pstate ;
  if(start_later_flag){
    pstate=SLEEPING;
  }
  else{
    pstate=RUNNABLE;
  }
  acquire(&ptable.lock);
  np->exec_time=exec_time;
  np->start_later_flag=start_later_flag;
  np->state = pstate;
  // np->scheduling_enabled=1;
  // np->cpu_ticks=0;
  // np->creation_time=ticks;
  // np->context_switches = 0;
  // np->first_run_time = -1;
  // np->finish_time = 0;
  // np->rtime = 0; 
  // np->initial_priority = INIT_PRIORITY;
  // np->wait_ticks = 0;
  // np->wait_ticks2=0;
  helper6(np);
  // np->last_scheduled = ticks;
  np->dyn_priority = np->initial_priority;

 // cprintf("%d %d %d here 1\n",np->scheduling_enabled,np->pid,np->start_later_flag);
  release(&ptable.lock);

  return pid;
  
}
int scheduler_start_handler(void){
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->start_later_flag ){
      if(p->state == SLEEPING){
  //    cprintf("%d %d %d here 2\n",p->scheduling_enabled,p->pid,p->start_later_flag);
      p->state = RUNNABLE;
      p->scheduling_enabled=0;
      }
    }
  }
  release(&ptable.lock);
  return 0;
}
void helper7(struct proc *p){
  p->wait_ticks+=ticks-last_ticks;
        p->wait_ticks2+=ticks-last_ticks;
}
void helper8(void ){
  myproc()->killed = 1;
}
void
update_times(void)
{
  struct proc *p;
  struct proc *curproc = myproc();
  //cprintf("%d %d\n",myproc()->pid,myproc()->exec_time,myproc()->cpu_ticks);
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid>2){
 //     cprintf("hi22 %d %d %d\n",p->pid,p->cpu_ticks,p->exec_time);
    }
    if(p->state == RUNNABLE){

     // p->wait_ticks+=ticks-last_ticks;
      //  p->wait_ticks2+=ticks-last_ticks;
        helper7(p);
       // cprintf("%d %d \n",ticks ,last_ticks);
      // p->wait_ticks+=1;
      //   p->wait_ticks2+=1;
        p->dyn_priority = p->initial_priority - (ALPHA * p->cpu_ticks) + (BETA * p->wait_ticks);
      
    }
  }
 // cprintf("%d %d\n",myproc()->pid,myproc()->exec_time,myproc()->cpu_ticks);

  if(curproc && curproc->state == RUNNING){
     myproc()->cpu_ticks+=ticks-last_ticks;
    if(myproc()->pid>2){
   //   cprintf("hi1111 %d %d %d\n",myproc()->pid,myproc()->cpu_ticks,myproc()->exec_time);
    }

    if(myproc()->pid>1 ) {
      if( myproc()->exec_time != -1){
        if(myproc()->cpu_ticks >= myproc()->exec_time){
    //  cprintf("Process %d exec_time limit reached; terminating.\n", myproc()->pid);
          helper8();
        }
      }
   }
  }
  if(ticks>last_ticks){
    last_ticks=ticks;
  }
  release(&ptable.lock);
}
void
handle_pending_custom_signal(struct trapframe *tf)
{
  struct proc *p = myproc();
  if(p == 0)
    return;

  if(p->pending_custom && p->custom_handler != 0 ){
    // Clear the pending flag.
    p->pending_custom = 0;
    if(!p->tf1){
    p->tf1 = (struct trapframe*)kalloc();}
    memmove(p->tf1, tf, sizeof(struct trapframe));
    // Save the current user EIP (where the process was executing).
    p->prev_eip = tf->eip;

    // Update the trapframe to redirect execution to the custom signal handler.
    tf->eip = (uint)p->custom_handler;
  }
}