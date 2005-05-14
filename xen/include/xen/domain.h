
#ifndef __XEN_DOMAIN_H__
#define __XEN_DOMAIN_H__

/*
 * Arch-specifics.
 */

struct exec_domain *arch_alloc_exec_domain_struct(void);

extern void arch_free_exec_domain_struct(struct exec_domain *ed);

extern void arch_do_createdomain(struct exec_domain *ed);

extern void arch_do_boot_vcpu(struct exec_domain *ed);

extern int  arch_set_info_guest(
    struct exec_domain *d, struct vcpu_guest_context *c);

extern void free_perdomain_pt(struct domain *d);

extern void domain_relinquish_resources(struct domain *d);

extern void dump_pageframe_info(struct domain *d);

#endif /* __XEN_DOMAIN_H__ */
