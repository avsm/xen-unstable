
#ifndef __ASM_DOMAIN_H__
#define __ASM_DOMAIN_H__

struct trap_bounce {
    unsigned long  error_code;
    unsigned long  cr2;
    unsigned short flags; /* TBF_ */
    unsigned short cs;
    unsigned long  eip;
};

struct arch_domain
{
    l1_pgentry_t *mm_perdomain_pt;
#ifdef __x86_64__
    l2_pgentry_t *mm_perdomain_l2;
    l3_pgentry_t *mm_perdomain_l3;
#endif

    /* I/O-port access bitmap mask. */
    u8 *iobmp_mask;       /* Address of IO bitmap mask, or NULL.      */

    /* shadow mode status and controls */
    unsigned int shadow_mode;  /* flags to control shadow table operation */
    spinlock_t   shadow_lock;
    unsigned long min_pfn;     /* min host physical */
    unsigned long max_pfn;     /* max host physical */

    /* shadow hashtable */
    struct shadow_status *shadow_ht;
    struct shadow_status *shadow_ht_free;
    struct shadow_status *shadow_ht_extras; /* extra allocation units */
    unsigned int shadow_extras_count;

    /* shadow dirty bitmap */
    unsigned long *shadow_dirty_bitmap;
    unsigned int shadow_dirty_bitmap_size;  /* in pages, bit per page */

    /* shadow mode stats */
    unsigned int shadow_page_count;
    unsigned int hl2_page_count;
    unsigned int snapshot_page_count;

    unsigned int shadow_fault_count;
    unsigned int shadow_dirty_count;
    unsigned int shadow_dirty_net_count;
    unsigned int shadow_dirty_block_count;

    /* full shadow mode */
    struct out_of_sync_entry *out_of_sync; /* list of out-of-sync pages */
    struct out_of_sync_entry *out_of_sync_free;
    struct out_of_sync_entry *out_of_sync_extras;
    unsigned int out_of_sync_extras_count;

    struct list_head free_shadow_frames;

    pagetable_t  phys_table;               /* guest 1:1 pagetable */

} __cacheline_aligned;

struct arch_exec_domain
{
    unsigned long      kernel_sp;
    unsigned long      kernel_ss;

    unsigned long      flags; /* TF_ */

    /* Hardware debugging registers */
    unsigned long      debugreg[8];  /* %%db0-7 debug registers */

    /* floating point info */
    struct i387_state  i387;

    /* general user-visible register state */
    execution_context_t user_ctxt;

    void (*schedule_tail) (struct exec_domain *);

    /*
     * Return vectors pushed to us by guest OS.
     * The stack frame for events is exactly that of an x86 hardware interrupt.
     * The stack frame for a failsafe callback is augmented with saved values
     * for segment registers %ds, %es, %fs and %gs:
     *  %ds, %es, %fs, %gs, %eip, %cs, %eflags [, %oldesp, %oldss]
     */

    unsigned long event_selector;    /* entry CS  (x86/32 only) */
    unsigned long event_address;     /* entry EIP */

    unsigned long failsafe_selector; /* entry CS  (x86/32 only) */
    unsigned long failsafe_address;  /* entry EIP */

    unsigned long syscall_address;   /* entry EIP (x86/64 only) */

    /* Bounce information for propagating an exception to guest OS. */
    struct trap_bounce trap_bounce;

    /* I/O-port access bitmap. */
    u8 *iobmp;        /* Guest kernel virtual address of the bitmap. */
    int iobmp_limit;  /* Number of ports represented in the bitmap.  */
    int iopl;         /* Current IOPL for this VCPU. */

    /* Trap info. */
#ifdef ARCH_HAS_FAST_TRAP
    int                fast_trap_idx;
    struct desc_struct fast_trap_desc;
#endif
    trap_info_t        traps[256];

    /* Virtual Machine Extensions */
    struct arch_vmx_struct arch_vmx;

    /*
     * Every domain has a L1 pagetable of its own. Per-domain mappings
     * are put in this table (eg. the current GDT is mapped here).
     */
    l1_pgentry_t *perdomain_ptes;

    pagetable_t  guest_table_user;      /* x86/64: user-space pagetable. */
    pagetable_t  guest_table;           /* (MA) guest notion of cr3 */
    pagetable_t  shadow_table;          /* (MA) shadow of guest */
    pagetable_t  monitor_table;         /* (MA) used in hypervisor */

    l2_pgentry_t *guest_vtable;         /* virtual address of pagetable */
    l2_pgentry_t *shadow_vtable;        /* virtual address of shadow_table */
    l2_pgentry_t *monitor_vtable;		/* virtual address of monitor_table */
    l1_pgentry_t *hl2_vtable;			/* virtual address of hl2_table */

#ifdef __x86_64__
    l3_pgentry_t *guest_vl3table;
    l4_pgentry_t *guest_vl4table;
#endif

    unsigned long monitor_shadow_ref;

    /* Virtual CR2 value. Can be read/written by guest. */
    unsigned long guest_cr2;

    /* Current LDT details. */
    unsigned long ldt_base, ldt_ents, shadow_ldt_mapcnt;
    /* Next entry is passed to LGDT on domain switch. */
    char gdt[10]; /* NB. 10 bytes needed for x86_64. Use 6 bytes for x86_32. */
} __cacheline_aligned;

#define IDLE0_ARCH_EXEC_DOMAIN                                      \
{                                                                   \
    perdomain_ptes: 0,                                              \
    monitor_table:  mk_pagetable(__pa(idle_pg_table))               \
}

#endif /* __ASM_DOMAIN_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
