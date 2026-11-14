/*
 * The clock of the cores the kernel runs on (cpufreq=1), and the memory bus's votes (ddr=).
 * License: APSL 2.0
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include "g510_cpufreq.h"

void
g510_cpufreq_init(void)
{
    /* TODO: initialize cpufreq from DT OPP table */
}