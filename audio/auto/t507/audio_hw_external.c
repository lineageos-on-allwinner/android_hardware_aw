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

#define LOG_TAG "audio_hw_external"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>
#include <fcntl.h>
#include <noise_reduction.h>

#include "audio_hw.h"

struct pcm_config pcm_config_mm_out = {
    .channels = 2,
    .rate = MM_SAMPLING_RATE,
    .period_size = SHORT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static int do_output_standby(struct sunxi_stream_out *out);

static int get_card(const char *card_name)
{
    int ret;
    int fd;
    int i;
    char path[128];
    char name[64];

    for (i = 0; i < MAX_AUDIO_DEVICES; i++) {
        sprintf(path, "/sys/class/sound/card%d/id", i);
        ret = access(path, F_OK);
        if (ret) {
            ALOGW("can't find node %s", path);
            return -1;
        }

        fd = open(path, O_RDONLY);
        if (fd <= 0) {
            ALOGE("can't open %s", path);
            return -1;
        }

        ret = read(fd, name, sizeof(name));
        close(fd);
        if (ret > 0) {
            name[ret-1] = '\0';
            if (!strcmp(name, card_name))
                return i;
        }
    }

    ALOGW("can't find card:%s", card_name);
    return -1;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct sunxi_stream_out *out)
{
    int ret = 0;
    struct sunxi_audio_device *adev = out->dev;
    int card = adev->card_id;
    unsigned int port = 0;

    adev->active_output = out;

    out->config.rate = MM_SAMPLING_RATE;
    out->write_threshold = PLAYBACK_PERIOD_COUNT * SHORT_PERIOD_SIZE;
    out->config.start_threshold = SHORT_PERIOD_SIZE * 2;
    out->config.avail_min = SHORT_PERIOD_SIZE;

    if(card >= 0) {
    {      //only support i2s
            ALOGV("use %s to playback audio", EXTERNAL_OUTPUT_DEVICE);

            out->pcm = pcm_open(card, port, PCM_OUT, &out->config);
            if (!pcm_is_ready(out->pcm)) {
                ALOGE("cannot open pcm driver: %s", pcm_get_error(out->pcm));
                pcm_close(out->pcm);
                out->pcm = NULL;
                adev->active_output = NULL;
                return -ENOMEM;
            }

            if (DEFAULT_OUT_SAMPLING_RATE != out->config.rate) {
                ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
                                        out->config.rate,
                                        2,
                                        RESAMPLER_QUALITY_DEFAULT,
                                        NULL,
                                        &out->resampler);
                if (ret != 0) {
                    ALOGE("create out resampler failed, %d -> %d", DEFAULT_OUT_SAMPLING_RATE, out->config.rate);
                    return ret;
                }

                ALOGV("create out resampler OK, %d -> %d", DEFAULT_OUT_SAMPLING_RATE, out->config.rate);
            } else
                ALOGV("do not use out resampler");

            if (out->resampler) {
                out->resampler->reset(out->resampler);
            }
        }
    }
    // match end

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    return DEFAULT_OUT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, rate = %d", __FUNCTION__, rate);
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = (SHORT_PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size((struct audio_stream_out *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, format = %d", __FUNCTION__, format);
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct sunxi_stream_out *out)
{
    struct sunxi_audio_device *adev = out->dev;
    int card = adev->card_id;
    if (!out->standby) {
        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }

        if (out->resampler) {
            release_resampler(out->resampler);
            out->resampler = NULL;
        }

       if(card >= 0) {
            if (out->pcm) {
                pcm_close(out->pcm);
                out->pcm = NULL;
            }

            if (out->resampler) {
                release_resampler(out->resampler);
                out->resampler = NULL;
            }
        }

        adev->active_output = 0;
        out->standby = 1;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    int status;

    ALOGD("out_standby");
    //pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    //pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, fd = %d", __FUNCTION__, fd);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("out_set_parameters: %s", kvpairs);
    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, keys = %s", __FUNCTION__, keys);
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;

    return (SHORT_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, left = %f, right = %f", __FUNCTION__, left, right);
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    struct sunxi_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    void *buf;
    int card = adev->card_id;

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);

    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;

    }
    pthread_mutex_unlock(&adev->lock);

    if (out->pcm)      //only support i2s
    {
        out->config.avail_min = SHORT_PERIOD_SIZE;
        pcm_set_avail_min(out->pcm, out->config.avail_min);

        if (out->resampler) {
            out->resampler->resample_from_input(out->resampler,
                                                            (int16_t *)buffer,
                                                            &in_frames,
                                                            (int16_t *)out->buffer,
                                                            &out_frames);
            buf = out->buffer;
        } else {
            out_frames = in_frames;
            buf = (void *)buffer;
        }

        if (out->config.channels == 2) {
            ret = pcm_write(out->pcm, (void *)buf, out_frames * frame_size);
        } else {
            size_t i;
            char *pcm_buf = (char *)buf;
            for (i = 0; i < out_frames; i++) {
                pcm_buf[2 * i + 2] = pcm_buf[4 * i + 4];
                pcm_buf[2 * i + 3] = pcm_buf[4 * i + 5];
            }
            ret = pcm_write(out->pcm, (void *)buf, out_frames * frame_size / 2);
        }

        if (ret!=0) {
            ALOGE("##############out_write()  Warning:write fail, reopen it ret = %d #######################", ret);
            do_output_standby(out);
            usleep(30000);
        }
    }
exit:
    pthread_mutex_unlock(&out->lock);
    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, dsp_frames = %u", __FUNCTION__, dsp_frames);
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    ALOGV("fun = %s, timestamp = %ld", __FUNCTION__, timestamp);
    return -EINVAL;
}

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct sunxi_stream_in *in)
{
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct sunxi_stream_in *out = (struct sunxi_stream_in *)stream;
    ALOGV("fun = %s, rate = %d", __FUNCTION__, rate);
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    return 0;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;

    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    ALOGV("fun = %s, format = %d", __FUNCTION__, format);
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    ALOGV("fun = %s, fd = %d", __FUNCTION__, fd);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    ALOGV("in_set_parameters: %s", kvpairs);

    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    ALOGV("in_get_parameters: %s", keys);
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    ALOGV("fun = %s, gain = %f", __FUNCTION__, gain);
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    return 0;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct sunxi_audio_device *ladev = (struct sunxi_audio_device *)dev;
    struct sunxi_stream_out *out;
    int ret;

    ALOGV("adev_open_output_stream, flags: %x", flags);

    out = (struct sunxi_stream_out *)calloc(1, sizeof(struct sunxi_stream_out));
    if (!out)
        return -ENOMEM;

    out->buffer = malloc(RESAMPLER_BUFFER_SIZE); /* todo: allow for reallocing */

    out->stream.common.get_sample_rate  = out_get_sample_rate;
    out->stream.common.set_sample_rate  = out_set_sample_rate;
    out->stream.common.get_buffer_size  = out_get_buffer_size;
    out->stream.common.get_channels     = out_get_channels;
    out->stream.common.get_format       = out_get_format;
    out->stream.common.set_format       = out_set_format;
    out->stream.common.standby          = out_standby;
    out->stream.common.dump             = out_dump;
    out->stream.common.set_parameters   = out_set_parameters;
    out->stream.common.get_parameters   = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency             = out_get_latency;
    out->stream.set_volume              = out_set_volume;
    out->stream.write                   = out_write;
    out->stream.get_render_position     = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->config                         = pcm_config_mm_out;
    out->dev                            = ladev;
    out->standby                        = 1;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
	 * adev->out_device = out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */
    config->format                = out_get_format(&out->stream.common);
    config->channel_mask          = out_get_channels(&out->stream.common);
    config->sample_rate           = out_get_sample_rate(&out->stream.common);

    ALOGV("+++++++++++++++ adev_open_output_stream: req_sample_rate: %d, fmt: %x, channel_count: %d",
        config->sample_rate, config->format, config->channel_mask);

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    struct sunxi_audio_device *adev = out->dev;
    int card = adev->card_id;

    if (adev->mode == AUDIO_MODE_IN_CALL || adev->mode == AUDIO_MODE_MODE_FACTORY_TEST  || adev->mode == AUDIO_MODE_FM) {
        ALOGW("mode in call, do not adev_close_output_stream");
        return ;
    }
	
    out_standby(&stream->common);

    if (out->buffer)
        free(out->buffer);
    if (out->resampler)
        release_resampler(out->resampler);

    if(card >= 0) {
        if (out->resampler)
            release_resampler(out->resampler);
    }
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("adev_set_parameters kvpairs = %s", kvpairs);
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("fun = %s, keys = %s", __FUNCTION__, keys);
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    ALOGV("fun = %s, volume = %f", __FUNCTION__, volume);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    F_LOG;
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("fun = %s, volume = %f", __FUNCTION__, volume);
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("fun = %s, volume = %f", __FUNCTION__, volume);
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("fun = %s, mode = %d", __FUNCTION__, mode);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct sunxi_audio_device *ladev = (struct sunxi_audio_device *)dev;
    struct sunxi_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);

