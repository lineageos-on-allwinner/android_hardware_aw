#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "type.h"

#include "libhwinfo.h"

extern driver_cmd_wext_cb bcm_wext_cb;
extern driver_cmd_wext_cb rtl_wext_cb;
extern driver_cmd_wext_cb  xr_wext_cb;
extern driver_cmd_wext_cb ssv_wext_cb;

static driver_cmd_wext_cb *pcb = NULL;

static int check_initialed(void)
{
	const char *vendor_name = NULL;

	if (pcb != NULL)
		return 0;

	if ((vendor_name = get_wifi_vendor_name()) == NULL)
		return -1;

	wpa_printf(MSG_WARNING, "%s for wext, use %s wpa_supplicant_8_lib.", __func__, vendor_name);
	if (strcmp(vendor_name, "broadcom") == 0)
		pcb = &bcm_wext_cb;
	else if (strcmp(vendor_name, "realtek") == 0)
		pcb = &rtl_wext_cb;
	else if (strcmp(vendor_name, "xradio") == 0)
		pcb = &xr_wext_cb;
	else if (strcmp(vendor_name, "ssv") == 0)
		pcb = &ssv_wext_cb;
	else
		return -1;

	return 0;
}

int wpa_driver_wext_combo_scan(void *priv, struct wpa_driver_scan_params *params)
{
	if (check_initialed() == 0)
		return pcb->wpa_driver_wext_combo_scan(priv, params);

	return 0;
}

int wpa_driver_wext_driver_cmd(void *priv, char *cmd, char *buf, size_t buf_len)
{
	if (check_initialed() == 0)
		return pcb->wpa_driver_wext_driver_cmd(priv, cmd, buf, buf_len);

	return 0;
}

int wpa_driver_signal_poll(void *priv, struct wpa_signal_info *si)
{
	if (check_initialed() == 0)
		return pcb->wpa_driver_signal_poll(priv, si);

	return 0;
}

