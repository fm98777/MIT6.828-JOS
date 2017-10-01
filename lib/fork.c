// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!((err & FEC_WR) && 
			(uvpd[PDX(addr)] & PTE_P) &&
			(uvpt[PGNUM(addr)] & PTE_P) &&
			(uvpt[PGNUM(addr)] & PTE_COW))) {
		panic("acess was not a write or to a COW page");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	envid_t eid = sys_getenvid();

	r = sys_page_alloc(eid, PFTEMP, PTE_U | PTE_W | PTE_P);
	if (r < 0) {
		panic("can't alloc new page");
	}
	addr = ROUNDDOWN(addr, PGSIZE);
	memcpy(PFTEMP, addr, PGSIZE);
	r = sys_page_unmap(eid, addr);
	if (r < 0) {
		panic("sys_page_unmap failed");
	}
	r = sys_page_map(eid, PFTEMP, eid, addr, PTE_U | PTE_P | PTE_W);
	if (r < 0) {
		panic("sys_page_map failed");
	}
	r = sys_page_unmap(eid, PFTEMP);
	if (r < 0) {
		panic("sys_page_unmap failed");
	}
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	//panic("duppage not implemented");

	envid_t myeid = sys_getenvid();
	void *va = (void *)(pn * PGSIZE);
	int perm;

	if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) {
		// make parent also unwritable, or child's memory will
		// change as parent writes
		perm = ((uvpt[pn] & PTE_SYSCALL) & (~PTE_W)) | PTE_COW;
	} else {
		perm = (uvpt[pn] & PTE_SYSCALL);
	}

	r = sys_page_map(myeid, va, envid, va, perm);
	if (r < 0) {
		panic("sys_page_map failed, err:%e", r);
	}
	r = sys_page_map(myeid, va, myeid, va, perm);
	if (r < 0) {
		panic("sys_page_map failed, err:%e", r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
//	panic("fork not implemented");
	envid_t childeid;
	uintptr_t va;
	int r;

	set_pgfault_handler(pgfault);
	childeid = sys_exofork();
	if (childeid < 0) {
		panic("sys_exofork failed: %e", childeid);
	}
	// don't map exception stack because we should alloc it
	for (va = 0; va < USTACKTOP; va += PGSIZE) {
		if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P)) {
			duppage(childeid, PGNUM(va));
		}
	}
	r = sys_page_alloc(childeid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		sys_env_destroy(childeid);
		panic("alloc exception stack failed");
	}

	extern void _pgfault_upcall();
	r = sys_env_set_pgfault_upcall(childeid, _pgfault_upcall);
	if (r < 0) {
		sys_env_destroy(childeid);
		panic("set pgfault upcall failed");
	}

	r = sys_env_set_status(childeid, ENV_RUNNABLE);
	if (r < 0) {
		sys_env_destroy(childeid);
		panic("set env status failed");
	}
	return childeid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
