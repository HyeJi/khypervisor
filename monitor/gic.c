#include "gic.h"
#include "armv7_p15.h"
#include "a15_cp15_sysregs.h"
#include "uart_print.h"
#include "smp.h"

#define CBAR_PERIPHBASE_MSB_MASK	0x000000FF

#define ARM_CPUID_CORTEXA15   0x412fc0f1

#define MIDR_MASK_PPN		(0x0FFF <<4)
#define MIDR_PPN_CORTEXA15	(0xC0F << 4)

#define GIC_OFFSET_GICD		0x1000
#define GIC_OFFSET_GICC		0x2000
#define GIC_OFFSET_GICH		0x4000
#define GIC_OFFSET_GICV		0x5000
#define GIC_OFFSET_GICVI	0x6000

#define GICD_CTLR	0x000
#define GICD_TYPER	(0x004/4)
#define GICD_IIDR	(0x008/4)
#define GICD_ISENABLER	(0x100/4)
#define GICD_ICENABLER	(0x180/4)
#define GICD_IPRIORITYR	(0x400/4)
#define GICD_ITARGETSR	(0x800/4) 
#define GICD_ICFGR	(0xC00/4)

#define GICC_CTLR	(0x0000/4)
#define GICC_PMR	(0x0004/4)
#define GICC_BPR	(0x0008/4)

#define GICD_CTLR_ENABLE	0x1
#define GICD_TYPE_LINES_MASK	0x01f
#define GICD_TYPE_CPUS_MASK	0x0e0
#define GICD_TYPE_CPUS_SHIFT	5

#define GICC_CTL_ENABLE 	0x1
#define GICC_CTL_EOI    	(0x1 << 9)

struct gic {
	uint32_t baseaddr;
	volatile uint32_t *ba_gicd;
	volatile uint32_t *ba_gicc;
	volatile uint32_t *ba_gich;
	volatile uint32_t *ba_gicv;
	volatile uint32_t *ba_gicvi;
	uint32_t lines;
	uint32_t cpus;
};

static struct gic _gic;

void gic_dump_registers(void)
{

	uint32_t midr;

	HVMM_TRACE_ENTER();

	midr = read_midr();
	uart_print( "midr:"); uart_print_hex32(midr); uart_print("\n\r");

	if ( (midr & MIDR_MASK_PPN) == MIDR_PPN_CORTEXA15) {
		uint32_t value;
		uart_print( "cbar:"); uart_print_hex32(_gic.baseaddr); uart_print("\n\r");
		uart_print( "ba_gicd:"); uart_print_hex32((uint32_t)_gic.ba_gicd); uart_print("\n\r");
		uart_print( "ba_gicc:"); uart_print_hex32((uint32_t)_gic.ba_gicc); uart_print("\n\r");
		uart_print( "ba_gich:"); uart_print_hex32((uint32_t)_gic.ba_gich); uart_print("\n\r");
		uart_print( "ba_gicv:"); uart_print_hex32((uint32_t)_gic.ba_gicv); uart_print("\n\r");
		uart_print( "ba_gicvi:"); uart_print_hex32((uint32_t)_gic.ba_gicvi); uart_print("\n\r");
		value = _gic.ba_gicd[GICD_CTLR]; uart_print( "GICD_CTLR:"); uart_print_hex32(value); uart_print("\n\r");
		value = _gic.ba_gicd[GICD_TYPER]; uart_print( "GICD_TYPER:"); uart_print_hex32(value); uart_print("\n\r");
		value = _gic.ba_gicd[GICD_IIDR]; uart_print( "GICD_IIDR:"); uart_print_hex32(value); uart_print("\n\r");
	}
	HVMM_TRACE_EXIT();
}


uint64_t gic_periphbase_pa(void)
{
// CBAR:   4,  c0,   0
// MRC p15, 4, <Rt>, c15, c0, 0; Read Configuration Base Address Register
	uint64_t periphbase = (uint64_t) read_cbar();
	uint64_t pbmsb = periphbase & ((uint64_t)CBAR_PERIPHBASE_MSB_MASK);
	if ( pbmsb ) {
		periphbase &= ~ ((uint64_t)CBAR_PERIPHBASE_MSB_MASK);
		periphbase |= (pbmsb << 32);
	}
	
	return periphbase;
}

