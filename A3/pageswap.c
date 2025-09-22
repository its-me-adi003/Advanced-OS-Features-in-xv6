#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "pageswap.h"

#define BLOCKS_PER_SLOT 8
#define PGSIZE 4096
#define PTE_S 0x200  // Swapped-out flag

struct swapslot swap_slots[NUM_SLOTS];
extern void calc_rss(struct proc* victim);

//static uint swapstart;
extern int T_h ;     // Threshold
static int N_pg = 4;      // Pages to swap out


static int alpha = ALPHA;
static int beta = BETA;

static int LIMIT = 100;   // Max N_pg

// Initialize swap slots at boot time
// void
// init_swap(void)
// {cprintf("init_swap called\n");
//   struct superblock sb;
//   cprintf("init_swap called1\n");
//   readsb(ROOTDEV, &sb);
//   cprintf("init_swap called2\n");
//   swapstart = sb.swapstart;
//   for (int i = 0; i < NUM_SLOTS; i++) {
//     swap_slots[i].is_free = 1;
//     swap_slots[i].page_perm = 0;
//   }
//  // cprintf("Booting swap space\n");
// }

// Write a page to a swap slot
static void
write_to_swap(char *mem, int slot)
{
  for (int j = 0; j < BLOCKS_PER_SLOT; j++) {
  //  cprintf("%d %d\n",j,swapstart);
    struct buf *b = bread(ROOTDEV, 2 + slot * BLOCKS_PER_SLOT + j);
   // cprintf("%d hi2 \n",j);
    memmove(b->data, mem + j * BSIZE, BSIZE);
    bwrite(b);
    brelse(b);
  }
}

// Read a page from a swap slot
static void
read_from_swap(char *mem, int slot)
{
  for (int j = 0; j < BLOCKS_PER_SLOT; j++) {
    struct buf *b = bread(ROOTDEV, 2 + slot * BLOCKS_PER_SLOT + j);
    memmove(mem + j * BSIZE, b->data, BSIZE);
    brelse(b);
  }
}

// Find a free swap slot
static int
find_free_slot(void)
{ //cprintf("%d\n",NUM_SLOTS);
  for (int i = 0; i < NUM_SLOTS; i++) {
   // cprintf("%d %d\n ",i,swap_slots[i].is_free);
    if (swap_slots[i].is_free==1) {
    //  cprintf("%d %d\n ",i,swap_slots[i].is_free);
      swap_slots[i].is_free = 0;
      return i;
    }
  }
  //panic("No free swap slots");
  return -1;
}

// Free a swap slot
void
free_swap_slot(uint slot)
{
  // if (slot >= NUM_SLOTS || swap_slots[slot].is_free) {
  //   panic("Invalid or already free swap slot");
  // }
  //cprintf("%d to be freed in pageswap.c\n",slot);
  swap_slots[slot].is_free = 1;
  swap_slots[slot].page_perm = 0;
}

// Swap out one page
void
swap_out_one_page(void)
{
  struct proc *p = myproc();
  struct proc *victim = 0;
  int max_rss = -1;

  // Find victim process
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING) {
      calc_rss(p);
      if (p->rss > max_rss || (p->rss == max_rss && p->pid < victim->pid)) {
        victim = p;
        max_rss = p->rss;
      }
    }
  }
  release(&ptable.lock);
  //cprintf("here2\n");
  // if (!victim || max_rss == 0) {
  //   panic("No victim process found");
  // }

  // Find victim page
  uint va;
  pte_t *pte=(void*)0;
  for (va = 0; va < victim->sz; va += PGSIZE) {
    pte = walkpgdir2(victim->pgdir, (void*)va, 0);
    if (!pte || !(*pte & PTE_P)){
      continue;}
    if (!(*pte & PTE_A)) {
      // found our victim
      break;
    }
    // give it a second chance
      *pte &= ~PTE_A;
  }
 // cprintf("here3\n");
  if (va >= victim->sz) {
    for (va = 0; va < victim->sz; va += PGSIZE) {
      pte = walkpgdir2(victim->pgdir,(void*)va,0);
      if (pte && (*pte & PTE_P))
        break;
    }
    // va = 0;  // Fallback to first page
    // pte = walkpgdir2(victim->pgdir, (void*)va, 0);
  }

  if (!pte || !(*pte & PTE_P)) {
    panic("No victim page found");
  }
  //cprintf("here4\n");
  // Swap out the page
  int slot = find_free_slot();
 // cprintf("free slot found : %d \n",slot);
//   cprintf("PTE value: %x\n", *pte);
// if (!(*pte & PTE_P)) {
//     panic("Invalid PTE: Page not present");
// }
  char *mem = (char *)P2V((*pte) & ~0xFFF);
  if (mem == (void*)0) {
    panic("Invalid memory pointer");
}
if ((*pte & PTE_P) == 0) {
    panic("Page is not present in memory");
}

 // cprintf("here5\n");
  swap_slots[slot].page_perm = *pte & 0xFFF & ~PTE_S;
  write_to_swap(mem, slot);
  //cprintf("here6\n");
  *pte = (slot << 12) | (*pte & 0xFFF & ~PTE_P) | PTE_S;
//  victim->rss--;
  kfree(mem);
}

// Check memory pressure and swap out if needed
void
swap_out_if_needed(void)
{//cprintf("Inside swap_out_if_needed in pageswap.c\n");
  if (get_free_pages() <= T_h) {
    cprintf("Current Threshold = %d, Swapping %d pages\n", T_h, N_pg);
    for (int i = 0; i < N_pg; i++) {
      swap_out_one_page();
    }
    T_h-=(T_h*beta)/100;
    N_pg+=(N_pg*alpha)/100;
   // T_h = (int)(T_h * (1 - beta / 100.0));
   // N_pg = (int)(N_pg * (1 + alpha / 100.0));
    if (N_pg > LIMIT) N_pg = LIMIT;
  }
}

// Swap in a page
void
swap_in(struct proc *p, uint va, uint slot)
{
  swap_out_if_needed();
  char *mem = kalloc();
  if (!mem) {
    panic("kalloc failed in swap_in");
  }
  read_from_swap(mem, slot);
  pte_t *pte = walkpgdir2(p->pgdir, (void*)va, 0);
  if (!pte) {
    panic("Invalid pte in swap_in");
  }
  *pte = V2P(mem) | swap_slots[slot].page_perm | PTE_P;
  //swap_slots[slot].is_free = 1;
 // p->rss++;
}