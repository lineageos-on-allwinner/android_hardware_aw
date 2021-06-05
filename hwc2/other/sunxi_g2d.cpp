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

#include <mutex>
#include <condition_variable>
#include <vector>

#include "../hwc.h"
#include "sunxi_g2d.h"

/* sync with submit thread ,may need 2 (waite sync afrer sbumit),
  * may 3(wait sync befor submit)
  */

typedef struct {
	int timeout;
	int trErrCnt;
}tr_per_disp_t;

#define ALLOC_PERMIT_PER_FRAME 1

static int trfd = -1;
static int sw_sync_fd = -1;
static unsigned sw_sync_count = 0;
static unsigned inc_count = 0;
int trBitMap;
tr_per_disp_t *tr_disp;
static pthread_mutex_t trCacheMutex;

char thread_name[32];
int priority;
static pthread_t thread_id;
static struct listnode SubmitHead;
static Mutex *rotate_mutex;
static Condition *rotate_cond;

typedef struct rotate_info {
	struct listnode node;
	int waite_fence;
	int dst_waite_fence;
	unsigned int syncCount;
	unsigned int gsyncCount;
	Layer_t *layer2;
	Display_t *disp;
}rotate_info_t;

/* alloc buffer */
static Mutex *alloc_mutex;
static Condition *alloc_cond;
static struct listnode mem_alloc_list;
static pthread_t mem_id;

#define TR_CACHE_SHRINK_NUM 16
static struct listnode rcacheList;
static int rcache_cout;
static pthread_mutex_t rchaceMutex;

#ifndef USE_IOMMU
bool mustconfig = 1;
#else
bool mustconfig = 0;
#endif

static pthread_t mBufferFreeThread;
std::mutex mPendingLock;
std::condition_variable mConditionForPendingBuffer;
std::vector<tr_cache_Array*> mPendingTransformBuffer;

void* pendingBufferFreeHandler(void *)
{
    while (1) {
        std::vector<tr_cache_Array*> pending;
        pending.clear();

        {
            std::unique_lock<std::mutex> _l(mPendingLock);
            if (mPendingTransformBuffer.empty()) {
                mConditionForPendingBuffer.wait(_l);
            }

            pending.swap(mPendingTransformBuffer);
        }

        for (int x = 0; x < pending.size(); x++) {
            tr_cache_Array* cache = pending[x];

            for (int i = 0; i < NOMORL_CACHE_N; i++) {
                if(cache->array[i].releasefd >=0) {
                    sync_wait(cache->array[i].releasefd, -1);
                    close(cache->array[i].releasefd);
                }
                close(cache->array[i].share_fd);
            }
            hwc_free(cache);
        }
    }
}

void moveCacheToPendingList(tr_cache_Array* cache)
{
    std::unique_lock<std::mutex> _l(mPendingLock);
    mPendingTransformBuffer.push_back(cache);
    mConditionForPendingBuffer.notify_all();
}

void submitMemAlloc(tr_cache_Array *cache);
void *rotateMemLoop(void *list);

rotate_info_t* trlistCacheGet(void)
{
	rotate_info_t *tr_info = NULL;
	struct listnode *node = NULL;

	pthread_mutex_lock(&rchaceMutex);
	if (rcache_cout > 0) {
		rcache_cout--;
		node = list_head(&rcacheList);
		list_remove(node);
		list_init(node);
        tr_info = node_to_item(node, rotate_info_t, node);
	}
	pthread_mutex_unlock(&rchaceMutex);
	if (tr_info != NULL)
		goto deal;
	tr_info = (rotate_info_t *)hwc_malloc(sizeof(rotate_info_t));
	if (tr_info == NULL){
		ALOGE("%s:malloc tr_info err...",__FUNCTION__);
		return NULL;
	}

deal:
	memset(tr_info, 0, sizeof(rotate_info_t));
	tr_info->dst_waite_fence = -1;
	tr_info->waite_fence = -1;
	tr_info->layer2 = NULL;
	list_init(&tr_info->node);

	return tr_info;
}

