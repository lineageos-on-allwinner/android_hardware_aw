/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <cutils/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <cutils/uevent.h>
#include <system/graphics.h>
#include <cutils/properties.h>

#include "hwc.h"

/*store write back context*/
typedef struct WriteBackContext {
	struct listnode freebuf;
	struct listnode wbbuf;
	disp_pixel_format format;
	int width;
	int height;
	int curW;
	int curH;
	int hpercent;
	int vpercent;
	bool secure;
	bool mustconfig;
	disp_capture_info2 capinfo;
	pthread_mutex_t wbmutex;
	Display_t* wbDisplay;
	struct disp_capture_info2* wbinfo;
	int wbFd;
	int state;
	Layer_t* curShow;
	bool bWbDispReset = false;
	bool ownBuffer = false;
} wbctx_t;

/*
  * DELayerPrivate_t all the hw assigned data
  */
typedef struct DELayerPrivate {
	int pipe;
	int layerId;
	int zOrder;
	float pipeScaleW;
	float pipeScaleH;
}DELayerPrivate_t;

wbctx_t WBCTX;
#define MAXCACHE 6
#define MINCACHE 2
#define WB_ALIGN 4
#define WB_WIDTH 1280
#define WB_HEIGHT 720
#define INITED 1
#define STARTED 2
#define INVALID 0
//#define WB_MODE 2

enum {
	NEVER = 0,
	ALWAYS = 1,
	ONNEED = 2,
};

/*bring up driver's writeback resource*/
void writebackStart(int hwid) {
	unsigned long args[4];
	args[0] = hwid;
	args[1] = 0;
	args[2] = 0;
	if (ioctl(WBCTX.wbFd, DISP_CAPTURE_START, args) < 0) {
		ALOGE("start de capture failed!");
	}
}


/*clean up driver's writeback resource*/
void writebackStop(int hwid) {
	unsigned long args[4];
	args[0] = hwid;
	args[1] = 0;
	args[2] = 0;
	if (ioctl(WBCTX.wbFd, DISP_CAPTURE_STOP, args) < 0) {
		ALOGE("stop de capture failed!");
	}
}

/*switch de0 to default mode when it's not plugIn*/
void switchDeviceToDefault(int hwid) {
	unsigned long args[4];
	args[0] = hwid;
	args[1] = DISP_OUTPUT_TYPE_HDMI;
	args[2] = DISP_TV_MOD_1080P_60HZ;
	if (ioctl(WBCTX.wbFd, DISP_DEVICE_SWITCH, args) < 0) {
		ALOGE("wb:switchdevice failed!");
	}
}

/*get current device config which can help to setup wb param*/
int getDeviceConfig(int hwid, struct disp_device_config* config) {
	unsigned long arg[3];
	if (config == NULL) {
		return -1;
	}
	arg[0] = hwid;
	arg[1] = (unsigned long)config;
	if (ioctl(WBCTX.wbFd, DISP_DEVICE_GET_CONFIG, (void*)arg) < 0) {
		ALOGE("getDeviceConfig failed!");
		return -1;
	}
	return 0;
}

/*release all buffers*/
int deinitBuffer() {
	struct listnode *node;
	Layer_t *layer;
	pthread_mutex_lock(&WBCTX.wbmutex);
	list_for_each(node, &WBCTX.freebuf) {
		layer = node_to_item(node, Layer_t, node);
		layerCachePut(layer);
	}
	list_for_each(node, &WBCTX.wbbuf) {
		layer = node_to_item(node, Layer_t, node);
		layerCachePut(layer);
	}
	pthread_mutex_unlock(&WBCTX.wbmutex);
	return 0;
}

