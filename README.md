# Part A:多处理器支持与协同多任务

## 多处理器
BSP（bootstrap processor)负责初始化及启动操作系统，操作系统启动后BSP激活APs（application processors）。

SMP中，每个核心都有一个LAPIC（local APIC），负责传递中断以及给核心提供标识符。Lab中（kern/lapic.c）用到如下功能：
* cpunum()：读APIC ID，得知运行当前代码的CPU
* lapic_startup()：BSP启动APs
* apic_init()：用LAPIC计时器实现调度

LAPIC的操作被映射在内存MMIOBASE处。

### Exercise 1
> Implement `mmio_map_region` in <tt>kern/pmap.c</tt>.

用`boot_map_region()`将设备的物理地址映射到虚拟内存的 MMIO 区域。

## 启动AP

1. mp_init() 首先获取CPU信息
2. boot_aps() 将AP的入口代码（kern/mpentry.S）复制到物理地址 MPENTRY_PADDR处，之后通过 STARTUP 信号逐个激活AP。
3. AP执行入口代码后进入保护模式，开启分页。调用 mp_main() 设置 C Runtime，。
4. boot_aps() 待AP发送 CPU_STARTED信号，并存入struct CpuInfo 后启动下一个AP。

### Exercise 2
> Then modify your implementation of `page_init()` in <tt>kern/pmap.c</tt> to avoid adding the page at `MPENTRY_PADDR` to the free list

在page_init()中，在物理地址MPENTRY_PADDR处留一页，给AP entry code。所有APs最初都会执行此处代码。

### Question
> What is the purpose of macro `MPBOOTPHYS`? Why is it necessary in <tt>kern/mpentry.S</tt> but not in <tt>boot/boot.S</tt>? In other words, what could go wrong if it were omitted in <tt>kern/mpentry.S</tt>?

宏MPBOOTPHYS(s) 算出内核的mpentry.S中的数据被复制到低地址MPENTRY_PADDR后对应的地址。
在boot/boot.S中链接地址和运行地址都在低地址处，所以不需要。
省略后实模式的AP读不到高地址，运行失败。

## CPU状态与初始化

CPU核心状态分为私有状态（per-cpu，定义在kern/cpu.h）和全局状态（global）。

此处需要注意的私有状态：
* **Per-CPU kernel stack：**因为各核心可能同时进入内核态，因此每个核心需要一个专属的内核栈 `percpu_kstacks[NCPU][KSTKSIZE]`。

* **Per-CPU TSS and TSS descriptor:**每个核心 *i* 存各自内核栈地址的TSS`cpus[i].cpu_ts`。

* **Per-CPU current environment pointer:**`thiscpu->cpu_env`是该CPU正在运行的用户进程。

* **Per-CPU system registers:**每个核心的寄存器都是独立的，因此初始化操作需要对各核心执行一次，如`env_init_percpu()`和`trap_init_percpu()`。

### Exercise 3
> Modify `mem_init_mp()` (in <tt>kern/pmap.c</tt>) to map per-CPU stacks starting at `KSTACKTOP`, as shown in <tt>inc/memlayout.h</tt>.

内核虚拟内存的页表中，为每个核心都设置一个栈。栈间有未映射的空间，用page fault防止栈溢出。所有CPU都使用该内核页表。

### Exercise 4
> The code in `trap_init_percpu()` (<tt>kern/trap.c</tt>) initializes the TSS and TSS descriptor for the BSP.

根据CPU号设置其TSS。每个CPU都会运行该CPU程序。

## 锁
JOS的内核空间由各进程共享，且不可抢占。故同时只能有一个CPU（进程）进入内核态，必须用锁（此处是大内核锁）对内核进行保护。

### Exercise 5
> Apply the big kernel lock as described above, by calling `lock_kernel()` and `unlock_kernel()` at the proper locations.

按报告提到的位置，在进入内核态上锁，回到用户态处（`env_run()`）解锁。

### Question
> It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.

中断发生时，在进入有加锁的trap()之前已执`trapentry.S`中压入中断号的操作，如果共用栈可能会错乱。

## 轮询调度
JOS调度函数`sched_yield()`轮询进程数组`envs[]`进行调度。查找第一个状态为`ENV_RUNNABLE`的进程并调用`env_run()`执行。

`sched_yield()`不能启动正在其他CPU运行的用户进程（状态为ENV_RUNNING)。

`sched_yield()`可加入系统调用，这样用户进程能主动休眠。
### Exercise 6
> Implement round-robin scheduling in `sched_yield()` as described above. Don't forget to modify `syscall()` to dispatch `sys_yield()`.

`sched_yield()`中实现按进程数组的顺序，在当前CPU上对进程调度。
内核的`syscall()`增加对sched_yield()的系统调用支持。
（此处调度是用户程序主动通过系统调用，放弃CPU）。
### Question
> In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch?

问为什么经过lcr3()载入不同页目录后，变量e还是同一个。
因为用户进程的页目录表是按内核空间页目录改的，在内核部分是一样的。
> Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?