void trlistCachePut(rotate_info_t *tr_info)
{
	if (tr_info->waite_fence >= 0) {
		close(tr_info->waite_fence);
		tr_info->waite_fence = -1;;
	}
	if (tr_info->dst_waite_fence >= 0) {
		close(tr_info->dst_waite_fence);
		tr_info->dst_waite_fence = -1;
	}

	list_remove(&tr_info->node);
	list_init(&tr_info->node);
	tr_info->layer2 = NULL;

	pthread_mutex_lock(&rchaceMutex);
	if (rcache_cout > TR_CACHE_SHRINK_NUM) {
        hwc_free(tr_info);
		pthread_mutex_unlock(&rchaceMutex);
		return;
	}

	rcache_cout++;
	list_add_tail(&rcacheList, &tr_info->node);
	pthread_mutex_unlock(&rchaceMutex);

}

void* rotateThreadLoop(void *list);

static inline int trFormatToHal(unsigned char tr)
{
	switch(tr) {
	case G2D_FORMAT_YUV420UVC_V1U1V0U0:
		return HAL_PIXEL_FORMAT_YCrCb_420_SP;
	case G2D_FORMAT_YUV420_PLANAR:
		return HAL_PIXEL_FORMAT_YV12;
	case G2D_FORMAT_YUV420UVC_U1V1U0V0:
		return HAL_PIXEL_FORMAT_AW_NV12;
	case G2D_FORMAT_RGBA8888:
		return HAL_PIXEL_FORMAT_RGBA_8888;
	case G2D_FORMAT_RGBX8888:
		return HAL_PIXEL_FORMAT_RGBX_8888;
	case G2D_FORMAT_BGRA8888:
		return HAL_PIXEL_FORMAT_BGRA_8888;
	case G2D_FORMAT_BGRX8888:
		return HAL_PIXEL_FORMAT_BGRX_8888;
	case G2D_FORMAT_RGB888:
		return HAL_PIXEL_FORMAT_RGB_888;
	case G2D_FORMAT_RGB565:
		return HAL_PIXEL_FORMAT_RGB_565;
	default :
		return HAL_PIXEL_FORMAT_AW_NV12;
	}

}

static inline g2d_fmt_enh halToTRFormat(int halFformat)
{
	switch(halFformat) {
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
		return G2D_FORMAT_YUV420UVC_V1U1V0U0;
	case HAL_PIXEL_FORMAT_YV12:
		return G2D_FORMAT_YUV420_PLANAR;
	case HAL_PIXEL_FORMAT_AW_NV12:
		return G2D_FORMAT_YUV420UVC_U1V1U0V0;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		return G2D_FORMAT_RGBA8888;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		return G2D_FORMAT_RGBX8888;
	case HAL_PIXEL_FORMAT_BGRA_8888:
		return G2D_FORMAT_BGRA8888;
	case HAL_PIXEL_FORMAT_BGRX_8888:
		return G2D_FORMAT_BGRX8888;
	case HAL_PIXEL_FORMAT_RGB_888:
		return G2D_FORMAT_RGB888;
	case HAL_PIXEL_FORMAT_RGB_565:
		return G2D_FORMAT_RGB565;
	default :
		return G2D_FORMAT_YUV420UVC_U1V1U0V0;
	}
}

int hwc_rotate_commit(g2d_blt_h *tr_info)
{
	int ret;

	ret = ioctl(trfd, G2D_CMD_BITBLT_H, (unsigned long)tr_info);
	if (ret < 0)
		ALOGE("commit rotate err");

	return ret;
}

