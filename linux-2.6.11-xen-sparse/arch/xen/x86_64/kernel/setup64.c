/* 
 * X86-64 specific CPU setup.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001, 2002, 2003 SuSE Labs / Andi Kleen.
 * See setup.c for older changelog.
 * $Id: setup64.c,v 1.12 2002/03/21 10:09:17 ak Exp $
 *
 * Jun Nakajima <jun.nakajima@intel.com> 
 *   Modified for Xen
 *
 */ 
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/bitops.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/i387.h>
#include <asm/percpu.h>
#include <asm/mtrr.h>
#include <asm/proto.h>
#include <asm/mman.h>
#include <asm/numa.h>

#include <asm-xen/hypervisor.h>

char x86_boot_params[2048] __initdata = {0,};

cpumask_t cpu_initialized __initdata = CPU_MASK_NONE;

struct x8664_pda cpu_pda[NR_CPUS] __cacheline_aligned; 

extern struct task_struct init_task;

extern unsigned char __per_cpu_start[], __per_cpu_end[]; 

extern struct desc_ptr cpu_gdt_descr[];
struct desc_ptr idt_descr = { 256 * 16, (unsigned long) idt_table }; 

char boot_cpu_stack[IRQSTACKSIZE] __attribute__((section(".bss.page_aligned")));

unsigned long __supported_pte_mask = ~0UL;
static int do_not_nx __initdata = 0;

/* noexec=on|off
Control non executable mappings for 64bit processes.

on	Enable(default)
off	Disable
*/ 
int __init nonx_setup(char *str)
{
	if (!strncmp(str, "on", 2)) {
                __supported_pte_mask |= _PAGE_NX; 
 		do_not_nx = 0; 
	} else if (!strncmp(str, "off", 3)) {
		do_not_nx = 1;
		__supported_pte_mask &= ~_PAGE_NX;
        }
	return 0;
} 
__setup("noexec=", nonx_setup);	/* parsed early actually */

int force_personality32 = READ_IMPLIES_EXEC;

/* noexec32=on|off
Control non executable heap for 32bit processes.
To control the stack too use noexec=off

on	PROT_READ does not imply PROT_EXEC for 32bit processes
off	PROT_READ implies PROT_EXEC (default)
*/
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 0;
}
__setup("noexec32=", nonx32_setup);

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{ 
	int i;
	unsigned long size;

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES);
#ifdef CONFIG_MODULES
	if (size < PERCPU_ENOUGH_ROOM)
		size = PERCPU_ENOUGH_ROOM;
#endif

	for (i = 0; i < NR_CPUS; i++) { 
		unsigned char *ptr;

		if (!NODE_DATA(cpu_to_node(i))) {
			printk("cpu with no node %d, num_online_nodes %d\n",
			       i, num_online_nodes());
			ptr = alloc_bootmem(size);
		} else { 
			ptr = alloc_bootmem_node(NODE_DATA(cpu_to_node(i)), size);
		}
		if (!ptr)
			panic("Cannot allocate cpu data for CPU %d\n", i);
		cpu_pda[i].data_offset = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
	}
} 

void pda_init(int cpu)
{ 
        pgd_t *old_level4 = (pgd_t *)xen_start_info.pt_base;
	struct x8664_pda *pda = &cpu_pda[cpu];

	/* Setup up data that may be needed in __get_free_pages early */
	asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0)); 
        HYPERVISOR_set_segment_base(SEGBASE_GS_KERNEL, 
                                    (unsigned long)(cpu_pda + cpu));

	pda->me = pda;
	pda->cpunumber = cpu; 
	pda->irqcount = -1;
	pda->kernelstack = 
		(unsigned long)stack_thread_info() - PDA_STACKOFFSET + THREAD_SIZE; 
	pda->active_mm = &init_mm;
	pda->mmu_state = 0;
        pda->kernel_mode = 1;

	if (cpu == 0) {
                memcpy((void *)init_level4_pgt, 
                       (void *) xen_start_info.pt_base, PAGE_SIZE);
		/* others are initialized in smpboot.c */
		pda->pcurrent = &init_task;
		pda->irqstackptr = boot_cpu_stack; 
                make_page_readonly(init_level4_pgt);
                make_page_readonly(init_level4_user_pgt);
                make_page_readonly(level3_user_pgt); /* for vsyscall stuff */
                xen_pgd_pin(__pa_symbol(init_level4_user_pgt));
                xen_pud_pin(__pa_symbol(level3_user_pgt));
                set_pgd((pgd_t *)(init_level4_user_pgt + 511), 
                        mk_kernel_pgd(__pa_symbol(level3_user_pgt)));
	} else {
		pda->irqstackptr = (char *)
			__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
		if (!pda->irqstackptr)
			panic("cannot allocate irqstack for cpu %d", cpu); 
	}

	xen_pt_switch(__pa(init_level4_pgt));
        xen_new_user_pt(__pa(init_level4_user_pgt));

	if (cpu == 0) {
                xen_pgd_unpin(__pa(old_level4));
#if 0
                early_printk("__pa: %x, <machine_phys> old_level 4 %x\n", 
                             __pa(xen_start_info.pt_base),
                             pfn_to_mfn(__pa(old_level4) >> PAGE_SHIFT));
#endif
//                make_page_writable(old_level4);
//                free_bootmem(__pa(old_level4), PAGE_SIZE);
        }

	pda->irqstackptr += IRQSTACKSIZE-64;
} 

