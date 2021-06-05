/*
 * Copyright (C) Allwinner Tech All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../hwc.h"

/* this is for A64 P3 ,
  * and ddr ram controler do not give us way to control it ,
  * so we will use experience
  */
struct mem_speed_limit_t{
	int ddrKHz;
	int demem;
};

typedef struct memCtrlInfo{
	int globlimit;// will varible for dvfs ddr
	int globcurlimit;
	int globcurrent;
	int globReseveMem;
	int dealReseveMem;
	int cnt;
	int numdisp;
	int globTRMemLimit;//for tr memctrl
	int currentTRMemLimit;
	int globTRPixelLimit;
	int currentTRPixelLimit;
	Display_t **display;
	int maxClientTarget;
}memCtrlInfo_t;

memCtrlInfo_t globCtrl;

static struct mem_speed_limit_t mem_speed_limit[3] =
{
#if (TARGET_BOARD_PLATFORM == tulip || TARGET_BOARD_PLATFORM == venus)

		 {672000, 37324800},
		 {552000, 29030400},
		 {432000, 20736000},
#elif (TARGET_BOARD_PLATFORM == uranus)

		{672000, 49152000},
		{552000, 49152000},
		{432000, 30736000},
#elif (TARGET_BOARD_PLATFORM == t8)
		{672000, 49152000},
		{552000, 49152000},
		{432000, 30736000},
#elif (TARGET_BOARD_PLATFORM == petrel)
		{672000, 49152000},
		{552000, 49152000},
		{432000, 30736000},
#elif (TARGET_BOARD_PLATFORM == cupid)
		 {672000, 49152000},
		 {552000, 49152000},
		 {432000, 30736000},
#else
		 {672000, 37324800},
		 {552000, 29030400},
		 {432000, 20736000},
/*#error "please select a platform\n"*/
#endif
};

static inline int memCtrlFbSize(Display_t *display)
{
	DisplayConfig_t *config;
	int max = 0;

	if (!display->plugIn) {
		return 0;
	}
	/* todo for homelet, need chang to var dp */
	config = display->displayConfigList[display->activeConfigId];
	if( max < config->height * config->width * 4)
		max = config->height * config->width * 4;

	return max;
}

static inline int memCtrlCheckResv(Layer_t *layer)
{
	int memcurrntresv;
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;

	if(handle == NULL)
		return 0;
	if (!layerIsVideo(layer)) {
		return 0;
	}

	memcurrntresv = (int)ceilf((layer->crop.right - layer->crop.left)
										* (layer->crop.bottom - layer->crop.top)
										* getBitsPerPixel(layer) / 8);
	layer->memresrve = 1;
	return memcurrntresv;
}

void memCtrlDealFBLayer(Layer_t *layer, bool add)
{
	int mem =  (int)ceilf((layer->crop.right - layer->crop.left)
			* (layer->crop.bottom - layer->crop.top)
			* getBitsPerPixel(layer) / 8);
	if (add)
		globCtrl.globReseveMem += mem;
	else
		globCtrl.globReseveMem -= mem;
}

void memCtrlDealLayer(Layer_t *layer, bool add)
{
	int mem =  (int)ceilf((layer->crop.right - layer->crop.left)
			* (layer->crop.bottom - layer->crop.top)
			* getBitsPerPixel(layer) / 8);
	if (!layer->memresrve)
		return;
	if (add)
		globCtrl.dealReseveMem += mem;
	else
		globCtrl.dealReseveMem -= mem;

}

void memCtrlDelCur(int mem)
{
	globCtrl.globcurrent -= mem;
}