保存寄存器的动作在`trapentry.S`的`_alltraps`中，保存于内核栈中，借指针`*tf`在各内核函数中传递。
恢复在`env.c`的`env_pop_tf()`中。

### Exercise 7
> Implement the system calls described above in <tt>kern/syscall.c</tt>.

`sys_exofork()`：
只创建子进程，复制寄存器，但用户态空间还不可用。用户态sys_exofork()在 lib/lib.h中。
内核sys_fork()中，待恢复的寄存器映像`struct Trapframe *tf`的eax寄存器被设为0。**父进程中**，调用链返回时，在 *trap_dispatch()* 中，映像tf的eax被更改为内核sys_fork()返回值，并随env_run()使回到用户态时eax寄存器为先前的返回值。而用户态sys_exofork()正好将eax寄存器值作为返回值。然而**子进程中**，并无从内核开始的调用链返回，而是等待 sched_yield()中的env_run()直接执行，因此返回值直接就是在内核sys_exofork()中设为0的eax值。

`sys_env_set_status`,`sys_page_alloc`,`sys_page_map`,`sys_page_unmap`等也作为系统调用，完善dumbfork()的功能，实现简单。

# Part B:写时复制
子进程使用和父进程一样的地址映射，并将页都设为只读。当程序试图对页写时，触发page fault。

在页错误处理函数中，再对子进程出现页错误的地址分配私有、可写的页。
## 用户级页错误处理函数
JOS中页错误处理函数位于用户空间而非内核中，如此以增加灵活性能让程序自己不同区域的处理方式。
## 设置页处理函数
用户进程通过系统调用`sys_env_set_pgfault_upcall()`，将自定义的处理函数注册到其`Env`结构上的`env_pgfault_upcall`。
### Exercise 8
> Implement the `sys_env_set_pgfault_upcall` system call。

在`kern/syscall.c`中的系统调用的内核函数。用来挂载用户态的页错误处理函数。
## 用户进程的普通栈和异常栈
进程在处理页错误时，使用开始于 UXSTACKTOP 的异常栈。

发生页错误时，内核先让进程切换到异常栈，然后在用户态运行处理函数，最后通过一段汇编代码回到原出错处继续运行。
## 调用用户态页处理函数
页错误时内核会在异常栈压入`struct UTrapframe`保存信息。
### Exercise 9
> Implement the code in `page_fault_handler` in <tt>kern/trap.c</tt> required to dispatch page faults to the user-mode handler. Be sure to take appropriate precautions when writing into the exception stack.

此处帮用户进程切换栈，压入保存的数据，并启动用户态处理函数。如果异常栈溢出着直接结束程序。
### Exercise 10
> Implement the `_pgfault_upcall` routine in <tt>lib/pfentry.S</tt>. The interesting part is returning to the original point in the user code that caused the page fault.

这段汇编代码就是用户处理函数，先调用具体的处理内容`_pgfault_handler`，再通过汇编回到原出错处以继续执行。
### Exercise 11
> Finish `set_pgfault_handler()` in <tt>lib/pgfault.c</tt>.

用户态，通过系统调用注册函数。并在第一次时给异常栈申请空间。
## 实现写时复制的fork
fork用到的两个子函数：

* **pgfault：**用户态页错误处理函数的主体，复制出现page fault的只读页到新建的可写页，并重新映射。
* **duppage:**复制自己的映射给子进程，并把自己的页面也设为COW、只读（此举为防止子进程的数据会随父进程而变）。

父进程fork中需帮子进程设置处理函数等信息；子进程的fork中需自行注册上处理函数的主体`pgfault()`。
# Part C：可抢占多进程与IPC

## 时钟中断与可抢占
用时钟中断实现可抢占。JOS中外部中断位于idt的32-47项。

JOS中内核态禁止所有外部中断（SETGATE中的istrap项控制），但在用户态中允许开启。
### Exercise 13
> Modify <tt>kern/trapentry.S</tt> and <tt>kern/trap.c</tt> to initialize the appropriate entries in the IDT and provide handlers for IRQs 0 through 15\. Then modify the code in `env_alloc()` in <tt>kern/env.c</tt> to ensure that user environments are always run with interrupts enabled.

添加中断项，并在用户进程开启外部中断。

## IPC
JOS中IPC由系统调用`sys_ipc_recv`和`sys_ipc_try_send`及其包装函数`ipc_recv`，`ipc_send`实现。

传递内容为一个32位数、一个内存页（可选）。传递内存页时，接收者将其的 dstva 处页映射到发送者的 srcva 对应的物理页上。

### Exercise 15
> Implement `sys_ipc_recv` and `sys_ipc_try_send` in <tt>kern/syscall.c</tt>.

当srcva 和 dstva 皆小于UTOP才再传输/共享页，否则只传输一个int32。包括int32等多个IPC相关信息都存在进程结构Env中。

`sys_ipc_recv`：只负责让接收者进入阻塞态的接收，设置 dstva。
`sys_ipc_try_send`：向接收者传递int32值及共享页（用page_insert插入到接收者）。