/*setup all layers */
int setupLayer(Layer_t* layer, Display_t* dp) {
	int size = 0, stride = 0;
	DELayerPrivate_t* deLayer = NULL;
	DisplayConfig_t* config = NULL;
	if (layer == NULL || dp == NULL) {
		ALOGD("setupLayer bad para");
		return -1;
	}
	deLayer = (DELayerPrivate_t*)layer->data;
	if (dp->displayConfigList != NULL) {
		config = dp->displayConfigList[dp->activeConfigId];
	}
	if (deLayer == NULL) {
		ALOGD("setupLayer bad para");
		return -1;
	}
	ALOGV("setupLayer height = %d, width = %d", WBCTX.height, WBCTX.width);
	ALOGV("setupLayer vpercent = %d, hpercent = %d", WBCTX.vpercent, WBCTX.hpercent);
	/*only allocate once*/
	if (layer->buffer == NULL) {
		layer->buffer = (private_handle_t *)hwc_malloc(sizeof(private_handle_t));
	}
	private_handle_t *handle = (private_handle_t *)layer->buffer;
	if (handle == NULL) {
		return -1;
	}
	if (WBCTX.format == DISP_FORMAT_YUV420_P) {
		/*set buffer metadata*/
		handle->format = HAL_PIXEL_FORMAT_YV12;
		handle->aw_byte_align[0] = 16;
		handle->aw_byte_align[1] = 8;
		handle->aw_byte_align[2] = 8;
		handle->width = WBCTX.width * WBCTX.hpercent / 100;
		handle->height = WBCTX.height * WBCTX.vpercent / 100;
		handle->height = HWC_ALIGN(handle->height, handle->aw_byte_align[1]);
		stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
		handle->width = stride;
		/*size = HWC_ALIGN(handle->height, handle->aw_byte_align[0]) * stride
			+ 2 * HWC_ALIGN(handle->width / 2, handle->aw_byte_align[1]) *
			HWC_ALIGN(handle->height / 2, handle->aw_byte_align[1]);*/
		size = stride * handle->height * 4;
		//size = HWC_ALIGN(size, 1024 * 4);
		handle->stride = stride;
		handle->size = size;
		/*let yuv use channel 0 of de which can handle enhance well*/
		deLayer->pipe = 0;
	} else {
		/*WBCTX.format == DISP_FORMAT_ABGR_8888*/
		/*init by this format too*/
		handle->format = HAL_PIXEL_FORMAT_RGBA_8888;
		handle->aw_byte_align[0] = 64;
		handle->aw_byte_align[1] = 0;
		handle->aw_byte_align[2] = 0;
		handle->width = WBCTX.width * WBCTX.hpercent / 100;
		handle->height = WBCTX.height * WBCTX.vpercent / 100;
		stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
		handle->width = stride;
		size = HWC_ALIGN(handle->width, handle->aw_byte_align[0])
			* HWC_ALIGN(handle->height, handle->aw_byte_align[0])
			* 32 / 8;
		handle->stride = stride;
		handle->size = size;
		/*let rgb use channel 1 of de which can handle enhance well*/
		deLayer->pipe = 1;
	}
	/*set layer configs*/
	deLayer->layerId = 0;
	deLayer->zOrder = 0;
	layer->blendMode = HWC2_BLEND_MODE_NONE;
	layer->planeAlpha = 1.0;
	layer->crop.left = 0;
	layer->crop.top = 0;
	layer->crop.right = WBCTX.width * WBCTX.hpercent / 100;
	layer->crop.bottom = WBCTX.height * WBCTX.vpercent / 100;
	if (config != NULL) {
		layer->frame.left = 0;
		layer->frame.top = 0;
		layer->frame.right = config->width;
		layer->frame.bottom = config->height;
		ALOGV("setupLayer config h,w = %d, %d", config->height, config->width);
	}

	/*only allocate once*/
	if (handle->share_fd <= 0) {
		handle->share_fd = ionAllocBuffer(size, WBCTX.mustconfig, WBCTX.secure);
		layer->acquireFence = -1;
		layer->releaseFence = -1;
	}
	if (handle->share_fd < 0) {
		return -1;
	}
	return 0;
}