hvmm_status_t gic_init_baseaddr(uint32_t *va_base)
{
// MIDR[15:4], CRn:c0, Op1:0, CRm:c0, Op2:0  == 0xC0F (Cortex-A15)
// Cortex-A15 C15 System Control, C15 Registers
// Name: Op1, CRm, Op2
	uint32_t midr;
	hvmm_status_t result = HVMM_STATUS_UNKNOWN_ERROR;

	HVMM_TRACE_ENTER();

	midr = read_midr();
	uart_print( "midr:"); uart_print_hex32(midr); uart_print("\n\r");

	/* 
	 * Note:
	 * We currently support GICv2 with Cortex-A15 only. 
	 * Other architectures with GICv2 support will be further listed and added for support later
	 */
	if ( (midr & MIDR_MASK_PPN) == MIDR_PPN_CORTEXA15) {
		/* fall-back to periphbase addr from cbar */
		if ( va_base == 0 ) va_base = (uint32_t *) (uint32_t) (gic_periphbase_pa() & 0x00000000FFFFFFFFULL);
		_gic.baseaddr = (uint32_t) va_base;
		uart_print( "cbar:"); uart_print_hex32(_gic.baseaddr); uart_print("\n\r");
		_gic.ba_gicd = (uint32_t *) (_gic.baseaddr + GIC_OFFSET_GICD);
		_gic.ba_gicc = (uint32_t *) (_gic.baseaddr + GIC_OFFSET_GICC);
		_gic.ba_gich = (uint32_t *) (_gic.baseaddr + GIC_OFFSET_GICH);
		_gic.ba_gicv = (uint32_t *) (_gic.baseaddr + GIC_OFFSET_GICV);
		_gic.ba_gicvi = (uint32_t *) (_gic.baseaddr + GIC_OFFSET_GICVI);

		result = HVMM_STATUS_SUCCESS;
	} else {
		uart_print( "GICv2 Unsupported\n\r" );
		uart_print( "midr.ppn:"); uart_print_hex32(midr & MIDR_MASK_PPN); uart_print("\n\r");

		result = HVMM_STATUS_UNSUPPORTED_FEATURE;
	}

	HVMM_TRACE_EXIT();
	return result;
}


hvmm_status_t gic_init_dist(void)
{
	uint32_t type;
	int i;
	uint32_t cpumask;

	HVMM_TRACE_ENTER();

	/* Disable Distributor */
	_gic.ba_gicd[GICD_CTLR] = 0;

	type = _gic.ba_gicd[GICD_TYPER];
	_gic.lines = 32 * ((type & GICD_TYPE_LINES_MASK) + 1);
	_gic.cpus = 1 + ((type & GICD_TYPE_CPUS_MASK) >> GICD_TYPE_CPUS_SHIFT);
	uart_print( "GIC: lines:"); uart_print_hex32(_gic.lines ); 
	uart_print( " cpus:"); uart_print_hex32(_gic.cpus);
	uart_print( " IID:" ); uart_print_hex32(_gic.ba_gicd[GICD_IIDR]); uart_print("\n\r");

	/* Interrupt polarity for SPIs (Global Interrupts) active-low */
	for( i = 32; i < _gic.lines; i += 16 ) {
		_gic.ba_gicd[GICD_ICFGR + i / 16] = 0x0;
	}

	/* Default Priority for all Interrupts
	 * Private/Banked interrupts will be configured separately 
	 */
	for( i = 32; i < _gic.lines; i+= 4 ) {
		_gic.ba_gicd[GICD_IPRIORITYR + i / 4] = 0xa0a0a0a0;
	}

	/* Disable all global interrupts. 
	 * Private/Banked interrupts will be configured separately 
	 */
	for( i = 32; i < _gic.lines; i+= 32 ) {
		_gic.ba_gicd[GICD_ICENABLER + i / 32] = 0xFFFFFFFF;
	}

	/* Route all global IRQs to this CPU */
	cpumask = 1 << smp_processor_id();
	cpumask |= cpumask << 8;
	cpumask |= cpumask << 16;
	for( i = 32; i < _gic.lines; i += 4 ) {
		_gic.ba_gicd[GICD_ITARGETSR + i / 4] = cpumask;
	}


	/* Enable Distributor */
	_gic.ba_gicd[GICD_CTLR] = GICD_CTLR_ENABLE;
	HVMM_TRACE_EXIT();
	return HVMM_STATUS_SUCCESS;
}

