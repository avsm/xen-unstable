--- ../../linux-2.6.11/include/asm-ia64/system.h	2005-03-02 00:38:07.000000000 -0700
+++ include/asm-ia64/system.h	2005-05-02 10:18:30.000000000 -0600
@@ -24,8 +24,15 @@
  * 0xa000000000000000+2*PERCPU_PAGE_SIZE
  * - 0xa000000000000000+3*PERCPU_PAGE_SIZE remain unmapped (guard page)
  */
+#ifdef XEN
+#define KERNEL_START		 0xf000000004000000
+#define PERCPU_ADDR		 0xf100000000000000-PERCPU_PAGE_SIZE
+#define SHAREDINFO_ADDR		 0xf100000000000000
+#define VHPT_ADDR		 0xf200000000000000
+#else
 #define KERNEL_START		 __IA64_UL_CONST(0xa000000100000000)
 #define PERCPU_ADDR		(-PERCPU_PAGE_SIZE)
+#endif
 
 #ifndef __ASSEMBLY__
 
@@ -218,9 +225,13 @@
 # define PERFMON_IS_SYSWIDE() (0)
 #endif
 
+#ifdef XEN
+#define IA64_HAS_EXTRA_STATE(t) 0
+#else
 #define IA64_HAS_EXTRA_STATE(t)							\
 	((t)->thread.flags & (IA64_THREAD_DBG_VALID|IA64_THREAD_PM_VALID)	\
 	 || IS_IA32_PROCESS(ia64_task_regs(t)) || PERFMON_IS_SYSWIDE())
+#endif
 
 #define __switch_to(prev,next,last) do {							 \
 	if (IA64_HAS_EXTRA_STATE(prev))								 \
