/*
 * The clock of the cores the kernel runs on (cpufreq=1), and the memory bus's votes (ddr=) for MT6765.
 * License: APSL 2.0
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>

void
hakios_mtk_cpufreq_init(void)
{
    /* TODO: initialize cpufreq from DT OPP table for MT6765 */
}