
/* -*-  Mode:C; c-basic-offset:4; tab-width:4; indent-tabs-mode:nil -*- */
/*
 * vmmu.h: virtual memory management unit related APIs and data structure.
 * Copyright (c) 2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 *  Yaozu Dong (Eddie Dong) (Eddie.dong@intel.com)
 */

#ifndef XEN_TLBthash_H
#define XEN_TLBthash_H

#include <xen/config.h>
#include <xen/types.h>
#include <public/xen.h>
#include <asm/tlb.h>
#include <asm/regionreg.h>

//#define         THASH_TLB_TR            0
//#define         THASH_TLB_TC            1


// bit definition of TR, TC search cmobination
//#define         THASH_SECTION_TR        (1<<0)
//#define         THASH_SECTION_TC        (1<<1)

/*
 * Next bit definition must be same with THASH_TLB_XX
 */
typedef union search_section {
        struct {
                u32 tr : 1;
                u32 tc : 1;
                u32 rsv: 30;
        };
        u32     v;
} search_section_t;

#define         MAX_CCN_DEPTH           4       // collision chain depth
#define         VCPU_TLB_SHIFT          (22)
#define         VCPU_TLB_SIZE           (1UL<<VCPU_TLB_SHIFT)
#define         VCPU_TLB_ORDER          VCPU_TLB_SHIFT - PAGE_SHIFT
#define         PTA_BASE_SHIFT          (15)

#ifndef __ASSEMBLY__
#define HIGH_32BITS(x)  bits(x,32,63)
#define LOW_32BITS(x)   bits(x,0,31)

typedef enum {
        ISIDE_TLB=0,
        DSIDE_TLB=1
} CACHE_LINE_TYPE;

typedef struct thash_data {
    union {
        struct {
            u64 p    :  1; // 0
            u64 rv1  :  1; // 1
            u64 ma   :  3; // 2-4
            u64 a    :  1; // 5
            u64 d    :  1; // 6
            u64 pl   :  2; // 7-8
            u64 ar   :  3; // 9-11
            u64 ppn  : 38; // 12-49
            u64 rv2  :  2; // 50-51
            u64 ed   :  1; // 52
            u64 ig1  :  11; //53-63
        };
        struct {
            u64 __rv1 : 53;	// 0-52
            // next extension to ig1, only for TLB instance
            u64 tc : 1;     // 53 TR or TC
            u64 locked  : 1;	// 54 entry locked or not
            CACHE_LINE_TYPE cl : 1; // I side or D side cache line
            u64 nomap : 1;   // entry cann't be inserted into machine TLB.
            u64 __ig1  :  5; // 56-61
            u64 checked : 1; // for VTLB/VHPT sanity check
            u64 invalid : 1; // invalid entry
        };
        u64 page_flags;
    };                  // same for VHPT and TLB

    union {
        struct {
            u64 rv3  :  2; // 0-1
            u64 ps   :  6; // 2-7
            u64 key  : 24; // 8-31
            u64 rv4  : 32; // 32-63
        };
        struct {
            u64 __rv3  : 32; // 0-31
            // next extension to rv4
            u64 rid  : 24;  // 32-55
            u64 __rv4  : 8; // 56-63
        };
        u64 itir;
    };
    union {
        struct {        // For TLB
            u64 ig2  :  12; // 0-11
            u64 vpn  :  49; // 12-60
            u64 vrn  :   3; // 61-63
        };
        u64 vadr;
        u64 ifa;
        struct {        // For VHPT
            u64 tag  :  63; // 0-62
            u64 ti   :  1;  // 63, invalid entry for VHPT
        };
        u64  etag;      // extended tag for VHPT
    };
    union {
        struct thash_data *next;
        u64  tr_idx;
    };
} thash_data_t;

#define INVALID_VHPT(hdata)     ((hdata)->ti)
#define INVALID_TLB(hdata)      ((hdata)->invalid)
#define INVALID_ENTRY(hcb, hdata)                       \
        ((hcb)->ht==THASH_TLB ? INVALID_TLB(hdata) : INVALID_VHPT(hdata))

typedef enum {
        THASH_TLB=0,
        THASH_VHPT
} THASH_TYPE;

struct thash_cb;
typedef union thash_cch_mem {
        thash_data_t    data;
        union thash_cch_mem *next;
} thash_cch_mem_t;