/* Initializes GICv2 CPU Interface */
hvmm_status_t gic_init_cpui(void)
{
	hvmm_status_t result = HVMM_STATUS_UNKNOWN_ERROR;
	int i;

	/* Disable forwarding PPIs(ID16~31) */
	_gic.ba_gicd[GICD_ICENABLER] = 0xFFFF0000;
	/* Enable forwarding SGIs(ID0~15) */
	_gic.ba_gicd[GICD_ISENABLER] = 0x0000FFFF;
	
	/* Default priority for SGIs and PPIs */
	for( i = 0; i < 32; i += 4 ) {
		_gic.ba_gicd[GICD_IPRIORITYR | i/4] = 0xa0a0a0a0;
	}

	/* No Priority Masking: the lowest value as the threshold : 255 */
	_gic.ba_gicc[GICC_PMR] = 0xFF;
	_gic.ba_gicc[GICC_BPR] = 0x0;

	/* Enable signaling of interrupts and GICC_EOIR only drops priority */
	_gic.ba_gicc[GICC_CTLR] = GICC_CTL_ENABLE | GICC_CTL_EOI;

	return result;
}

hvmm_status_t gic_init(void)
{
	hvmm_status_t result = HVMM_STATUS_UNKNOWN_ERROR;

	HVMM_TRACE_ENTER();
	/*
	 * Determining VA of GIC base adddress has not been defined yet. Let is use the PA for the time being
	 */
	result = gic_init_baseaddr(0);

	if ( result == HVMM_STATUS_SUCCESS ) {
		gic_dump_registers();
	}

	/*
	 * Initialize and Enable GIC Distributor
	 */
	if ( result == HVMM_STATUS_SUCCESS ) {
		result = gic_init_dist();
	}

	/*
	 * Initialize and Enable GIC CPU Interface for this CPU
	 */
	if ( result == HVMM_STATUS_SUCCESS ) {
		result = gic_init_cpui();
	}

	HVMM_TRACE_EXIT();
	return result;
}

/*
 * Note: Sequence of Routing and Requesting an IRQ

1. Routing

1.1 Disable Routing
	GICD_ICENABLER[irq] = 1

static void gic_irq_shutdown(struct irq_desc *desc)
{
    int irq = desc->irq;

    // Disable forwarding 
    GICD[GICD_ICENABLER + irq / 32] = (1u << (irq % 32));
}

1.2 Edge/Level
	GICD_ICFGR[irq] = edge | level

1.3 Routing: Target CPU
	GICD_ITARGETSR[irq] = cpumask

1.4 Priority
	GICD_IPRIORITY[irq] = priority
** Example

// needs to be called with gic.lock held
static void gic_set_irq_properties(unsigned int irq, bool_t level,
        unsigned int cpu_mask, unsigned int priority)
{
    volatile unsigned char *bytereg;
    uint32_t cfg, edgebit;

    // Set edge / level 
    cfg = GICD[GICD_ICFGR + irq / 16];
    edgebit = 2u << (2 * (irq % 16));
    if ( level )
        cfg &= ~edgebit;
    else
        cfg |= edgebit;
    GICD[GICD_ICFGR + irq / 16] = cfg;

    // Set target CPU mask (RAZ/WI on uniprocessor) 
    bytereg = (unsigned char *) (GICD + GICD_ITARGETSR);
    bytereg[irq] = cpu_mask;

    // Set priority 
    bytereg = (unsigned char *) (GICD + GICD_IPRIORITYR);
    bytereg[irq] = priority;
}

2. Enable Forwarding
	GICD_ISENABLER[irq] = enable
 */
