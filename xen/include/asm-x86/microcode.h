#ifndef ASM_X86__MICROCODE_H
#define ASM_X86__MICROCODE_H

struct cpu_signature;

struct microcode_ops {
    int (*get_matching_microcode)(void *mc, int cpu);
    int (*cpu_request_microcode)(int cpu, const void *buf, size_t size);
    int (*collect_cpu_info)(int cpu_num, struct cpu_signature *csig);
    int (*apply_microcode)(int cpu);
};

struct microcode_header_intel {
    unsigned int hdrver;
    unsigned int rev;
    unsigned int date;
    unsigned int sig;
    unsigned int cksum;
    unsigned int ldrver;
    unsigned int pf;
    unsigned int datasize;
    unsigned int totalsize;
    unsigned int reserved[3];
};

struct microcode_intel {
    struct microcode_header_intel hdr;
    unsigned int bits[0];
};

/* microcode format is extended from prescott processors */
struct extended_signature {
    unsigned int sig;
    unsigned int pf;
    unsigned int cksum;
};

struct extended_sigtable {
    unsigned int count;
    unsigned int cksum;
    unsigned int reserved[3];
    struct extended_signature sigs[0];
};

struct equiv_cpu_entry {
    unsigned int installed_cpu;
    unsigned int fixed_errata_mask;
    unsigned int fixed_errata_compare;
    unsigned int equiv_cpu;
};

struct microcode_header_amd {
    unsigned int  data_code;
    unsigned int  patch_id;
    unsigned char mc_patch_data_id[2];
    unsigned char mc_patch_data_len;
    unsigned char init_flag;
    unsigned int  mc_patch_data_checksum;
    unsigned int  nb_dev_id;
    unsigned int  sb_dev_id;
    unsigned char processor_rev_id[2];
    unsigned char nb_rev_id;
    unsigned char sb_rev_id;
    unsigned char bios_api_rev;
    unsigned char reserved1[3];
    unsigned int  match_reg[8];
};

struct microcode_amd {
    struct microcode_header_amd hdr;
    unsigned int mpb[0];
};

struct cpu_signature {
    unsigned int sig;
    unsigned int pf;
    unsigned int rev;
};

struct ucode_cpu_info {
    struct cpu_signature cpu_sig;
    int valid;
    union {
        struct microcode_intel *mc_intel;
        struct microcode_amd *mc_amd;
        void *valid_mc;
    } mc;
};

extern struct ucode_cpu_info ucode_cpu_info[];
extern const struct microcode_ops *microcode_ops;

#endif /* ASM_X86__MICROCODE_H */
