/*
 *  linux/arch/x86-64/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * 
 *  X86-64 port
 *	Andi Kleen.
 * 
 *  $Id: process.c,v 1.38 2002/01/15 10:08:03 ak Exp $
 * 
 *  Jun Nakajima <jun.nakajima@intel.com> 
 *     Modified for Xen
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/module.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/ptrace.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/mmu_context.h>
#include <asm/pda.h>
#include <asm/prctl.h>
#include <asm/kdebug.h>
#include <asm-xen/multicall.h>
#include <asm-xen/xen-public/dom0_ops.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/hardirq.h>
#include <asm/ia32.h>

asmlinkage extern void ret_from_fork(void);

unsigned long kernel_thread_flags = CLONE_VM | CLONE_UNTRACED;

atomic_t hlt_counter = ATOMIC_INIT(0);

unsigned long boot_option_idle_override = 0;
EXPORT_SYMBOL(boot_option_idle_override);

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);
static cpumask_t cpu_idle_map;

void disable_hlt(void)
{
	atomic_inc(&hlt_counter);
}

EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	atomic_dec(&hlt_counter);
}

EXPORT_SYMBOL(enable_hlt);

/* XXX XEN doesn't use default_idle(), poll_idle(). Use xen_idle() instead. */
extern int set_timeout_timer(void);
void xen_idle(void)
{
	int cpu;

	cpu = smp_processor_id();
	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, 0);

	if (need_resched()) {
		local_irq_enable();
	} else if (set_timeout_timer() == 0) {
		/* NB. Blocking reenable events in a race-free manner. */
		HYPERVISOR_block();
	} else {
		local_irq_enable();
		HYPERVISOR_yield();
	}
        set_need_resched();
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	int cpu = smp_processor_id();

	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched()) {
			if (cpu_isset(cpu, cpu_idle_map))
				cpu_clear(cpu, cpu_idle_map);
			rmb();
                        
                        __IRQ_STAT(cpu,idle_timestamp) = jiffies;
			xen_idle();
		}
		schedule();
	}
}

/* XXX XEN doesn't use mwait_idle(), select_idle_routine(), idle_setup(). */
/* Always use xen_idle() instead. */
void __init select_idle_routine(const struct cpuinfo_x86 *c) {}

/* Prints also some state that isn't saved in the pt_regs */ 
void __show_regs(struct pt_regs * regs)
{
	unsigned long fs, gs, shadowgs;
	unsigned int fsindex,gsindex;
	unsigned int ds,cs,es; 

	printk("\n");
	print_modules();
	printk("Pid: %d, comm: %.20s %s %s\n", 
	       current->pid, current->comm, print_tainted(), system_utsname.release);
	printk("RIP: %04lx:[<%016lx>] ", regs->cs & 0xffff, regs->rip);
	printk_address(regs->rip); 
	printk("\nRSP: %04lx:%016lx  EFLAGS: %08lx\n", regs->ss, regs->rsp, regs->eflags);
	printk("RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       regs->rax, regs->rbx, regs->rcx);
	printk("RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       regs->rdx, regs->rsi, regs->rdi); 
	printk("RBP: %016lx R08: %016lx R09: %016lx\n",
	       regs->rbp, regs->r8, regs->r9); 
	printk("R10: %016lx R11: %016lx R12: %016lx\n",
	       regs->r10, regs->r11, regs->r12); 
	printk("R13: %016lx R14: %016lx R15: %016lx\n",
	       regs->r13, regs->r14, regs->r15); 

	asm("movl %%ds,%0" : "=r" (ds)); 
	asm("movl %%cs,%0" : "=r" (cs)); 
	asm("movl %%es,%0" : "=r" (es)); 
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs); 
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs); 

	printk("FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n", 
	       fs,fsindex,gs,gsindex,shadowgs); 
	printk("CS:  %04x DS: %04x ES: %04x\n", cs, ds, es); 

}

void show_regs(struct pt_regs *regs)
{
	__show_regs(regs);
	show_trace(&regs->rsp);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *me = current;
	struct thread_struct *t = &me->thread;
	if (me->thread.io_bitmap_ptr) { 
		struct tss_struct *tss = &per_cpu(init_tss, get_cpu());

		kfree(t->io_bitmap_ptr);
		t->io_bitmap_ptr = NULL;
		/*
		 * Careful, clear this in the TSS too:
		 */
		memset(tss->io_bitmap, 0xff, t->io_bitmap_max);
		t->io_bitmap_max = 0;
		put_cpu();
	}
}

void load_gs_index(unsigned gs)
{
    __load_gs_index(gs);
}