int rotateDeviceInit(void)
{
	trfd = open("/dev/g2d", O_RDWR);
	if(trfd < 0) {
		ALOGE("Failed to open g2d device");
		return -1;
	}
	tr_disp = (tr_per_disp_t *)hwc_malloc(sizeof(tr_per_disp_t));
	if(tr_disp == NULL) {
		close(trfd);
		trfd = -1;
		ALOGE("Failed to alloc client for hwc");
		return -1;
	}
	sw_sync_fd = sw_sync_timeline_create();
	if (sw_sync_fd < 0) {
		close(trfd);
		trfd = -1;
		hwc_free(tr_disp);
		tr_disp = NULL;
		ALOGE("Failed to init sy_sync for hwc");
		return -1;
	}
	rotate_mutex = new Mutex();
	rotate_cond = new Condition();

	pthread_mutex_init(&rchaceMutex, 0);
	list_init(&rcacheList);
	rcache_cout = 0;
	list_init(&SubmitHead);
	pthread_create(&thread_id, NULL, rotateThreadLoop, &SubmitHead);

	pthread_mutex_init(&trCacheMutex, 0);

	alloc_mutex = new Mutex();
	alloc_cond = new Condition();
	list_init(&mem_alloc_list);
	pthread_create(&mem_id, NULL, rotateMemLoop, &mem_alloc_list);

	pthread_create(&mBufferFreeThread, NULL, pendingBufferFreeHandler, NULL);
	return 0;
}

int rotateDeviceDeInit(int num)
{
	unusedpara(num);
	if (trfd < 0)
		return 0;
	pthread_join(thread_id, NULL);
	delete(rotate_mutex);
	delete(rotate_cond);
	pthread_join(mem_id, NULL);
	delete(alloc_mutex);
	delete(alloc_cond);
	close(sw_sync_fd);
	close(trfd);
	trfd = -1;
	hwc_free(tr_disp);
	tr_disp = NULL;
	return 0;
}

static inline g2d_blt_flags_h toTrMode(unsigned int mode)
{
    int ret = G2D_ROT_0;

    if (mode == HAL_TRANSFORM_ROT_180)
        ret = G2D_ROT_180;
    else if (mode == HAL_TRANSFORM_ROT_270) {
        ret = G2D_ROT_270;
    } else {
        ret = 0;
        if (mode & HAL_TRANSFORM_FLIP_H)
            ret |= G2D_ROT_H;
        if (mode & HAL_TRANSFORM_FLIP_V)
            ret |= G2D_ROT_V;
        if (mode & HAL_TRANSFORM_ROT_90)
            ret |= G2D_ROT_90;
    }
    return (g2d_blt_flags_h)ret;
}

int culateTimeout(Layer_t *layer)
{
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;

    unsigned int dst = handle->width * handle->height;
    if (dst > 2073600) {
        return 100;
    }
    if (dst > 1024000) {
        return 50;
    }
    return 32;
}

bool trCachePut(void *aCache, bool destroyed)
{
	tr_cache_Array *cache;
	cache = (tr_cache_Array *)aCache;

	if (cache == NULL)
		return true;
	if (!destroyed && !cache->complete)
		return false;

	pthread_mutex_lock(&trCacheMutex);
	cache->ref--;
	if (cache->ref > 0) {
		pthread_mutex_unlock(&trCacheMutex);
		return cache->complete;
	}
	pthread_mutex_unlock(&trCacheMutex);

	if (cache->ref < 0)
		return true;/*already release*/

	// We should not free transform buffer until display had
	// finish reading from them, In other word, we should wait
	// release fence signal before freeing pending buffers.
	moveCacheToPendingList(cache);
	return true;
}

void* trCacheGet(Layer_t *layer)
{
	tr_cache_Array *cache;
	cache = (tr_cache_Array *)layer->trcache;
	if (cache == NULL)
		return NULL;
	pthread_mutex_lock(&trCacheMutex);
	cache->ref++;
	pthread_mutex_unlock(&trCacheMutex);
	return (void*)cache;
}