char boot_exception_stacks[N_EXCEPTION_STACKS * EXCEPTION_STKSZ] 
__attribute__((section(".bss.page_aligned")));

/* May not be marked __init: used by software suspend */
void syscall_init(void)
{
#ifdef CONFIG_IA32_EMULATION   		
	syscall32_cpu_init ();
#endif
}

void __init check_efer(void)
{
	unsigned long efer;

        /*	rdmsrl(MSR_EFER, efer);  */

        /*
         * At this point, Xen does not like the bit 63.
         * So NX is not supported. Come back later.
         */
        efer = 0;

        if (!(efer & EFER_NX) || do_not_nx) { 
                __supported_pte_mask &= ~_PAGE_NX; 
        }       
}

void __init cpu_gdt_init(struct desc_ptr *gdt_descr)
{
	unsigned long frames[16];
	unsigned long va;
	int f;

	for (va = gdt_descr->address, f = 0;
	     va < gdt_descr->address + gdt_descr->size;
	     va += PAGE_SIZE, f++) {
		frames[f] = virt_to_machine(va) >> PAGE_SHIFT;
		make_page_readonly((void *)va);
	}
	if (HYPERVISOR_set_gdt(frames, gdt_descr->size /
                               sizeof (struct desc_struct)))
		BUG();
}


/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init.
 */
void __init cpu_init (void)
{
#ifdef CONFIG_SMP
	int cpu = stack_smp_processor_id();
#else
	int cpu = smp_processor_id();
#endif
	struct tss_struct *t = &per_cpu(init_tss, cpu);
	unsigned long v; 
	char *estacks = NULL; 
	struct task_struct *me;
	int i;

	/* CPU 0 is initialised in head64.c */
	if (cpu != 0) {
		pda_init(cpu);
	} else 
		estacks = boot_exception_stacks; 

	me = current;

	if (test_and_set_bit(cpu, &cpu_initialized))
		panic("CPU#%d already initialized!\n", cpu);

	printk("Initializing CPU#%d\n", cpu);

#if 0
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);
#endif
	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
	if (cpu) {
		memcpy(cpu_gdt_table[cpu], cpu_gdt_table[0], GDT_SIZE);
	}	

	cpu_gdt_descr[cpu].size = GDT_SIZE;
	cpu_gdt_descr[cpu].address = (unsigned long)cpu_gdt_table[cpu];
#if 0
	asm volatile("lgdt %0" :: "m" (cpu_gdt_descr[cpu]));
	asm volatile("lidt %0" :: "m" (idt_descr));
#endif
        cpu_gdt_init(&cpu_gdt_descr[cpu]);

#if 0
	memcpy(me->thread.tls_array, cpu_gdt_table[cpu], GDT_ENTRY_TLS_ENTRIES * 8);

#endif
 	memcpy(me->thread.tls_array, &get_cpu_gdt_table(cpu)[GDT_ENTRY_TLS_MIN],
	    GDT_ENTRY_TLS_ENTRIES * 8);
       
	/*
	 * Delete NT
	 */

	asm volatile("pushfq ; popq %%rax ; btr $14,%%rax ; pushq %%rax ; popfq" ::: "eax");

	if (cpu == 0) 
		early_identify_cpu(&boot_cpu_data);

	syscall_init();

	barrier(); 
	check_efer();

	/*
	 * set up and load the per-CPU TSS
	 */
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		if (cpu) {
			estacks = (char *)__get_free_pages(GFP_ATOMIC, 
						   EXCEPTION_STACK_ORDER);
			if (!estacks)
				panic("Cannot allocate exception stack %ld %d\n",
				      v, cpu); 
		}
		estacks += EXCEPTION_STKSZ;
		t->ist[v] = (unsigned long)estacks;
	}

	t->io_bitmap_base = offsetof(struct tss_struct, io_bitmap);
	/*
	 * <= is required because the CPU will access up to
	 * 8 bits beyond the end of the IO permission bitmap.
	 */
	for (i = 0; i <= IO_BITMAP_LONGS; i++)
		t->io_bitmap[i] = ~0UL;

	atomic_inc(&init_mm.mm_count);
	me->active_mm = &init_mm;
	if (me->mm)
		BUG();
	enter_lazy_tlb(&init_mm, me);

	load_LDT(&init_mm.context);

	/*
	 * Clear all 6 debug registers:
	 */
#define CD(register) HYPERVISOR_set_debugreg(register, 0)

	CD(0); CD(1); CD(2); CD(3); /* no db4 and db5 */; CD(6); CD(7);

#undef CD
	fpu_init(); 

#ifdef CONFIG_NUMA
	numa_add_cpu(cpu);
#endif
}
