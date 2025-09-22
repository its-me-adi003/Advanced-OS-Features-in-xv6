#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
extern int yield_on_timer;
extern void update_times(void);
// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
void handle_pending_custom_signal(struct trapframe *tf);
void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
  update_times();
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
   // cprintf("hi");
    
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:{
    //cprintf("hi1\n");
      if(myproc() == 0){
       break ;
      }
     if(myproc()->killed){
       exit();
     }
     memmove(myproc()->tf, myproc()->tf1, sizeof(struct trapframe));
     break;
   }
  case 13:{
    //     cprintf("hi2\n");
     if(myproc()->killed){
       exit();
     }
     memmove(myproc()->tf, myproc()->tf1, sizeof(struct trapframe));
     break;
   }
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();




  if(myproc() && myproc()->state == RUNNING && yield_on_timer == 1 && tf->trapno == T_IRQ0+IRQ_TIMER){
     // cprintf("[DEBUG] Yielding on timer interrupt\n");
    //  cprintf("%d \n",myproc()->pid);
      yield_on_timer = 2;
      yield2();
  }
  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
  if(myproc()&& myproc()->pending_custom && myproc()->custom_handler != 0 && !myproc()->killed) {

      handle_pending_custom_signal(tf);
    }
  // if(myproc() && (tf->cs & 3) == DPL_USER){
  //     if(myproc()->pending_custom && myproc()->custom_handler){
  //       // Clear the pending flag
  //       myproc()->pending_custom = 0;
  //       // Redirect execution to the user-registered custom handler.
  //       // Push current eip onto the user stack
  //       tf->esp -= 4;
  //       // cprintf("[DEBUG] Pushing EIP %x to user stack at %x\n", tf->eip, tf->esp);
  
  //       if (copyout(myproc()->pgdir, tf->esp, (char *)&tf->eip, 4) < 0) {
  //         // cprintf("[DEBUG] copyout failed — killing proc\n");
  //         myproc()->killed = 1;
  //         return;
  //       }
  //       // (Optionally, you might save the old eip to allow resumption.)
  //       // cprintf("[DEBUG] Jumping to handler at %x\n", (uint)myproc()->custom_handler);
  //       tf->eip = (uint) myproc()->custom_handler;
  //     }
  // }



}
