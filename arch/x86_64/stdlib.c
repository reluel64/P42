#if 0
void gdt_setup(void)
{
    memset(&gdt, 0, sizeof(gdt));
    uint64_t tss_addr = (uint64_t)&tss;
    /* CODE segment */
    gdt[1].type = ((1 << 1) | (1 << 3) | (1 << 4) | (1 << 7));
    gdt[1].flags = (1 << 1) | (1 << 3);

    /* DATA segment */
    gdt[2].type = ((1 << 1) | (1 << 4) | (1 << 7)) ;
    gdt[2].flags = (1 << 1) | (1 << 3);


    /* TSS Segment */

    gdt[3].type = (1 << 1) | (1 << 7) ;
    gdt[3].flags = (1 << 3);

    gdt[3].seg_base_low = tss_addr & 0xffffff;
    gdt[3].seg_base_mid = ((tss_addr >> 16) & 0xff);
    gdt[3].seg_base_high = ((tss_addr >> 24) & 0xff);
    *((uint32_t*)&gdt[4]) = (tss_addr >> 32);

}

#endif