bool memCtrlAddLayer(Display_t *display, Layer_t *layer, int* pAddmem)
{
	DisplayOpr_t *opt;
	int addmem = 0, add = 0, srcmem = 0;
	opt = display->displayOpration;
	if (checkSoildLayer(layer)) {
		*pAddmem = 0;
		return true;
	}
	srcmem = (int)ceilf((layer->crop.right - layer->crop.left)
							* (layer->crop.bottom - layer->crop.top)
							* getBitsPerPixel(layer) / 8);

	addmem = opt->memCtrlAddLayer(display, layer);
	add = addmem;
	if (layer->memresrve
		|| layer->compositionType == HWC2_COMPOSITION_CLIENT_TARGET) {
		add = addmem - srcmem;
		if (add > 0)
			ALOGE("Cal the mem err %d", add);
	}
	/* if 2 or more video layer  overlay so add < srcmem, so del srcmem from dealReseveMem add <= 0
	  * if FB so the add = 0, and globReseveMem has include FB
	  */
	if (add + globCtrl.globReseveMem - globCtrl.dealReseveMem + globCtrl.globcurrent > globCtrl.globcurlimit){
		ALOGV("memctrl layer:%p: %d %d %d  %d  %d  %d", layer, add, globCtrl.globlimit,
			globCtrl.globcurlimit, globCtrl.globcurrent, globCtrl.globReseveMem, globCtrl.dealReseveMem);
		return false;
	}
	if (layer->compositionType == HWC2_COMPOSITION_CLIENT_TARGET)
		globCtrl.globReseveMem -= addmem;
	globCtrl.globcurrent += addmem;

	*pAddmem = addmem;
	return true;
}

void memContrlComplet(Display_t *display)
{
	int max = 0;
	unusedpara(display);
	for (int i = 0; i < globCtrl.numdisp; i++){
		max +=  globCtrl.display[i]->active;
	}
	if (globCtrl.cnt < max)
		return;
	globCtrl.cnt = 0;
}

void memResetPerframe(Display_t *display)
{
	int i = 0;
	struct listnode *node;
	Layer_t *layer;

	if (globCtrl.cnt == 0) {
		globCtrl.globcurrent = 0;
		globCtrl.dealReseveMem = 0;
		globCtrl.currentTRMemLimit = 0;
		globCtrl.currentTRPixelLimit = 0;
		globCtrl.globReseveMem = 0;
		for (i = 0; i < 2; i++) {
			if (!globCtrl.display[i]->active)
				continue;
			/* Must reserve for 2nd display */
			if (toClientId(globCtrl.display[i]->clientId) != 0)
				globCtrl.globReseveMem += memCtrlFbSize(globCtrl.display[i]);
#ifdef ENABLE_WRITEBACK
                               if (globCtrl.display[i]->displayId == 1) {
                                       continue;
                               }
#endif
			list_for_each(node, globCtrl.display[i]->layerSortedByZorder) {
				layer = node_to_item(node, Layer_t, node);
				globCtrl.globReseveMem += memCtrlCheckResv(layer);
			}
		}
	}
	/* delete the 2nd fb */
	if (toClientId(display->clientId) != 0)
		globCtrl.globReseveMem -= memCtrlFbSize(display);
	globCtrl.cnt++;

}

void memCtrlLimmitSet(Display_t *display, int screen)
{
	DisplayConfig_t *config;
	Display_t *displaytmp;

	if (display->forceClient) {
		for(int i = 0; i < globCtrl.numdisp; i++) {
			displaytmp = globCtrl.display[i];
			if (!displaytmp->plugIn)
				continue;
			config = displaytmp->displayConfigList[displaytmp->activeConfigId];
			if (i == 0)
				globCtrl.globcurlimit = config->height * config->width * 4;
			else
				globCtrl.globcurlimit += config->height * config->width * 4;
		}
	}else{
		globCtrl.globcurlimit += screen;
		if (globCtrl.globcurlimit > globCtrl.globlimit)
			globCtrl.globcurlimit = globCtrl.globlimit;
	}
}

int memCtrlDump(char* outBuffer)
{
	return sprintf(outBuffer, "GL:%d GCL:%d GC:%d GR:%d DR:%d RM(C):%d(%d) RP(C):%d(%d)\n", globCtrl.globlimit,
			globCtrl.globcurlimit, globCtrl.globcurrent, globCtrl.globReseveMem, globCtrl.dealReseveMem,
			globCtrl.globTRMemLimit, globCtrl.currentTRMemLimit, globCtrl.globTRPixelLimit, globCtrl.currentTRPixelLimit);

}