int setupWbInfo(struct disp_capture_info2* info, Layer_t* layer) {
	int left = 0, top = 0, width = 0, height = 0;
	private_handle_t *handle = (private_handle_t *)layer->buffer;
	if (handle == NULL) {
		ALOGD("setupWbInfo without buffer handle");
	}
	width = WBCTX.curW * WBCTX.hpercent / 100;
	height = WBCTX.curH * WBCTX.vpercent / 100;
	left = (WBCTX.curW - width) / 2;
	top = (WBCTX.curH - height) / 2;
	info->window.x = left;
	info->window.y = top;
	info->window.width = width;
	info->window.height = height;

	width = WBCTX.width * WBCTX.hpercent / 100;
	height = WBCTX.height * WBCTX.vpercent / 100;
	info->out_frame.size[0].width = handle->width;
	info->out_frame.size[0].height = handle->height;
	if (WBCTX.format == DISP_FORMAT_YUV420_P) {
		info->out_frame.size[1].width = handle->width / 2;
		info->out_frame.size[1].height = handle->height / 2;
		info->out_frame.size[2].width = handle->width / 2;
		info->out_frame.size[2].height = handle->height / 2;
		info->out_frame.format = WBCTX.format;
	} else {
		/*WBCTX.format == DISP_FORMAT_ABGR_8888*/
		info->out_frame.size[1].width = handle->width;
		info->out_frame.size[1].height = handle->height;
		info->out_frame.size[2].width = handle->width;
		info->out_frame.size[2].height = handle->height;
		info->out_frame.format = DISP_FORMAT_ABGR_8888;
	}
	info->out_frame.crop.x = 0;
	info->out_frame.crop.y = 0;
	info->out_frame.crop.width =  width;
	info->out_frame.crop.height = height;
	info->out_frame.fd = handle->share_fd;
	ALOGV("wbinfo win[%d,%d,%d,%d],stride=%d,fd=%d", info->window.x, info->window.y,
			info->window.width, info->window.height,
			handle->stride, info->out_frame.fd);
	return 0;
}

int initBuffer(Display_t* dp) {
	pthread_mutex_lock(&WBCTX.wbmutex);
	DisplayOpr_t* opt = dp->displayOpration;
	list_init(&WBCTX.freebuf);
	list_init(&WBCTX.wbbuf);
	for (int i = 0; i < MAXCACHE; i++) {
		Layer_t* layer = opt->createLayer(dp);
		if (setupLayer(layer, dp) == 0) {
			list_add_tail(&WBCTX.freebuf, &layer->node);
		} else {
			ALOGE("setupLayer fail!");
		}
	}
	pthread_mutex_unlock(&WBCTX.wbmutex);
	return 0;
}

int initWriteBack(Display_t* dp, bool isInitBuf) {
	pthread_mutex_init(&WBCTX.wbmutex, 0);
#ifndef USE_IOMMU
	WBCTX.mustconfig = 1;
#else
	WBCTX.mustconfig = 0;
#endif
	WBCTX.width = WB_WIDTH;
	WBCTX.height = WB_HEIGHT;
	WBCTX.hpercent = 100;
	WBCTX.vpercent = 100;
	/*hw only writeback rgb->rgb or yuv->yuv series*/
	/*set rbg first to alloc larger buffer which can hold yuv too*/
	WBCTX.format = DISP_FORMAT_ABGR_8888;
	/*WBCTX.format = DISP_FORMAT_YUV420_P;*/
	if (isInitBuf) {
		initBuffer(dp);
		WBCTX.ownBuffer = true;
	}
	WBCTX.wbinfo = (struct disp_capture_info2 *)malloc(sizeof(struct disp_capture_info2));
	if (WBCTX.wbinfo == NULL) {
		ALOGE("malloc capture info fail!");
	}
	WBCTX.wbFd = open("/dev/disp", O_RDWR);
	writebackStop(0);
	writebackStart(0);
	WBCTX.state = INITED;
	WBCTX.curShow = NULL;
	return 0;
}

void deinitWriteBack(Display_t* dp) {
	if (dp->displayId != 1) {
		/*de0 is better, should de0 wb to de1*/
		ALOGD("deinitWriteBack with wrong id=%d", dp->displayId);
	}
	writebackStop(0);
	deinitBuffer();
	if(WBCTX.wbinfo != NULL) {
		free(WBCTX.wbinfo);
	}
	if(WBCTX.wbFd >= 0) {
		close(WBCTX.wbFd);
	}
	if(WBCTX.wbDisplay != NULL) {
		hwc_free((void*)WBCTX.wbDisplay);
	}
	WBCTX.state = INVALID;
	WBCTX.curShow = NULL;
	if (WBCTX.wbDisplay != NULL) {
		WBCTX.wbDisplay->displayOpration->deInit(WBCTX.wbDisplay);
		WBCTX.wbDisplay = NULL;
	}
}

