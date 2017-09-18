# 物理内存
因内核连接地址在高处，为执行内核，entry.S中先粗略映射，将虚拟地址[KERNBASE,KERNBASE+4mb)映射物理地址到[0,4mb)。

为管理物理内存，需要先实现物理页框相关的init，alloc,free功能，JOS中以`struct PageInfo`代表一个物理页。

## Exercise 1
> In the file kern/pmap.c, you must implement code for the following functions (probably in the order given).

`boot_alloc()`：字节单位申请内存，page_alloc()实现后便不再用。
`mem_init()`：内存功能初始化，只负责内核地址部分。
`page_init()`：初始化物理页框表。
`page_alloc()` `page_free()`：物理页分配（从最顶端顺序开始）、释放，此处实现简单（可用空闲链表）。此处还无swap机制，所有申请数据结构、加载文件都在蚕食仅剩的物理内存。

# 虚拟内存
![](index_files/188326234.png)
x86页目录\表项如图，硬件通过cr3寄存器中的页目录表根地址，检索页表，硬件自动检查后面的状态位。其中（PAGE FRAME ADDRESS)存的地址是物理地址。

U/S、R/W目标页权限设置，当前权限CPL则在段寄存器中。

JOS中将KERNBASE(0xf0000000，其中内核加载于KERNBASE+1MB)以上的256MB内核虚拟空间映射到物理地址0开始处.

4G虚拟地址的分布见inc/memlayout.h。
## Exercise 4
> In the file kern/pmap.c, you must implement code for the following functions.

`pgdir_walk()`：根据页目录表和虚拟地址，返回页表项
`boot_map_region()`：[va,va+size)映射到[pa,pa+size)
`page_lookup()`：根据虚拟地址返回物理页框
`page_remove()`：取消某虚拟地址到物理页框的映射
`page_insert()`：将虚拟地址映射到物理页框

## Exercise 5
> Fill in the missing code in mem_init() after the call to check_page().ill in the missing code in `mem_init()` after the call to `check_page()`.

此处对几处内核虚拟地址进行映射:

让user能读物理页框表。
```
boot_map_region(kern_pgdir, UPAGES, n, PADDR(pages), PTE_U);
```
内核栈溢出时触发页错误。
```
boot_map_region(kern_pgdir, KSTACKTOP - KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);
```
内核空间映射到物理地址底部，因为之前开启页保护CR0_WP，所以supervisor权限下依然必须设置PTE_W才能写。
```
boot_map_region(kern_pgdir, KERNBASE, 0x0u - KERNBASE, 0, PTE_W);
```

## Question
> We have placed the kernel and user environment in the same address space. Why will user programs not be able to read or write the kernel's memory? What specific mechanisms protect the kernel memory?

因为页表项中U/S,R/W位的保护作用。

> What is the maximum amount of physical memory that this operating system can support? Why?

PageInfo大小8KB，UPAGES上的PTSIZE空间内能放PTSIZE/8个，对应 PTSIZE/sizeof(PageInfo) * 4K 大小的物理空间（2G）。

> How much space overhead is there for managing memory, if we actually had the maximum amount of physical memory? How is this overhead broken down?

物理空间2G时，要4MB的PageInfo表，4K页目录，2MB页表.

> At what point do we transition to running at an EIP above KERNBASE? What makes it possible for us to continue executing at a low EIP between when we enable paging and when we begin running at an EIP above KERNBASE? Why is this transition necessary?

EIP goes up after `jmp *%eax`. Because entry_pgdir also map va[0,4mb) to pa[0,4mb).
