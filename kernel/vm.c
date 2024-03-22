#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"


/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void kvm_map_pagetable(pagetable_t pgtbl) {
    // 将各种内核需要的 direct mapping 添加到页表 pgtbl 中。

    // uart registers
    kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // CLINT
    // kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

    // PLIC
    kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // map kernel text executable and read-only.
    kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}


pagetable_t kvminit_newpgtbl() {
    pagetable_t pgtbl = (pagetable_t)kalloc();
    memset(pgtbl, 0, PGSIZE);

    kvm_map_pagetable(pgtbl);

    return pgtbl;
}

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
    kernel_pagetable = kvminit_newpgtbl();
    // 仍然需要有全局的内核页表，用于内核 boot 过程，
    // 以及无进程在运行时使用。

    // CLINT *is* however required during kernel boot up and
    // we should map it for the global kernel pagetable
    kvmmap(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t*
walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {

        // pte指向某一级page table中，va对应的那条entry的地址
        pte_t* pte = &pagetable[PX(level, va)];

        // 若该entry对应的page table已经存在
        // 则直接把pagetable指过去
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        }

        // 若不存在则立刻把这个page table创建出来
        else {
            if (!alloc || (pagetable = (pde_t*)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }

    // 返回最低级的page table中，对应entry的地址
    // 所以注意啊，我们不是直接用walk()的返回值pte
    // 还要加一层pa = PTE2PA(*pte)转换
    // 才能得到真实的物理地址
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t* pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

// 将某个逻辑地址映射到某个物理地址（添加第一个参数 pgtbl）
void
kvmmap(pagetable_t pgtlb, uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(pgtlb, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// kvmpa 将内核逻辑地址转换为物理地址（添加第一个参数 kernelpgtbl）
uint64
kvmpa(pagetable_t pgtbl, uint64 va)
{
    uint64 off = va % PGSIZE;
    pte_t* pte;
    uint64 pa;

    pte = walk(pgtbl, va, 0);
    if (pte == 0)
        panic("kvmpa");
    if ((*pte & PTE_V) == 0)
        panic("kvmpa");
    pa = PTE2PA(*pte);
    return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t* pte;

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
            panic("remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
    uint64 a;
    pte_t* pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if ((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void*)pa);
        }
        *pte = 0;
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar* src, uint sz)
{
    char* mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
    memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    char* mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE) {
        mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];

        // 如果这条PTE有效且不可读不可写不可执行（即不为叶子节点）
        // 就说明这条PTE指向更低等级的page table
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            uint64 child = PTE2PA(pte); // 把PTE转换成PA，这个PA就是下一级的page table的地址
            freewalk((pagetable_t)child);
            pagetable[i] = 0; // 把某个PTE全设为0的时候，其flags也全为0了
        }
        else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t* pte;
    uint64 pa, i;
    uint flags;
    char* mem;

    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if ((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char*)pa, PGSIZE);
        if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
            kfree(mem);
            goto err;
        }
    }
    return 0;

err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
    pte_t* pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char* src, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void*)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len)
{
    return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char* dst, uint64 srcva, uint64 max)
{
    return copyinstr_new(pagetable, dst, srcva, max);
}

void print_pagetable_info(pagetable_t pagetable, int depth) {
    // 参考freewalk()
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];

        if (pte & PTE_V) {
            printf("..");
            for (int j = 0; j < depth; j++) {
                printf(" ..");
            }
            uint64 child = PTE2PA(pte);
            printf("%d: pte %p pa %p\n", i, pte, child);

            // 如果不为叶子节点
            if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
                print_pagetable_info((pagetable_t)child, depth + 1);
            }
        }
    }
}

void vmprint(pagetable_t pagetable) {
    printf("page table %p\n", pagetable);
    return print_pagetable_info(pagetable, 0);
}

// 递归释放一个内核页表中的所有 mapping，但是不释放其指向的物理页
// 修改自freewalk()
void kvm_free_kernelpgtbl(pagetable_t pagetable) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        uint64 child = PTE2PA(pte);
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) { // 如果该页表项指向更低一级的页表
            // 递归释放低一级页表及其页表项
            kvm_free_kernelpgtbl((pagetable_t)child);
            pagetable[i] = 0;
        }
    }
    kfree((void*)pagetable); // 释放当前级别页表所占用空间
}

// 将 src 页表的一部分页映射关系拷贝到 dst 页表中。
// 只拷贝页表项，不拷贝实际的物理页内存。
// 成功返回0，失败返回 -1
int kvm_copy_mappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz) {
    pte_t* pte;
    uint64 pa, i;
    uint flags;

    // PGROUNDUP: prevent re-mapping already mapped pages 
    // (eg. when doing growproc)
    for (i = PGROUNDUP(start); i < start + sz; i += PGSIZE) {
        if ((pte = walk(src, i, 0)) == 0) {
            panic("kvm_copy_mappings: pte not exist!");
        }
        if ((*pte & PTE_V) == 0) {
            panic("kvm_copy_mappings: page not exist!");
        }
        pa = PTE2PA(*pte);

        // `& ~PTE_U` 表示将该页的权限设置为非用户页
        // 必须设置该权限，RISC-V 中内核是无法直接访问用户页的。
        flags = PTE_FLAGS(*pte);
        if (mappages(dst, i, PGSIZE, pa, flags & ~PTE_U) != 0) {
            goto error;
        }
    }
    return 0;
error:
    uvmunmap(dst, PGROUNDUP(start), (i - PGROUNDUP(start)) / PGSIZE, 0);
    return -1;
}

// 与 uvmdealloc 功能类似，
// 将程序内存从 oldsz 缩减到 newsz。
// 但区别在于不释放实际内存
// 用于内核页表内程序内存映射与用户页表程序内存映射之间的同步
uint64 kvm_dealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz) {
        return oldsz;
    }

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
    }
    return newsz;
}