tr_cache_t* acquireLastValid(Layer_t *layer)
{
	tr_cache_Array *aCache = (tr_cache_Array *)layer->trcache;
	int i = 0;
	tr_cache_t *ccache = NULL;
	unsigned int last = 0;

	for (i = 0; i < NOMORL_CACHE_N; i++) {
		if (aCache->array[i].sync_cnt > last
			&& aCache->array[i].valid) {
			ccache = &aCache->array[i];
			last = aCache->array[i].sync_cnt;
		}
	}

	return ccache;
}

tr_cache_t* dequeueTrBuffer(Layer_t *layer, rotate_info_t *rt_info)
{
	tr_cache_t *ccache = NULL;
	tr_cache_Array *aCache = (tr_cache_Array *)layer->trcache;

	ccache = &aCache->array[rt_info->gsyncCount%NOMORL_CACHE_N];
	if(ccache->share_fd < 0)
		return NULL;
	if (ccache->releasefd >= 0) {
		if (sync_wait((int)ccache->releasefd, 3000)) {
				ALOGE("dequeueTrBuffer waite aquire fence err %d current:%u disp:%u",
					ccache->releasefd, rt_info->gsyncCount,rt_info->disp->commitThread->diplayCount);
						/* dump fence */
		}
		close(ccache->releasefd);
		ccache->releasefd = -1;
	}

	return ccache;
}

bool dequeueTrCache(Display_t *display, Layer_t *layer)
{
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;
	tr_cache_Array *aCache =  NULL;
	int size, i, stride;
	/* duto alloc buffer usually use 100'ms, so need control */

	stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
	size = HWC_ALIGN(handle->width, handle->aw_byte_align[0]) * HWC_ALIGN(handle->height, handle->aw_byte_align[0])
			* getBitsPerPixel(layer) / 8;

	/* because yv12 need align u pitch and y pitch */
	if (handle->format == HAL_PIXEL_FORMAT_YV12) {
		int dstw, dsth;
		if (layer->transform & HAL_TRANSFORM_ROT_90) {
			dstw = handle->height;
			dsth = handle->width;
		} else {
			dstw = handle->width;
			dsth = handle->height;
		}

		int ystride = HWC_ALIGN(dstw,     handle->aw_byte_align[0]);
		int vstride = HWC_ALIGN(dstw / 2, handle->aw_byte_align[1]);
		int ustride = HWC_ALIGN(dstw / 2, handle->aw_byte_align[2]);
		size = dsth * ystride + dsth * vstride / 2 + dsth * ustride / 2;
	}

	size = HWC_ALIGN(size, 4096);

	aCache = (tr_cache_Array *) layer->trcache;
	if (aCache != NULL) {
		if (aCache->size >= size)
			goto deal;
		trCachePut(layer->trcache, 1);
		layer->trcache = NULL;
	}

	aCache = (tr_cache_Array *)hwc_malloc(sizeof(tr_cache_Array));
	if (aCache == NULL) {
		ALOGE("malloc cache array err");
		return false;
	}
	ALOGV("layer:%p:  %p:size:%d x %d size:%d",layer, aCache,handle->width, handle->height, size);
	memset(aCache, 0, sizeof(tr_cache_Array));
	for (i= 0; i < NOMORL_CACHE_N; i++) {
		aCache->array[i].sync_cnt = -NOMORL_CACHE_N;
		aCache->array[i].releasefd = -1;
		aCache->array[i].share_fd = -1;
	}
	/* 1 for init, 1 for alloc = 2 */
	aCache->ref = 2;
	aCache->size = size;
	layer->trcache = (void*)aCache;
	list_init(&aCache->node);
	submitMemAlloc(aCache);
deal:
	return aCache->array[display->frameCount%NOMORL_CACHE_N].share_fd >= 0;

}

void trResetErr(void)
{
	if (trfd < 0)
		return;
	tr_disp->trErrCnt = 0;
}

