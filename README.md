JOS中用inc/env.h中的结构`struct Env`表示用户进程。

# Part A：用户进程与异常处理
mem_init()中申请`struct Env`数组，存放后续进程结构。

x86中，idt表项0-31属于同步的异常，而32-255属于异步的中断。发生时处理器会自动将一些寄存器信息压入内核栈。

## Exercise 1
> Modify `mem_init()` in <tt>kern/pmap.c</tt> to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in <tt>inc/memlayout.h</tt>) so user processes can read from this array.

申请Env表为存储后续所有用户进程信息。
```
en = ROUNDUP(NENV * sizeof(struct Env), PGSIZE);
envs = (struct Env *)boot_alloc(en);
```
映射使用户空间可读。
```
boot_map_region(kern_pgdir, UENVS, en, PADDR(envs), PTE_U | PTE_P);
```

## Exercise 2
> Exercise 2. In the file env.c, finish coding the following functions.

`env_init()`：初始化Env表
`env_setup_vm()`：为新进程设置好其自己页目录表（虚拟内存），映射到UVPT方便用户进程读。
`region_alloc()`：为新用户进程分配物理空间并映射到其虚拟地址上。
`load_icode()`：将被链接进内核的raw ELF，按其每段指定的用户虚拟地址，加载到新进程的虚拟空间。
`env_create()`：创建新进程并载入到内存
`env_run()`：让一个进程进入RUNNING状态

## Exercise 4
> Edit trapentry.S and trap.c and implement the features described above.

异常/中断需要处理函数，此处设置好各处理函数（但实际处理方法在trao_dispatch()中），栈中压入处理时需要的一些信息，其结构等同结构体Trapframe。

在 `trapentry.S`中：
* 用宏定义各异常的处理函数
* _alltraps中保存一些信息，为给转跳到trap()进一步处理

在`trap.c`的trap_init()中：
* 初始化 idt[]表，为后面lidt指令载入

> Q：What is the purpose of having an individual handler function for each exception/interrupt?

可以压入不同的中断号，来调用不同的处理函数。

> Q：The grade script expects it to produce a general protection fault (trap 13), but softint's code says int 14. Why should this produce interrupt vector 13? What happens if the kernel actually allows softint's int 14 instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

中断14特权级被SETGATE设为0（内核），所以用户态调用会触发保护。如果用户态可调用，则恶意程序一直调用该中断，可能会不断消耗可用内存页。

# Part B：页错误、断点异常、系统调用

**断点异常：**x86中为int 3，调试器断点的实现是将对应指令替换成`int 3`软中断实现暂停。

**系统调用：**JOS中系统调用软中断为`int $0x30`。用户API`lib/syscall.c`用int 0x30进入中断，且通过寄存器将调用号（eax)和最多五个的函数参数（edx,ecx,ebx,edi,esi）传递给内核，同时内核也用eax返回返回值。

**用户态startup 和 exit：**编译器作用下，用户进程开始于`lib/entry.S`，部分初始化完毕后进入libmain()，其中正式调用程序主函数，结束后exit()对用户进程收尾。

**Page fault：**当访问不存在、没有权限的页时会出现页错误，根据相应处理函数进行修复，无法修复则终止程序。如果内核出现页错误则是bug，需直接panic。

**内存保护：**系统调用可传指针给内核进行读写，内核针对指针需要识别两种情况：
* 访问中发生页错误时，能认得这不是内核空间的内存出现页错误，不需要panic。
* 避免用户进程破坏、窃取内核数据，禁止用户传递指向内核空间的指针。

## Exercise 5
> Modify `trap_dispatch()` to dispatch page fault exceptions to `page_fault_handler()`.

trap_dispatch()中根据压入的中断/异常号，运行实际的处理函数。

## Exercise 6
> Modify `trap_dispatch()` to make breakpoint exceptions invoke the kernel monitor.

调试器断点的实现是将对应指令替换成`int 3`软中断。此处是将kernel monitor（依靠其停顿等待输入的性质），作伪系统调用实现int 3（需要修改SETGATE中的权限）。

## Challenge
> Modify the JOS kernel monitor so that you can 'continue' execution from the current location.

monitor中添加两个命令作为continue和stepi，通过操作EFLAGS的TF寄存器控制

## Exercise 7
> Add a handler in the kernel for interrupt vector `T_SYSCALL`.

在`kern/trapentry.S`和`kern/trap.c`的`trap_init()`中添加0x30项。

遵守寄存器传递参数的规则，在`trap_dispatch()`中添加T_SYSCALL(0x30)项调用内核的syscall（kern/syscall.c)。

在kern/syscall.c的syscall中根据中断号、参数调用具体的系统函数。

## Exercise 8
> Add the required code to the user library, then boot your kernel.

libmain()中thisenv设置为当前的envid，此处为user/hello使用。

## Exercise 9
> Change `kern/trap.c` to panic if a page fault happens in kernel mode.

page_fault_handler()检查tf_cs的RPL位，如果是内核模式则panic。

> Read `user_mem_assert` in <tt>kern/pmap.c</tt> and implement `user_mem_check` in that same file.

user_mem_check()按页检查该虚拟空间是否属于用户态。防止用户进程向内核传递指向内核空间的指针。

> Change <tt>kern/syscall.c</tt> to sanity check arguments to system calls.

对含指针参数的系统调用，需用user_mem_assert()检查指针是否包含内核空间。

> you should be able to run backtrace from the kernel monitor and see the backtrace traverse into lib/libmain.c before the kernel panics with a page fault. What causes this page fault?

追踪到用户栈进行访问args时，超出用户栈的USTACKTOP，访问到Empty Memory区导致page fault
