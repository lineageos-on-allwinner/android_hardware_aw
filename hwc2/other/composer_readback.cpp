
#include <unistd.h>
#include <cutils/log.h>
#include "composer_readback.h"

enum {
    READBACK_IDLE = 1,
    READBACK_PENDING,
    READBACK_COMMITTED,
    READBACK_ERROR,
};

struct ReadbackInfo {
    hwc2_display_t targetDisplay;
    buffer_handle_t buffer;
    int32_t releaseFence;
    int32_t readbackFence;

    // fake layer to satisfy writeback api
    Layer_t* wblayer;

    int state;
};

static ReadbackInfo _readbackInfo;

extern int initWriteBack(Display_t* dp, bool allocBuffer);
extern int writebackOneFrame(Display_t* dp, Layer_t* layer, struct sync_info *sync);

int initReadback(void)
{
    initWriteBack(/* Display_t* */NULL, false);
    ALOGD("%s: finish", __FUNCTION__);

    _readbackInfo.state = READBACK_IDLE;
    _readbackInfo.buffer = 0;
    _readbackInfo.targetDisplay = -1;
    _readbackInfo.readbackFence = -1;
    _readbackInfo.releaseFence  = -1;
    return 0;
}

void resetReadback(void)
{
    ALOGD("resetReadback");
    _readbackInfo.state = READBACK_IDLE;
    _readbackInfo.buffer = 0;
    _readbackInfo.targetDisplay = -1;

    if (_readbackInfo.readbackFence >= 0) {
        close(_readbackInfo.readbackFence);
        _readbackInfo.readbackFence = -1;
    }
    if (_readbackInfo.releaseFence >= 0) {
        close(_readbackInfo.releaseFence);
        _readbackInfo.releaseFence = -1;
    }
}

static Layer_t* allocHwcLayer(Display_t* hwdevice)
{
    DisplayOpr_t* opt = NULL;
    Layer_t* layer = NULL;
    if (hwdevice == NULL) {
        return NULL;
    }
    opt = hwdevice->displayOpration;
    if (opt == NULL) {
        return NULL;
    }
    layer = opt->createLayer(hwdevice);
    if (layer == NULL) {
        return NULL;
    }
    private_handle_t* handle = (private_handle_t *)hwc_malloc(sizeof(private_handle_t));
    if (handle == NULL) {
        return NULL;
    }
    handle->share_fd = -1;
    layer->buffer = handle;

    layer->releaseFence = -1;
    layer->acquireFence = -1;
    return layer;
}

static void setupWritebackTarget(Layer_t* wblayer)
{
    private_handle_t *rbBufferHandle = (private_handle_t *)_readbackInfo.buffer;
    private_handle_t *wbBufferHandle = (private_handle_t *)wblayer->buffer;

    if (wbBufferHandle->share_fd >= 0) {
        close(wbBufferHandle->share_fd);
        wbBufferHandle->share_fd = -1;
    }

    wbBufferHandle->share_fd = dup(rbBufferHandle->share_fd);
    wblayer->releaseFence = dup(_readbackInfo.releaseFence);

    ALOGD("Readback1: format %d width %d height %d stride %d align %d %d %d",
            rbBufferHandle->format,
            rbBufferHandle->width, rbBufferHandle->height, rbBufferHandle->stride,
            rbBufferHandle->aw_byte_align[0], rbBufferHandle->aw_byte_align[1], rbBufferHandle->aw_byte_align[2]);
}

int doReadback(Display_t* hwdevice, struct sync_info *sync)
{
    // No valid readback buffer, or no readback task
    if (!_readbackInfo.buffer
            || _readbackInfo.state != READBACK_PENDING) {
        return 0;
    }

    if (!_readbackInfo.wblayer)
        _readbackInfo.wblayer = allocHwcLayer(hwdevice);

    setupWritebackTarget(_readbackInfo.wblayer);
    int error = writebackOneFrame(hwdevice, _readbackInfo.wblayer, sync);
    _readbackInfo.state = (!error) ? READBACK_COMMITTED : READBACK_ERROR;

    if (_readbackInfo.readbackFence >= 0) {
        close(_readbackInfo.readbackFence);
        _readbackInfo.readbackFence = -1;
    }
    _readbackInfo.readbackFence = _readbackInfo.wblayer->acquireFence;
    _readbackInfo.wblayer->acquireFence = -1;

    // debug
    private_handle_t *wbBufferHandle = (private_handle_t *)_readbackInfo.wblayer->buffer;
    ALOGD("Readback2: format %d width %d height %d stride %d align %d %d %d",
            wbBufferHandle->format,
            wbBufferHandle->width, wbBufferHandle->height, wbBufferHandle->stride,
            wbBufferHandle->aw_byte_align[0], wbBufferHandle->aw_byte_align[1], wbBufferHandle->aw_byte_align[2]);

    return error;
}

void setReadbackBuffer(hwc2_display_t display,
        buffer_handle_t buffer, int32_t releaseFence)
{
    _readbackInfo.targetDisplay = display;
    _readbackInfo.buffer = buffer;

    if (_readbackInfo.releaseFence >= 0) {
        close(_readbackInfo.releaseFence);
    }
    _readbackInfo.releaseFence = releaseFence;
    _readbackInfo.state = READBACK_PENDING;
}

int32_t getReadbackBufferFence(hwc2_display_t display, int32_t* fence)
{
    if (_readbackInfo.targetDisplay != display) {
        ALOGE("%s: readback display not match", __FUNCTION__);
        return -1;
    }
    if (_readbackInfo.state != READBACK_COMMITTED) {
        ALOGE("%s: readback not committed yet", __FUNCTION__);
        return -1;
    }

    _readbackInfo.state = READBACK_IDLE;
    *fence = -1;
    if (_readbackInfo.readbackFence >= 0) {
        *fence = dup(_readbackInfo.readbackFence);
        close(_readbackInfo.readbackFence);
        _readbackInfo.readbackFence = -1;
    }
    return 0;
}

