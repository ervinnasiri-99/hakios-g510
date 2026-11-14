/*
 * PMIC wrapper (PWRAP) for the G510 (MT6765).
 * License: APSL 2.0
 */
#ifndef _G510_MTKPWRAP_H_
#define _G510_MTKPWRAP_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Initialize the PWRAP block.
 * Returns 0 on success, error code on failure.
 */
int g510_pwrap_init(void);

/*
 * Read a register from the PMIC via PWRAP WACS2.
 * Returns 0 on success, error code on failure.
 */
int g510_pwrap_read(uint32_t pmic_addr, uint16_t *out_val);

/*
 * Write a register to the PMIC via PWRAP WACS2.
 * Returns 0 on success, error code on failure.
 */
int g510_pwrap_write(uint32_t pmic_addr, uint16_t value);

/*
 * Read MT6358 chip ID to verify PMIC communication.
 * Returns 0 on success, error code on failure.
 */
int g510_pwrap_read_chip_id(uint16_t *out_id);

__END_DECLS

#endif /* _G510_MTKPWRAP_H_ */