/*
 * Use to calculate the HASH index of thash_data_t.
 */
typedef u64 *(THASH_FN)(PTA pta, u64 va);
typedef u64 *(TTAG_FN)(PTA pta, u64 va);
typedef u64 *(GET_MFN_FN)(domid_t d, u64 gpfn, u64 pages);
typedef void *(REM_NOTIFIER_FN)(struct thash_cb *hcb, thash_data_t *entry);
typedef void (RECYCLE_FN)(struct thash_cb *hc, u64 para);
typedef ia64_rr (GET_RR_FN)(struct vcpu *vcpu, u64 reg);
typedef thash_data_t *(FIND_OVERLAP_FN)(struct thash_cb *hcb, 
        u64 va, u64 ps, int rid, char cl, search_section_t s_sect);
typedef thash_data_t *(FIND_NEXT_OVL_FN)(struct thash_cb *hcb);
typedef void (REM_THASH_FN)(struct thash_cb *hcb, thash_data_t *entry);
typedef void (INS_THASH_FN)(struct thash_cb *hcb, thash_data_t *entry, u64 va);

typedef struct tlb_special {
        thash_data_t     itr[NITRS];
        thash_data_t     dtr[NDTRS];
        struct thash_cb  *vhpt;
} tlb_special_t;

typedef struct vhpt_cb {
        //u64     pta;    // pta value.
        GET_MFN_FN      *get_mfn;
        TTAG_FN         *tag_func;
} vhpt_special;

typedef struct thash_internal {
        thash_data_t *hash_base;
        thash_data_t *cur_cch;  // head of overlap search
        int     rid;
        int     ps;
        union {
            u64  tag;           // for VHPT
            struct {            // for TLB
                char    _tr_idx;        // -1: means done of TR search
                char    cl;
                search_section_t s_sect;   // search section combinations
            };
        };
        u64     _curva;         // current address to search
        u64     _eva;
} thash_internal_t;

#define  THASH_CB_MAGIC         0x55aa00aa55aa55aaUL
typedef struct thash_cb {
        /* THASH base information */
        THASH_TYPE      ht;     // For TLB or VHPT
        u64             magic;
        thash_data_t    *hash; // hash table pointer, aligned at thash_sz.
        u64     hash_sz;        // size of above data.
        void    *cch_buf;       // base address of collision chain.
        u64     cch_sz;         // size of above data.
        THASH_FN        *hash_func;
        GET_RR_FN       *get_rr_fn;
        RECYCLE_FN      *recycle_notifier;
        thash_cch_mem_t *cch_freelist;
        struct vcpu *vcpu;
        PTA     pta;
        /* VTLB/VHPT common information */
        FIND_OVERLAP_FN *find_overlap;
        FIND_NEXT_OVL_FN *next_overlap;
        REM_THASH_FN    *rem_hash; // remove hash entry.
        INS_THASH_FN    *ins_hash; // insert hash entry.
        REM_NOTIFIER_FN *remove_notifier;
        /* private information */
        thash_internal_t  priv;
        union {
                tlb_special_t  *ts;
                vhpt_special   *vs;
        };
        // Internal positon information, buffer and storage etc. TBD
} thash_cb_t;

#define ITR(hcb,id)             ((hcb)->ts->itr[id])
#define DTR(hcb,id)             ((hcb)->ts->dtr[id])
#define INVALIDATE_HASH(hcb,hash)           {   \
           if ((hcb)->ht==THASH_TLB)            \
             INVALID_TLB(hash) = 1;             \
           else                                 \
             INVALID_VHPT(hash) = 1;            \
           hash->next = NULL; }

#define PURGABLE_ENTRY(hcb,en)  1
//		((hcb)->ht == THASH_VHPT || ( (en)->tc && !(en->locked)) )


/*
 * Initialize internal control data before service.
 */
extern void thash_init(thash_cb_t *hcb, u64 sz);

/*
 * Insert an entry to hash table. 
 *    NOTES:
 *      1: TLB entry may be TR, TC or Foreign Map. For TR entry,
 *         itr[]/dtr[] need to be updated too.
 *      2: Inserting to collision chain may trigger recycling if 
 *         the buffer for collision chain is empty.
 *      3: The new entry is inserted at the hash table.
 *         (I.e. head of the collision chain)
 *      4: Return the entry in hash table or collision chain.
 *
 */