bool trHarewareRistrict(Layer_t *layer)
{
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;
	int stride;
	if (handle == NULL)
		return false;

	/* just for yuv stride*/
	/*if (((long long)handle->width) * handle->height > 1100 * 1950)
	 *	return false;
	 */
	stride = HWC_ALIGN(handle->width, handle->aw_byte_align[0]);
	switch(handle->format) {
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_BGRX_8888:
		stride = HWC_ALIGN(handle->width * 4, handle->aw_byte_align[0]);
		if (stride % 4)
			return false;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		stride = HWC_ALIGN(handle->width * 2, handle->aw_byte_align[0]);
		if (stride % 2)
			return false;
		break;
	case HAL_PIXEL_FORMAT_YV12:
		if (stride % 4)
			return false;
		// workaround for android q media vp9 cts, when the
		// height is 182 or 362, using GPU composer
		if (handle->height == 182 || handle->height == 362) {
			ALOGD("miles_debug: hwc workaround for vp9 cts\n");
			return false;
		}
		break;
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
	case HAL_PIXEL_FORMAT_AW_NV12:
		if (stride % 2)
			return false;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		stride = HWC_ALIGN(handle->width * 3, handle->aw_byte_align[0]);
		/* display use info->fb.crop.x and x is pixel ,
		  * so if rotate, but the blank is the begin,
		  * but not pixel's allign,
		  */
		if (stride % 3)
			return false;
		break;
	default:
		return false;
	}
	return true;
}

bool supportTR(Display_t *display, Layer_t *layer)
{
	if (trfd < 0)
		return false;
	if (tr_disp->trErrCnt > 3) {
		ALOGV("display:%d rotate has 3 contig err", display->displayId);
		return false;
	}
	if (!trHarewareRistrict(layer))
		return false;

	return dequeueTrCache(display, layer);
}

bool layerToTrinfo(Layer_t *layer, g2d_blt_h *tr_info, tr_cache_t *bCache)
{
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;

	memset(tr_info, 0, sizeof(g2d_blt_h));
	tr_info->src_image_h.use_phy_addr = 0;
	tr_info->dst_image_h.use_phy_addr = 0;
	tr_info->src_image_h.format = halToTRFormat(handle->format);
	unsigned int w_stride, h_stride;
	tr_info->flag_h = toTrMode(layer->transform);

	int bpp[3] = {4, 2, 1};
	switch(handle->format) {
	case HAL_PIXEL_FORMAT_YV12:
		bpp[1] = 1;
		bpp[0] = 1;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
	case HAL_PIXEL_FORMAT_AW_NV12:
		bpp[1] = 2;
		bpp[0] = 1;
		break;
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
		bpp[0] = 4;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		bpp[0] = 3;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		bpp[0] = 2;
		break;
	default:
		ALOGV("RGB");
	}

	/* rotate is 8byte cal */
	tr_info->src_image_h.width = HWC_ALIGN(handle->width * bpp[0], handle->aw_byte_align[0]) / bpp[0];
	tr_info->src_image_h.height = handle->height;

	tr_info->src_image_h.fd = handle->share_fd;
	tr_info->src_image_h.clip_rect.x = 0;
	tr_info->src_image_h.clip_rect.y = 0;
	tr_info->src_image_h.clip_rect.w = tr_info->src_image_h.width;
	tr_info->src_image_h.clip_rect.h = handle->height;
	tr_info->src_image_h.align[0] = handle->aw_byte_align[0];
	tr_info->src_image_h.align[1] = handle->aw_byte_align[1];
	tr_info->src_image_h.align[2] = handle->aw_byte_align[2];


	/* yuv only support yuv420 output*/
	if (tr_info->src_image_h.format == G2D_FORMAT_YUV422UVC_V1U1V0U0)
		tr_info->dst_image_h.format = G2D_FORMAT_YUV420UVC_V1U1V0U0;
	else if (tr_info->src_image_h.format == G2D_FORMAT_YUV422UVC_U1V1U0V0)
		tr_info->dst_image_h.format = G2D_FORMAT_YUV420UVC_U1V1U0V0;
	else if (tr_info->src_image_h.format == G2D_FORMAT_YUV422_PLANAR)
		tr_info->dst_image_h.format = G2D_FORMAT_YUV420_PLANAR;
	else
		tr_info->dst_image_h.format = tr_info->src_image_h.format;

	tr_info->dst_image_h.clip_rect.x = 0;
	tr_info->dst_image_h.clip_rect.y = 0;

	//here
	if (layer->transform & HAL_TRANSFORM_ROT_90) {
		w_stride = handle->height;
		h_stride = handle->width;
	} else {
		w_stride = handle->width;
		h_stride = handle->height;
	}

	w_stride = HWC_ALIGN(w_stride * bpp[0], handle->aw_byte_align[0]) / bpp[0];
	h_stride = HWC_ALIGN(h_stride * bpp[0], handle->aw_byte_align[0]) / bpp[0];

	tr_info->dst_image_h.width = w_stride;
	tr_info->dst_image_h.height = h_stride;

	tr_info->dst_image_h.clip_rect.w = w_stride;
	tr_info->dst_image_h.clip_rect.h = h_stride;
	tr_info->dst_image_h.align[0] = handle->aw_byte_align[0];
	tr_info->dst_image_h.align[1] = handle->aw_byte_align[1];
	tr_info->dst_image_h.align[2] = handle->aw_byte_align[2];
	/*YUV format we only support yuv420 */

	tr_info->dst_image_h.fd = bCache->share_fd;

	return 1;
}