void flush_thread(void)
{
	struct task_struct *tsk = current;
	struct thread_info *t = current_thread_info();

	if (t->flags & _TIF_ABI_PENDING)
		t->flags ^= (_TIF_ABI_PENDING | _TIF_IA32);

	tsk->thread.debugreg0 = 0;
	tsk->thread.debugreg1 = 0;
	tsk->thread.debugreg2 = 0;
	tsk->thread.debugreg3 = 0;
	tsk->thread.debugreg6 = 0;
	tsk->thread.debugreg7 = 0;
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));	
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	clear_used_math();
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%p/%d>\n",
					dead_task->comm,
					dead_task->mm->context.ldt,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

static inline void set_32bit_tls(struct task_struct *t, int tls, u32 addr)
{
	struct user_desc ud = { 
		.base_addr = addr,
		.limit = 0xfffff,
		.seg_32bit = 1,
		.limit_in_pages = 1,
		.useable = 1,
	};
	struct n_desc_struct *desc = (void *)t->thread.tls_array;
	desc += tls;
	desc->a = LDT_entry_a(&ud); 
	desc->b = LDT_entry_b(&ud); 
}

static inline u32 read_32bit_tls(struct task_struct *t, int tls)
{
	struct desc_struct *desc = (void *)t->thread.tls_array;
	desc += tls;
	return desc->base0 | 
		(((u32)desc->base1) << 16) | 
		(((u32)desc->base2) << 24);
}

/*
 * This gets called before we allocate a new thread and copy
 * the current task into it.
 */
void prepare_to_copy(struct task_struct *tsk)
{
	unlazy_fpu(tsk);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long rsp, 
		unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	int err;
	struct pt_regs * childregs;
	struct task_struct *me = current;

	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;

	*childregs = *regs;

	childregs->rax = 0;
	childregs->rsp = rsp;
	if (rsp == ~0UL) {
		childregs->rsp = (unsigned long)childregs;
	}

	p->thread.rsp = (unsigned long) childregs;
	p->thread.rsp0 = (unsigned long) (childregs+1);
	p->thread.userrsp = me->thread.userrsp; 

	set_ti_thread_flag(p->thread_info, TIF_FORK);

	p->thread.fs = me->thread.fs;
	p->thread.gs = me->thread.gs;

	asm("movl %%gs,%0" : "=m" (p->thread.gsindex));
	asm("movl %%fs,%0" : "=m" (p->thread.fsindex));
	asm("movl %%es,%0" : "=m" (p->thread.es));
	asm("movl %%ds,%0" : "=m" (p->thread.ds));

	if (unlikely(me->thread.io_bitmap_ptr != NULL)) { 
		p->thread.io_bitmap_ptr = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) {
			p->thread.io_bitmap_max = 0;
			return -ENOMEM;
		}
		memcpy(p->thread.io_bitmap_ptr, me->thread.io_bitmap_ptr, IO_BITMAP_BYTES);
	} 

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS) {
#ifdef CONFIG_IA32_EMULATION
		if (test_thread_flag(TIF_IA32))
			err = ia32_child_tls(p, childregs); 
		else 			
#endif	 
			err = do_arch_prctl(p, ARCH_SET_FS, childregs->r8); 
		if (err) 
			goto out;
	}
        p->thread.io_pl = current->thread.io_pl;

	err = 0;
