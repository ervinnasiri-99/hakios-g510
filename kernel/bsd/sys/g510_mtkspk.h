/*
 * Speaker amplifier driver for the G510 (MT6765, AW87359).
 * License: APSL 2.0
 */
#ifndef _G510_MTKSPK_H_
#define _G510_MTKSPK_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Initialize AW87359 speaker amplifier.
 * Returns 0 on success, error code on failure.
 */
int g510_mtkspk_init(void);

/*
 * Set speaker volume (0-255).
 * Returns 0 on success, error code on failure.
 */
int g510_mtkspk_set_volume(uint8_t volume);

/*
 * Enable/disable speaker output.
 * Returns 0 on success, error code on failure.
 */
int g510_mtkspk_enable(bool enable);

/*
 * Get speaker fault status.
 * Returns 0 on success, error code on failure.
 */
int g510_mtkspk_get_status(uint8_t *out_status);

__END_DECLS

#endif /* _G510_MTKSPK_H_ */