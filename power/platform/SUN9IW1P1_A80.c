/*
 * Copyright (C) 2018 The Allwinnertech
 *
 * 
 * Author:jiangbin@allwinnertech.com
 * Time:20180802
 * 
 * description:
 *     Target TARGET_BOARD_PLATFORM: kylin(A80)
 *     power_hint-policy
 */

#include <string.h>
#include <stdio.h>

#define  LOG_TAG "AW_PowerHAL_Platform"
#include <hardware/hardware.h>
#include <hardware/power.h>
#include <utils/Log.h>
#include "../power-common.h"

void power_set_interactive_platform(int enable) {

//reserve
}

/*
 *return: false -check fail
 *		  true  -check success
*/

static bool check_policy_platform(unsigned int hint, int parmdata) {

   //reserve

   return true;

}

bool power_hint_platform(unsigned int hint,int parmdata) {

	Power_Perfromance_State current_state = get_cur_power_state();

	if(!check_policy_platform(hint,parmdata)) {
		ALOGI("==check policy fail,do nothing==");
		return false;
	}

	   switch (hint) {
		   case POWER_HINT_LAUNCH:
		   {
			   if((parmdata== 1) && (current_state != POWER_STATE_LAUNCH)) {
				   set_power_state(POWER_STATE_LAUNCH);
				   sysfs_write(ROOMAGE,ROOMAGE_LAUNCH);
			   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_CLOSE_EMUL_TEMP);

				   
				   ALOGI("==LAUNCH_MODE==");
			   
			   } else if(current_state == POWER_STATE_LAUNCH) {
				   set_power_state(POWER_STATE_NORMAL);
				   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_OPEN_EMUL_TEMP);
				   sysfs_write(ROOMAGE,ROOMAGE_NORMAL);
				   ALOGI("==LAUNCH_NORMAL==");
			   } else {
				   ALOGE("==HINT_LAUNCH_ERROR==");
			   }
			   
		   }
		   	   break;

		   case POWER_HINT_HOME:
			   ALOGI("==HOME MODE==");
			   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_OPEN_EMUL_TEMP);
			   sysfs_write(ROOMAGE,ROOMAGE_HOME);
		   
			   sysfs_write(DRAMSCEN,DRAM_HOME);
		   
			   sysfs_write(GPUFREQ,GPU_NORMAL);
			   set_power_state(POWER_STATE_HOME);

		   break;

		   case POWER_HINT_BOOTCOMPLETE:
			   ALOGI("==BOOTCOMPLETE MODE==");
			   sysfs_write(CPU0GOV,INTERACTIVE_GOVERNOR);
			   sysfs_write(CPUHOT,"1");
			   break;
		   case POWER_HINT_BENCHMARK:
			   if(current_state == POWER_STATE_BENCHMARK) {
				   ALOGD("HINT_BENCHMARK:same state");
				   return hitcase;
			   }
			   if(parmdata == 0) {
				   ALOGE("HINT_BENCHMARK:data==NULL");
				   return hitcase;
			   }else {
				   ALOGD("HINT_BENCHMARK:data =%d",parmdata);
			   }
			   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_CLOSE_EMUL_TEMP);
			   sysfs_write(ROOMAGE,ROOMAGE_BENCHMARK);
			   
			   sysfs_write(GPUFREQ,GPU_PERF);
		   
			   set_power_state(POWER_STATE_BENCHMARK);
			   ALOGI("==BENCHMARK MODE==");
               break;
		   case POWER_HINT_NORMAL:
		   	   ALOGI("==NORMAL MODE==");
			   sysfs_write(CPU0GOV,INTERACTIVE_GOVERNOR);
			   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_OPEN_EMUL_TEMP);
			   sysfs_write(ROOMAGE,ROOMAGE_NORMAL);
			   
			   sysfs_write(DRAMSCEN,DRAM_NORMAL);



			   sysfs_write(GPUFREQ,GPU_NORMAL);
			   set_power_state(POWER_STATE_NORMAL);
			   break;
		   case POWER_HINT_BG_MUSIC:

			   ALOGI("==BG_MUSIC MODE==");
			   sysfs_write(THERMAL_EMUL_TEMP,THERMAL_OPEN_EMUL_TEMP);
			   sysfs_write(ROOMAGE, ROOMAGE_MUSIC);

			   sysfs_write(DRAMSCEN,DRAM_BGMUSIC);
		       set_power_state(POWER_STATE_MUSIC);


			   sysfs_write(GPUFREQ,GPU_NORMAL);



			   break;
			   
		   default:
			   hitcase = false;
			   break;
		  }
	   return hitcase;

}




