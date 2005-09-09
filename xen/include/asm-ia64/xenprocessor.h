#ifndef _ASM_IA64_XENPROCESSOR_H
#define _ASM_IA64_XENPROCESSOR_H
/*
 * xen specific processor definition
 *
 * Copyright (C) 2005 Hewlett-Packard Co.
 *	Dan Magenheimer (dan.magenheimer@hp.com)
 *
 * Copyright (C) 2005 Intel Co.
 * 	Kun Tian (Kevin Tian) <kevin.tian@intel.com>
 *
 */


#define ia64_is_local_fpu_owner(t) 0

/* like above but expressed as bitfields for more efficient access: */
struct ia64_psr {
	__u64 reserved0 : 1;
	__u64 be : 1;
	__u64 up : 1;
	__u64 ac : 1;
	__u64 mfl : 1;
	__u64 mfh : 1;
	__u64 reserved1 : 7;
	__u64 ic : 1;
	__u64 i : 1;
	__u64 pk : 1;
	__u64 reserved2 : 1;
	__u64 dt : 1;
	__u64 dfl : 1;
	__u64 dfh : 1;
	__u64 sp : 1;
	__u64 pp : 1;
	__u64 di : 1;
	__u64 si : 1;
	__u64 db : 1;
	__u64 lp : 1;
	__u64 tb : 1;
	__u64 rt : 1;
	__u64 reserved3 : 4;
	__u64 cpl : 2;
	__u64 is : 1;
	__u64 mc : 1;
	__u64 it : 1;
	__u64 id : 1;
	__u64 da : 1;
	__u64 dd : 1;
	__u64 ss : 1;
	__u64 ri : 2;
	__u64 ed : 1;
	__u64 bn : 1;
	__u64 ia : 1;
	__u64 vm : 1;
	__u64 reserved5 : 17;
};

/* vmx like above but expressed as bitfields for more efficient access: */
typedef  union{
    __u64 val;
    struct{
    	__u64 reserved0 : 1;
	__u64 be : 1;
    	__u64 up : 1;
    	__u64 ac : 1;
    	__u64 mfl : 1;
    	__u64 mfh : 1;
    	__u64 reserved1 : 7;
    	__u64 ic : 1;
    	__u64 i : 1;
    	__u64 pk : 1;
    	__u64 reserved2 : 1;
    	__u64 dt : 1;
    	__u64 dfl : 1;
    	__u64 dfh : 1;
    	__u64 sp : 1;
    	__u64 pp : 1;
    	__u64 di : 1;
	__u64 si : 1;
    	__u64 db : 1;
    	__u64 lp : 1;
    	__u64 tb : 1;
    	__u64 rt : 1;
    	__u64 reserved3 : 4;
    	__u64 cpl : 2;
    	__u64 is : 1;
    	__u64 mc : 1;
    	__u64 it : 1;
    	__u64 id : 1;
    	__u64 da : 1;
    	__u64 dd : 1;
    	__u64 ss : 1;
    	__u64 ri : 2;
    	__u64 ed : 1;
    	__u64 bn : 1;
    	__u64 reserved4 : 19;
    };
}   IA64_PSR;

typedef union {
    __u64 val;
    struct {
        __u64 code : 16;
        __u64 vector : 8;
        __u64 reserved1 : 8;
        __u64 x : 1;
        __u64 w : 1;
        __u64 r : 1;
        __u64 na : 1;
        __u64 sp : 1;
        __u64 rs : 1;
        __u64 ir : 1;
        __u64 ni : 1;
        __u64 so : 1;
        __u64 ei : 2;
        __u64 ed : 1;
        __u64 reserved2 : 20;
    };
}   ISR;


typedef union {
    __u64 val;
    struct {
        __u64 ve : 1;
        __u64 reserved0 : 1;
        __u64 size : 6;
        __u64 vf : 1;
        __u64 reserved1 : 6;
        __u64 base : 49;
    };
}   PTA;

typedef union {
    __u64 val;
    struct {
        __u64  rv  : 16;
        __u64  eid : 8;
        __u64  id  : 8;
        __u64  ig  : 32;
    };
} LID;

typedef union{
    __u64 val;
    struct {
        __u64 rv  : 3;
        __u64 ir  : 1;
        __u64 eid : 8;
        __u64 id  : 8;
        __u64 ib_base : 44;
    };
} ipi_a_t;

typedef union{
    __u64 val;
    struct {
        __u64 vector : 8;
        __u64 dm  : 3;
        __u64 ig  : 53;
    };
} ipi_d_t;

typedef union {
    __u64 val;
    struct {
        __u64 ig0 : 4;
        __u64 mic : 4;
        __u64 rsv : 8;
        __u64 mmi : 1;
        __u64 ig1 : 47;
    };
} tpr_t;

#define IA64_ISR_CODE_MASK0     0xf
#define IA64_UNIMPL_DADDR_FAULT     0x30
#define IA64_UNIMPL_IADDR_TRAP      0x10
#define IA64_RESERVED_REG_FAULT     0x30
#define IA64_REG_NAT_CONSUMPTION_FAULT  0x10
#define IA64_NAT_CONSUMPTION_FAULT  0x20
#define IA64_PRIV_OP_FAULT      0x10

/* indirect register type */
enum {
    IA64_CPUID,     /*  cpuid */
    IA64_DBR,       /*  dbr */
    IA64_IBR,       /*  ibr */
    IA64_PKR,       /*  pkr */
    IA64_PMC,       /*  pmc */
    IA64_PMD,       /*  pmd */
    IA64_RR         /*  rr */
};

/* instruction type */
enum {
    IA64_INST_TPA=1,
    IA64_INST_TAK
};

/* Generate Mask
 * Parameter:
 *  bit -- starting bit
 *  len -- how many bits
 */
#define MASK(bit,len)                   \
({                              \
        __u64    ret;                    \
                                \
        __asm __volatile("dep %0=-1, r0, %1, %2"    \
                : "=r" (ret):                   \
          "M" (bit),                    \
          "M" (len) );                  \
        ret;                            \
})

typedef union {
	struct {
		__u64 kr0;
		__u64 kr1;
		__u64 kr2;
		__u64 kr3;
		__u64 kr4;
		__u64 kr5;
		__u64 kr6;
		__u64 kr7;
	};
	__u64 _kr[8];
} cpu_kr_ia64_t;

DECLARE_PER_CPU(cpu_kr_ia64_t, cpu_kr);

#endif // _ASM_IA64_XENPROCESSOR_H
