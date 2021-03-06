#include <guestloader.h>
#include <arch_types.h>
#include <loadlinux.h>
#include <loadguest.h>
void copy_loader_next_to_guest(void)
{
    uint32_t *src = &loader_start;
    uint32_t *dst = &guest_bin_end;
    uint32_t *end = &guest_bin_start;
    uint32_t offset;
    while (src < end)
        *dst++ = *src++;
    offset = ((uint32_t)(&guest_bin_end - &loader_start) * sizeof(uint32_t));
    asm volatile ("add  lr, lr, %0" : : "r" (offset) : "memory", "cc");
    offset -= 4;
    asm volatile ("add  pc, pc, %0" : : "r" (offset) : "memory", "cc");
}
void copy_guestos_to_start_addr(uint32_t start_addr)
{
    uint32_t *src = &guest_bin_start;
    uint32_t *end = &guest_bin_end;
    uint32_t *dst = (uint32_t *)start_addr;
    while (src < end)
        *dst++ = *src++;
}
void load_linuxguest(uint32_t start_addr)
{
    /* Set atags for booting linux guest */
    loadlinux_setup_tags((uint32_t *) 0x80000000);
    /* Jump to zImage at start_addr */
    loadlinux_run_zImage(start_addr);
}
void load_bmguest(uint32_t start_addr)
{
    /* Jump to 0x8000_0000 */
    asm volatile ("mov pc, %0" : : "r" (start_addr) : "memory", "cc");
}
