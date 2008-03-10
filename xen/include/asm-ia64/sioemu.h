/******************************************************************************
 * sioemu.h
 * 
 * Copyright (c) 2008 Tristan Gingold <tgingold@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __ASM_SIOEMU_H_
#define __ASM_SIOEMU_H_
extern void sioemu_hypercall (struct pt_regs *regs);
extern void sioemu_deliver_event (void);
extern void sioemu_callback_return (void);
extern void sioemu_io_emulate (unsigned long padr, unsigned long data,
                              unsigned long data1, unsigned long word);
extern void sioemu_wakeup_vcpu (int vcpu_id);
extern void sioemu_sal_assist (struct vcpu *v);
#endif /* __ASM_SIOEMU_H_ */
