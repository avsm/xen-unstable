/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/multicall.h>

#include <linux/irq.h>

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

int hlt_counter;

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

void disable_hlt(void)
{
    hlt_counter++;
}

void enable_hlt(void)
{
    hlt_counter--;
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
    /* endless idle loop with no priority at all */
    init_idle();
    current->nice = 20;
    current->counter = -100;

    while (1) {
        while (!current->need_resched)
            HYPERVISOR_yield();
        schedule();
        check_pgt_cache();
    }
}

void machine_restart(char * __unused)
{
    HYPERVISOR_exit();
}

void machine_halt(void)
{
    HYPERVISOR_exit();
}

void machine_power_off(void)
{
    HYPERVISOR_exit();
}

extern void show_trace(unsigned long* esp);

void show_regs(struct pt_regs * regs)
{
    printk("\n");
    printk("Pid: %d, comm: %20s\n", current->pid, current->comm);
    printk("EIP: %04x:[<%08lx>] CPU: %d",0xffff & regs->xcs,regs->eip, smp_processor_id());
    if (regs->xcs & 2)
        printk(" ESP: %04x:%08lx",0xffff & regs->xss,regs->esp);
    printk(" EFLAGS: %08lx    %s\n",regs->eflags, print_tainted());
    printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
           regs->eax,regs->ebx,regs->ecx,regs->edx);
    printk("ESI: %08lx EDI: %08lx EBP: %08lx",
           regs->esi, regs->edi, regs->ebp);
    printk(" DS: %04x ES: %04x\n",
           0xffff & regs->xds,0xffff & regs->xes);

    show_trace(&regs->esp);
}

/*
 * No need to lock the MM as we are the last user
 */
void release_segments(struct mm_struct *mm)
{
    void * ldt = mm->context.segments;

    /*
     * free the LDT
     */
    if (ldt) {
        mm->context.segments = NULL;
        clear_LDT();
        vfree(ldt);
    }
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
    long retval, d0;

    __asm__ __volatile__(
        "movl %%esp,%%esi\n\t"
        "int $0x80\n\t"		/* Linux/i386 system call */
        "cmpl %%esp,%%esi\n\t"	/* child or parent? */
        "je 1f\n\t"		/* parent - jump */
        /* Load the argument into eax, and push it.  That way, it does
         * not matter whether the called function is compiled with
         * -mregparm or not.  */
        "movl %4,%%eax\n\t"
        "pushl %%eax\n\t"		
        "call *%5\n\t"		/* call fn */
        "movl %3,%0\n\t"	/* exit */
        "int $0x80\n"
        "1:\t"
        :"=&a" (retval), "=&S" (d0)
        :"0" (__NR_clone), "i" (__NR_exit),
        "r" (arg), "r" (fn),
        "b" (flags | CLONE_VM)
        : "memory");

    return retval;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
    /* nothing to do ... */
}

void flush_thread(void)
{
    struct task_struct *tsk = current;

    memset(tsk->thread.debugreg, 0, sizeof(unsigned long)*8);

    /*
     * Forget coprocessor state..
     */
    clear_fpu(tsk);
    tsk->used_math = 0;
}

void release_thread(struct task_struct *dead_task)
{
    if (dead_task->mm) {
        void * ldt = dead_task->mm->context.segments;

        // temporary debugging check
        if (ldt) {
            printk("WARNING: dead process %8s still has LDT? <%p>\n",
                   dead_task->comm, ldt);
            BUG();
        }
    }
}

/*
 * we do not have to muck with descriptors here, that is
 * done in switch_mm() as needed.
 */
void copy_segments(struct task_struct *p, struct mm_struct *new_mm)
{
    struct mm_struct * old_mm;
    void *old_ldt, *ldt;

    ldt = NULL;
    old_mm = current->mm;
    if (old_mm && (old_ldt = old_mm->context.segments) != NULL) {
        /*
         * Completely new LDT, we initialize it from the parent:
         */
        ldt = vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE);
        if (!ldt)
            printk(KERN_WARNING "ldt allocation failed\n");
        else
            memcpy(ldt, old_ldt, LDT_ENTRIES*LDT_ENTRY_SIZE);
    }
    new_mm->context.segments = ldt;
    new_mm->context.cpuvalid = ~0UL;	/* valid on all CPU's - they can't have stale data */
}

/*
 * Save a segment.
 */
#define savesegment(seg,value) \
	asm volatile("movl %%" #seg ",%0":"=m" (*(int *)&(value)))

int copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
                unsigned long unused,
                struct task_struct * p, struct pt_regs * regs)
{
    struct pt_regs * childregs;

    childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p)) - 1;
    struct_cpy(childregs, regs);
    childregs->eax = 0;
    childregs->esp = esp;

    p->thread.esp = (unsigned long) childregs;
    p->thread.esp0 = (unsigned long) (childregs+1);

    p->thread.eip = (unsigned long) ret_from_fork;

    savesegment(fs,p->thread.fs);
    savesegment(gs,p->thread.gs);

    unlazy_fpu(current);
    struct_cpy(&p->thread.i387, &current->thread.i387);

    return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
    int i;