/*alloc once dislay for wb when de0 without screen plugin*/
Display_t* getWbDisplay(Display_t* dp) {
	struct disp_device_config devCfg;
	if (dp == NULL) {
		return NULL;
	}
	if (WBCTX.wbDisplay == NULL) {
		WBCTX.wbDisplay = (Display_t *)hwc_malloc(sizeof(Display_t) + sizeof(DisplayPrivate_t));
		if (WBCTX.wbDisplay == NULL) {
			ALOGE("Alloc display err, Can not initial the hwc module.");
			return NULL;
		}
		memcpy(WBCTX.wbDisplay, dp, sizeof(Display_t) + sizeof(DisplayPrivate_t));
		/*active and wb full screen*/
		WBCTX.wbDisplay->active = 1;
		WBCTX.wbDisplay->vpercent = 100;
		WBCTX.wbDisplay->hpercent = 100;
		WBCTX.wbDisplay->plugIn = 0;
		if (getDeviceConfig(0, &devCfg)) {
			switchDeviceToDefault(0);
		}
	} else if (WBCTX.bWbDispReset) {
		WBCTX.bWbDispReset = false;
		/*active and wb full screen*/
		WBCTX.wbDisplay->active = 1;
		WBCTX.wbDisplay->vpercent = dp->vpercent;
		WBCTX.wbDisplay->hpercent = dp->hpercent;
		WBCTX.wbDisplay->VarDisplayHeight = dp->VarDisplayHeight;
                WBCTX.wbDisplay->VarDisplayWidth = dp->VarDisplayWidth;
		WBCTX.wbDisplay->screenRadio = dp->screenRadio;
	}
	return WBCTX.wbDisplay;
}

void resetWbDisplay() {
	WBCTX.bWbDispReset = true;
}

Layer_t* dequeueLayer(Display_t* dp) {
	struct listnode *node;
	Layer_t *layer, *result = NULL;
	if (WBCTX.state == INVALID) {
		ALOGD("wb not ready yet");
		return NULL;
	}
	if (dp == NULL) {
		return NULL;
	}
	/*find a free buf*/
	pthread_mutex_lock(&WBCTX.wbmutex);
	list_for_each(node, &WBCTX.freebuf) {
		layer = node_to_item(node, Layer_t, node);
		if (result == NULL && layer != NULL
			&& layer != WBCTX.curShow) {
			result = layer;
			break;
		}
	}
	/*de1 consume too slow,recycle some, make sure wb buf enough*/
	if (result == NULL) {
		list_for_each(node, &WBCTX.wbbuf) {
			layer = node_to_item(node, Layer_t, node);
			if (layer != NULL && layer != WBCTX.curShow) {
				result = layer;
				break;
			}
		}
	}
	/*dequeue one buf for wb*/
	if (result != NULL) {
		list_remove(&result->node);
		if (result->acquireFence >= 0) {
			close(result->acquireFence);
			result->acquireFence = -1;
		}
	}
	pthread_mutex_unlock(&WBCTX.wbmutex);

	return result;
}

int queueLayer(Layer_t* layer) {
	if (layer == NULL) {
		return -1;
	}
	/*store buf for showing*/
	pthread_mutex_lock(&WBCTX.wbmutex);
	list_add_tail(&WBCTX.wbbuf, &layer->node);
	pthread_mutex_unlock(&WBCTX.wbmutex);
	return 0;
}

