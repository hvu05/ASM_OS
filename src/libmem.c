/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t read_addr;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */

int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  /*Allocate at the toproof */
  struct vm_rg_struct rgnode;

  struct vm_area_struct *vm_area = get_vma_by_num(caller->mm, vmaid);  // Lấy VMA ứng với vmaid
  //
  
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
 struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = size;
  __sys_memmap(caller, &regs);  // Increase the VMA limit
 if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  pthread_mutex_unlock(&mmvm_lock);
  return -1;
}


/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid) {
  // Kiểm tra hợp lệ của rgid
  pthread_mutex_lock(&mmvm_lock);
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;  // rgid không hợp lệ
  }

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!cur_vma) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;  // Không tìm thấy VMA
  }

  struct vm_rg_struct *rg_elmt = &caller->mm->symrgtbl[rgid];  // Vùng nhớ ảo cần giải phóng
  if (!rg_elmt) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;  // Không tìm thấy vùng nhớ ảo
  }


  // Bước 2: Thêm vùng nhớ vào danh sách vùng nhớ trống của VMA
  struct vm_rg_struct *rg_free = malloc(sizeof(struct vm_rg_struct));
  rg_free->rg_start = rg_elmt->rg_start;
  rg_free->rg_end = rg_elmt->rg_end;
  rg_free->rg_next = cur_vma->vm_freerg_list;
  cur_vma->vm_freerg_list = rg_free;

  // Bước 3: Xóa thông tin của vùng nhớ ảo (do đã giải phóng)
  rg_elmt->rg_start = 0;
  rg_elmt->rg_end = 0;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;  // Thành công
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int pte_is_valid(uint32_t pte) {
  return (pte >> 31) & 0x1;
}

 void print_page_frame_mapping(uint32_t *pgd) {
  for (int i = 0; i < PAGING_MAX_PGN; i++) {
      uint32_t pte = pgd[i];
      if (pte_is_valid(pte)) {
          int frame_number = PAGING_FPN(pte);
          printf("Page Number: %d -> Frame Number: %d\n", i, frame_number);
      }
  }
}
int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  int addr;
  int result = __alloc(proc, 0, reg_index, size, &addr);
  /* By default using vmaid = 0 */
  #ifdef IODUMP
  printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
  printf("PID=%d - Region=%d - Address=%08X - Size=%d byte\n", proc->pid , reg_index, addr, size);
  #ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
  #endif
  print_page_frame_mapping(proc->mm->pgd);
  printf("================================================================\n");
  #endif

  return result;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  /* By default using vmaid = 0 */
  #ifdef IODUMP
  printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
  printf("PID=%d - Region=%d\n", proc->pid , reg_index);
  #ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
  #endif
  print_page_frame_mapping(proc->mm->pgd);
  printf("================================================================\n");
  #endif

  return __free(proc, 0, reg_index);
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */

int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller) {
  //pthread_mutex_lock(&mmvm_lock);
  uint32_t pte = mm->pgd[pgn];

  if (!PAGING_PAGE_PRESENT(pte)) {
      int vicpgn, swpfpn;
      
      // Tìm victim page trong RAM
      find_victim_page(caller->mm, &vicpgn);
      // Lấy khung trống trong swap
      MEMPHY_get_freefp(caller->active_mswp, &swpfpn);

      // Lấy frame tương ứng và cập nhật lại danh sách used_fp_list
      int vicfpn = PAGING_FPN(caller->mm->pgd[vicpgn]);

      // Lấy frame chứa trang cần lấy từ swap
      int tgtfpn = PAGING_PTE_SWP(pte);

      // Hoán đổi trang: SYSCALL 17
      struct sc_regs regs;

      // Swap out victim page: RAM -> Swap
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = vicfpn;
      regs.a3 = swpfpn;
      __sys_memmap(caller, &regs);

      // Swap in target page: Swap -> RAM
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = tgtfpn;
      regs.a3 = vicfpn;
      __sys_memmap(caller, &regs);

      // Swap in temp page: Swap -> Swap
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = swpfpn;
      regs.a3 = tgtfpn;
      __sys_memmap(caller, &regs);

      //Tạo và thêm lại framephy_struct swpfpn vào free_list
      struct framephy_struct *swpframe = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
      swpframe->fpn = swpfpn;
      swpframe->owner = NULL;
      swpframe->fp_next = caller->active_mswp->free_fp_list;
      caller->active_mswp->free_fp_list = swpframe;

      // Cập nhật lại page table
      pte_set_swap(&mm->pgd[vicpgn], caller->active_mswp_id ,tgtfpn); // Đánh dấu victim đã bị swapped
      pte_set_fpn(&mm->pgd[pgn], vicfpn);     // Đánh dấu trang mới đã được load vào RAM

      // Thêm trang vào FIFO
      enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(mm->pgd[pgn]);
  //pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
    pthread_mutex_lock(&mmvm_lock);
    int pgn = PAGING_PGN(addr);  // Lấy chỉ mục trang ảo từ địa chỉ ảo
    int fpn;

    /* Lấy trang từ bộ nhớ, hoán đổi nếu cần thiết */
    if (pg_getpage(mm, pgn, &fpn, caller) != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;  // Truy cập trang không hợp lệ
    }

    /* Tính toán địa chỉ vật lý từ fpn (khung bộ nhớ vật lý) và offset */
    int off = PAGING_OFFST(addr);  // Tính toán offset trong trang
    int phyaddr = (fpn * PAGING_PAGESZ) + off;  // Tính địa chỉ vật lý

    //printf("[ADDR] pgn=%d fpn=%d off=%d phyaddr=%d\n", pgn, fpn, off, phyaddr);
    /* Đọc dữ liệu từ bộ nhớ vật lý */
    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_READ;  // Mã lệnh yêu cầu đọc bộ nhớ
    regs.a2 = phyaddr;  // Địa chỉ vật lý cần đọc
    regs.a3 = data;  // Địa chỉ bộ nhớ đích (chỗ lưu dữ liệu)
    __sys_memmap(caller, &regs);  // Gọi syscall 17 để thực hiện thao tác I/O

    // Sau khi syscall, giá trị đã được lưu vào regs.a3, giờ chúng ta lưu vào `data`
    *data = (BYTE)regs.a3;  // Cập nhật dữ liệu vào biến data
    read_addr = addr;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;  // Thành công
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);  // Địa chỉ offset trong trang
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0){
    pthread_mutex_unlock(&mmvm_lock);
    return -1; // Trang không có sẵn hoặc lỗi truy cập
  }
    

  int phyaddr = (fpn * PAGING_PAGESZ) + off;  // Tính địa chỉ vật lý

  // SYSCALL ghi bộ nhớ vật lý
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;   // Lệnh ghi
  regs.a2 = phyaddr;           // Địa chỉ vật lý cần ghi
  regs.a3 = value;             // Giá trị cần ghi

  __sys_memmap(caller, &regs);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    uint32_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{

  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  if (val != 0) {
    // Nếu có lỗi khi đọc, trả về giá trị lỗi
    return val;
  }
  // Nếu không có lỗi, cập nhật kết quả vào destination
  *destination = (uint32_t)data;
#ifdef IODUMP
  printf("===== PHYSICAL MEMORY AFTER READING =====\n");
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  print_page_frame_mapping(proc->mm->pgd);
  printf("================================================================\n");
  MEMPHY_dump(proc->mram);
#endif
  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    uint32_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
#ifdef IODUMP
  printf("===== PHYSICAL MEMORY AFTER WRITING =====\n");
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  print_page_frame_mapping(proc->mm->pgd);
  printf("================================================================\n");
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_PTE_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn);
    } else {
      fpn = PAGING_PTE_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn);    
    }
  }

  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, int *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  // Kiểm tra nếu FIFO rỗng
  if (!pg) {
    return -1; // Không tìm thấy trang
  }

  // Duyệt qua danh sách để tìm phần tử cuối cùng
  struct pgn_t *prev = NULL;
  while (pg->pg_next != NULL) {
    prev = pg;
    pg = pg->pg_next;
  }

  // Lấy chỉ mục trang ảo (virtual page number) của phần tử cuối cùng
  *retpgn = pg->pgn;

  // Nếu có phần tử trước đó, điều chỉnh lại con trỏ pg_next
  if (prev != NULL) {
    prev->pg_next = NULL; // Loại bỏ phần tử cuối cùng khỏi danh sách FIFO
  } else {
    // Nếu chỉ có một phần tử trong danh sách FIFO
    mm->fifo_pgn = NULL;
  }

  // Giải phóng bộ nhớ của phần tử cuối cùng
  free(pg);

  return 0; // Thành công
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  struct vm_rg_struct *prev = NULL;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;
  while (rgit != NULL) {
    if ((rgit->rg_end - rgit->rg_start) >= size) {  // Kiểm tra vùng nhớ có đủ chỗ
        newrg->rg_start = rgit->rg_start;  // Cập nhật địa chỉ bắt đầu
        newrg->rg_end = rgit->rg_start + size;  // Cập nhật địa chỉ kết thúc
        rgit->rg_start += size;  // Cập nhật lại vùng nhớ trống còn lại
        /* Nếu vùng trống bị cấp phát hoàn toàn, loại bỏ nó khỏi danh sách */
        if (rgit->rg_start == rgit->rg_end) {
          if (prev == NULL) {  // Nếu vùng đầu tiên bị xóa
              cur_vma->vm_freerg_list = rgit->rg_next;
          } else {
              prev->rg_next = rgit->rg_next;
          }
          free(rgit);
      }
        return 0;  // Tìm thấy vùng trống phù hợp
    }
    prev = rgit;
    rgit = rgit->rg_next;  // Chuyển đến phần tử tiếp theo trong danh sách
  }

  return -1;  // Không tìm thấy vùng trống phù hợp
}

//#endif