out:
	if (err && p->thread.io_bitmap_ptr) {
		kfree(p->thread.io_bitmap_ptr);
		p->thread.io_bitmap_max = 0;
	}
	return err;
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread,register) \
		HYPERVISOR_set_debugreg((register),	\
			(thread->debugreg ## register))

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * This could still be optimized: 
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 */
struct task_struct *__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();  
	struct tss_struct *tss = &per_cpu(init_tss, cpu);
        dom0_op_t op;

        /* NB. No need to disable interrupts as already done in sched.c */
        /* __cli(); */

//	MULTICALL_flush_page_update_queue();

	/* never put a printk in __switch_to... printk() calls wake_up*() indirectly */

	/*
	 * This is basically '__unlazy_fpu', except that we queue a
	 * multicall to indicate FPU task switch, rather than
	 * synchronously trapping to Xen.
	 */
	if (prev_p->thread_info->status & TS_USEDFPU) {
		save_init_fpu(prev_p);
		queue_multicall0(__HYPERVISOR_fpu_taskswitch);
	}

	/*
	 * Reload esp0, LDT and the page table pointer:
	 */
	tss->rsp0 = next->rsp0;
//	queue_multicall1(__HYPERVISOR_stack_switch, tss->rsp0);
	HYPERVISOR_stack_switch(__KERNEL_DS, tss->rsp0);

	/*
	 * Load the per-thread Thread-Local Storage descriptor.
	 * This is load_TLS(next, cpu) with multicalls.
	 */
#define C(i) do {							\
          if (unlikely(next->tls_array[i] != prev->tls_array[i]))       \
            queue_multicall2(__HYPERVISOR_update_descriptor,            \
                             virt_to_machine(&get_cpu_gdt_table(cpu)    \
                                             [GDT_ENTRY_TLS_MIN + i]),  \
                             (unsigned long) &next->tls_array[i]);      \
} while (0)
	C(0); C(1); C(2);
#undef C

	if (xen_start_info.flags & SIF_PRIVILEGED) {
		op.cmd           = DOM0_IOPL;
		op.u.iopl.domain = DOMID_SELF;
		op.u.iopl.iopl   = next->io_pl;
		op.interface_version = DOM0_INTERFACE_VERSION;
		HYPERVISOR_dom0_op(&op);
#if 0
		queue_multicall1(__HYPERVISOR_dom0_op, (unsigned long)&op);
#endif
	}

	/* EXECUTE ALL TASK SWITCH XEN SYSCALLS AT THIS POINT. */
//	execute_multicall_list();
        /* __sti(); */

	/* 
	 * Switch DS and ES.
	 * This won't pick up thread selector changes, but I guess that is ok.
	 */
	asm volatile("movl %%es,%0" : "=m" (prev->es)); 
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es); 
	
	asm volatile ("movl %%ds,%0" : "=m" (prev->ds)); 
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	/* 
	 * Switch FS and GS.
	 */
	{ 
		unsigned fsindex;
		asm volatile("movl %%fs,%0" : "=g" (fsindex)); 
		/* segment register != 0 always requires a reload. 
		   also reload when it has changed. 
		   when prev process used 64bit base always reload
		   to avoid an information leak. */
		if (unlikely(fsindex | next->fsindex | prev->fs)) {
			loadsegment(fs, next->fsindex);
			/* check if the user used a selector != 0
	                 * if yes clear 64bit base, since overloaded base
                         * is always mapped to the Null selector
                         */
			if (fsindex)
			prev->fs = 0;				
		}
		/* when next process has a 64bit base use it */
		if (next->fs) {
                        HYPERVISOR_set_segment_base(SEGBASE_FS, next->fs); 
                }
		prev->fsindex = fsindex;
	}
	{ 
		unsigned gsindex;
		asm volatile("movl %%gs,%0" : "=g" (gsindex)); 
		if (unlikely(gsindex | next->gsindex | prev->gs)) {
			load_gs_index(next->gsindex);
			if (gsindex)
			prev->gs = 0;				
		}
		if (next->gs)
                    HYPERVISOR_set_segment_base(SEGBASE_GS_USER, next->gs); 
		prev->gsindex = gsindex;
	}

	/* 
	 * Switch the PDA context.
	 */
	prev->userrsp = read_pda(oldrsp); 
	write_pda(oldrsp, next->userrsp); 
	write_pda(pcurrent, next_p); 
	write_pda(kernelstack, (unsigned long)next_p->thread_info + THREAD_SIZE - PDA_STACKOFFSET);

	/*
	 * Now maybe reload the debug registers
	 */
	if (unlikely(next->debugreg7)) {
		loaddebug(next, 0);
		loaddebug(next, 1);
		loaddebug(next, 2);
		loaddebug(next, 3);
		/* no 4 and 5 */
		loaddebug(next, 6);
		loaddebug(next, 7);
	}

	/* 
	 * Handle the IO bitmap 
	 */ 
	if (unlikely(prev->io_bitmap_ptr || next->io_bitmap_ptr)) {
		if (next->io_bitmap_ptr)
			/*
			 * Copy the relevant range of the IO bitmap.
			 * Normally this is 128 bytes or less:
 			 */
			memcpy(tss->io_bitmap, next->io_bitmap_ptr,
				max(prev->io_bitmap_max, next->io_bitmap_max));
		else {
			/*
			 * Clear any possible leftover bits:
			 */
			memset(tss->io_bitmap, 0xff, prev->io_bitmap_max);
		}
	}

	return prev_p;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage 
long sys_execve(char __user *name, char __user * __user *argv,
		char __user * __user *envp, struct pt_regs regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) 
		return error;
	error = do_execve(filename, argv, envp, &regs); 
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
	return error;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32); 

	/* TBD: overwrites user setup. Should have two bits.
	   But 64bit processes have always behaved this way,
	   so it's not too bad. The main problem is just that
   	   32bit childs are affected again. */
	current->personality &= ~READ_IMPLIES_EXEC;
}