bool aquireTRlimmit(Layer_t *layer)
{
	int size;
	int pixel;
#if (TARGET_BOARD_PLATFORM == tulip)
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;
	if (handle == NULL)
		return false;
	pixel =  handle->stride * handle->height;
	size = pixel * getBitsPerPixel(layer) / 8;

#else
	pixel = (int)ceilf((layer->crop.right - layer->crop.left)
			* (layer->crop.bottom - layer->crop.top));

	size = pixel * getBitsPerPixel(layer) / 8;
#endif
	if (!layer->transform)
		return false;
	if (pixel + globCtrl.currentTRPixelLimit > globCtrl.globTRPixelLimit
		|| size + globCtrl.currentTRMemLimit > globCtrl.globTRMemLimit)
		return false;
	globCtrl.currentTRPixelLimit += pixel;
	globCtrl.currentTRMemLimit += size;

	return true;

}

void releaseTRlimmit(Layer_t *layer)
{
	int size;
	int pixel;
	if (!layer->transform)
		return;

#if (TARGET_BOARD_PLATFORM == tulip)
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;
	if (handle == NULL)
		return;
	pixel =  handle->stride * handle->height;
	size = pixel * getBitsPerPixel(layer) / 8;

#else
	pixel = (int)ceilf((layer->crop.right - layer->crop.left)
			* (layer->crop.bottom - layer->crop.top));

	size = pixel * getBitsPerPixel(layer) / 8;
#endif
	globCtrl.currentTRPixelLimit -= pixel;
	globCtrl.currentTRMemLimit -= size;

}

void memCtrlInit(Display_t **display, int num)
{
	int ddrFreFd;
	globCtrl.globcurrent = 0;
	globCtrl.globReseveMem = 0;
	globCtrl.numdisp = num;
	globCtrl.display = display;

	ddrFreFd = open("/sys/class/devfreq/dramfreq/max_freq", O_RDONLY);
    if(ddrFreFd >= 0)
    {
        char val_ddr[10] = {0x0,};
        int ret = -1, i = 0, speed = 0;
        ret = read(ddrFreFd, &val_ddr, 6);
	    ALOGD("the ddr speed is %s",val_ddr);
        close(ddrFreFd);
        while(ret--) {
            speed *= 10;
            if ( val_ddr[i] >= '0' && val_ddr[i] <= '9') {
                speed += val_ddr[i++] - 48;
            } else {
                speed = 552000;//defalt ddr max speed
                break;
            }
        }
        i = 0;
        ret = sizeof(mem_speed_limit)/sizeof(mem_speed_limit_t);
        for (i =0; i< ret; i++) {
            if (mem_speed_limit[i].ddrKHz <= speed) {
                break;
            }
        }
        if (i == ret) {
            i--;
        }
        globCtrl.globlimit = mem_speed_limit[i].demem;
    } else {
        ALOGD("open /sys/class/devfreq/dramfreq/max_freq err.");
		globCtrl.globlimit  = mem_speed_limit[1].demem;
    }
	globCtrl.globcurlimit = globCtrl.globlimit;
	/*  */
#if (TARGET_BOARD_PLATFORM == venus)
	/* g2d  hardware limmit and  ddr limmit:
	  * g2d 1080p 120 Hz  ddr mem limmit 512M/s
	  */
	globCtrl.globTRPixelLimit = 1080 * 1920 * 2;
	globCtrl.globTRMemLimit = 512 * 1024 * 1024 / 60;
#elif (TARGET_BOARD_PLATFORM == tulip)
	/* a64 rotate hardware and mem is 1080p video 60Hz */
	globCtrl.globTRPixelLimit = 1080 * 1920;
	globCtrl.globTRMemLimit = 1080 * 1920 * 3 / 2;
#else
	globCtrl.globTRPixelLimit = 1080 * 1920 * 2;
	globCtrl.globTRMemLimit = 512 * 1024 * 1024 / 60;
#endif

}