extern void thash_insert(thash_cb_t *hcb, thash_data_t *entry, u64 va);
extern void thash_tr_insert(thash_cb_t *hcb, thash_data_t *entry, u64 va, int idx);

/*
 * Force to delete a found entry no matter TR or foreign map for TLB. 
 *    NOTES:
 *      1: TLB entry may be TR, TC or Foreign Map. For TR entry,
 *         itr[]/dtr[] need to be updated too.
 *      2: This API must be called after thash_find_overlap() or
 *         thash_find_next_overlap().
 *      3: Return TRUE or FALSE
 *
 */
extern void thash_remove(thash_cb_t *hcb, thash_data_t *entry);
extern void thash_tr_remove(thash_cb_t *hcb, thash_data_t *entry/*, int idx*/);

/*
 * Find an overlap entry in hash table and its collision chain.
 * Refer to SDM2 4.1.1.4 for overlap definition.
 *    PARAS:
 *      1: in: TLB format entry, rid:ps must be same with vrr[].
 *             va & ps identify the address space for overlap lookup
 *      2: section can be combination of TR, TC and FM. (THASH_SECTION_XX)
 *      3: cl means I side or D side.
 *    RETURNS:
 *      NULL to indicate the end of findings.
 *    NOTES:
 *
 */
extern thash_data_t *thash_find_overlap(thash_cb_t *hcb, 
                        thash_data_t *in, search_section_t s_sect);
extern thash_data_t *thash_find_overlap_ex(thash_cb_t *hcb, 
                u64 va, u64 ps, int rid, char cl, search_section_t s_sect);


/*
 * Similar with thash_find_overlap but find next entry.
 *    NOTES:
 *      Intermediate position information is stored in hcb->priv.
 */
extern thash_data_t *thash_find_next_overlap(thash_cb_t *hcb);

/*
 * Find and purge overlap entries in hash table and its collision chain.
 *    PARAS:
 *      1: in: TLB format entry, rid:ps must be same with vrr[].
 *             rid, va & ps identify the address space for purge
 *      2: section can be combination of TR, TC and FM. (thash_SECTION_XX)
 *      3: cl means I side or D side.
 *    NOTES:
 *
 */
extern void thash_purge_entries(thash_cb_t *hcb, 
                        thash_data_t *in, search_section_t p_sect);
extern void thash_purge_entries_ex(thash_cb_t *hcb,
                        u64 rid, u64 va, u64 sz, 
                        search_section_t p_sect, 
                        CACHE_LINE_TYPE cl);
extern void thash_purge_and_insert(thash_cb_t *hcb, thash_data_t *in);

/*
 * Purge all TCs or VHPT entries including those in Hash table.
 *
 */
extern void thash_purge_all(thash_cb_t *hcb);

/*
 * Lookup the hash table and its collision chain to find an entry
 * covering this address rid:va.
 *
 */
extern thash_data_t *vtlb_lookup(thash_cb_t *hcb, 
                        thash_data_t *in);
extern thash_data_t *vtlb_lookup_ex(thash_cb_t *hcb, 
                        u64 rid, u64 va,CACHE_LINE_TYPE cl);
extern int thash_lock_tc(thash_cb_t *hcb, u64 va, u64 size, int rid, char cl, int lock);


#define   ITIR_RV_MASK      (((1UL<<32)-1)<<32 | 0x3)
#define   PAGE_FLAGS_RV_MASK    (0x2 | (0x3UL<<50)|(((1UL<<11)-1)<<53))
extern u64 machine_ttag(PTA pta, u64 va);
extern u64 machine_thash(PTA pta, u64 va);
extern void purge_machine_tc_by_domid(domid_t domid);
extern void machine_tlb_insert(struct vcpu *d, thash_data_t *tlb);
extern ia64_rr vmmu_get_rr(struct vcpu *vcpu, u64 va);
extern thash_cb_t *init_domain_tlb(struct vcpu *d);

#define   VTLB_DEBUG
#ifdef   VTLB_DEBUG
extern void check_vtlb_sanity(thash_cb_t *vtlb);
extern void dump_vtlb(thash_cb_t *vtlb);
#endif

#endif  /* __ASSEMBLY__ */

#endif  /* XEN_TLBthash_H */
