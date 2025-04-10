// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(uint32_t *pte,
             int pre,    // present
             int fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             int swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_swap - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */

 /* get the first layer index */
static addr_t get_first_lv(addr_t addr) {
	return addr >> (8 + PAGE_LEN);
}

/* get the second layer index */
static addr_t get_second_lv(addr_t addr) {
  return (addr >> 8) & ((1 << PAGE_LEN) - 1);
}

int add_page_table_entry(struct page_table_t *page_table, addr_t v_index, addr_t p_index, addr_t address){
  addr_t first_lv = get_first_lv(address);
  addr_t second_lv = get_second_lv(address);
  struct trans_table_t* second_tab = NULL;
  for (int i = 0; i < page_table->size; i++){
    if (first_lv == page_table->table[i].v_index){
      second_tab = page_table->table[i].next_lv;
      break;
    }   
  }

  if (second_tab == NULL){
    second_tab = calloc(1, sizeof(struct trans_table_t));
    if (second_tab == NULL)
      return -1;
    second_tab->size = 0;
    page_table->size++;
    page_table->table[page_table->size - 1].v_index = first_lv;
    page_table->table[page_table->size - 1].next_lv = second_tab;
  }

  if (second_lv < 0 || second_lv >= (1 << SECOND_LV_LEN))
    return -1;
  second_tab->table[second_lv].p_index = p_index;
  second_tab->table[second_lv].v_index = v_index;
  second_tab->size++;
  return 0;
}

int vmap_page_range(struct pcb_t *caller,           // process call
  int addr,                       // start address which is aligned to pagesz
  int pgnum,                      // num of mapping page
  struct framephy_struct *frames, // list of the mapped frames
  struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
//struct framephy_struct *fpit;
int pgit = 0;
int pgn = PAGING_PGN(addr);
ret_rg->rg_start = addr;
ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;
/* Tracking for later page replacement activities (if needed)
* Enqueue new usage page */
// enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);
struct framephy_struct *fpit = frames;
//for (pgit = 0; pgit < pgnum; pgit++) {
for (pgit = pgnum - 1 ; pgit >= 0; pgit--) {
if (fpit == NULL) break; // Tránh lỗi nếu hết frame trước khi ánh xạ xong

// Lưu thông tin vào bảng trang
uint32_t *pte = &caller->mm->pgd[pgn + pgit]; 
pte_set_fpn(pte, fpit->fpn); // Thiết lập số hiệu khung trang vào PTE

// Ghi nhận trang vào danh sách FIFO (dùng cho thay thế trang sau này)
enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);

addr_t curr_vaddr = addr + pgit * PAGING_PAGESZ;
add_page_table_entry(caller->page_table, pgn + pgit, fpit->fpn, curr_vaddr);
// Chuyển đến frame tiếp theo
fpit = fpit->fp_next;
}
return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

 /* Hàm giải phóng danh sách các trang đã cấp phát */
void free_allocated_frames(struct framephy_struct **frm_lst)
{
    struct framephy_struct *temp;
    while (*frm_lst)
    {
        temp = *frm_lst;
        *frm_lst = (*frm_lst)->fp_next;
        free(temp);
    }
}

int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst) //
{

    int allocated_pages = 0; // Số trang đã cấp phát
    struct framephy_struct *newfp_str = NULL, *last = NULL;

    for (int pgit = 0; pgit < req_pgnum; pgit++)
    {
        int fpn; // ID khung trang mới cấp phát

        /* Thử lấy khung trang trống trong RAM */
        if (MEMPHY_get_freefp(caller->mram, &fpn) != 0)
        {
          free_allocated_frames(frm_lst);
          return -3000;
        }

        /* Cấp phát trang mới */
        newfp_str = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        if (!newfp_str)
        {
            free_allocated_frames(frm_lst);
            return -3000; // Lỗi: Không đủ bộ nhớ cấp phát
        }

        /* Gán thông tin trang mới */
        newfp_str->fpn = fpn;
        newfp_str->fp_next = NULL;
        newfp_str->owner = caller->mm;

        /* Thêm trang vào danh sách cấp phát */
        if (*frm_lst == NULL)
            *frm_lst = newfp_str;
        else
            last->fp_next = newfp_str;

        last = newfp_str;
        allocated_pages++;

        /* Cập nhật danh sách used_fp_list*/
        newfp_str->fp_next = caller->mram->used_fp_list;
        caller->mram->used_fp_list = newfp_str;
    }
    return allocated_pages; // Trả về số trang đã cấp phát
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
int vm_map_ram(struct pcb_t *caller, int astart, int aend, int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  int ret_alloc;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
#ifdef MMDBG
    printf("OOM: vm_map_ram out of memory \n");
#endif
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
                   struct memphy_struct *mpdst, int dstfpn)
{
  int cellidx;
  int addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  mm->pgd = calloc(PAGING_MAX_PGN, sizeof(uint32_t));

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /*update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /*update mmap */
  mm->mmap = vma0;

  return 0;
}

struct vm_rg_struct *init_vm_rg(int rg_start, int rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, int pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[%d]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[%ld->%ld]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[%ld->%ld]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[%d]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, uint32_t start, uint32_t end)
{
  int pgn_start, pgn_end;
  int pgit;

  if (end == -1)
  {
    pgn_start = 0;
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
    end = cur_vma->vm_end;
  }
  pgn_start = PAGING_PGN(start);
  pgn_end = PAGING_PGN(end);

  printf("print_pgtbl: %d - %d", start, end);
  if (caller == NULL) { printf("NULL caller\n"); return -1;}
  printf("\n");

  for (pgit = pgn_start; pgit < pgn_end; pgit++)
  {
    printf("%08ld: %08x\n", pgit * sizeof(uint32_t), caller->mm->pgd[pgit]);
  }

  return 0;
}

// #endif