int trAfterDeal(Layer_t *layer, tr_cache_t *bCache, g2d_blt_h *trInfo)
{
	private_handle_t *handle;
	handle = (private_handle_t *)layer->buffer;
	close(handle->share_fd);
	handle->share_fd = dup(bCache->share_fd);
/*	handle->aw_byte_align[0] = ROTATE_ALIGN;*/
/*	handle->aw_byte_align[1] = ROTATE_ALIGN / 2;*/
/*	handle->aw_byte_align[2] = ROTATE_ALIGN / 2;*/

	handle->format = trFormatToHal(trInfo->dst_image_h.format);
	return 0;
}

int submitTransformLayer(rotate_info_t *rt_info, unsigned int syncCount)
{
	Layer_t *layer;
	private_handle_t *handle;

	layer = rt_info->layer2;
	handle = (private_handle_t *)layer->buffer;
	tr_cache_t *bCache = NULL;
	g2d_blt_h trInfo;
	int timeout = 0;
	bool bad_frame = 1;
	/* if 2 screen use the same tr layer, we must reduce this case
	*/
	/* maybe crach for aCache== NULL, but amost impossible ,so no care*/
	bCache = dequeueTrBuffer(layer, rt_info);
	if (!layerToTrinfo(layer, &trInfo, bCache)) {
		goto Last;
	}
	timeout = culateTimeout(layer);
		tr_disp->timeout = timeout;

	//ALOGD("before  rotate:%u %d x %d ",syncCount,handle->width, handle->height);
	if (hwc_rotate_commit(&trInfo) < 0) {
		ALOGE("[G2D] rot err!");
		bCache->valid = 0;
		tr_disp->trErrCnt++;
		goto Last;
	}

	bCache->valid = 1;
	bad_frame = 0;
	bCache->sync_cnt = syncCount;
	bCache->releasefd = dup(rt_info->dst_waite_fence);
	tr_disp->trErrCnt = 0;;
Last:
	if (bad_frame)
		bCache = acquireLastValid(layer);
	if (!bCache) {
		tr_disp->trErrCnt = 4;
		/* here will screen display err */
		return -1;
	}
	trAfterDeal(layer, bCache, &trInfo);
	return 0;
}