/* changed the size calculations - should hopefully work better. lbt */
    dump->magic = CMAGIC;
    dump->start_code = 0;
    dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
    dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
    dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
    dump->u_dsize -= dump->u_tsize;
    dump->u_ssize = 0;
    for (i = 0; i < 8; i++)
        dump->u_debugreg[i] = current->thread.debugreg[i];  

    if (dump->start_stack < TASK_SIZE)
        dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

    dump->regs.ebx = regs->ebx;
    dump->regs.ecx = regs->ecx;
    dump->regs.edx = regs->edx;
    dump->regs.esi = regs->esi;
    dump->regs.edi = regs->edi;
    dump->regs.ebp = regs->ebp;
    dump->regs.eax = regs->eax;
    dump->regs.ds = regs->xds;
    dump->regs.es = regs->xes;
    savesegment(fs,dump->regs.fs);
    savesegment(gs,dump->regs.gs);
    dump->regs.orig_eax = regs->orig_eax;
    dump->regs.eip = regs->eip;
    dump->regs.cs = regs->xcs;
    dump->regs.eflags = regs->eflags;
    dump->regs.esp = regs->esp;
    dump->regs.ss = regs->xss;

    dump->u_fpvalid = dump_fpu (regs, &dump->i387);
}

/*
 *	switch_to(x,yn) should switch tasks from x to y.
 *
 * We fsave/fwait so that an exception goes off at the right time
 * (as a call from the fsave or fwait in effect) rather than to
 * the wrong process. Lazy FP saving no longer makes any sense
 * with modern CPU's, and this simplifies a lot of things (SMP
 * and UP become the same).
 *
 * NOTE! We used to use the x86 hardware context switching. The
 * reason for not using it any more becomes apparent when you
 * try to recover gracefully from saved state that is no longer
 * valid (stale segment register values in particular). With the
 * hardware task-switch, there is no way to fix up bad state in
 * a reasonable manner.
 *
 * The fact that Intel documents the hardware task-switching to
 * be slow is a fairly red herring - this code is not noticeably
 * faster. However, there _is_ some room for improvement here,
 * so the performance issues may eventually be a valid point.
 * More important, however, is the fact that this allows us much
 * more flexibility.
 */
void __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
    struct thread_struct *prev = &prev_p->thread,
        *next = &next_p->thread;

    /*
     * This is basically 'unlazy_fpu', except that we queue a multicall to 
     * indicate FPU task switch, rather than synchronously trapping to Xen.
     */
    if ( prev_p->flags & PF_USEDFPU )
    {
	if ( cpu_has_fxsr )
            asm volatile( "fxsave %0 ; fnclex"
                          : "=m" (prev_p->thread.i387.fxsave) );
	else
            asm volatile( "fnsave %0 ; fwait"
                          : "=m" (prev_p->thread.i387.fsave) );
	prev_p->flags &= ~PF_USEDFPU;
        queue_multicall0(__HYPERVISOR_fpu_taskswitch);
    }

    if ( next->esp0 != 0 )
        queue_multicall2(__HYPERVISOR_stack_switch, __KERNEL_DS, next->esp0);

    /* EXECUTE ALL TASK SWITCH XEN SYSCALLS AT THIS POINT. */
    execute_multicall_list();
    sti(); /* matches 'cli' in switch_mm() */

    /*
     * Save away %fs and %gs. No need to save %es and %ds, as
     * those are always kernel segments while inside the kernel.
     */
    asm volatile("movl %%fs,%0":"=m" (*(int *)&prev->fs));
    asm volatile("movl %%gs,%0":"=m" (*(int *)&prev->gs));

    /*
     * Restore %fs and %gs.
     */
    loadsegment(fs, next->fs);
    loadsegment(gs, next->gs);

    /*
     * Now maybe reload the debug registers
     */
    if ( next->debugreg[7] != 0 )
    {
        HYPERVISOR_set_debugreg(0, next->debugreg[0]);
        HYPERVISOR_set_debugreg(1, next->debugreg[1]);
        HYPERVISOR_set_debugreg(2, next->debugreg[2]);
        HYPERVISOR_set_debugreg(3, next->debugreg[3]);
        /* no 4 and 5 */
        HYPERVISOR_set_debugreg(6, next->debugreg[6]);
        HYPERVISOR_set_debugreg(7, next->debugreg[7]);
    }
}

asmlinkage int sys_fork(struct pt_regs regs)
{
    return do_fork(SIGCHLD, regs.esp, &regs, 0);
}

asmlinkage int sys_clone(struct pt_regs regs)
{
    unsigned long clone_flags;
    unsigned long newsp;

    clone_flags = regs.ebx;
    newsp = regs.ecx;
    if (!newsp)
        newsp = regs.esp;
    return do_fork(clone_flags, newsp, &regs, 0);
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
asmlinkage int sys_vfork(struct pt_regs regs)
{
    return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.esp, &regs, 0);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
    int error;
    char * filename;

    filename = getname((char *) regs.ebx);
    error = PTR_ERR(filename);
    if (IS_ERR(filename))
        goto out;
    error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
    if (error == 0)
        current->ptrace &= ~PT_DTRACE;
    putname(filename);
 out:
    return error;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
    unsigned long ebp, esp, eip;
    unsigned long stack_page;
    int count = 0;
    if (!p || p == current || p->state == TASK_RUNNING)
        return 0;
    stack_page = (unsigned long)p;
    esp = p->thread.esp;
    if (!stack_page || esp < stack_page || esp > 8188+stack_page)
        return 0;
    /* include/asm-i386/system.h:switch_to() pushes ebp last. */
    ebp = *(unsigned long *) esp;
    do {
        if (ebp < stack_page || ebp > 8184+stack_page)
            return 0;
        eip = *(unsigned long *) (ebp+4);
        if (eip < first_sched || eip >= last_sched)
            return eip;
        ebp = *(unsigned long *) ebp;
    } while (count++ < 16);
    return 0;
}
#undef last_sched
#undef first_sched
