/*
 *	linux/arch/x86_64/kernel/ioport.c
 *
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <asm-xen/xen-public/dom0_ops.h>

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 *
 */

// asmlinkage long sys_iopl(unsigned int level, struct pt_regs *regs)
asmlinkage long sys_iopl(unsigned int new_io_pl)
{
        unsigned int old_io_pl = current->thread.io_pl;
        dom0_op_t op;


	if (new_io_pl > 3)
		return -EINVAL;
	/* Trying to gain more privileges? */
	if (new_io_pl > old_io_pl) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
        
        if (!(xen_start_info.flags & SIF_PRIVILEGED))
                return -EPERM;

	/* Maintain OS privileges even if user attempts to relinquish them. */
	if (new_io_pl == 0)
		new_io_pl = 1;

	/* Change our version of the privilege levels. */
	current->thread.io_pl = new_io_pl;

	/* Force the change at ring 0. */
	op.cmd           = DOM0_IOPL;
	op.u.iopl.domain = DOMID_SELF;
	op.u.iopl.iopl   = new_io_pl;
	HYPERVISOR_dom0_op(&op);

	return 0;
        
}

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
  return turn_on ? sys_iopl(3) : 0;
}
