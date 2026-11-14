/*
 * Camera driver for the G510 (MT6765).
 * License: APSL 2.0
 */
#ifndef _G510_MTKCAM_H_
#define _G510_MTKCAM_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for camera driver.
 * Called with boot-arg camera=1.
 */
void g510_mtkcam_init(void);

__END_DECLS

#endif /* _G510_MTKCAM_H_ */