
#include <stdlib.h>
#include <inttypes.h>

#include "../hwc.h"

extern int primary_disp;

void platform_init(Display_t **disp, int num) {
	// publish service
	/* platform decide the primary disp and second disp */

	int i = 0;
	for (i = 0; i < num; i++) {
		if (disp[i]->displayId == 0)
			disp[i]->displayName = displayName[1];
		else
			disp[i]->displayName = displayName[2];
		disp[i]->clientId = toHwc2client(disp[i]->displayId, disp[i]->displayId);
		disp[i]->plugInListen = 0;
		/* must set  primary_disp  */
	}
	primary_disp = 0;
	ALOGD("hwc use homlet platform.");
}
