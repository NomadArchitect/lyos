/*  This file is part of Lyos.

    Lyos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Lyos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Lyos.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef _HWINT_H_
#define _HWINT_H_

void plic_mask(int hwirq);
void plic_unmask(int hwirq);
void plic_ack(int hwirq);

#define hwint_mask(irq)   plic_mask(irq)
#define hwint_unmask(irq) plic_unmask(irq)
#define hwint_ack(irq)    plic_ack(irq)
#define hwint_used(irq)
#define hwint_not_used(irq)

#endif
