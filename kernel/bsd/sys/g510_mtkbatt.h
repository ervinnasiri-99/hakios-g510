/*
 * Battery driver for the G510 (MT6765, MT6357/MT6358 PMIC fuel gauge).
 * License: APSL 2.0
 */
#ifndef _G510_MTKBATT_H_
#define _G510_MTKBATT_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for battery driver.
 * Called with boot-arg battery=1.
 */
void g510_mtkbatt_init(void);

__END_DECLS

#endif /* _G510_MTKBATT_H_ */