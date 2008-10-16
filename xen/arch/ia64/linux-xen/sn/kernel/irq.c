/*
 * Platform dependent support for SGI SN
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#ifdef XEN
#include <linux/linux-pci.h>
#include <asm/hw_irq.h>
#endif
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#ifndef XEN
#include <asm/sn/pcidev.h>
#endif
#include <asm/sn/shub_mmr.h>
#include <asm/sn/sn_sal.h>

#ifdef XEN
#define pci_dev_get(dev)	do {} while(0)
#define move_native_irq(foo)	do {} while(0)
#endif

static void force_interrupt(int irq);
#ifndef XEN
static void register_intr_pda(struct sn_irq_info *sn_irq_info);
static void unregister_intr_pda(struct sn_irq_info *sn_irq_info);
#endif

int sn_force_interrupt_flag = 1;
extern int sn_ioif_inited;
struct list_head **sn_irq_lh;
static DEFINE_SPINLOCK(sn_irq_info_lock); /* non-IRQ lock */

u64 sn_intr_alloc(nasid_t local_nasid, int local_widget,
				     struct sn_irq_info *sn_irq_info,
				     int req_irq, nasid_t req_nasid,
				     int req_slice)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_ALLOC, (u64) local_nasid,
			(u64) local_widget, __pa(sn_irq_info), (u64) req_irq,
			(u64) req_nasid, (u64) req_slice);

	return ret_stuff.status;
}

void sn_intr_free(nasid_t local_nasid, int local_widget,
				struct sn_irq_info *sn_irq_info)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_FREE, (u64) local_nasid,
			(u64) local_widget, (u64) sn_irq_info->irq_irq,
			(u64) sn_irq_info->irq_cookie, 0, 0);
}

static unsigned int sn_startup_irq(unsigned int irq)
{
	return 0;
}

static void sn_shutdown_irq(unsigned int irq)
{
}

static void sn_disable_irq(unsigned int irq)
{
}

static void sn_enable_irq(unsigned int irq)
{
}

static void sn_ack_irq(unsigned int irq)
{
	u64 event_occurred, mask;

	irq = irq & 0xff;
	event_occurred = HUB_L((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED));
	mask = event_occurred & SH_ALL_INT_MASK;
	HUB_S((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED_ALIAS), mask);
	__set_bit(irq, (volatile void *)pda->sn_in_service_ivecs);

	move_native_irq(irq);
}

static void sn_end_irq(unsigned int irq)
{
	int ivec;
	u64 event_occurred;

	ivec = irq & 0xff;
	if (ivec == SGI_UART_VECTOR) {
		event_occurred = HUB_L((u64*)LOCAL_MMR_ADDR (SH_EVENT_OCCURRED));
		/* If the UART bit is set here, we may have received an
		 * interrupt from the UART that the driver missed.  To
		 * make sure, we IPI ourselves to force us to look again.
		 */
		if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
			platform_send_ipi(smp_processor_id(), SGI_UART_VECTOR,
					  IA64_IPI_DM_INT, 0);
		}
	}
	__clear_bit(ivec, (volatile void *)pda->sn_in_service_ivecs);
	if (sn_force_interrupt_flag)
		force_interrupt(irq);
}

#ifndef XEN
static void sn_irq_info_free(struct rcu_head *head);

