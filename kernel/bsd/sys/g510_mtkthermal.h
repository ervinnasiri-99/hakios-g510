/*
 * Thermal driver for the G510 (MT6765).
 * License: APSL 2.0
 */
#ifndef _G510_MTKTHERMAL_H_
#define _G510_MTKTHERMAL_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for thermal driver.
 * Called with boot-arg thermal=1.
 */
void g510_mtkthermal_init(void);

__END_DECLS

#endif /* _G510_MTKTHERMAL_H_ */