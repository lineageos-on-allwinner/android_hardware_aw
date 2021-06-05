#ifndef _HWC_VENDOR_SERVICE_H_
#define _HWC_VENDOR_SERVICE_H_

void vendorservice_init();
void homlet_dataspace_change_callback(int dataspace);
void setup_snr_info(struct disp_snr_info* snr);

#endif