struct sn_irq_info *sn_retarget_vector(struct sn_irq_info *sn_irq_info,
				       nasid_t nasid, int slice)
{
	int vector;
	int cpuphys;
	int64_t bridge;
	int local_widget, status;
	nasid_t local_nasid;
	struct sn_irq_info *new_irq_info;
	struct sn_pcibus_provider *pci_provider;

	new_irq_info = kmalloc(sizeof(struct sn_irq_info), GFP_ATOMIC);
	if (new_irq_info == NULL)
		return NULL;

	memcpy(new_irq_info, sn_irq_info, sizeof(struct sn_irq_info));

	bridge = (u64) new_irq_info->irq_bridge;
	if (!bridge) {
		kfree(new_irq_info);
		return NULL; /* irq is not a device interrupt */
	}

	local_nasid = NASID_GET(bridge);

	if (local_nasid & 1)
		local_widget = TIO_SWIN_WIDGETNUM(bridge);
	else
		local_widget = SWIN_WIDGETNUM(bridge);

	vector = sn_irq_info->irq_irq;
	/* Free the old PROM new_irq_info structure */
	sn_intr_free(local_nasid, local_widget, new_irq_info);
	/* Update kernels new_irq_info with new target info */
	unregister_intr_pda(new_irq_info);

	/* allocate a new PROM new_irq_info struct */
	status = sn_intr_alloc(local_nasid, local_widget,
			       new_irq_info, vector,
			       nasid, slice);

	/* SAL call failed */
	if (status) {
		kfree(new_irq_info);
		return NULL;
	}

	cpuphys = nasid_slice_to_cpuid(nasid, slice);
	new_irq_info->irq_cpuid = cpuphys;
	register_intr_pda(new_irq_info);

	pci_provider = sn_pci_provider[new_irq_info->irq_bridge_type];

	/*
	 * If this represents a line interrupt, target it.  If it's
	 * an msi (irq_int_bit < 0), it's already targeted.
	 */
	if (new_irq_info->irq_int_bit >= 0 &&
	    pci_provider && pci_provider->target_interrupt)
		(pci_provider->target_interrupt)(new_irq_info);

	spin_lock(&sn_irq_info_lock);
#ifdef XEN
	list_replace(&sn_irq_info->list, &new_irq_info->list);
#else
	list_replace_rcu(&sn_irq_info->list, &new_irq_info->list);
#endif
	spin_unlock(&sn_irq_info_lock);
#ifndef XEN
	call_rcu(&sn_irq_info->rcu, sn_irq_info_free);
#endif

#ifdef CONFIG_SMP
	set_irq_affinity_info((vector & 0xff), cpuphys, 0);
#endif

	return new_irq_info;
}

static void sn_set_affinity_irq(unsigned int irq, cpumask_t mask)
{
	struct sn_irq_info *sn_irq_info, *sn_irq_info_safe;
	nasid_t nasid;
	int slice;

	nasid = cpuid_to_nasid(first_cpu(mask));
	slice = cpuid_to_slice(first_cpu(mask));

	list_for_each_entry_safe(sn_irq_info, sn_irq_info_safe,
				 sn_irq_lh[irq], list)
		(void)sn_retarget_vector(sn_irq_info, nasid, slice);
}
#endif

struct hw_interrupt_type irq_type_sn = {
#ifndef XEN
	.name		= "SN hub",
#else
	.typename	= "SN hub",
#endif
	.startup	= sn_startup_irq,
	.shutdown	= sn_shutdown_irq,
	.enable		= sn_enable_irq,
	.disable	= sn_disable_irq,
	.ack		= sn_ack_irq,
	.end		= sn_end_irq,
#ifndef XEN
	.set_affinity	= sn_set_affinity_irq
#endif
};

unsigned int sn_local_vector_to_irq(u8 vector)
{
	return (CPU_VECTOR_TO_IRQ(smp_processor_id(), vector));
}

