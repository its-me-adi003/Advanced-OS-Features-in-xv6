#ifndef PAGESWAP_H
#define PAGESWAP_H
#include "types.h"
#include "proc.h" 
struct swapslot {
    int page_perm;  // Permissions of the swapped page
    int is_free;    // 1 if free, 0 if used
  };
#define NUM_SLOTS 800
extern struct swapslot swap_slots[NUM_SLOTS];
// Called once at boot
//void init_swap(void);
void swap_out_one_page(void);
// Swaps out pages if free memory dips below the threshold
void swap_out_if_needed(void);

// Swap in the page at virtual address va (page-aligned) using slot
void swap_in(struct proc *p, uint va, uint slot);

// Free a previously allocated swap slot
void free_swap_slot(uint slot);

#endif // PAGESWAP_H
