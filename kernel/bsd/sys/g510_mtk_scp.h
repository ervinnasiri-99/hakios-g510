/*
 * SCP sensor hub driver for the G510 (MT6765).
 * License: APSL 2.0
 */
#ifndef _G510_MTK_SCP_H_
#define _G510_MTK_SCP_H_

#include <sys/types.h>

__BEGIN_DECLS

/*
 * Main initialization function for SCP sensor hub.
 * Called with boot-arg sensors=1.
 */
void g510_mtk_scp_init(void);

__END_DECLS

#endif /* _G510_MTK_SCP_H_ */