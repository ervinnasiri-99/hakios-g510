/*
 * Microphone driver for the G510 (MT6765, MT6357/MT6358 PMIC).
 * License: APSL 2.0
 */
#ifndef _G510_MTKMIC_H_
#define _G510_MTKMIC_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for microphone.
 * Called with boot-arg mic=1.
 */
void g510_mtkmic_init(void);

__END_DECLS

#endif /* _G510_MTKMIC_H_ */