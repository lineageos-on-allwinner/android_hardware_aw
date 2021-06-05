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
#include "hwc.h"
#include <utils/Trace.h>
#include "other/screen_orientation.h"

#ifdef HWC_VENDOR_SERVICE
#include "other/vendorservice.h"
#endif

#include "other/composer_readback.h"

enum class Hdr : int32_t {
    /**
     * Device supports Dolby Vision HDR  */
    DOLBY_VISION = 1,
    /**
     * Device supports HDR10  */
    HDR10 = 2,
    /**
     * Device supports hybrid log-gamma HDR  */
    HLG = 3,
};

static int numberDisplay;
Display_t **mDisplay;
static int socketpair_fd[2];

int primary_disp;
Mutex *primarymutex;
Condition *primarycond;
#ifdef TARGET_PLATFORM_HOMLET
extern int hdmifd;
#endif

void deviceManger(void *data, hwc2_display_t display, hwc2_connection_t connection);

char dumpchar[] = {
"     layer      |  handle        |  format  |"
"bl|space|TR|pAlp|     crop or color       "
"|       frame         |zOrder|hz|ch|id|duto\n"
};

int find_config(Display_t *dp, hwc2_config_t config)
{
	int	i;
	if (Hwc2ConfigToDisp(config) != toClientId(dp->clientId)) {
		goto bad;
	}
	i = Hwc2ConfigTohwConfig(config);
	if (i < dp->configNumber)
		return i;
bad:
	ALOGE("find a err display config%08x", config);
    return BAD_HWC_CONFIG;
}

void hwc_device_getCapabilities(struct hwc2_device* device, uint32_t* outCount,
    int32_t* /*hwc2_capability_t*/ outCapabilities)
{
	unusedpara(device);

    if(outCapabilities == NULL){
        *outCount = 0;
    }else{
        *outCapabilities = HWC2_CAPABILITY_INVALID;
    }
}

int32_t hwc_create_virtual_display(hwc2_device_t* device, uint32_t width, uint32_t height,
    int32_t* format, hwc2_display_t* outDisplay)
{
	unusedpara(device);
	unusedpara(width);
	unusedpara(height);
	unusedpara(format);
	unusedpara(outDisplay);

    ALOGE("ERROR %s: do not support virtual display", __FUNCTION__);
    return HWC2_ERROR_NO_RESOURCES;
}

int32_t hwc_destroy_virtual_display(hwc2_device_t* device, hwc2_display_t display)
{
	unusedpara(device);
	unusedpara(display);

    return HWC2_ERROR_NONE;
}

void hwc_dump(hwc2_device_t* device, uint32_t* outSize, char* outBuffer)
{
	int i = 0;
	uint32_t cout = 0;
	unusedpara(device);

	if (outBuffer != NULL) {
		cout += sprintf(outBuffer, "     layer      |  handle        |  format  |"
						"bl|space|TR|pAlp|       crop or color         "
						"|       frame         |zOrder|hz|ch|id|duto\n");
		cout += sprintf(outBuffer + cout,
			"--------------------------------------------------------------------------"
			"--------------------------------------------------------------------------\n");
	}else{
		cout += sizeof(dumpchar);
		*outSize = cout;
	}
	for (i = 0; i < numberDisplay; i++) {
       mDisplay[i]->displayOpration->dump(mDisplay[i], &cout, outBuffer, *outSize);
	}

	if (outBuffer == NULL)
		*outSize = cout;
}

const char *hwcPrintInfo(enum sunnxi_dueto_flags eError)
{
    switch(eError)
	{

#define AssignDUETO(x) \
	case x: \
		return #x;
	AssignDUETO(HWC_LAYER)
	AssignDUETO(NOCONTIG_MEM)
	AssignDUETO(SOLID_COLOR)
	AssignDUETO(TRANSFROM_RT)
	AssignDUETO(SCALE_OUT)
	AssignDUETO(SKIP_FLAGS)
	AssignDUETO(FORMAT_MISS)
	AssignDUETO(NO_V_PIPE)
	AssignDUETO(NO_U_PIPE)
	AssignDUETO(COLORT_HINT)
	AssignDUETO(CROSS_FB)
	AssignDUETO(NOT_ASSIGN)
	AssignDUETO(NO_BUFFER)
	AssignDUETO(MEM_CTRL)
	AssignDUETO(FORCE_GPU)
	AssignDUETO(VIDEO_PREM)
#undef AssignDUETO
	default:
		return "Unknown reason";
	}
}

uint32_t hwc_get_max_virtual_display_count(hwc2_device_t* device)
{
	unusedpara(device);

    return 0;
}

#ifdef ENABLE_WRITEBACK
Display_t* findHwDisplay(int hwid)
{
    for (int i = 0; i < numberDisplay; i++) {
        if(mDisplay[i]->displayId == hwid) {
            return mDisplay[i];
        }
    }
    return NULL;
}
#endif

Display_t* findDisplay(hwc2_display_t display, bool needCheckDeinited = true)
{
    for (int i = 0; i < numberDisplay; i++) {
        if(mDisplay[i]->clientId == display) {
            if (needCheckDeinited && mDisplay[i] != NULL && mDisplay[i]->deinited) {
                ALOGD("%s:display %d has been plug out and deinited", __FUNCTION__, i);
                return NULL;
            } else {
                return mDisplay[i];
            }
        }
    }
    return NULL;
}

int hwc_set_primary(int hwid,bool wait)
{
	ALOGD("set primary: %d", hwid);
	int old = -1;
	Display_t *primarydisp = mDisplay[0];
	Display_t *secdisp = mDisplay[1];
	if (toClientId(primarydisp->clientId) != 0) {
		primarydisp = mDisplay[1];
		secdisp = mDisplay[0];
	}

	if (primary_disp == hwid)
		return old;
	old = primary_disp;
	primary_disp = hwid;
	if (!wait)
		return old;
	int loop = 200;
loop:
	if (!primarydisp->plugIn && primarydisp->displayId != hwid ) {
		secdisp->displayId = primarydisp->displayId;
		primarydisp->displayId = hwid;
	} else if (loop-- && primarydisp->displayId != hwid) {
		primarymutex->lock();
		primarycond->waitRelative(*primarymutex, 20000000);
		primarymutex->unlock();
		if (primarydisp->displayId != hwid) {
			ALOGD("wait for swap primary display %d.", loop);
			callRefresh(primarydisp);
			goto loop;
		}
	}
	/*refresh screen avoiding black screen when vice disp newly plug in*/
	if (old == 0 && primary_disp == 1) {
		callRefresh(primarydisp);
	}
	return old;
}

void accept_swap_disp(Display_t *dp)
{
	int tmp;

	if (toClientId(dp->clientId) != 0
		||dp->displayId == primary_disp)
		return;
	ALOGD("swap display:%llx %d(%d) %llx %d(%d)",
		mDisplay[0]->clientId, (int)toClientId(mDisplay[0]->clientId), mDisplay[0]->displayId,
		mDisplay[1]->clientId, (int)toClientId(mDisplay[1]->clientId), mDisplay[1]->displayId);

	primarymutex->lock();
	tmp = mDisplay[0]->displayId;
	mDisplay[0]->displayId = mDisplay[1]->displayId;
	mDisplay[1]->displayId = tmp;

	primarycond->signal();
	primarymutex->unlock();

}