void sn_irq_init(void)
{
	int i;
	irq_desc_t *base_desc = irq_desc;

#ifndef XEN
	ia64_first_device_vector = IA64_SN2_FIRST_DEVICE_VECTOR;
	ia64_last_device_vector = IA64_SN2_LAST_DEVICE_VECTOR;
#endif

	for (i = 0; i < NR_IRQS; i++) {
#ifdef XEN
		if (base_desc[i].handler == &no_irq_type) {
			base_desc[i].handler = &irq_type_sn;
#else
		if (base_desc[i].chip == &no_irq_type) {
			base_desc[i].chip = &irq_type_sn;
#endif
		}
	}
}

static void register_intr_pda(struct sn_irq_info *sn_irq_info)
{
	int irq = sn_irq_info->irq_irq;
	int cpu = sn_irq_info->irq_cpuid;

	if (pdacpu(cpu)->sn_last_irq < irq) {
		pdacpu(cpu)->sn_last_irq = irq;
	}

	if (pdacpu(cpu)->sn_first_irq == 0 || pdacpu(cpu)->sn_first_irq > irq)
		pdacpu(cpu)->sn_first_irq = irq;
}

#ifndef XEN
static void unregister_intr_pda(struct sn_irq_info *sn_irq_info)
{
	int irq = sn_irq_info->irq_irq;
	int cpu = sn_irq_info->irq_cpuid;
	struct sn_irq_info *tmp_irq_info;
	int i, foundmatch;

#ifndef XEN
	rcu_read_lock();
#else
	spin_lock(&sn_irq_info_lock);
#endif
	if (pdacpu(cpu)->sn_last_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_last_irq - 1;
		     i && !foundmatch; i--) {
#ifdef XEN
			list_for_each_entry(tmp_irq_info,
						sn_irq_lh[i],
						list) {
#else
			list_for_each_entry_rcu(tmp_irq_info,
						sn_irq_lh[i],
						list) {
#endif
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch = 1;
					break;
				}
			}
		}
		pdacpu(cpu)->sn_last_irq = i;
	}

	if (pdacpu(cpu)->sn_first_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_first_irq + 1;
		     i < NR_IRQS && !foundmatch; i++) {
#ifdef XEN
			list_for_each_entry(tmp_irq_info,
						sn_irq_lh[i],
						list) {
#else
			list_for_each_entry_rcu(tmp_irq_info,
						sn_irq_lh[i],
						list) {
#endif
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch = 1;
					break;
				}
			}
		}
		pdacpu(cpu)->sn_first_irq = ((i == NR_IRQS) ? 0 : i);
	}
#ifndef XEN
	rcu_read_unlock();
#else
	spin_unlock(&sn_irq_info_lock);
#endif
}

static void sn_irq_info_free(struct rcu_head *head)
{
	struct sn_irq_info *sn_irq_info;

	sn_irq_info = container_of(head, struct sn_irq_info, rcu);
	kfree(sn_irq_info);
}
#endif

#ifdef XEN
void sn_irq_fixup(struct sn_pci_dev *pci_dev, struct sn_irq_info *sn_irq_info)
#else	
void sn_irq_fixup(struct pci_dev *pci_dev, struct sn_irq_info *sn_irq_info)
#endif
{
	nasid_t nasid = sn_irq_info->irq_nasid;
	int slice = sn_irq_info->irq_slice;
	int cpu = nasid_slice_to_cpuid(nasid, slice);

	pci_dev_get(pci_dev);
	sn_irq_info->irq_cpuid = cpu;
#ifndef XEN
	sn_irq_info->irq_pciioinfo = SN_PCIDEV_INFO(pci_dev);
#endif

	/* link it into the sn_irq[irq] list */
	spin_lock(&sn_irq_info_lock);
#ifdef XEN
	list_add(&sn_irq_info->list, sn_irq_lh[sn_irq_info->irq_irq]);
#else
	list_add_rcu(&sn_irq_info->list, sn_irq_lh[sn_irq_info->irq_irq]);
#endif
#ifndef XEN
	reserve_irq_vector(sn_irq_info->irq_irq);
#endif
	spin_unlock(&sn_irq_info_lock);

	register_intr_pda(sn_irq_info);
}

#ifdef XEN
void sn_irq_unfixup(struct sn_pci_dev *pci_dev)
#else
void sn_irq_unfixup(struct pci_dev *pci_dev)
#endif
{
#ifndef XEN
	struct sn_irq_info *sn_irq_info;

	/* Only cleanup IRQ stuff if this device has a host bus context */
	if (!SN_PCIDEV_BUSSOFT(pci_dev))
		return;

	sn_irq_info = SN_PCIDEV_INFO(pci_dev)->pdi_sn_irq_info;
	if (!sn_irq_info)
		return;
	if (!sn_irq_info->irq_irq) {
		kfree(sn_irq_info);
		return;
	}

	unregister_intr_pda(sn_irq_info);
	spin_lock(&sn_irq_info_lock);
#ifdef XEN
	list_del(&sn_irq_info->list);
#else
	list_del_rcu(&sn_irq_info->list);
#endif
	spin_unlock(&sn_irq_info_lock);
	if (list_empty(sn_irq_lh[sn_irq_info->irq_irq]))
		free_irq_vector(sn_irq_info->irq_irq);
#ifndef XEN
	call_rcu(&sn_irq_info->rcu, sn_irq_info_free);
#endif
	pci_dev_put(pci_dev);

#endif
}

