/*
 * PowerVR GE8320 GPU driver for the G510 (MT6765).
 * License: APSL 2.0
 */
#ifndef _G510_MTKPVRGPU_H_
#define _G510_MTKPVRGPU_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for PowerVR GPU.
 * Called with boot-arg mali=1 (or pvr=1).
 */
void g510_mtkpvrgpu_init(void);

__END_DECLS

#endif /* _G510_MTKPVRGPU_H_ */