Layer_t* acquireLayer(struct sync_info *sync, Display_t* dp) {
	struct listnode *node;
	Layer_t *layer, *result = NULL;
	Layer_t *second = NULL;
	int count  = 0;
	DisplayConfig_t* config = dp->displayConfigList[dp->activeConfigId];
	if (WBCTX.state == INVALID) {
		ALOGD("wb not ready yet");
		return NULL;
	}
	if (config == NULL) {
		ALOGD("acquireLayer no active config");
		return NULL;
	}
	ALOGV("acquireLayer config varw,h=%d,%d", config->VarDisplayWidth,
			config->VarDisplayHeight);

	/*acquire one buf to show*/
	pthread_mutex_lock(&WBCTX.wbmutex);
	list_for_each(node, &WBCTX.wbbuf) {
		layer = node_to_item(node, Layer_t, node);
		count++;
		if (result && layer != NULL
			&& count == 2) {
			second = layer;
		}
		if (result == NULL && layer != NULL
			&& layer != WBCTX.curShow) {
			result = layer;
		}
		if (count > MINCACHE + 2) {
			break;
		}
	}
	/*set layer config and queue to free buf list*/
	if (result != NULL && count > MINCACHE) {
		/*use state to cache more buffer for smoothly*/
		/*not use now, fence can handle well*/
		WBCTX.state = STARTED;
		list_remove(&result->node);
		if (sync->fd >= 0) {
			if (layer->releaseFence >= 0) {
				close(layer->releaseFence);
			}
			result->releaseFence = dup(sync->fd);
			ALOGV("acquireLayer  release fence  %d", result->releaseFence);
		}
		if (second != NULL) {
			if (result->acquireFence >= 0) {
				close(result->acquireFence);
				result->acquireFence = dup(second->acquireFence);
			}
		} else {
			ALOGD("%s ,second == NULL", __func__);
		}
		WBCTX.curShow = result;
		list_add_tail(&WBCTX.freebuf, &result->node);
	} else {
		result = NULL;
		ALOGD("wbbuf count=%d, state=%d", count, WBCTX.state);
	}
	/*de1 consume too slow, drop some frame*/
	if (count > MINCACHE + 2) {
		Layer_t* leftLayer = NULL;
		list_for_each(node, &WBCTX.wbbuf) {
			layer = node_to_item(node, Layer_t, node);
			if (layer != NULL && layer != WBCTX.curShow) {
				leftLayer = layer;
				break;
			}
		}
		if (leftLayer != NULL) {
			if (leftLayer->acquireFence >= 0) {
				close(leftLayer->acquireFence);
				leftLayer->acquireFence = -1;
			}
			list_remove(&leftLayer->node);
			list_add_tail(&WBCTX.freebuf, &leftLayer->node);
		}
	}
	pthread_mutex_unlock(&WBCTX.wbmutex);
	return result;
}

int writebackOneFrame(Display_t* dp, Layer_t* layer, struct sync_info *sync) {
	unsigned long arg[4];
	int curW = 0, curH = 0;
	struct disp_device_config devCfg;
	disp_pixel_format curFmt;
	private_handle_t *handle = NULL;
	DisplayConfig_t* config = NULL;

	if (dp == NULL || layer == NULL || sync == NULL) {
		return -1;
	}
	handle = (private_handle_t *)layer->buffer;
	if (handle == NULL) {
		return -1;
	}

	/*get de0 output width and height*/
	/*hw's wb spec only support 1x~1/2x, so take it carefully*/
	/*actually, > 1x not ok, <1/2x still work, spec...*/
	config = dp->displayConfigList[dp->activeConfigId];
	if (config == NULL) {
		ALOGD("writebackOneFrame getconfig failed ");
		return -1;
	}
	curW = config->VarDisplayWidth;
	curH = config->VarDisplayHeight;
	ALOGV("writebackOneFrame config varw,h=%d,%d", config->VarDisplayWidth,
			config->VarDisplayHeight);

	if (getDeviceConfig(0, &devCfg)) {
		return -1;
	}

	/*interlace output only half height one frame, handle it*/
	if (devCfg.mode == DISP_TV_MOD_480I || devCfg.mode == DISP_TV_MOD_576I
			|| devCfg.mode == DISP_TV_MOD_1080I_50HZ
			|| devCfg.mode == DISP_TV_MOD_1080I_60HZ) {
		curH = curH / 2;
	}

	if (config->hpercent <= 0 || config->hpercent > 100) {
		config->hpercent = 100;
	}
	if (config->vpercent <= 0 || config->vpercent > 100) {
		config->vpercent = 100;
	}

	if (WBCTX.hpercent != config->hpercent || WBCTX.vpercent != config->vpercent) {
		WBCTX.hpercent = config->hpercent;
		WBCTX.vpercent = config->vpercent;
	}
	/*hw's wb spec only support rgb->rgb or yuv->yuv, otherwise data error*/
	if (devCfg.format == DISP_CSC_TYPE_RGB) {
		curFmt = DISP_FORMAT_ABGR_8888;
	} else {
#if (TARGET_BOARD_PLATFORM == cupid)
		curFmt = DISP_FORMAT_ABGR_8888;
#else
		curFmt = DISP_FORMAT_YUV420_P;
#endif
	}
	if (curFmt != WBCTX.format) {
		WBCTX.format = curFmt;
		/*writebackStop(0);
		writebackStart(0);*/
	}

	/*if (WBCTX.height != curH || WBCTX.width != curW) {
		writebackStop(0);
		writebackStart(0);
	}*/

	/*handle width and height changed, full screen wb primary*/
	if (curH != WBCTX.curH || curW != WBCTX.curW
			|| curH != layer->crop.bottom
			|| curW != layer->crop.right) {
		WBCTX.height = curH;
		WBCTX.width = curW;
		WBCTX.curH = curH;
		WBCTX.curW = curW;
		if (WBCTX.height > WB_HEIGHT) {
			WBCTX.height = WB_HEIGHT;
		}
		if (WBCTX.width > WB_WIDTH) {
			WBCTX.width = WB_WIDTH;
		}
	}
	/*something change reset layer info*/
	if (setupLayer(layer, dp)) {
		ALOGD("writebackOneFrame setupLayer failed ");
		return -1;
	}
	/*setup wb buffer*/
	setupWbInfo(WBCTX.wbinfo, layer);

	/*handle fence, de0's wb buf must without de1 using*/
	if (layer->releaseFence >= 0) {
		if (sync_wait((int)layer->releaseFence, 3000)) {
			ALOGE("writebackOneFrame release fence err %d", layer->releaseFence);
		}
		close(layer->releaseFence);
		layer->releaseFence = -1;
	}

	/*active one write back*/
	arg[0] = dp->displayId;
	arg[1] = (unsigned long)WBCTX.wbinfo;
	arg[2] = 0;
	arg[3] = 0;
	if (ioctl(WBCTX.wbFd, DISP_CAPTURE_COMMIT2, (void*)arg) < 0) {
		ALOGE("DISP_CAPTURE_COMMIT2 fail! ownBuffer= %d", WBCTX.ownBuffer);
		if (WBCTX.ownBuffer) {
			pthread_mutex_lock(&WBCTX.wbmutex);
			list_add_tail(&WBCTX.freebuf, &layer->node);
			pthread_mutex_unlock(&WBCTX.wbmutex);
		}
		if (layer->acquireFence >= 0) {
			close(layer->acquireFence);
			layer->acquireFence = -1;
		}
		writebackStop(0);
		writebackStart(0);
		return -1;
	}

	if (sync->fd > 0) {
		if (layer->acquireFence >= 0) {
			close(layer->acquireFence);
		}
		layer->acquireFence = dup(sync->fd);
	}
	return 0;
}