    *stream_in = NULL;

    in = (struct sunxi_stream_in *)calloc(1, sizeof(struct sunxi_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate   = in_get_sample_rate;
    in->stream.common.set_sample_rate   = in_set_sample_rate;
    in->stream.common.get_buffer_size   = in_get_buffer_size;
    in->stream.common.get_channels      = in_get_channels;
    in->stream.common.get_format        = in_get_format;
    in->stream.common.set_format        = in_set_format;
    in->stream.common.standby           = in_standby;
    in->stream.common.dump              = in_dump;
    in->stream.common.set_parameters    = in_set_parameters;
    in->stream.common.get_parameters    = in_get_parameters;
    in->stream.common.add_audio_effect  = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read 	= in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->requested_rate 	= config->sample_rate;
    in->config.channels = channel_count;

    ALOGV("to malloc in-buffer: period_size: %d, frame_size: %d",
        in->config.period_size, audio_stream_in_frame_size(&in->stream));
    in->buffer = malloc(in->config.period_size *
                        audio_stream_in_frame_size(&in->stream) * 8);

    if (!in->buffer) {
        ret = -ENOMEM;
        goto err;
    }

    in->dev     = ladev;
    in->standby = 1;
    in->device 	= devices & ~AUDIO_DEVICE_BIT_IN;

    *stream_in 	= &in->stream;
    return 0;

err:
    if (in->resampler)
        release_resampler(in->resampler);

    free(in);
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct sunxi_stream_in *in          = (struct sunxi_stream_in *)stream;
    struct sunxi_audio_device *ladev    = (struct sunxi_audio_device *)dev;

    in_standby(&stream->common);

    if (in->buffer) {
        free(in->buffer);
        in->buffer = 0;
    }

    if (in->resampler) {
        release_resampler(in->resampler);
    }

    free(stream);

    ALOGD("adev_close_input_stream set voice record status");
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    struct sunxi_audio_device *adev    = (struct sunxi_audio_device *)device;
    ALOGV("fun = %s, fd = %d", __FUNCTION__, fd);

    return 0;
}

static int adev_close(hw_device_t *device)
{
    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct sunxi_audio_device *adev;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct sunxi_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag              = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version          = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module           = (struct hw_module_t *) module;
    adev->hw_device.common.close            = adev_close;

    adev->hw_device.init_check              = adev_init_check;
    adev->hw_device.set_voice_volume        = adev_set_voice_volume;
    adev->hw_device.set_master_volume       = adev_set_master_volume;
    adev->hw_device.get_master_volume       = adev_get_master_volume;
    adev->hw_device.set_mode                = adev_set_mode;
    adev->hw_device.set_mic_mute            = adev_set_mic_mute;
    adev->hw_device.get_mic_mute            = adev_get_mic_mute;
    adev->hw_device.set_parameters          = adev_set_parameters;
    adev->hw_device.get_parameters          = adev_get_parameters;
    adev->hw_device.get_input_buffer_size   = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream      = adev_open_output_stream;
    adev->hw_device.close_output_stream     = adev_close_output_stream;
    adev->hw_device.open_input_stream       = adev_open_input_stream;
    adev->hw_device.close_input_stream      = adev_close_input_stream;
    adev->hw_device.dump                    = adev_dump;

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);

    adev->mode       = AUDIO_MODE_NORMAL;

    adev->card_id = get_card(EXTERNAL_OUTPUT_DEVICE);
    if(adev->card_id < 0)
    {
        ALOGE("i2s audio device is not exist!");
        pthread_mutex_unlock(&adev->lock);
        goto error_out;
    }

    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

    ALOGD("adev_open success ,LINE:%d,FUNC:%s",__LINE__,__FUNCTION__);
    return 0;

error_out:
    free(adev);
    return -EINVAL;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag                = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = AUDIO_HARDWARE_MODULE_ID,
        .name               = "sunxi audio HW HAL",
        .author             = "author",
        .methods            = &hal_module_methods,
    },
};