int get_rotate_fence_fd(Layer_t *layer2,
	Display_t *disp, int releasefence, unsigned int syncCount)
{
	char name[20];
	int count;
	int fence_fd = -1;
	rotate_info_t *tr_info;

	tr_info = trlistCacheGet();
	if (tr_info == NULL)
		return -1;

	count = sprintf(name, "tr_fence_%u\n", sw_sync_count);
	name[count+1] ='0';
	tr_info->syncCount = ++sw_sync_count;

	fence_fd = sw_sync_fence_create(sw_sync_fd, name, tr_info->syncCount);
	tr_info->waite_fence = layer2->acquireFence;
	layer2->acquireFence = dup(fence_fd);
	tr_info->layer2 = layer2;
	tr_info->dst_waite_fence = dup(releasefence);
	tr_info->gsyncCount = syncCount;
	tr_info->disp = disp;

	incRef(layer2);
	if (inc_count + 2 < tr_info->syncCount)
		ALOGV("hw rotate so slowly:%u   %u",
			tr_info->syncCount, inc_count);

	rotate_mutex->lock();
	list_add_tail(&SubmitHead, &tr_info->node);
	rotate_cond->signal();
	rotate_mutex->unlock();

	return fence_fd;
}

void deal_rotate_fence(rotate_info_t *tr_info)
{
	inc_count++;
	if (inc_count != tr_info->syncCount)
		ALOGD("some wrong about rotate fence:%d %d...", inc_count, tr_info->syncCount);
	sw_sync_timeline_inc(sw_sync_fd, 1);
}

void submitMemAlloc(tr_cache_Array *cache)
{
	alloc_mutex->lock();
	list_add_tail(&mem_alloc_list, &cache->node);
	alloc_cond->signal();
	alloc_mutex->unlock();
}

void *rotateMemLoop(void *list)
{
	struct listnode allocHead1, *allocHead, *node, *node2, *alloc_list;
	allocHead = &allocHead1;
	list_init(allocHead);
	alloc_list = (struct listnode *)list;
	tr_cache_Array *aCache;

	while (1) {
		alloc_mutex->lock();
		if (list_empty(alloc_list))
			alloc_cond->wait(*alloc_mutex);

		allocHead->next = alloc_list->next;
		alloc_list->next->prev = allocHead;
		allocHead->prev = alloc_list->prev;
		alloc_list->prev->next = allocHead;
		list_init(alloc_list);
		alloc_mutex->unlock();

		int i;
		list_for_each_safe(node, node2, allocHead) {
			aCache = node_to_item(node, tr_cache_Array, node);
			for (i = 0; i < NOMORL_CACHE_N; i++) {
				aCache->array[i].share_fd = ionAllocBuffer(aCache->size, mustconfig, aCache->secure);
			}
			aCache->complete = 1;
			list_remove(&aCache->node);
			trCachePut(aCache, 0);
		}
	}
}

void* rotateThreadLoop(void *list)
{
	struct listnode comHead, *commitHead, *node, *node2, *rotate_list;
	rotate_info_t *tr_info;
	rotate_list = (struct listnode*)list;

	commitHead = &comHead;
	list_init(commitHead);
	ALOGD("new a rotate thread to commit the display");
	setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

	while(1) {
		rotate_mutex->lock();
		if (list_empty(rotate_list))
			rotate_cond->wait(*rotate_mutex);

		commitHead->next = rotate_list->next;
		rotate_list->next->prev = commitHead;
		commitHead->prev = rotate_list->prev;
		rotate_list->prev->next = commitHead;
		list_init(rotate_list);
		rotate_mutex->unlock();

		list_for_each_safe(node, node2, commitHead) {
			tr_info = node_to_item(node, rotate_info_t, node);
			if (tr_info->waite_fence >= 0) {
					if (sync_wait((int)tr_info->waite_fence, 3000)) {
						ALOGE("rotateThreadLoop waite aquire fence err %d", tr_info->waite_fence);
						/* dump fence */
					}
			}
			submitTransformLayer(tr_info, tr_info->gsyncCount);
			deal_rotate_fence(tr_info);
			layerCachePut(tr_info->layer2);
			trlistCachePut(tr_info);
		}
		list_init(commitHead);
	}

}

bool supportedRotateWithFlip()
{
    // capability of rotate with flip
    return true;
}