/*dump one wb buffer for debug*/
void dumpLayer(Layer_t *layer,unsigned int framecout)
{
	void *addr_0 = NULL;
	int size = 0;
	int fd = 0;
	int ret = -1;
	private_handle_t *handle = NULL;
	char dump_src[40] = "/data/dump_layer";

	handle = (private_handle_t *)layer->buffer;
	if (handle == NULL) {
		ALOGD("dump null buffer %d",framecout);
		return;
	}

	sprintf(dump_src, "/data/dump_%u_%d", framecout, layer->zorder);
	fd = ::open(dump_src, O_RDWR|O_CREAT, 0644);
	if(fd < 0) {
		ALOGD("open %s %d", dump_src, fd);
		return ;
	}
	size = handle->size;
	ALOGD("###Size:%d at frame:%d###", size, framecout);
	addr_0 = ::mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
			handle->share_fd, 0);
	ret = ::write(fd, addr_0, size);
	if(ret != size) {
		ALOGD("write %s err %d", dump_src, ret);
	}
	::munmap(addr_0,size);
	close(fd);
}

/*jugde if it should need write back*/
bool isNeedWb(Layer_t *layer) {
	if (WB_MODE == ALWAYS) {
		return true;
	} else if (WB_MODE == ONNEED) {
		private_handle_t *handle = NULL;
		if (layer == NULL) {
			return false;
		}
		handle = (private_handle_t *)layer->buffer;
		if (handle != NULL && is_afbc_buf(handle) && layerIsVideo(layer)) {
			return true;
		}
		if (layerIsVideo(layer) && layer->crop.right - layer->crop.left >= 3840
				&& layer->crop.bottom - layer->crop.top >= 2048) {
			return true;
		}
	}

	return false;
}

void cleanCaches() {
	struct listnode *node;
	Layer_t *lyr, *result = NULL;
	/*clean up wb cache when no need to wb*/
	pthread_mutex_lock(&WBCTX.wbmutex);
	list_for_each(node, &WBCTX.wbbuf) {
		lyr = node_to_item(node, Layer_t, node);
		if (lyr != NULL) {
			result = lyr;
		}
	}
	if (result != NULL) {
		list_remove(&result->node);
		list_add_tail(&WBCTX.freebuf, &result->node);
	}
	pthread_mutex_unlock(&WBCTX.wbmutex);
}