static inline void
sn_call_force_intr_provider(struct sn_irq_info *sn_irq_info)
{
	struct sn_pcibus_provider *pci_provider;

	pci_provider = sn_pci_provider[sn_irq_info->irq_bridge_type];
	if (pci_provider && pci_provider->force_interrupt)
		(*pci_provider->force_interrupt)(sn_irq_info);
}

static void force_interrupt(int irq)
{
	struct sn_irq_info *sn_irq_info;

#ifndef XEN
	if (!sn_ioif_inited)
		return;
#endif

#ifdef XEN
	spin_lock(&sn_irq_info_lock);
#else
	rcu_read_lock();
#endif
#ifdef XEN
	list_for_each_entry(sn_irq_info, sn_irq_lh[irq], list)
#else
	list_for_each_entry_rcu(sn_irq_info, sn_irq_lh[irq], list)
#endif
		sn_call_force_intr_provider(sn_irq_info);

#ifdef XEN
	spin_unlock(&sn_irq_info_lock);
#else
	rcu_read_unlock();
#endif
}

#ifndef XEN
/*
 * Check for lost interrupts.  If the PIC int_status reg. says that
 * an interrupt has been sent, but not handled, and the interrupt
 * is not pending in either the cpu irr regs or in the soft irr regs,
 * and the interrupt is not in service, then the interrupt may have
 * been lost.  Force an interrupt on that pin.  It is possible that
 * the interrupt is in flight, so we may generate a spurious interrupt,
 * but we should never miss a real lost interrupt.
 */
static void sn_check_intr(int irq, struct sn_irq_info *sn_irq_info)
{
	u64 regval;
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;

	/*
	 * Bridge types attached to TIO (anything but PIC) do not need this WAR
	 * since they do not target Shub II interrupt registers.  If that
	 * ever changes, this check needs to accomodate.
	 */
	if (sn_irq_info->irq_bridge_type != PCIIO_ASIC_TYPE_PIC)
		return;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (!pcidev_info)
		return;

	pcibus_info =
	    (struct pcibus_info *)pcidev_info->pdi_host_pcidev_info->
	    pdi_pcibus_info;
	regval = pcireg_intr_status_get(pcibus_info);

	if (!ia64_get_irr(irq_to_vector(irq))) {
		if (!test_bit(irq, pda->sn_in_service_ivecs)) {
			regval &= 0xff;
			if (sn_irq_info->irq_int_bit & regval &
			    sn_irq_info->irq_last_intr) {
				regval &= ~(sn_irq_info->irq_int_bit & regval);
				sn_call_force_intr_provider(sn_irq_info);
			}
		}
	}
	sn_irq_info->irq_last_intr = regval;
}
#endif

void sn_lb_int_war_check(void)
{
#ifndef XEN
	struct sn_irq_info *sn_irq_info;
	int i;

#ifdef XEN
	if (pda->sn_first_irq == 0)
#else
	if (!sn_ioif_inited || pda->sn_first_irq == 0)
#endif
		return;

#ifdef XEN
	spin_lock(&sn_irq_info_lock);
#else
	rcu_read_lock();
#endif
	for (i = pda->sn_first_irq; i <= pda->sn_last_irq; i++) {
#ifdef XEN
		list_for_each_entry(sn_irq_info, sn_irq_lh[i], list) {
#else
		list_for_each_entry_rcu(sn_irq_info, sn_irq_lh[i], list) {
#endif
			sn_check_intr(i, sn_irq_info);
		}
	}
#ifdef XEN
	spin_unlock(&sn_irq_info_lock);
#else
	rcu_read_unlock();
#endif
#endif
}

void __init sn_irq_lh_init(void)
{
	int i;

	sn_irq_lh = kmalloc(sizeof(struct list_head *) * NR_IRQS, GFP_KERNEL);
	if (!sn_irq_lh)
		panic("SN PCI INIT: Failed to allocate memory for PCI init\n");

	for (i = 0; i < NR_IRQS; i++) {
		sn_irq_lh[i] = kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (!sn_irq_lh[i])
			panic("SN PCI INIT: Failed IRQ memory allocation\n");

		INIT_LIST_HEAD(sn_irq_lh[i]);
	}
}