int32_t hwc_register_callback(hwc2_device_t* device, int32_t descriptor,
    hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer)
{
	/* hot plug surfaceflinger care second display,but must call primary display for first.
	  * vsync and refresh surfaceflinger care all.
	  */

	unusedpara(device);
	switch(descriptor){
        case HWC2_CALLBACK_HOTPLUG:
		registerEventCallback(0x03, descriptor, 1, callbackData, pointer);
#ifdef COMPOSER_READBACK
		// To make composer vts happy.
		resetReadback();
#endif
		break;
        case HWC2_CALLBACK_REFRESH:
        case HWC2_CALLBACK_VSYNC:
		registerEventCallback(0x3, descriptor, 0, callbackData, pointer);
		break;
        default:
		ALOGE("ERROR %s: bad parameter", __FUNCTION__);
		return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t hwc_accept_display_changes(hwc2_device_t* device, hwc2_display_t display)
{
    Display_t *dp = findDisplay(display);

	unusedpara(device);
    if(!dp) {
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

int32_t hwc_create_layer(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t* outLayer)
{
    Layer_t* layer;
	DisplayOpr_t *opt;
	Display_t *dp = findDisplay(display);

	unusedpara(device);

    if(!dp){
        return HWC2_ERROR_BAD_DISPLAY;
    }
	dp->active = 1;
	opt = dp->displayOpration;
    layer = opt->createLayer(dp);
    if(layer == NULL){
        ALOGE("ERROR %s:not enought memory to allow!", __FUNCTION__);
        return HWC2_ERROR_NO_RESOURCES;
    }

    *outLayer = (hwc2_layer_t)layer;
    addLayerRecord(display, (hwc2_layer_t)layer);

	return HWC2_ERROR_NONE;
}

int32_t hwc_destroy_layer(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer)
{

    Layer_t* ly = (Layer*)layer;
    Display_t *dp = findDisplay(display);
	unusedpara(device);

	if(!dp) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (!removeLayerRecord(display, layer)) {
        // Not a active layer
        return HWC2_ERROR_BAD_LAYER;
    }

	pthread_mutex_lock(&dp->listMutex);
	if (deletLayerByZorder(ly, dp->layerSortedByZorder)) {
		dp->nubmerLayer--;
    	layerCachePut(ly);
	}
	pthread_mutex_unlock(&dp->listMutex);
	if (!dp->plugIn) {
		/* fix android comper 2.1 hotplug remove the display' s bug  */
		return HWC2_ERROR_NO_RESOURCES;
	}
 	return HWC2_ERROR_NONE;
}

int32_t hwc_get_active_config(hwc2_device_t* device, hwc2_display_t display,
    hwc2_config_t* outConfig)
{
    Display_t *dp = findDisplay(display);
	unusedpara(device);

    if(!dp || (dp != NULL && dp->configNumber <= 0)) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    /**outConfig = toHwc2Config(toClientId(dp->clientId), dp->activeConfigId);*/
    /*only support one config, do not return config index > 0*/
    *outConfig = toHwc2Config(toClientId(dp->clientId), 0);
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_changed_composition_types(hwc2_device_t* device, hwc2_display_t display,
    uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outTypes)
{
	int num = 0;
	struct listnode *list;
	struct listnode *node;
	Layer_t *ly;
	Display_t* dp = findDisplay(display);

	unusedpara(device);

	if(!dp) {
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}

	list = dp->layerSortedByZorder;
	if(outLayers == NULL | outTypes == NULL){
		list_for_each(node, list) {
			ly = node_to_item(node, Layer, node);
			if (ly != NULL
					&& ly->typeChange
					&& ly->compositionType != HWC2_COMPOSITION_CLIENT_TARGET){
				num++;
			}
		}
		*outNumElements = num;
		return HWC2_ERROR_NONE;
	}else{
		list_for_each(node, list) {
			ly = node_to_item(node, Layer_t, node);
			if(ly != NULL && ly->typeChange && ly->compositionType != HWC2_COMPOSITION_CLIENT_TARGET){
				*outLayers = (hwc2_layer_t)ly;
				outLayers++;
				*outTypes = ly->compositionType;
				outTypes++;
				ALOGV("%s: layer=%p, type=%d", __FUNCTION__, ly, ly->compositionType);
			}
		}
		return HWC2_ERROR_NONE;
	}
}

int32_t hwc_get_client_target_support(hwc2_device_t* device, hwc2_display_t display,
    uint32_t width, uint32_t height, int32_t format, int32_t dataspace)
{
	unusedpara(device);

    ALOGV("%s display=%p, width=%d, height=%d, format=%d, dataspace=%x",
        __FUNCTION__, (void*)display, width, height, format, dataspace);

    if(!findDisplay(display)) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if(format > HAL_PIXEL_FORMAT_BGRA_8888) {
        ALOGE("ERROR %s:unsupported", __FUNCTION__);
        return HWC2_ERROR_UNSUPPORTED;
    }
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_color_modes(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumModes,
    int32_t* outModes)
{
	unusedpara(device);
    if (!findDisplay(display)) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (!outModes) {
        *outNumModes = 1;
        return HWC2_ERROR_NONE;
    } else {
        *outModes = HAL_COLOR_MODE_NATIVE;
    }
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_display_attribute(hwc2_device_t* device, hwc2_display_t display,
    hwc2_config_t config, int32_t attribute, int32_t* outValue)
{
    Display_t* dp = findDisplay(display);
	DisplayConfig_t* cfg;
	int i;
	unusedpara(device);

    if(!dp) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
	ALOGD("hwc_get_display_attribute :%d %d", (int)toClientId(dp->clientId), dp->displayId);
	i = find_config(dp, config);
    if (i != BAD_HWC_CONFIG) {
        cfg = dp->displayConfigList[i];
        switch (attribute) {
            case HWC2_ATTRIBUTE_WIDTH:
                *outValue = cfg->width;
                break;
            case HWC2_ATTRIBUTE_HEIGHT:
                *outValue = cfg->height;
                break;
            case HWC2_ATTRIBUTE_VSYNC_PERIOD:
                *outValue = cfg->vsyncPeriod;
                break;
            case HWC2_ATTRIBUTE_DPI_X:
                *outValue = cfg->dpiX;
                break;
            case HWC2_ATTRIBUTE_DPI_Y:
                *outValue = cfg->dpiY;
                break;
            default:
                goto failed;
        }
        return HWC2_ERROR_NONE;
    }

failed:
    ALOGE("ERROR %s:bad config", __FUNCTION__);
    return HWC2_ERROR_BAD_CONFIG;

}

int32_t hwc_get_display_configs(hwc2_device_t* device, hwc2_display_t display,
    uint32_t* outNumConfigs, hwc2_config_t* outConfigs)
{
    Display_t *dp = findDisplay(display);
	unusedpara(device);

    if(!dp || (dp != NULL && dp->configNumber <= 0)) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if(outConfigs == NULL){
        ALOGE("ERROR %s:dp->configNumber=%d", __FUNCTION__, dp->configNumber);
	/*not support setFrameRate, so only one config support*/
        *outNumConfigs = 1;//dp->configNumber;
    }else{
        for(uint32_t i = 0; i < *outNumConfigs; i++){
            *outConfigs = toHwc2Config(toClientId(dp->clientId), i);
            outConfigs++;
        }
    }

    return HWC2_ERROR_NONE;
}

int32_t hwc_get_display_name(hwc2_device_t* device, hwc2_display_t display, uint32_t* outSize,
    char* outName)
{
	Display_t *dp = findDisplay(display);
	unusedpara(device);

    if(!dp) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if(outName == NULL){
        *outSize = strlen(dp->displayName);
    }else{
        strncpy(outName, dp->displayName, *outSize);
    }
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_display_requests(hwc2_device_t* device, hwc2_display_t display,
    int32_t* outDisplayRequests, uint32_t* outNumElements, hwc2_layer_t* outLayers,
    int32_t* outLayerRequests)
{
    Display_t* dp = findDisplay(display);
	struct listnode *list = NULL;
    struct listnode *node;
    Layer_t *ly;
    int num = 0;

	unusedpara(device);

	/* JetCui: only get the FB clear flags dueto the hwc2.c */
    if(!dp) {
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

	list = dp->layerSortedByZorder;
    if(outLayers == NULL || outLayerRequests == NULL){

        list_for_each(node, list) {
            ly = node_to_item(node, Layer_t, node);
			if (ly != NULL && ly->compositionType == HWC2_COMPOSITION_CLIENT_TARGET) {
				if (dp->needclientTarget)
					*outDisplayRequests = HWC2_DISPLAY_REQUEST_FLIP_CLIENT_TARGET;
				continue;
			}
            if (ly != NULL && ly->clearClientTarget){
                num++;
            }
        }
        *outNumElements = num;
        return HWC2_ERROR_NONE;
    }else{
        list_for_each(node, list) {
            ly = node_to_item(node, Layer_t, node);
			if (ly->compositionType == HWC2_COMPOSITION_CLIENT_TARGET) {
				if (dp->needclientTarget)
					*outDisplayRequests = HWC2_DISPLAY_REQUEST_FLIP_CLIENT_TARGET;
				continue;
			}
            if (!ly->clearClientTarget){
                continue;
            }
            *outLayers = (hwc2_layer_t)ly;
            outLayers++;
            *outLayerRequests = HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET;
            outLayerRequests++;
        }

        return HWC2_ERROR_NONE;
    }
}

int32_t hwc_get_display_type(hwc2_device_t* device, hwc2_display_t display, int32_t* outType)
{
	unusedpara(device);

    if(!findDisplay(display)){
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    *outType = HWC2_DISPLAY_TYPE_PHYSICAL;
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_doze_support(hwc2_device_t* device, hwc2_display_t display, int32_t* outSupport)
{
	unusedpara(device);

    if(!findDisplay(display)){
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    *outSupport = 0;
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_hdr_capabilities(hwc2_device_t* device, hwc2_display_t display,
    uint32_t* outNumTypes,int32_t* outTypes, float* outMaxLuminance,
    float* outMaxAverageLuminace, float* outMinLuminance)
{
	unusedpara(device);
	unusedpara(outMaxLuminance);
	unusedpara(outMaxAverageLuminace);
	unusedpara(outMinLuminance);
    if(!findDisplay(display)){
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if(outTypes == NULL){
#if DE_VERSION == 30
	    *outNumTypes = 2;
	    return HWC2_ERROR_NONE;
#else
	    *outNumTypes = 0;
	    return HWC2_ERROR_NONE;
#endif
    }

#if DE_VERSION == 30
    outTypes[0] = (int32_t)Hdr::HDR10;
    outTypes[1] = (int32_t)Hdr::HLG;
    //outTypes[2] = Hdr::DOLBY_VISION;
    *outNumTypes = 2;
#endif
    return HWC2_ERROR_NONE;
}

int32_t hwc_get_release_fences(hwc2_device_t* device, hwc2_display_t display,
    uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outFences)
{
	Display_t *dp = findDisplay(display);
	struct listnode *list = NULL;
	struct listnode *node;
	Layer_t *ly;
	unusedpara(outLayers);
	unusedpara(outFences);
	unusedpara(device);

	if(!dp){
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}
	list = dp->layerSortedByZorder;

	if (outLayers == NULL || outFences == NULL){
		*outNumElements = dp->nubmerLayer;
		ALOGV("%s :display=%d, nubmerlayer=%d", __FUNCTION__, dp->displayId, *outNumElements);
	}else{
		if (*outNumElements != dp->nubmerLayer)
			ALOGE("%s:has %u but need %d ",__FUNCTION__,*outNumElements, dp->nubmerLayer);
		list_for_each(node, list) {
			ly = node_to_item(node, Layer_t, node);
			if(ly->compositionType == HWC2_COMPOSITION_CLIENT_TARGET){
				continue;
			}
			*outLayers = (hwc2_layer_t)ly;
			outLayers++;

			ALOGV("%s: frame:%d, ID=%d, layer=%p, Fence=%d", __FUNCTION__, dp->frameCount-1, dp->displayId, ly, ly->preReleaseFence);
			if (ly->preReleaseFence >= 0) {
				*outFences = dup(ly->preReleaseFence);
				close(ly->preReleaseFence);
				ly->preReleaseFence = -1;
			} else {
				*outFences = -1;
			}
			ly->preReleaseFence = ly->releaseFence;
			ly->releaseFence = -1;
			outFences++;
		}
	}

	return HWC2_ERROR_NONE;
}

void releasAllFence(Display_t* dp) {
	Layer_t *layer;
	struct listnode *node;
	if (dp == NULL) {
		return;
	}
	pthread_mutex_lock(&dp->listMutex);
	list_for_each(node, dp->layerSortedByZorder) {
		layer = node_to_item(node, Layer_t, node);

		if (layer->releaseFence >= 0)
			close(layer->releaseFence);
		layer->releaseFence = -1;
		if (layer->acquireFence >= 0)
			close(layer->acquireFence);
		layer->acquireFence = -1;
	}
	pthread_mutex_unlock(&dp->listMutex);
}

int32_t hwc_present_display(hwc2_device_t* device, hwc2_display_t display,
    int32_t* outRetireFence)
{
	ATRACE_CALL();
	int privateLayerSize = 0;
	LayerSubmit_t *submitLayer;
	Layer_t *layer, *layer2;
	Display_t *dp;
	DisplayOpr_t *dispOpr;
	struct listnode *node;
	unusedpara(device);
	struct sync_info sync;
#ifdef ENABLE_WRITEBACK
	Display_t* de0 = findHwDisplay(0);
	Display_t* de1 = findHwDisplay(1);
	LayerSubmit_t *wbSubmit;
	struct sync_info sync0;
	bool bNeedWb = false;
#endif

	dp = findDisplay(display);
	if (!dp) {
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}

	dp->active = 1;
	dispOpr = dp->displayOpration;

#ifdef TARGET_PLATFORM_HOMLET
	struct listnode *node1, *node2;
	int cachedNum = 0;
	submitThread_t *myThread = dp->commitThread;

	if (dp->hwPlug == -1) {
		ALOGD("%s:send buf without hw plug in", __FUNCTION__);
		hwc_setBlank(dp->displayId);
		*outRetireFence = -1;
		releasAllFence(dp);
		return HWC2_ERROR_NONE;
	}
	myThread->mutex->lock();
	list_for_each_safe(node1, node2, &myThread->SubmitHead) {
		cachedNum++;
	}
	myThread->mutex->unlock();
	/*get frames in cached*/
	if (cachedNum > 1) {
		if (toClientId(dp->clientId) == 0) {
			/*should not happen as surfacefliner had synced to primary disp*/
			ALOGV("ERROR %s:bad primary disp vsync", __FUNCTION__);
		} else {
			/*external disp consume too slow should drop some*/
			ALOGV("%s:drop 1 frame for external disp", __FUNCTION__);
			*outRetireFence = -1;
			releasAllFence(dp);
			return HWC2_ERROR_NONE;
		}
	}
#endif

	pthread_mutex_lock(&dp->listMutex);
	if(dispOpr->presentDisplay(dp, &sync, &privateLayerSize)) {
		pthread_mutex_unlock(&dp->listMutex);
		ALOGE("ERROR %s:%d(%d)present err...", __FUNCTION__,
			(int)toClientId(dp->clientId), dp->displayId);

		*outRetireFence = -1;
		releasAllFence(dp);
		return HWC2_ERROR_NONE;
	}
	submitLayer = submitLayerCacheGet();
	if (submitLayer == NULL) {
		pthread_mutex_unlock(&dp->listMutex);
		ALOGE("get submit layer err...");
		return HWC2_ERROR_NO_RESOURCES;
	}

#ifdef ENABLE_WRITEBACK
	list_for_each(node, dp->layerSortedByZorder) {
		layer = node_to_item(node, Layer_t, node);
		if(layer != NULL && layer->compositionType != HWC2_COMPOSITION_CLIENT_TARGET
			&& isNeedWb(layer)) {
			bNeedWb = true;
		}
	}
#endif
	dp->secure = false;
	list_for_each(node, dp->layerSortedByZorder) {
		layer = node_to_item(node, Layer_t, node);

		if (layer->releaseFence >= 0)
			close(layer->releaseFence);
		layer->releaseFence = -1;

		if (layer->compositionType != HWC2_COMPOSITION_CLIENT) {
			if (layer->compositionType == HWC2_COMPOSITION_CLIENT_TARGET
					&& !dp->needclientTarget)
				goto deal;
#ifdef ENABLE_WRITEBACK
			Display_t* de0 = findHwDisplay(0);
			if (bNeedWb && dp->displayId == 1  && de0 != NULL && de0->plugIn) {
				goto deal;
			}
#endif
			layer2 = layerDup(layer, privateLayerSize);
			if (layer2 == NULL) {
				pthread_mutex_unlock(&dp->listMutex);
				ALOGE("layer dup err...");
				goto err_setup_layer;
			}

			if (layer->transform == 0) {
				if (layer->compositionType != HWC2_COMPOSITION_CLIENT_TARGET)
					layer->releaseFence = dup(sync.fd);
			} else {
				if (checkSoildLayer(layer)) {
					// Do not submit rotate task for soild layer with transform
					layer->releaseFence = dup(sync.fd);
				} else if (layer->compositionType != HWC2_COMPOSITION_CLIENT_TARGET) {
					layer->releaseFence = get_rotate_fence_fd(layer2, dp, sync.fd, dp->frameCount);
				}
			}

			ALOGV("%s:%p frame:%d fence:%d  sync:%d",
				__FUNCTION__, layer, dp->frameCount, layer->releaseFence, sync.count);
			list_add_tail(&submitLayer->layerNode, &layer2->node);
		}
		if (layerIsProtected(layer))
			dp->secure = true;
deal:
		if (layer->acquireFence >= 0)
			close(layer->acquireFence);
		layer->acquireFence = -1;
	}

	pthread_mutex_unlock(&dp->listMutex);
	submitLayer->frameCount = dp->frameCount;

	submitLayer->currentConfig = *dp->displayConfigList[dp->activeConfigId];

	submitLayer->sync = sync;

	submitLayer->hwid = dp->displayId;

	// setup frame name for systrace output
	if (CC_UNLIKELY(atrace_is_tag_enabled(ATRACE_TAG_GRAPHICS))) {
		snprintf(submitLayer->traceName, sizeof(submitLayer->traceName),
				"frame-%d-%d",
				submitLayer->hwid, submitLayer->frameCount);
	}
#ifdef ENABLE_WRITEBACK
	if (!bNeedWb || de1 == NULL || (de1 != NULL && !de1->plugIn)) {
		if (!bNeedWb) {
			cleanCaches();
		}
		//screen of de1 plug out, just submit
		submitLayerToDisplay(dp, submitLayer);
	} else if (dp->displayId == 1 && de0 != NULL && !de0->plugIn) {
		//screen of de0 plug out,fake it
		Layer_t* frame =  NULL;
		de0 = getWbDisplay(de0);
		if (de0 == NULL) {
			ALOGE("getWbDisplay failed");
			*outRetireFence = -1;
			submitLayerCachePut(submitLayer);
			return HWC2_ERROR_NO_RESOURCES;
		}
		/*init the dispay info*/
		if (de0 != NULL && !de0->plugIn) {
			dispOpr->init(de0);
			de0->plugIn = 1;
			if (de0->clientTargetLayer != NULL) {
				layerCachePut(de0->clientTargetLayer);
				de0->clientTargetLayer = dp->clientTargetLayer;
				de0->clientTargetLayer->ref++;
			}
		}
		/*get release fence*/
		if(dispOpr->presentDisplay(de0, &sync0, &privateLayerSize)) {
			ALOGE("wb: %s:%d(%d)present err...", __FUNCTION__,
					(int)toClientId(de0->clientId), de0->displayId);
			*outRetireFence = -1;
			submitLayerCachePut(submitLayer);
			/*restart vsync avoding hotplug make it stop*/
			deinit_sync(de0->displayId);
			init_sync(de0->displayId);
			return HWC2_ERROR_NO_RESOURCES;
		}
		/*init de1 submit info*/
		wbSubmit = submitLayerCacheGet();
		if (wbSubmit  == NULL) {
			ALOGE("get submit layer err...");
			*outRetireFence = -1;
			submitLayerCachePut(submitLayer);
			return HWC2_ERROR_NO_RESOURCES;
		}
		wbSubmit->frameCount = dp->frameCount;
		wbSubmit->currentConfig = *dp->displayConfigList[dp->activeConfigId];
		wbSubmit->sync = sync;
		wbSubmit->hwid = dp->displayId;

		/*init de0 submit info*/
		submitLayer->frameCount = de0->frameCount;
		submitLayer->currentConfig = *de0->displayConfigList[de0->activeConfigId];
		submitLayer->sync = sync0;
		submitLayer->hwid = de0->displayId;

		/*wb de0's one frame*/
		frame = dequeueLayer(de0);
		if (frame != NULL) {
			if (writebackOneFrame(de0, frame, &sync0)) {
				ALOGE("wboneframe failed");
			}
			queueLayer(frame);
		} else {
			ALOGE("wb:dequeueLayer failed");
		}
		submitLayerToDisplay(de0, submitLayer);

		frame =  acquireLayer(&sync, dp);
		if (frame != NULL) {
			layer2 = layerDup(frame, privateLayerSize);
			if (layer2 == NULL) {
				ALOGE("layer dup err...");
				submitLayerCachePut(wbSubmit);
				goto err_setup_layer;
			}
			if (frame->acquireFence >= 0) {
				close(frame->acquireFence);
				frame->acquireFence = -1;
			}
			list_add_tail(&wbSubmit->layerNode, &layer2->node);
			submitLayerToDisplay(dp, wbSubmit);
		} else {
			submitLayerCachePut(wbSubmit);
			ALOGE("wb:acquireLayer failed");
			*outRetireFence = -1;
			return HWC2_ERROR_NO_RESOURCES;
		}
	} else if (dp->displayId == 0) {
		/*release resource of wbdisplay*/
		resetWbDisplay();
		//de0,just write back
		if (de1 != NULL && de1->plugIn) {
			Layer_t* frame = dequeueLayer(dp);
			if (frame != NULL) {
				writebackOneFrame(dp, frame, &sync);
			} else {
				ALOGE("wb:dequeueLayer failed");
			}
			queueLayer(frame);
		}
		submitLayerToDisplay(dp, submitLayer);
	} else if (dp->displayId == 1 && de0 != NULL && de0->plugIn) {
		//de1, just show one write back frame
		Layer_t* frame =  acquireLayer(&sync, dp);
		if (frame != NULL) {
			layer2 = layerDup(frame, privateLayerSize);
			if (layer2 == NULL) {
				ALOGE("layer dup err...");
				goto err_setup_layer;
			}
			if (frame->acquireFence >= 0) {
				close(frame->acquireFence);
				frame->acquireFence = -1;
			}
			list_add_tail(&submitLayer->layerNode, &layer2->node);
			submitLayerToDisplay(dp, submitLayer);
		} else {
			submitLayerCachePut(submitLayer);
			ALOGE("wb:acquireLayer failed");
			*outRetireFence = -1;
			return HWC2_ERROR_NO_RESOURCES;
		}
	}
#else
	submitLayerToDisplay(dp, submitLayer);
#endif
	*outRetireFence = dp->retirfence;
	dp->retirfence = dup(sync.fd);
	dp->frameCount++;

#ifdef COMPOSER_READBACK
    if (toClientId(dp->clientId) == HWC_DISPLAY_PRIMARY)
        doReadback(dp, &sync);
#endif

	return HWC2_ERROR_NONE;

err_setup_layer:
	ALOGE("hwc set err....");
	return HWC2_ERROR_NO_RESOURCES;

}

int32_t hwc_set_active_config(hwc2_device_t* device, hwc2_display_t display,
    hwc2_config_t config)
{
	Display_t *dp = findDisplay(display);
	int i, ret;
	unusedpara(device);

	if(!findDisplay(display)){
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}
	i = find_config(dp, config);
	if (i != BAD_HWC_CONFIG) {
		dp->activeConfigId = i;
		/* JetCui:now do not implement the mutex with other call,
		  * because surfaceflinger just do call it 1 time on the initial.
		  * but second display must be careful.
		  */
		ret = dp->displayOpration->setActiveConfig(dp, dp->displayConfigList[i]);
		if (ret < 0) {
			ALOGE("ERROR %s:bad switch config", __FUNCTION__);
			return HWC2_ERROR_BAD_CONFIG;
		}
		return HWC2_ERROR_NONE;
	}

	ALOGE("ERROR %s:bad config", __FUNCTION__);
	return HWC2_ERROR_BAD_CONFIG;
}

int32_t hwc_set_client_target(hwc2_device_t* device, hwc2_display_t display,
    buffer_handle_t target, int32_t acquireFence, int32_t dataspace, hwc_region_t damage)
{

	Layer_t *layer;
	Display_t *dp;
	unusedpara(device);
	dp = findDisplay(display);

	if(!dp){
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}

	layer = dp->clientTargetLayer;
	layer->compositionType = HWC2_COMPOSITION_CLIENT_TARGET;
	layer->acquireFence = acquireFence;
	layer->releaseFence = -1;
	layer->buffer = target;
	layer->crop.left = 0;
	layer->crop.right = dp->displayConfigList[dp->activeConfigId]->width;
	layer->crop.top = 0;
	layer->crop.bottom = dp->displayConfigList[dp->activeConfigId]->height;
	layer->damageRegion = damage;
	layer->dataspace = dataspace;
	layer->frame.left = 0;
	layer->frame.right = dp->displayConfigList[dp->activeConfigId]->width;
	layer->frame.top = 0;
	layer->frame.bottom = dp->displayConfigList[dp->activeConfigId]->height;
	layer->transform = 0;
	layer->typeChange = false;
	layer->zorder = CLIENT_TARGET_ZORDER;
	layer->blendMode = 2;
	layer->planeAlpha = 1.0;

	ALOGV("%s : create FbLayer=%p, acquirefence=%d, dataspace=%d, buffer=%p, target=%p",
			__FUNCTION__, layer, acquireFence, dataspace, layer->buffer, target);
	return HWC2_ERROR_NONE;
}

int32_t hwc_set_color_mode(hwc2_device_t* device, hwc2_display_t display, int32_t mode)
{
	unusedpara(device);
	if (!findDisplay(display))
		return HWC2_ERROR_BAD_DISPLAY;
	if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
		return HWC2_ERROR_BAD_PARAMETER;

	if (mode != HAL_COLOR_MODE_NATIVE && mode != HAL_COLOR_MODE_SRGB) {
		return HWC2_ERROR_UNSUPPORTED;
	}
	return HWC2_ERROR_NONE;
}

int32_t /*hwc2_error_t*/ hwc_set_color_mode_with_render_intent(
        hwc2_device_t* device, hwc2_display_t display,
        int32_t /*android_color_mode_t*/ mode,
        int32_t /*android_render_intent_v1_1_t */ intent)
{
    unusedpara(device);
    if (!findDisplay(display))
        return HWC2_ERROR_BAD_DISPLAY;
    if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
        return HWC2_ERROR_BAD_PARAMETER;
    if (intent < HAL_RENDER_INTENT_COLORIMETRIC || intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE)
        return HWC2_ERROR_BAD_PARAMETER;

    if (mode != HAL_COLOR_MODE_NATIVE || intent != HAL_RENDER_INTENT_COLORIMETRIC) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return HWC2_ERROR_NONE;
}

int32_t hwc_set_color_transform(hwc2_device_t* device, hwc2_display_t display,
    const float* matrix, int32_t hint)
{
    Display_t* dp = findDisplay(display);
	unusedpara(matrix);
	unusedpara(device);

    if(!dp){
        ALOGE("ERROR %s:bad display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    dp->colorTransformHint = hint;
    return HWC2_ERROR_NONE;

}

int32_t hwc_set_output_buffer(hwc2_device_t* device, hwc2_display_t display,
    buffer_handle_t buffer, int32_t releaseFence)
{
	unusedpara(device);
	unusedpara(display);
	unusedpara(buffer);
	unusedpara(releaseFence);

    ALOGE("%s : we do not support virtual display yet,should not be called", __FUNCTION__);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t hwc_set_power_mode(hwc2_device_t* device, hwc2_display_t display, int32_t mode)
{
    Display_t* dp = findDisplay(display);
	unusedpara(device);

    if(!dp){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return dp->displayOpration->setPowerMode(dp, mode);
}

int32_t hwc_set_vsync_enabled(hwc2_device_t* device, hwc2_display_t display, int32_t enabled)
{
	Display_t *dp = findDisplay(display);
	unusedpara(device);

    if(!dp){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if(!dp->displayOpration->setVsyncEnabled(dp, enabled)){
        return HWC2_ERROR_NONE;
    }
    return HWC2_ERROR_BAD_PARAMETER;
}

int32_t hwc_validate_display(hwc2_device_t* device, hwc2_display_t display,
    uint32_t* outNumTypes, uint32_t* outNumRequests)
{
	ATRACE_CALL();
	Display_t *dp = findDisplay(display);
	struct listnode* list;
	struct listnode *node;
	Layer_t *ly;
	uint32_t numTypes = 0, numRequests = 0;
	hwc2_error_t ret = HWC2_ERROR_NO_RESOURCES;
#ifdef ENABLE_WRITEBACK
	Display_t* de0 = NULL;
	bool duetoDe = false;
	bool bNeedWb = false;
	DisplayConfig_t *dispconfig;
	Display_t* dpBak = NULL;
#endif
	unusedpara(device);

	if(!dp){
		ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
		return HWC2_ERROR_BAD_DISPLAY;
	}

	list = dp->layerSortedByZorder;
	*outNumRequests = 0;
	*outNumRequests = 0;
	accept_swap_disp(dp);

	ALOGV("ID=%d(%d), Before assign, size=%d", (int)toClientId(dp->clientId), dp->displayId, dp->nubmerLayer);
#ifdef ENABLE_WRITEBACK
	dpBak = dp;
	de0 = findHwDisplay(0);
	list_for_each(node, list) {
		ly = node_to_item(node, Layer, node);
		if(ly != NULL && ly->compositionType != HWC2_COMPOSITION_CLIENT_TARGET
			&& isNeedWb(ly)) {
			bNeedWb = true;
			break;
		}
	}
	/*gpu no need to compose de1 which handle by wb*/
	if (bNeedWb && de0 != NULL && de0->plugIn && dp->displayId == 1 && dp->plugIn) {
		duetoDe = true;
	}
	/*use de0's hw resource when de1 plugin only*/
	if (bNeedWb && dp->displayId == 1 && dp->plugIn && de0 != NULL && !de0->plugIn) {
		de0 = getWbDisplay(de0);
		if (de0 != NULL) {
			if (!de0->plugIn) {
				de0->displayOpration->init(de0);
				if (de0->clientTargetLayer != NULL) {
					layerCachePut(de0->clientTargetLayer);
					de0->clientTargetLayer = dp->clientTargetLayer;
					de0->clientTargetLayer->ref++;
				}
			}
			/*copy configs which set by displayd, ugly...*/
			dispconfig = dp->displayConfigList[dp->activeConfigId];
			if (dispconfig != NULL) {
				pthread_mutex_lock(&dp->ConfigMutex);
				dispconfig->vpercent = dp->vpercent;
				dispconfig->hpercent = dp->hpercent;
				dispconfig->VarDisplayHeight = dp->VarDisplayHeight;
				dispconfig->VarDisplayWidth = dp->VarDisplayWidth;
				dispconfig->screenRadio = dp->screenRadio;
				pthread_mutex_unlock(&dp->ConfigMutex);
				ALOGV("%s : display varDisplay h,w=%d, %d", __FUNCTION__,
						dp->VarDisplayHeight, dp->VarDisplayWidth);
				ALOGV("%s : display persent v,h=%d, %d", __FUNCTION__,
						dp->vpercent, dp->hpercent);
			}
			/*use de0 to assign layers*/
			de0->layerSortedByZorder = dp->layerSortedByZorder;
			dp = de0;
		}
	}
#endif

	pthread_mutex_lock(&dp->listMutex);

	dp->displayOpration->AssignLayer(dp);

#ifdef ENABLE_WRITEBACK
	/*no need gpu to compose de1's layers*/
	if (bNeedWb && duetoDe) {
		dpBak->needclientTarget = 0;
	}

	/*cp assigned flag to use gpu compose*/
	if (bNeedWb && dpBak->displayId == 1 && dp == de0) {
		dpBak->needclientTarget = de0->needclientTarget;
	}
#endif
	list = dp->layerSortedByZorder;
	list_for_each(node, list) {
		ly = node_to_item(node, Layer, node);
		if(ly != NULL && ly->typeChange
				&& ly->compositionType != HWC2_COMPOSITION_CLIENT_TARGET) {
			numTypes++;
		}
		if (ly->clearClientTarget)
			numRequests++;
	}
	*outNumRequests = numRequests;
	*outNumTypes = numTypes;
	if (numTypes || numRequests) {
		ret = HWC2_ERROR_HAS_CHANGES;
	}else{
		ret = HWC2_ERROR_NONE;
	}
	ALOGV("%s: %d layer changes and numRequests %d.\n",__FUNCTION__, numTypes, numRequests);

	pthread_mutex_unlock(&dp->listMutex);

	return ret;
}

int32_t hwc_set_cursor_position(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, int32_t x, int32_t y)
{
	unusedpara(device);
	unusedpara(display);
	unusedpara(layer);
	unusedpara(x);
	unusedpara(y);

    ALOGE("%s : (Warning) we do not support cursor layer alone.", __FUNCTION__);
    return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_buffer(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquireFence){

	Layer_t *ly = (Layer_t *)layer;
	unusedpara(device);
	unusedpara(display);

	ly->buffer = buffer;
	ly->acquireFence = acquireFence;
	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_surface_damage(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, hwc_region_t damage)
{
    Layer_t *ly = (Layer_t *)layer;
	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    ly->damageRegion = damage;
    return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_blend_mode(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, int32_t mode)
{
    Layer_t* ly = (Layer_t *) layer;
	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    ly->blendMode = mode;
    return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_color(hwc2_display_t* device, hwc2_display_t display, hwc2_layer_t layer,
    hwc_color_t color)
{
	Layer_t *ly = (Layer_t *)layer;
	unusedpara(device);

	if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    ly->color = color;
    return HWC2_ERROR_NONE;
}
/* JetCui:
  * before hwc prepare, is surfaceFlinger want type.is client,must client
  * after hwc prepare, is surfaceFlinger accept the chang(type).
  * if SurfaceFlinger want device, but HWC can chang it to client.
  */
int32_t hwc_set_layer_composition_type(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, int32_t type)
{
    Layer_t *ly = (Layer_t *)layer;

	unusedpara(device);
	if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
	ly->compositionType = type;

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_dataspace(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, int32_t dataspace)
{
	Layer_t *ly = (Layer_t *) layer;

	unusedpara(device);
	if(!findDisplay(display)){
		ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
		return HWC2_ERROR_BAD_DISPLAY;
	}
	ly->dataspace = dataspace;

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_display_frame(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, hwc_rect_t frame)
{
    Layer_t *ly = (Layer_t *)layer;
	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    ly->frame = frame;

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_plane_alpha(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, float alpha)
{
    Layer_t *ly = (Layer_t *)layer;
	unusedpara(device);

	if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    ly->planeAlpha = alpha;

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_sideband_stream(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, const native_handle_t* stream)
{
    Layer_t *ly = (Layer_t *) layer;
	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    ly->stream = stream;

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_source_crop(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, hwc_frect_t crop)
{
    Layer_t *ly = (Layer_t *) layer;

	unusedpara(device);
	if(!findDisplay(display)){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    ly->crop = crop;	

	return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_transform(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, int32_t transform)
{
    Layer_t *ly = (Layer_t *) layer;

	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %llx", __FUNCTION__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

	ly->transform = transform;
	if (ly->transform == 0) {
		trCachePut(ly->trcache, 1);
		ly->trcache = NULL;
	}

    return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_visible_region(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, hwc_region_t visible)
{
	Layer_t *ly = (Layer_t *)layer;

	unusedpara(device);
    if(!findDisplay(display)){
        ALOGE("%s : bad display %llx", __FUNCTION__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    ly->visibleRegion = visible;

    return HWC2_ERROR_NONE;
}

int32_t hwc_set_layer_z_order(hwc2_device_t* device, hwc2_display_t display,
    hwc2_layer_t layer, uint32_t z)
{

	Layer_t *ly = (Layer_t *)layer;
    Display_t* dp = findDisplay(display);

	unusedpara(device);
    if(!dp){
        ALOGE("%s : bad display %p", __FUNCTION__, (void*)display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
	ly->zorder = z;
	pthread_mutex_lock(&dp->listMutex);
    dp->nubmerLayer += insertLayerByZorder(ly, dp->layerSortedByZorder);
	pthread_mutex_unlock(&dp->listMutex);

    return HWC2_ERROR_NONE;
}

#ifdef COMPOSER_READBACK
int32_t hwc_get_readback_buffer_attributes(
        hwc2_device_t* device, hwc2_display_t display,
        int32_t* /*android_pixel_format_t*/ outFormat,
        int32_t* /*android_dataspace_t*/ outDataspace)
{
    unusedpara(device);
    Display_t* dp = findDisplay(display);

    if (toClientId(dp->clientId) != HWC_DISPLAY_PRIMARY) {
        ALOGE("%s : not supported readback on non primary display", __FUNCTION__);
        return HWC2_ERROR_UNSUPPORTED;
    }
    if (outFormat)
        *outFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    if (outDataspace)
        *outDataspace = HAL_DATASPACE_SRGB;

    return HWC2_ERROR_NONE;
}

int32_t hwc_get_readback_buffer_fence(
        hwc2_device_t* device, hwc2_display_t display,
        int32_t* outFence)
{
    unusedpara(device);
    Display_t* dp = findDisplay(display);

    if (toClientId(dp->clientId) != HWC_DISPLAY_PRIMARY || !outFence) {
        ALOGE("%s : not supported readback on non primary display", __FUNCTION__);
        return HWC2_ERROR_UNSUPPORTED;
    }
    int error = getReadbackBufferFence(display, outFence);
    return error != 0 ? HWC2_ERROR_UNSUPPORTED : HWC2_ERROR_NONE;
}

int32_t hwc_set_readback_buffer(
        hwc2_device_t* device, hwc2_display_t display,
        buffer_handle_t buffer, int32_t releaseFence)
{
    if (!buffer)
        return HWC2_ERROR_BAD_PARAMETER;

    unusedpara(device);
    Display_t* dp = findDisplay(display);

    if (toClientId(dp->clientId) != HWC_DISPLAY_PRIMARY) {
        ALOGE("%s : not supported readback on non primary display", __FUNCTION__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    setReadbackBuffer(display, buffer, releaseFence);
    return HWC2_ERROR_NONE;
}
#endif

int32_t /*hwc2_error_t*/ hwc_get_render_intents(
        hwc2_device_t* device, hwc2_display_t display, int32_t mode,
        uint32_t* outNumIntents,
        int32_t* /*android_render_intent_v1_1_t*/ outIntents)
{
    unusedpara(device);
    if (!findDisplay(display))
        return HWC2_ERROR_BAD_DISPLAY;
    if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
        return HWC2_ERROR_BAD_PARAMETER;

    if (mode != HAL_COLOR_MODE_NATIVE)
        return HWC2_ERROR_UNSUPPORTED;
    else {
        if (!outIntents) {
            *outNumIntents = 1;
            return HWC2_ERROR_NONE;
        } else {
            *outIntents = HAL_RENDER_INTENT_COLORIMETRIC;
            return HWC2_ERROR_NONE;
        }
    }
}

int hwc_set_hdmi_mode(int display, int mode)
{
	mesg_pair_t mesg;
	mesg.disp = display;
	mesg.cmd = 1;
	mesg.data = mode;

	return write(socketpair_fd[0], &mesg, sizeof(mesg)) == sizeof(mesg);
}

int hwc_set_3d_mode(int display, int mode)
{
	mesg_pair_t mesg;
	mesg.disp = display;
	mesg.cmd = 2;
	mesg.data = mode;

	return write(socketpair_fd[0], &mesg, sizeof(mesg)) == sizeof(mesg);
}

int hwc_setHotplug(int display, bool plug)
{
	mesg_pair_t mesg;
	mesg.disp = display;
	mesg.cmd = 3;
	mesg.data = plug;

	return write(socketpair_fd[0], &mesg, sizeof(mesg)) == sizeof(mesg);
}

int hwc_setMargin(int display, int hpercent, int vpercent);
int hwc_setVideoRatio(int display, int radioType);

int hwc_set_margin(int display, int data)
{
	int hpercent, vpercent;
	hpercent = (data & 0xffff0000) >> 16;
	vpercent = data & 0xffff;

	hwc_setMargin(display, hpercent, vpercent);
	return 0;
}

int hwc_set_screenfull(int display, int enable)
{
#if 0
	Display_t **dp = mDisplay;

	for(int i = 0; i < numberDisplay; i++) {
		if (dp[i]->displayId == display)
			dp[i]->ScreenFull = enable;
	}
#endif
	unusedpara(display);
	unusedpara(enable);
	return 0;
}

/* hwc_set_display_command
  *  this is HIDL call for set display arg,
  *
  */
typedef int (*SUNXI_SET_DISPLY_COMMAND)(int display, int cmd1, int cmd2, int data);

int hwc_set_display_command(int display, int cmd1, int cmd2, int data)
{
	int ret = -1;
	switch (cmd1) {
	case HIDL_HDMI_MODE_CMD:
		ret = hwc_set_hdmi_mode(display, cmd2);
	break;
	case HIDL_ENHANCE_MODE_CMD:
		ret = hwc_set_enhance_mode(display, cmd2, data);
	break;
	case HIDL_SML_MODE_CMD:
		ret = hwc_set_smt_backlight(display, data);
	break;
	case HIDL_COLOR_TEMPERATURE_CMD:
		ret = hwc_set_color_temperature(display, cmd2, data);
	break;
	case HIDL_SET3D_MODE:
		ret = hwc_set_3d_mode(display, cmd2);
	break;
	case HIDL_SETMARGIN:
		ret = hwc_set_margin(display, data);
	break;
	case HIDL_SETVIDEORATIO:
		ret = hwc_set_screenfull(display, data);
	break;
	default:
		ALOGD("give us a err cmd");
	}
	return ret;
}

int hwc_setDataSpacemode(int display, int dataspace_mode)
{
	Display_t **dp = mDisplay;
	for (int i = 0; i < numberDisplay; i++) {
		if (toClientId(dp[i]->clientId) == display)
			dp[i]->dataspace_mode = dataspace_mode;
	}
	return 0;
}


/* API for H6 */
int hwc_setBlank(int clientId, int enable)
{
	Display_t* dp = NULL;
	for (int i = 0; i < numberDisplay; i++) {
		if(toClientId(mDisplay[i]->clientId) == clientId) {
			dp = mDisplay[i];
			break;
		}
	}
	if (!dp) {
		ALOGE("ERROR %s:bad display", __FUNCTION__);
		return HWC2_ERROR_BAD_DISPLAY;
	}

#ifdef TARGET_PLATFORM_HOMLET
	if (enable == 1) {
		hwc_setBlank(dp->displayId);
		dp->hwPlug = -1;
		deinit_sync(dp->displayId);
		init_sync(dp->displayId);
	} else {
		dp->hwPlug = 0;
	}
#endif
	return 0;
}

/* API for H6 */
int hwc_setBlank(int hwid)
{
	int ret;
	ret = clearAllLayers(hwid);
	if (ret)
		ALOGE("Clear all layers failed!");
	return 0;
}

tv_para_t tv_mode[]=
{
	/* 1'st is default */
	{DISP_TV_MOD_1080P_60HZ,       1920,   1080, 60, 0},
	{DISP_TV_MOD_720P_60HZ,        1280,   720,  60, 0},

	{DISP_TV_MOD_480I,             720,    480, 60, 0},
	{DISP_TV_MOD_576I,             720,    576, 60, 0},
	{DISP_TV_MOD_480P,             720,    480, 60, 0},
	{DISP_TV_MOD_576P,             720,    576, 60, 0},
	{DISP_TV_MOD_720P_50HZ,        1280,   720, 50, 0},

	{DISP_TV_MOD_1080P_24HZ,       1920,   1080, 24, 0},
	{DISP_TV_MOD_1080P_50HZ,       1920,   1080, 50, 0},

	{DISP_TV_MOD_1080I_50HZ,       1920,   1080, 50, 0},
	{DISP_TV_MOD_1080I_60HZ,       1920,   1080, 60, 0},
	{DISP_TV_MOD_3840_2160P_25HZ,  3840,   2160, 25, 0},
	{DISP_TV_MOD_3840_2160P_24HZ,  3840,   2160, 24, 0},
	{DISP_TV_MOD_3840_2160P_30HZ,  3840,   2160, 30, 0},
	{DISP_TV_MOD_4096_2160P_24HZ,  4096,   2160, 24, 0},
	{DISP_TV_MOD_4096_2160P_25HZ,  4096,   2160, 25, 0},
	{DISP_TV_MOD_4096_2160P_30HZ,  4096,   2160, 30, 0},
	{DISP_TV_MOD_3840_2160P_60HZ,  3840,   2160, 60, 0},
	{DISP_TV_MOD_4096_2160P_60HZ,  4096,   2160, 60, 0},
	{DISP_TV_MOD_3840_2160P_50HZ,  3840,   2160, 50, 0},
	{DISP_TV_MOD_4096_2160P_50HZ,  4096,   2160, 50, 0},
	{DISP_TV_MOD_1080P_24HZ_3D_FP, 1920,   1080, 24, 0},
	{DISP_TV_MOD_720P_50HZ_3D_FP,  1280,   720, 50, 0},
	{DISP_TV_MOD_720P_60HZ_3D_FP,  1280,   720, 60, 0},
};
/* display is the clientId(0~1) */
int hwc_setOutputMode(int display, int type, int mode)
{
	Display_t **dp = mDisplay;
	int i, num;
	int ScreenWidth, ScreenHeight;

	if (type == DISP_OUTPUT_TYPE_HDMI) {
		num = sizeof(tv_mode)/sizeof(tv_mode[0]);
		for (i = 0; i < num; i++) {
			if (tv_mode[i].mode == mode) {
				ScreenWidth = tv_mode[i].width;
				ScreenHeight = tv_mode[i].height;
				break;
			}
		}
	} else if (type == DISP_OUTPUT_TYPE_TV) {
		if (mode == DISP_TV_MOD_PAL) {
			ScreenWidth = 720;
			ScreenHeight = 576;
		} else if (mode == DISP_TV_MOD_NTSC) {
			ScreenWidth = 720;
			ScreenHeight = 480;
		}
	}

	for (int i = 0; i < numberDisplay; i++) {
		if (toClientId(dp[i]->clientId) == display) {
			pthread_mutex_lock(&dp[i]->ConfigMutex);
			dp[i]->VarDisplayWidth = ScreenWidth;
			dp[i]->VarDisplayHeight = ScreenHeight;
			pthread_mutex_unlock(&dp[i]->ConfigMutex);
                        callRefresh(dp[i]);
		}
	}
	return 0;
}

int hwc_setMargin(int display, int hpercent, int vpercent)
{
	Display_t **dp = mDisplay;
	for (int i = 0; i < numberDisplay; i++) {
		if (toClientId(dp[i]->clientId) == display) {
			pthread_mutex_lock(&dp[i]->ConfigMutex);
			dp[i]->hpercent = hpercent;
			dp[i]->vpercent = vpercent;
			pthread_mutex_unlock(&dp[i]->ConfigMutex);
		}
	}
	return 0;
}

int hwc_setVideoRatio(int display, int radioType)
{
	Display_t **dp = mDisplay;

	switch(radioType)
	{
		case SCREEN_AUTO:
		case SCREEN_FULL:
			for(int i = 0; i < numberDisplay; i++)
			{
				if (toClientId(dp[i]->clientId) == display)
					dp[i]->screenRadio = radioType;
			}
			break;
		default:
			break;
	}
	return 0;
}

int hwc_setSwitchdevice(struct switchdev *switchdev)
{
	static bool cvbs_init = 0;
	int i, old_active, new_active, old = -1;
	Display_t *secdisp = mDisplay[1];
	Display_t *firstisp = mDisplay[0];
	if (toClientId(secdisp->clientId) == 0) {
		secdisp = mDisplay[0];
		firstisp = mDisplay[1];
	}
	old_active = secdisp->plugIn + firstisp->plugIn;
	new_active = switchdev[0].en + switchdev[1].en;

#ifdef TARGET_PLATFORM_HOMLET
	firstisp->hwPlug = 0;
#endif

	if (!cvbs_init) {
		init_sync(1);
		cvbs_init = 1;
	}

	for (i = 0; i < numberDisplay; i++) {
		ALOGD("display=%d, type=%d, mode=%d, en=%d",
			switchdev[i].display, switchdev[i].type, switchdev[i].mode, switchdev[i].en);
		hwc_setOutputMode(switchdev[i].display, switchdev[i].type, switchdev[i].mode);
	}

	ALOGD("%s:old_active=%d, new_active=%d",__func__, old_active, new_active);

	if (switchdev[0].type == DISP_OUTPUT_TYPE_TV) {
		old = hwc_set_primary(1, 1);
	} else if (switchdev[0].type == DISP_OUTPUT_TYPE_HDMI) {
#ifdef TARGET_PLATFORM_HOMLET
		int state;
		if (old_active == 1 && new_active == 1
				&& hdmifd > 0) {
			lseek(hdmifd, 5, SEEK_SET);
			read(hdmifd, &state, 1);
			if (state != '1') {
				ALOGD("%s:switch hdmi but it plug out",__func__);
				/*TODO should stop sent buf or not*/
				/*firstisp->hwPlug = -1;*/
			}
		}
#endif
		old = hwc_set_primary(0, 1);
	}
	/* all plugout must set 0(hdmi) active */
	if (new_active == 0) {
		if (old == -1)
			old = hwc_set_primary(0, 1);
		init_sync(0);
	}
	/* 2-->1,  or 2-->0 will 0 is the primary display, must call 1 hotplug out  */
	if (old_active > new_active) {
		if (old_active == 2) {
			hwc_setHotplug(1, 0);
		}
	}
	/* 1-->2, no 0-->2  must promise primary display is in active*/
	if (old_active < new_active) {
		if (new_active == 2) {
			if (old == 1) {
				init_sync(0);
				/*fence will be reset and never signal when init without deinit*/
				/*so, deinit here first, hotplug below will init back*/
				deinit_sync(1);
			}
			hwc_setHotplug(1, 1);
		}
	}
	/* 1==1, 2==2, swap it, and if 1 deactive will relase the swap out fence */
	if (old_active == new_active) {
		if (new_active == 1) {
			if (old != -1){
				/* clear layers */
				hwc_setBlank(old);
				/* init fence */
				init_sync(primary_disp);
				/* release fence */
				deinit_sync(old);
			}
		}
	}

	return 0;
}

/*get primary display solution for show external display point to point*/
int hwc_getPriDispSlt(int* width, int* height) {
    for (int i = 0; i < numberDisplay; i++) {
        if(toClientId(mDisplay[i]->clientId) == 0) {
            Display_t *dp = mDisplay[i];
            *width = dp->displayConfigList[dp->activeConfigId]->width;
            *height = dp->displayConfigList[dp->activeConfigId]->height;
            return 0;
        }
    }
    return -1;
}

/* hwc_device_getFunction(..., descriptor)
 * Returns a function pointer which implements the requested description
 *
 * Parameters:
 *  descriptor - the function to return
 * Returns either a function pointer implementing the requested descriptor
 *  or NULL if the described function is not supported by this device.
 */
template <typename PFN, typename T>
static hwc2_function_pointer_t asFP(T function){
    return reinterpret_cast<hwc2_function_pointer_t>(function);
}

hwc2_function_pointer_t hwc_device_getFunction(struct hwc2_device* device,
    int32_t /*hwc2_function_descriptor_t*/ descriptor)
{
    unusedpara(device);
    switch(descriptor){
    case HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES:
        return asFP<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(
            hwc_accept_display_changes);
    case HWC2_FUNCTION_CREATE_LAYER:
        return asFP<HWC2_PFN_CREATE_LAYER>(
            hwc_create_layer);
    case HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY:
        return asFP<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(
            hwc_create_virtual_display);
    case HWC2_FUNCTION_DESTROY_LAYER:
        return asFP<HWC2_PFN_DESTROY_LAYER>(
            hwc_destroy_layer);
    case HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY:
        return asFP<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(
            hwc_destroy_virtual_display);
    case HWC2_FUNCTION_DUMP:
        return asFP<HWC2_PFN_DUMP>(hwc_dump);
    case HWC2_FUNCTION_GET_ACTIVE_CONFIG:
        return asFP<HWC2_PFN_GET_ACTIVE_CONFIG>(
            hwc_get_active_config);
    case HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES:
        return asFP<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(
            hwc_get_changed_composition_types);
    case HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT:
        return asFP<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(
            hwc_get_client_target_support);
    case HWC2_FUNCTION_GET_COLOR_MODES:
        return asFP<HWC2_PFN_GET_COLOR_MODES>(
            hwc_get_color_modes);
    case HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE:
        return asFP<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(
            hwc_get_display_attribute);
    case HWC2_FUNCTION_GET_DISPLAY_CONFIGS:
        return asFP<HWC2_PFN_GET_DISPLAY_CONFIGS>(
            hwc_get_display_configs);
    case HWC2_FUNCTION_GET_DISPLAY_NAME:
        return asFP<HWC2_PFN_GET_DISPLAY_NAME>(
            hwc_get_display_name);
    case HWC2_FUNCTION_GET_DISPLAY_REQUESTS:
        return asFP<HWC2_PFN_GET_DISPLAY_REQUESTS>(
            hwc_get_display_requests);
    case HWC2_FUNCTION_GET_DISPLAY_TYPE:
        return asFP<HWC2_PFN_GET_DISPLAY_TYPE>(
            hwc_get_display_type);
    case HWC2_FUNCTION_GET_DOZE_SUPPORT:
        return asFP<HWC2_PFN_GET_DOZE_SUPPORT>(
            hwc_get_doze_support);
    case HWC2_FUNCTION_GET_HDR_CAPABILITIES:
        return asFP<HWC2_PFN_GET_HDR_CAPABILITIES>(
            hwc_get_hdr_capabilities);
    case HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT:
        return asFP<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(
            hwc_get_max_virtual_display_count);
    case HWC2_FUNCTION_GET_RELEASE_FENCES:
        return asFP<HWC2_PFN_GET_RELEASE_FENCES>(
            hwc_get_release_fences);
    case HWC2_FUNCTION_PRESENT_DISPLAY:
        return asFP<HWC2_PFN_PRESENT_DISPLAY>(
            hwc_present_display);
    case HWC2_FUNCTION_REGISTER_CALLBACK:
        return asFP<HWC2_PFN_REGISTER_CALLBACK>(
            hwc_register_callback);
    case HWC2_FUNCTION_SET_ACTIVE_CONFIG:
        return asFP<HWC2_PFN_SET_ACTIVE_CONFIG>(
            hwc_set_active_config);
    case HWC2_FUNCTION_SET_CLIENT_TARGET:
        return asFP<HWC2_PFN_SET_CLIENT_TARGET>(
            hwc_set_client_target);
    case HWC2_FUNCTION_SET_COLOR_MODE:
        return asFP<HWC2_PFN_SET_COLOR_MODE>(
            hwc_set_color_mode);
    case HWC2_FUNCTION_SET_COLOR_MODE_WITH_RENDER_INTENT:
        return asFP<HWC2_PFN_SET_COLOR_MODE_WITH_RENDER_INTENT>(
            hwc_set_color_mode_with_render_intent);
    case HWC2_FUNCTION_SET_COLOR_TRANSFORM:
        return asFP<HWC2_PFN_SET_COLOR_TRANSFORM>(
            hwc_set_color_transform);
    case HWC2_FUNCTION_SET_CURSOR_POSITION:
        return asFP<HWC2_PFN_SET_CURSOR_POSITION>(
            hwc_set_cursor_position);
    case HWC2_FUNCTION_SET_LAYER_BLEND_MODE:
        return asFP<HWC2_PFN_SET_LAYER_BLEND_MODE>(
            hwc_set_layer_blend_mode);
    case HWC2_FUNCTION_SET_LAYER_BUFFER:
        return asFP<HWC2_PFN_SET_LAYER_BUFFER>(
            hwc_set_layer_buffer);
    case HWC2_FUNCTION_SET_LAYER_COLOR:
        return asFP<HWC2_PFN_SET_LAYER_COLOR>(
            hwc_set_layer_color);
    case HWC2_FUNCTION_SET_LAYER_COMPOSITION_TYPE:
        return asFP<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(
            hwc_set_layer_composition_type);
    case HWC2_FUNCTION_SET_LAYER_DATASPACE:
        return asFP<HWC2_PFN_SET_LAYER_DATASPACE>(
            hwc_set_layer_dataspace);
    case HWC2_FUNCTION_SET_LAYER_DISPLAY_FRAME:
        return asFP<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(
            hwc_set_layer_display_frame);
    case HWC2_FUNCTION_SET_LAYER_PLANE_ALPHA:
        return asFP<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(
            hwc_set_layer_plane_alpha);
    case HWC2_FUNCTION_SET_LAYER_SIDEBAND_STREAM:
        return asFP<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(
            hwc_set_layer_sideband_stream);
    case HWC2_FUNCTION_SET_LAYER_SOURCE_CROP:
        return asFP<HWC2_PFN_SET_LAYER_SOURCE_CROP>(
            hwc_set_layer_source_crop);
    case HWC2_FUNCTION_SET_LAYER_SURFACE_DAMAGE:
        return asFP<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(
            hwc_set_layer_surface_damage);
    case HWC2_FUNCTION_SET_LAYER_TRANSFORM:
        return asFP<HWC2_PFN_SET_LAYER_TRANSFORM>(
            hwc_set_layer_transform);
    case HWC2_FUNCTION_SET_LAYER_VISIBLE_REGION:
        return asFP<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(
            hwc_set_layer_visible_region);
    case HWC2_FUNCTION_SET_LAYER_Z_ORDER:
        return asFP<HWC2_PFN_SET_LAYER_Z_ORDER>(
            hwc_set_layer_z_order);
    case HWC2_FUNCTION_SET_OUTPUT_BUFFER:
        return asFP<HWC2_PFN_SET_OUTPUT_BUFFER>(
            hwc_set_output_buffer);
    case HWC2_FUNCTION_SET_POWER_MODE:
        return asFP<HWC2_PFN_SET_POWER_MODE>(
            hwc_set_power_mode);
    case HWC2_FUNCTION_SET_VSYNC_ENABLED:
        return asFP<HWC2_PFN_SET_VSYNC_ENABLED>(
            hwc_set_vsync_enabled);
    case HWC2_FUNCTION_VALIDATE_DISPLAY:
        return asFP<HWC2_PFN_VALIDATE_DISPLAY>(
            hwc_validate_display);
#ifdef COMPOSER_READBACK
    case HWC2_FUNCTION_SET_READBACK_BUFFER:
		return asFP<HWC2_PFN_SET_READBACK_BUFFER>(
            hwc_set_readback_buffer);
    case HWC2_FUNCTION_GET_READBACK_BUFFER_ATTRIBUTES:
		return asFP<HWC2_PFN_GET_READBACK_BUFFER_ATTRIBUTES>(
            hwc_get_readback_buffer_attributes);
    case HWC2_FUNCTION_GET_READBACK_BUFFER_FENCE:
		return asFP<HWC2_PFN_GET_READBACK_BUFFER_FENCE>(
            hwc_get_readback_buffer_fence);
#endif
	// render intent
	case HWC2_FUNCTION_GET_RENDER_INTENTS:
		return asFP<HWC2_PFN_GET_RENDER_INTENTS>(hwc_get_render_intents);
	// legacy, sunxi private command
	case HWC2_FUNCTION_SUNXI_SET_DISPLY:
		return asFP<SUNXI_SET_DISPLY_COMMAND>(
            hwc_set_display_command);
    }
    return NULL;
}

int hwc_device_close(struct hw_device_t* device)
{
	unusedpara(device);
	/* TODO */
    return 0;
}

void deviceManger(void *data, hwc2_display_t display, hwc2_connection_t connection)
{
	Display_t *dp = findDisplay(display, false);
	DisplayOpr_t *opt;
	int ret = 1;
	unusedpara(data);

	if(!dp){
		ALOGE("%s : bad display %p", __FUNCTION__, dp);
		return;
	}
	opt = dp->displayOpration;
	switch (connection) {
		/* think about surfaceFlinger switch it ,but not hotplug */
		case HWC2_CONNECTION_CONNECTED:
			if (dp->plugIn)
				return;
			ret = opt->init(dp);
			dp->deinited = false;
			break;
		case HWC2_CONNECTION_DISCONNECTED:
			if (!dp->plugIn)
				return;
			dp->deinited = true;
			ret = opt->deInit(dp);
			break;
		default:
			ALOGE("give us an error connection[%d]",connection);
	}
}
/* usually pad  initial... */
void __attribute__((weak)) platform_init(Display_t **disp, int num)
{
	/* Pad use the lcd for primary disp, and hdmi is the second disp */
	int i = 0;
	for (i = 0; i < num; i++) {
		if (disp[i]->displayName != NULL
			&& !strcmp(disp[i]->displayName, "lcd")) {
			disp[i]->clientId = toHwc2client(0, disp[i]->displayId);
			disp[i]->plugInListen = 0;
			primary_disp = 0;
		}else{
			disp[i]->displayName = displayName[1];
			disp[i]->clientId = toHwc2client(1, disp[i]->displayId);
			disp[i]->plugInListen = 1;
		}

	}
	ALOGD("hwc use default platform[pad].");
}

static int hwc_device_open(const struct hw_module_t* module, const char* id,
    struct hw_device_t** device)
{
    hwc2_device_t* hwcDevice;
    hw_device_t* hwDevice;
    int ret = 0;

    if(strcmp(id, HWC_HARDWARE_COMPOSER)){
        return -EINVAL;
    }
    hwcDevice = (hwc2_device_t*)malloc(sizeof(hwc2_device_t));
    if(!hwcDevice){
        ALOGE("%s: Failed to allocate memory", __func__);
        return -ENOMEM;
    }
    memset(hwcDevice, 0, sizeof(hwc2_device_t));
    hwDevice = (hw_device_t*)hwcDevice;

    hwcDevice->common.tag = HARDWARE_DEVICE_TAG;
    hwcDevice->common.version = HWC_DEVICE_API_VERSION_2_0;
    hwcDevice->common.module = const_cast<hw_module_t*>(module);
    hwcDevice->common.close = hwc_device_close;
    hwcDevice->getCapabilities = hwc_device_getCapabilities;
    hwcDevice->getFunction = hwc_device_getFunction;
    *device = hwDevice;
	numberDisplay = displayDeviceInit(&mDisplay);
	if (numberDisplay > 2) {
		ALOGD("Display support visual display");
	}
	if (numberDisplay < 0) {
		ALOGE("initial the hwc err...");
		return -ENODEV;
	}

	ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, socketpair_fd);
	if (ret != 0) {
		ALOGE("socket pair err %d", ret);
	}
	layerCacheInit();
	Iondeviceinit();
	rotateDeviceInit();
	eventThreadInit(mDisplay, numberDisplay, socketpair_fd[1]);
	debugInit(numberDisplay);
	memCtrlInit(mDisplay, numberDisplay);
	registerEventCallback(0x03, HWC2_CALLBACK_HOTPLUG, 0,
		NULL, (hwc2_function_pointer_t)deviceManger);

	primarymutex = new Mutex();
	primarycond = new Condition();

	platform_init(mDisplay, numberDisplay);

	// Private service for vendor display command handling.
	vendorservice_init();

#ifdef ENABLE_WRITEBACK
	Display_t* dp0 =  findHwDisplay(0);
	if (dp0 != NULL) {
		initWriteBack(dp0);
	}
#endif

#ifdef COMPOSER_READBACK
    initReadback();
#endif

	ALOGD("open completely successful ");
    return 0;
}

//define the module methods
static struct hw_module_methods_t hwc_module_methods = {
    .open = hwc_device_open,
};

//define the entry point of the module
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "Allwinner Hwcomposer Module",
    .author = "Jet Cui",
    .methods = &hwc_module_methods,
};
static void __attribute__((constructor))hal_Init(void) {
	hwc_mem_debug_init();
	ALOGD("hwc init constructor");
}

static void __attribute__((destructor)) hal_exit(void){
	IondeviceDeinit();
	rotateDeviceDeInit(numberDisplay);
	eventThreadDeinit();
#ifdef ENABLE_WRITEBACK
	Display_t* dp0 =  findHwDisplay(0);
	if (dp0 != NULL) {
		deinitWriteBack(dp0);
	}
#endif
	displayDeviceDeInit(mDisplay);
	layerCacheDeinit();
	ALOGD("hwc deinit destructor");
}
