
#ifndef __XEN_MM_H__
#define __XEN_MM_H__

#include <xen/config.h>
#include <xen/types.h>
#include <xen/list.h>
#include <xen/spinlock.h>

struct domain;
struct pfn_info;

/* Boot-time allocator. Turns into generic allocator after bootstrap. */
physaddr_t init_boot_allocator(physaddr_t bitmap_start);
void init_boot_pages(physaddr_t ps, physaddr_t pe);
unsigned long alloc_boot_pages(unsigned long nr_pfns, unsigned long pfn_align);
void end_boot_allocator(void);

/* Generic allocator. These functions are *not* interrupt-safe. */
void init_heap_pages(
    unsigned int zone, struct pfn_info *pg, unsigned long nr_pages);
struct pfn_info *alloc_heap_pages(unsigned int zone, unsigned int order);
void free_heap_pages(
    unsigned int zone, struct pfn_info *pg, unsigned int order);
void scrub_heap_pages(void);

/* Xen suballocator. These functions are interrupt-safe. */
void init_xenheap_pages(physaddr_t ps, physaddr_t pe);
void *alloc_xenheap_pages(unsigned int order);
void free_xenheap_pages(void *v, unsigned int order);
#define alloc_xenheap_page() (alloc_xenheap_pages(0))
#define free_xenheap_page(v) (free_xenheap_pages(v,0))

/* Domain suballocator. These functions are *not* interrupt-safe.*/
void init_domheap_pages(physaddr_t ps, physaddr_t pe);
struct pfn_info *alloc_domheap_pages(
    struct domain *d, unsigned int order, unsigned int flags);
void free_domheap_pages(struct pfn_info *pg, unsigned int order);
unsigned long avail_domheap_pages(void);
#define alloc_domheap_page(d) (alloc_domheap_pages(d,0,0))
#define free_domheap_page(p)  (free_domheap_pages(p,0))

#define ALLOC_DOM_DMA 1

/* Automatic page scrubbing for dead domains. */
extern struct list_head page_scrub_list;
#define page_scrub_schedule_work()              \
    do {                                        \
        if ( !list_empty(&page_scrub_list) )    \
            raise_softirq(PAGE_SCRUB_SOFTIRQ);  \
    } while ( 0 )

#include <asm/mm.h>

#ifndef sync_pagetable_state
#define sync_pagetable_state(d) ((void)0)
#endif

#endif /* __XEN_MM_H__ */
