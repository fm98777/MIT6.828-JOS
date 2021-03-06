// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Stack backtrace", mon_backtrace},
	{ "showmappings", "Display mapping info. Input:\"showmappings 0x400 0xf00c\"", mon_showmappings},
	{ "continue", "Continue in debug", mon_continue},
	{ "stepi", "Single-step in debug", mon_stepi}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

/* Continue in debug */
int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if (argc > 1) {
		cprintf("Should be no arguments\n");
		return 1;
	}
	if (!tf) {
		cprintf("continue error: Trapframe is null\n");
		return 1;
	}	
	tf->tf_eflags &= ~FL_TF;
	//tf->tf_eflags |= FL_RF;
	return -1;	// this cause monitor exit, which end breakpoint 
}

/* Single-step in debug, breakpoint after each instruction */
int
mon_stepi(int argc, char **argv, struct Trapframe *tf)
{
	if (argc > 1) {
		cprintf("Should be no arguments\n");
		return 1;
	}
	if (!tf) {
		cprintf("continue error: Trapframe is null\n");
		return 1;
	}
	tf->tf_eflags |= FL_TF;
	return -1;
}

/* Input virtual address to show physical address and perm bits */
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	int i, j, len;
	char c;
	uintptr_t addr[8] = {0};

	if (argc < 2 || argc > 9) {
		goto show_mappings_error;
	}
	for (i = 1; i < argc; i++) {
		len = strlen(argv[i]);
		if (len < 3 || len > 10 || argv[i][0] != '0' || argv[i][1] != 'x') {
			goto show_mappings_error;
		}
		for (j = len - 1; j > 1; j--) {
			c = argv[i][j];
			if (c <= '9' && c >= '0') {
				addr[i - 1] += (c - '0') << (4 *  (len - j - 1));
			} else if (c <= 'f' && c >= 'a') {
				addr[i - 1] += (c - 87) << (4 * (len - j - 1));
			} else {
				goto show_mappings_error;
			}
		}
	}
	show_mappings(argc - 1, addr);
	return 0;
show_mappings_error:
		cprintf("Don't input  more than 8 32-bit hex virtual addresses\n");
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t eip, ebp, arg;
	int i;
	struct Eipdebuginfo info;

	ebp = read_ebp();
	for (; ebp != 0; ebp = *((uint32_t *)ebp)) {
		eip = ((uint32_t *)ebp)[1];
		cprintf("ebp %08x  eip %08x  args", ebp, eip);
		for (i = 0; i < 5; i++) {
			arg = *((uint32_t *)ebp + 2 + i);
			cprintf(" %08x", arg);
		}
		cprintf("\n");
		debuginfo_eip(eip, &info);
		cprintf("       %s:%d: %.*s+%d\n", info.eip_file, info.eip_line
									, info.eip_fn_namelen, info.eip_fn_name
									, eip - info.eip_fn_addr);
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