asmlinkage long sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->rsp, regs, 0, NULL, NULL);
}

asmlinkage long sys_clone(unsigned long clone_flags, unsigned long newsp, void __user *parent_tid, void __user *child_tid, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->rsp;
	return do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage long sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->rsp, regs, 0,
		    NULL, NULL);
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long stack;
	u64 fp,rip;
	int count = 0;

	if (!p || p == current || p->state==TASK_RUNNING)
		return 0; 
	stack = (unsigned long)p->thread_info; 
	if (p->thread.rsp < stack || p->thread.rsp > stack+THREAD_SIZE)
		return 0;
	fp = *(u64 *)(p->thread.rsp);
	do { 
		if (fp < (unsigned long)stack || fp > (unsigned long)stack+THREAD_SIZE)
			return 0; 
		rip = *(u64 *)(fp+8); 
		if (!in_sched_functions(rip))
			return rip; 
		fp = *(u64 *)fp; 
	} while (count++ < 16); 
	return 0;
}

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr)
{ 
	int ret = 0; 
	int doit = task == current;
	int cpu;

	switch (code) { 
	case ARCH_SET_GS:
		if (addr >= TASK_SIZE) 
			return -EPERM; 
		cpu = get_cpu();
		/* handle small bases via the GDT because that's faster to 
		   switch. */
		if (addr <= 0xffffffff) {  
			set_32bit_tls(task, GS_TLS, addr); 
			if (doit) { 
				load_TLS(&task->thread, cpu);
				load_gs_index(GS_TLS_SEL); 
			}
			task->thread.gsindex = GS_TLS_SEL; 
			task->thread.gs = 0;
		} else { 
			task->thread.gsindex = 0;
			task->thread.gs = addr;
			if (doit) {
		load_gs_index(0);
                ret = HYPERVISOR_set_segment_base(SEGBASE_GS_USER, addr);
                printk("do_arch_prctl: SET_SET: addr = %lx\n", addr);
			} 
		}
		put_cpu();
		break;
	case ARCH_SET_FS:
		/* Not strictly needed for fs, but do it for symmetry
		   with gs */
		if (addr >= TASK_SIZE)
			return -EPERM; 
		cpu = get_cpu();
		/* handle small bases via the GDT because that's faster to 
		   switch. */
		if (addr <= 0xffffffff) { 
			set_32bit_tls(task, FS_TLS, addr);
			if (doit) { 
				load_TLS(&task->thread, cpu); 
				asm volatile("movl %0,%%fs" :: "r" (FS_TLS_SEL));
			}
			task->thread.fsindex = FS_TLS_SEL;
			task->thread.fs = 0;
		} else { 
			task->thread.fsindex = 0;
			task->thread.fs = addr;
			if (doit) {
				/* set the selector to 0 to not confuse
				   __switch_to */
                                asm volatile("movl %0,%%fs" :: "r" (0));
                                ret = HYPERVISOR_set_segment_base(SEGBASE_FS, addr);

			}
		}
		put_cpu();
		break;
	case ARCH_GET_FS: { 
		unsigned long base; 
		if (task->thread.fsindex == FS_TLS_SEL)
			base = read_32bit_tls(task, FS_TLS);
		else if (doit) {
			rdmsrl(MSR_FS_BASE, base);
                        printk("do_arch_prctl: GET_FS: addr = %lx\n", addr);
		} else
			base = task->thread.fs;
		ret = put_user(base, (unsigned long __user *)addr); 
		break; 
	}
	case ARCH_GET_GS: { 
		unsigned long base;
		if (task->thread.gsindex == GS_TLS_SEL)
			base = read_32bit_tls(task, GS_TLS);
		else if (doit) {
			rdmsrl(MSR_KERNEL_GS_BASE, base);
                        printk("do_arch_prctl: GET_GS: addr = %lx\n", addr);
		} else
			base = task->thread.gs;
		ret = put_user(base, (unsigned long __user *)addr); 
		break;
	}

	default:
		ret = -EINVAL;
		break;
	} 

	return ret;	
} 

long sys_arch_prctl(int code, unsigned long addr)
{
	return do_arch_prctl(current, code, addr);
} 

/* 
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs *pp, ptregs;

	pp = (struct pt_regs *)(tsk->thread.rsp0);
	--pp; 

	ptregs = *pp; 
	ptregs.cs &= 0xffff;
	ptregs.ss &= 0xffff;

	elf_core_copy_regs(regs, &ptregs);
        
        boot_option_idle_override = 1;
	return 1;
}
