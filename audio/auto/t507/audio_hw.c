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

#define LOG_TAG "audio_hw_primary"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <audio_route/audio_route.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>
#include <fcntl.h>
#include <unistd.h>

#include <cutils/properties.h>

#include "audio_hw.h"

#define UNUSED(x) (void)(x)

#define AUDIO_CODEC_XML_PATH "/vendor/etc/auto_codec_paths.xml"
#define AUDIO_AHUB_XML_PATH "/vendor/etc/auto_ahub_paths.xml"

/*normal path*/
#define  codec_lineout_vol      "LINEOUT volume"
#define  codec_linein_omix_vol      "LINEIN to output mixer gain control"
#define  codec_fmin_omix_vol      "FMIN to output mixer gain control"
#define  ac107_c1_pga_vol      "Channel 1 PGA Gain"
#define  ac107_c2_pga_vol      "Channel 2 PGA Gain"
#define  ac107_c1_digital_vol      "Channel 1 Digital Volume"
#define  ac107_c2_digital_vol      "Channel 2 Digital Volume"

#define	media_out_lineout   "codec-lineout"   /* OUT_DEVICE_LINEOUT */
#define	media_out_hdmi   "ahub-daudio1-output"/* OUT_DEVICE_HDMI */
#define	media_out_i2s2     "ahub-daudio2-output"/* OUT_DEVICE_I2S2 */
#define	media_out_i2s3  "ahub-daudio3-output"/* OUT_DEVICE_I2S3 */

#define	media_in_ac107    "ahub-daudio0-input"/* IN_DEVICE_AC107 */
#define	media_in_i2s2    "ahub-daudio2-input"/* IN_DEVICE_I2S2 */
#define	media_in_i2s3    "ahub-daudio3-input"/* IN_DEVICE_I2S3 */

#define	media_linein_lineout    "codec-linein-lineout"/* AA_LINEIN_LINEOUT */
#define	media_fmin_lineout    "codec-fmin-lineout"/* AA_FMIN_LINEOUT */

enum {
    OUT_DEVICE_LINEOUT,
    OUT_DEVICE_HDMI,
    OUT_DEVICE_I2S2,
    OUT_DEVICE_I2S3,

    IN_DEVICE_AC107,
    IN_DEVICE_I2S2,
    IN_DEVICE_I2S3,

    AA_LINEIN_LINEOUT,
    AA_FMIN_LINEOUT,

    DEVICE_TAB_SIZE,
};


const char * const normal_route_configs[DEVICE_TAB_SIZE] = {
    media_out_lineout,
    media_out_hdmi,
    media_out_i2s2,
    media_out_i2s3,

    media_in_ac107,
    media_in_i2s2,
    media_in_i2s3,

    media_linein_lineout,
    media_fmin_lineout,
};

struct pcm_config pcm_config_out = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_SAMPLING_RATE,
    .period_size = DEFAULT_OUTPUT_PERIOD_SIZE,
    .period_count = DEFAULT_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_in = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_SAMPLING_RATE,
    .period_size = DEFAULT_INPUT_PERIOD_SIZE,
    .period_count = DEFAULT_INPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

typedef struct name_map_t {
    char name_linux[32];
    char name_android[32];
}name_map;

static name_map audio_name_map[MAX_AUDIO_DEVICES] = {
    {"audiocodec",      AUDIO_NAME_CODEC},//inside codec
    {"snddaudio2",       AUDIO_NAME_I2S2},//daudio2
    {"snddaudio3",       AUDIO_NAME_I2S3},//daudio3
    {"sndahub",        AUDIO_NAME_AHUB},//ahub
    {"sndac10730036",        AUDIO_NAME_AC107},//ac107
    {"sndhdmi",         AUDIO_NAME_HDMI},
    {"sndspdif",        AUDIO_NAME_SPDIF},
};

static void set_audio_path(struct sunxi_audio_device *adev, int device_path);
static int do_input_standby(struct sunxi_stream_in *in);
static int do_output_standby(struct sunxi_stream_out *out);
static int set_audio_devices_active(struct sunxi_audio_device *adev, int in_out);

static int set_mixer_value(struct mixer *mixer, const char *name, int value)
{
    struct mixer_ctl *ctl = NULL;
    ctl = mixer_get_ctl_by_name(mixer, name);
    if (ctl == NULL) {
        ALOGE("Control '%s' doesn't exist ", name);
        return -1;
    }
    return mixer_ctl_set_value(ctl, 0, value);
}

static int find_name_map(struct sunxi_audio_device *adev, char * in, char * out)
{
    UNUSED(adev);

    int index = 0;

    if (in == 0 || out == 0) {
        ALOGE("error params");
        return -1;
    }

    for (; index < MAX_AUDIO_DEVICES; index++) {
        if (strlen(audio_name_map[index].name_linux) == 0) {

            //sprintf(out, "AUDIO_USB%d", adev->usb_audio_cnt++);
            sprintf(out, "AUDIO_USB_%s", in);
            strcpy(audio_name_map[index].name_linux, in);
            strcpy(audio_name_map[index].name_android, out);
            ALOGD("linux name = %s, android name = %s",
                audio_name_map[index].name_linux,
                audio_name_map[index].name_android);
            return 0;
        }

        if (!strcmp(in, audio_name_map[index].name_linux)) {
            strcpy(out, audio_name_map[index].name_android);
            ALOGD("linux name = %s, android name = %s",
                audio_name_map[index].name_linux,
                audio_name_map[index].name_android);
            return 0;
        }
    }

    return 0;
}

static int do_init_audio_card(struct sunxi_audio_device *adev, int card)
{
    int ret = -1;
    int fd = 0;
    char * snd_path = "/sys/class/sound";
    char snd_card[128], snd_node[128];
    char snd_id[32], snd_name[32];

    memset(snd_card, 0, sizeof(snd_card));
    memset(snd_node, 0, sizeof(snd_node));
    memset(snd_id, 0, sizeof(snd_id));
    memset(snd_name, 0, sizeof(snd_name));

    sprintf(snd_card, "%s/card%d", snd_path, card);
    ret = access(snd_card, F_OK);
    if(ret == 0) {
        // id / name
        sprintf(snd_node, "%s/card%d/id", snd_path, card);
        ALOGD("read card %s/card%d/id",snd_path, card);

        fd = open(snd_node, O_RDONLY);
        if (fd > 0) {
            ret = read(fd, snd_id, sizeof(snd_id));
            if (ret > 0) {
                snd_id[ret - 1] = 0;
                ALOGD("%s, %s, len: %d", snd_node, snd_id, ret);
            }
            close(fd);
        } else {
            return -1;
        }

        strcpy(adev->dev_manager[card].card_id, snd_id);
        find_name_map(adev, snd_id, snd_name);
        strcpy(adev->dev_manager[card].name, snd_name);
        ALOGD("find name map, card_id = %s, card_name = %s ",adev->dev_manager[card].card_id,adev->dev_manager[card].name);

        adev->dev_manager[card].card = card;
        adev->dev_manager[card].port = 0;
        adev->dev_manager[card].flag_exist = true;
        adev->dev_manager[card].flag_out = AUDIO_NONE;
        adev->dev_manager[card].flag_in = AUDIO_NONE;
        adev->dev_manager[card].flag_out_active = 0;
        adev->dev_manager[card].flag_in_active = 0;

        if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_CODEC)){
            adev->dev_manager[card].port = 0;
            adev->dev_manager[card].ahub_device = 0;
            adev->card_codec = card;
            adev->ar_codec = audio_route_init(adev->card_codec, AUDIO_CODEC_XML_PATH);
        }else if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_AHUB)){
            adev->card_ahub = card;
            adev->ar_ahub = audio_route_init(adev->card_ahub, AUDIO_AHUB_XML_PATH);
            return 0;
        }else if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_AC107)){
            adev->dev_manager[card].port = PORT_AC107;
            adev->dev_manager[card].ahub_device = 1;
            adev->card_ac107 = card;
            adev->mixer_ac107 = mixer_open(adev->card_ac107);
        }else if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_HDMI)){
            adev->dev_manager[card].port = PORT_HDMI;
            adev->dev_manager[card].ahub_device = 1;
        }else if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_I2S2)){
            adev->dev_manager[card].port = PORT_I2S2;
            adev->dev_manager[card].ahub_device = 1;
        }else if(!strcmp(adev->dev_manager[card].name,  AUDIO_NAME_I2S3)){
            adev->dev_manager[card].port = PORT_I2S3;
            adev->dev_manager[card].ahub_device = 1;
        }else{
            adev->dev_manager[card].ahub_device = 0;
            adev->dev_manager[card].port = 0;
        }

        if(strcmp(adev->dev_manager[card].name,  AUDIO_NAME_AC107)){
            // playback device
            sprintf(snd_node, "%s/card%d/pcmC%dD0p", snd_path, card, card);
            ret = access(snd_node, F_OK);
            if(ret == 0) {
                // there is a playback device
                adev->dev_manager[card].flag_out = AUDIO_OUT;
                adev->dev_manager[card].flag_out_active = 0;
            }
        }
        if(strcmp(adev->dev_manager[card].name,  AUDIO_NAME_HDMI)
			|| strcmp(adev->dev_manager[card].name,  AUDIO_NAME_CODEC)){
            // capture device
            sprintf(snd_node, "%s/card%d/pcmC%dD0c", snd_path, card, card);
            ret = access(snd_node, F_OK);
            if(ret == 0) {
                // there is a capture device
                adev->dev_manager[card].flag_in = AUDIO_IN;
                adev->dev_manager[card].flag_in_active = 0;
            }
        }
    } else {
        return -1;
    }

    return 0;
}

static void init_audio_devices(struct sunxi_audio_device *adev)
{
    int card = 0;
    F_LOG;

    memset(adev->dev_manager, 0, sizeof(adev->dev_manager));

    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        if (do_init_audio_card(adev, card) == 0) {
            // break;
            ALOGV("card: %d, name: %s, capture: %d, playback: %d",
                card, adev->dev_manager[card].name,
                adev->dev_manager[card].flag_in == AUDIO_IN,
                adev->dev_manager[card].flag_out == AUDIO_OUT);
        }
    }

    if(adev->ar_codec){
        audio_route_reset(adev->ar_codec);
    }
    if(adev->ar_ahub){
        audio_route_reset(adev->ar_ahub);
    }
}

static void init_audio_devices_active(struct sunxi_audio_device *adev)
{
    int card = 0;
    int flag_active = 0;

    F_LOG;

    if (set_audio_devices_active(adev, AUDIO_IN) == 0) {
        flag_active |= AUDIO_IN;
    }

    if (set_audio_devices_active(adev, AUDIO_OUT) == 0) {
        flag_active |= AUDIO_OUT;
    }

    if ((flag_active & AUDIO_IN)
        && (flag_active & AUDIO_OUT)){
        return;
    }

    ALOGV("midle priority, use codec & ac107");
    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        // default use auido codec out, ac107 in.
        if ((!strcmp(adev->dev_manager[card].name, AUDIO_NAME_AC107)) &&
			(adev->dev_manager[card].flag_in == AUDIO_IN)) {
                ALOGV("OK, default use %s capture", adev->dev_manager[card].name);
                adev->dev_manager[card].flag_in_active = 1;
                flag_active |= AUDIO_IN;
        }

        if ( (!strcmp(adev->dev_manager[card].name, AUDIO_NAME_CODEC)) &&
        	(adev->dev_manager[card].flag_out == AUDIO_OUT)) {
            ALOGV("OK, default use %s playback", adev->dev_manager[card].name);
            adev->dev_manager[card].flag_out_active = 1;
            flag_active |= AUDIO_OUT;
        }
        if ((flag_active & AUDIO_IN) && (flag_active & AUDIO_OUT)) {
            return;
        }
    }

    ALOGV("low priority, chose any device");
    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        if (!adev->dev_manager[card].flag_exist) {
            break;
        }

        // there is no auido codec in
        if (!(flag_active & AUDIO_IN)) {
            if (adev->dev_manager[card].flag_in == AUDIO_IN) {
                ALOGV("OK, default use %s capture", adev->dev_manager[card].name);
                adev->dev_manager[card].flag_in_active = 1;
                flag_active |= AUDIO_IN;
            }
        }

        // there is no auido codec out
        if (!(flag_active & AUDIO_OUT)) {
            if (adev->dev_manager[card].flag_out == AUDIO_OUT) {
                ALOGV("OK, default use %s playback", adev->dev_manager[card].name);
                adev->dev_manager[card].flag_out_active = 1;
                flag_active |= AUDIO_OUT;
            }
        }
    }
    return;
}

static int update_audio_devices(struct sunxi_audio_device *adev)
{
    int card = 0;
    int ret = -1;
    char * snd_path = "/sys/class/sound";
    char snd_card[128];

    memset(snd_card, 0, sizeof(snd_card));

    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        sprintf(snd_card, "%s/card%d", snd_path, card);
        ret = access(snd_card, F_OK);
        if (ret == 0) {
            if (adev->dev_manager[card].flag_exist == true) {
                continue;       // no changes
            } else {
                // plug-in
                ALOGD("do init audio card");
                do_init_audio_card(adev, card);
            }
        } else {
            if (adev->dev_manager[card].flag_exist == false) {
                continue;       // no changes
            } else  {
                // plug-out
                adev->dev_manager[card].flag_exist = false;
                adev->dev_manager[card].flag_in = 0;
                adev->dev_manager[card].flag_out = 0;
            }
        }
    }

    return 0;
}

static char * get_audio_devices(struct sunxi_audio_device *adev, int in_out)
{
    char * in_devices = adev->in_devices;
    char * out_devices = adev->out_devices;

    update_audio_devices(adev);

    memset(in_devices, 0, 128);
    memset(out_devices, 0, 128);

    ALOGD("getAudioDevices()");
    int card = 0;
    for(card = 0; card < MAX_AUDIO_DEVICES; card++) {
        if (adev->dev_manager[card].flag_exist == true) {
            // device in
            if (adev->dev_manager[card].flag_in == AUDIO_IN) {
                strcat(in_devices, adev->dev_manager[card].name);
                strcat(in_devices, ",");
                ALOGD("in dev:%s",adev->dev_manager[card].name);
            }
            // device out
            if (adev->dev_manager[card].flag_out == AUDIO_OUT) {
                strcat(out_devices, adev->dev_manager[card].name);
                strcat(out_devices, ",");
                ALOGD("out dev:%s",adev->dev_manager[card].name);
            }
        }
    }

    in_devices[strlen(in_devices) - 1] = 0;
    out_devices[strlen(out_devices) - 1] = 0;

    //
    if (in_out & AUDIO_IN) {
        ALOGD("in capture: %s",in_devices);
        return in_devices;
    } else if (in_out & AUDIO_OUT) {
        ALOGD("out playback: %s",out_devices);
        return out_devices;
    } else {
        ALOGE("unknown in/out flag");
        return 0;
    }
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct sunxi_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct sunxi_stream_in *)((char *)buffer_provider -
                                   offsetof(struct sunxi_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm,
                                   (void*)in->buffer,
                                   in->config.period_size *
                                       audio_stream_in_frame_size(&in->stream));
        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d, %s", in->read_status, strerror(errno));
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                                                in->config.channels;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct sunxi_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct sunxi_stream_in *)((char *)buffer_provider -
                                   offsetof(struct sunxi_stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

static int get_hdmi_status()
{
    int fd = 0;
    char hdmi_status[8];
    int ret;
    
    fd = open("/sys/class/switch/hdmi/state", O_RDONLY);
    if (fd > 0) {
        ret = read(fd, hdmi_status, sizeof(hdmi_status));
        if (ret > 0) {
            hdmi_status[ret - 1] = 0;
            ALOGD("hdmi_status : %s", hdmi_status);
        }
        close(fd);
    } else {
        return -1;
    }
    return atoi(hdmi_status);
}

static int set_audio_devices_active(struct sunxi_audio_device *adev, int in_out)
{
    int card = 0, i = 0;
    char name[8][32];
    int cnt = 0;
    ALOGV("output active = %x", adev->output_active_cards);
    ALOGV("input active = %x", adev->input_active_cards);

    if(in_out & AUDIO_OUT){
        if(adev->output_active_cards & AUDIO_CARD_CODEC){
            strcpy(name[cnt++], AUDIO_NAME_CODEC);
            set_audio_path(adev, OUT_DEVICE_LINEOUT);
        }
        if(adev->output_active_cards & AUDIO_CARD_HDMI){
            strcpy(name[cnt++], AUDIO_NAME_HDMI);
        }
        if(adev->output_active_cards & AUDIO_CARD_I2S2){
            strcpy(name[cnt++], AUDIO_NAME_I2S2);
            set_audio_path(adev, OUT_DEVICE_I2S2);
        }
	if(adev->output_active_cards & AUDIO_CARD_I2S3){
            strcpy(name[cnt++], AUDIO_NAME_I2S3);
            set_audio_path(adev, OUT_DEVICE_I2S3);
        }
        if(adev->output_active_cards & AUDIO_CARD_SPDIF){
            strcpy(name[cnt++], AUDIO_NAME_SPDIF);
        }
    }

    if(in_out & AUDIO_IN){
        if(adev->input_active_cards & AUDIO_CARD_AC107){
            strcpy(name[cnt++], AUDIO_NAME_AC107);
            set_audio_path(adev, IN_DEVICE_AC107);
        }else if(adev->input_active_cards & AUDIO_CARD_I2S2){
            strcpy(name[cnt++], AUDIO_NAME_I2S2);
            set_audio_path(adev, IN_DEVICE_I2S2);
        }else if(adev->input_active_cards & AUDIO_CARD_I2S3){
            strcpy(name[cnt++], AUDIO_NAME_I2S3);
            set_audio_path(adev, IN_DEVICE_I2S3);
        }
    }

    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        if (in_out & AUDIO_IN) {
            adev->dev_manager[card].flag_in_active = 0;
        } else {
            adev->dev_manager[card].flag_out_active = 0;
        }
    }

    for (i = 0; i < cnt; i++) {
        for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
            if (in_out & AUDIO_IN) {
                if ((adev->dev_manager[card].flag_in == in_out)
                    && (strcmp(adev->dev_manager[card].name, name[i]) == 0)) {
                    ALOGV("%s %s device will be active", name[i], "input");
                    adev->dev_manager[card].flag_in_active = 1;
                    // only one capture device can be active
                    return 0;
                }
            } else if ((adev->dev_manager[card].flag_out == in_out)
                       && (strcmp(adev->dev_manager[card].name, name[i]) == 0)) {
                    ALOGV("%s %s card %d device will be active", name[i], "output", card);
                    adev->dev_manager[card].flag_out_active = 1;
                    break;
            }
        }

        if (card == MAX_AUDIO_DEVICES && i > cnt) {
            if (in_out & AUDIO_IN) {
                ALOGE("can not set %s active device", (in_out & AUDIO_IN) ? "input" : "ouput");
                adev->dev_manager[adev->card_codec].flag_in_active = 1;
                ALOGE("but %s %s will be active", adev->dev_manager[adev->card_codec].name, (in_out & AUDIO_IN) ? "input" : "ouput");
                return 0;
            } else {
                ALOGE("can not set %s active device", (in_out & AUDIO_IN) ? "input" : "ouput");
                adev->dev_manager[adev->card_ac107].flag_out_active = 1;
                ALOGE("but %s %s will be active", adev->dev_manager[adev->card_ac107].name,(in_out & AUDIO_IN) ? "input" : "ouput");
                return -1;
            }
            return -1;
        }
    }

    return 0;
}

static int get_audio_devices_active(struct sunxi_audio_device *adev, int in_out, char * devices)
{
    ALOGD("get_audio_devices_active: %s", devices);
    int card = 0;

    if (devices == 0)
        return -1;

    for (card = 0; card < MAX_AUDIO_DEVICES; card++) {
        if (in_out & AUDIO_IN) {
            if ((adev->dev_manager[card].flag_in == in_out)
                && (adev->dev_manager[card].flag_in_active == 1)) {
                strcat(devices, adev->dev_manager[card].name);
                strcat(devices, ",");
            }
        } else {
            if ((adev->dev_manager[card].flag_out == in_out)
                && (adev->dev_manager[card].flag_out_active == 1)) {
                strcat(devices, adev->dev_manager[card].name);
                strcat(devices, ",");
            }
        }
    }

    devices[strlen(devices) - 1] = 0;

    ALOGD("get_audio_devices_active: %s", devices);

    return 0;
}

static void force_all_standby(struct sunxi_audio_device *adev)
{
    struct sunxi_stream_in *in;
    struct sunxi_stream_out *out;

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }
    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}

static void select_mode(struct sunxi_audio_device *adev)
{
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGV("Entering IN_CALL state, in_call=%d", adev->in_call);
        if (!adev->in_call) {
            force_all_standby(adev);
            adev->in_call = 1;
        }
    } else {
        ALOGV("Leaving IN_CALL state, in_call=%d, mode=%d",
             adev->in_call, adev->mode);
        if (adev->in_call) {
            adev->in_call = 0;
            force_all_standby(adev);
        }
    }
}

static void set_audio_path(struct sunxi_audio_device *adev, int device_path)
{
    const char *name = NULL;
    struct audio_route *ar = NULL;

    if(device_path == OUT_DEVICE_LINEOUT){
        ar = adev->ar_codec;
    }else{
        ar = adev->ar_ahub;
    }

    if (!ar) {
        ALOGD("FUNC: %s, LINE: %d, audio route is not init",__FUNCTION__,__LINE__);
        return;
    }

    //audio_route_reset(ar);
    name = normal_route_configs[device_path];
    audio_route_apply_path(ar, name);

    audio_route_update_mixer(ar);
}

static void select_output_device(struct sunxi_audio_device *adev)
{
    ALOGD("line:%d,%s,adev->mode:%d",  __LINE__,__FUNCTION__, adev->mode);
}

static void select_input_device(struct sunxi_audio_device *adev)
{
    ALOGD("line:%d,%s,adev->mode:%d",  __LINE__,__FUNCTION__, adev->mode);
}

/* must be called with hw device and output stream mutexes locked */

static int start_output_stream(struct sunxi_stream_out *out)
{
    struct sunxi_audio_device *adev = out->dev;
    unsigned int card = 0;
    unsigned int port = 0;
    unsigned int index = 0;
    int ret;

    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGW("mode in call, do not start stream");
        return 0;
    }

    adev->active_output = out;

    for (index = 0; index < MAX_AUDIO_DEVICES; index++) {
        if (adev->dev_manager[index].flag_exist
            && (adev->dev_manager[index].flag_out == AUDIO_OUT)
            && adev->dev_manager[index].flag_out_active) {
            card = index;
            port = 0;

            ALOGV("use %s to playback audio", adev->dev_manager[index].name);
            out->multi_config[index] = pcm_config_out;
            out->multi_config[index].start_threshold = DEFAULT_OUTPUT_PERIOD_SIZE * 2;

            if(adev->dev_manager[index].ahub_device){
                out->sub_pcm[index] = pcm_open(index, port, PCM_OUT, &out->multi_config[index]);
                if (!pcm_is_ready(out->sub_pcm[index])) {
                    ALOGE("cannot open pcm driver: %s", pcm_get_error(out->sub_pcm[index]));
                    pcm_close(out->sub_pcm[index]);
                    out->sub_pcm[index] = NULL;
                    adev->active_output = NULL;
                    return -ENOMEM;
                }
                card = adev->card_ahub;
                port = adev->dev_manager[index].port;
                pcm_start(out->sub_pcm[index]);
            }

            out->multi_pcm[index] = pcm_open(card, port, PCM_OUT, &out->multi_config[index]);
            if (!pcm_is_ready(out->multi_pcm[index])) {
                ALOGE("cannot open pcm driver: %s", pcm_get_error(out->multi_pcm[index]));
                pcm_close(out->multi_pcm[index]);
                out->multi_pcm[index] = NULL;
                adev->active_output = NULL;
                return -ENOMEM;
            }

            if (DEFAULT_SAMPLING_RATE != out->multi_config[index].rate) {
                ret = create_resampler(DEFAULT_SAMPLING_RATE,
                                        out->multi_config[index].rate,
                                        2,
                                        RESAMPLER_QUALITY_DEFAULT,
                                        NULL,
                                        &out->multi_resampler[index]);
                if (ret != 0) {
                    ALOGE("create out resampler failed, %d -> %d", DEFAULT_SAMPLING_RATE, out->multi_config[index].rate);
                    return ret;
                }

                ALOGV("create out resampler OK, %d -> %d", DEFAULT_SAMPLING_RATE, out->multi_config[index].rate);
            } else
                ALOGV("play audio with %d Hz serial sample rate.", DEFAULT_SAMPLING_RATE);

            if (out->multi_resampler[index]) {
                out->multi_resampler[index]->reset(out->multi_resampler[index]);
            }
        }
    }

    return 0;
}

static int check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    UNUSED(stream);
    return DEFAULT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    UNUSED(stream);
    UNUSED(rate);
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = ((DEFAULT_OUTPUT_PERIOD_SIZE + 15) / 16) * 16;
    return size * audio_stream_out_frame_size(&out->stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    UNUSED(stream);
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    UNUSED(stream);
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    UNUSED(stream);
    UNUSED(format);
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct sunxi_stream_out *out)
{
    struct sunxi_audio_device *adev = out->dev;
    int index = 0;

    if (!out->standby) {
        for (index = 0; index < MAX_AUDIO_DEVICES; index++) {
            if (out->multi_pcm[index]) {
                pcm_close(out->multi_pcm[index]);
                out->multi_pcm[index] = NULL;
            }

            if (out->sub_pcm[index]) {
                pcm_close(out->sub_pcm[index]);
                out->sub_pcm[index] = NULL;
            }

            if (out->multi_resampler[index]) {
                release_resampler(out->multi_resampler[index]);
                out->multi_resampler[index] = NULL;
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
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    UNUSED(stream);
    UNUSED(fd);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    UNUSED(stream);
    struct str_parms *parms;
    char value[128];
    int ret, val = 0;

    parms = str_parms_create_str(kvpairs);

    ALOGV("out_set_parameters: %s", kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
    }

    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    UNUSED(stream);
    UNUSED(keys);
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    UNUSED(stream);

    return (DEFAULT_OUTPUT_PERIOD_SIZE * DEFAULT_OUTPUT_PERIOD_COUNT * 1000) / DEFAULT_SAMPLING_RATE;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    UNUSED(stream);
    UNUSED(left);
    UNUSED(right);
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    struct sunxi_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(&out->stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    void *buf;
    int index;
    int card;
    
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGD("mode in call, do not out_write");
        int time = bytes*1000*1000/4/DEFAULT_SAMPLING_RATE;
        usleep(time);
        return bytes;
    }

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
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

    for (index = MAX_AUDIO_DEVICES; index >= 0; index--)
    {
        if (adev->dev_manager[index].flag_exist
            && (adev->dev_manager[index].flag_out == AUDIO_OUT)
            && adev->dev_manager[index].flag_out_active)
        {
            card = index;

            if (out->multi_resampler[card]) {
                out->multi_resampler[card]->resample_from_input(out->multi_resampler[card],
                                                                (int16_t *)buffer,
                                                                &in_frames,
                                                                (int16_t *)out->buffer,
                                                                &out_frames);
                buf = out->buffer;
            } else {
                out_frames = in_frames;
                buf = (void *)buffer;
            }

            if (out->multi_config[card].channels == 2) {
                ret = pcm_write(out->multi_pcm[card], (void *)buf, out_frames * frame_size);
            } else {
                size_t i;
                char *pcm_buf = (char *)buf;
                for (i = 0; i < out_frames; i++) {
                    pcm_buf[2 * i + 2] = pcm_buf[4 * i + 4];
                    pcm_buf[2 * i + 3] = pcm_buf[4 * i + 5];
                }
                ret = pcm_write(out->multi_pcm[card], (void *)buf, out_frames * frame_size / 2);
            }

            if (ret!=0) {
                ALOGE("##############out_write()  Warning:write fail, reopen it ret = %d #######################", ret);
                do_output_standby(out);
                usleep(30000);
                break;
            }
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
    UNUSED(stream);
    UNUSED(dsp_frames);
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    UNUSED(stream);
    UNUSED(effect);
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    UNUSED(stream);
    UNUSED(effect);
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    UNUSED(stream);
    UNUSED(timestamp);
    return -EINVAL;
}

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct sunxi_stream_in *in)
{
    int ret = 0;
    int index = 0;
    int card = 0;
    int port = 0;
    struct sunxi_audio_device *adev = in->dev;

    adev->active_input = in;

    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGD("in call mode , start_input_stream, return");
        return 0;
    }

    ALOGV("catpure audio with %d Hz serial sample rate.", DEFAULT_SAMPLING_RATE);

    for (index = 0; index < MAX_AUDIO_DEVICES; index++) {
        if (adev->dev_manager[index].flag_exist
            && (adev->dev_manager[index].flag_in == AUDIO_IN)
            && adev->dev_manager[index].flag_in_active) {
            ALOGV("use %s to capture audio", adev->dev_manager[index].name);
            break;
        }
    }

    card = index;
    port = 0;
    if(adev->dev_manager[index].ahub_device){
        in->sub_pcm = pcm_open(index, port, PCM_IN, &in->config);
        if (!pcm_is_ready(in->sub_pcm)) {
            ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->sub_pcm));
            pcm_close(in->sub_pcm);
            adev->active_input = NULL;
            return -ENOMEM;
        }
        card = adev->card_ahub;
        port = adev->dev_manager[index].port;
        ALOGV("ahub card port : %d", port);
        pcm_start(in->sub_pcm);
    }

    in->pcm = pcm_open(card, port, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }

    if (in->requested_rate != in->config.rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(in->config.rate,
                                in->requested_rate,
                                in->config.channels,
                                RESAMPLER_QUALITY_DEFAULT,
                                &in->buf_provider,
                                &in->resampler);
        if (ret != 0) {
            ALOGE("create in resampler failed, %d -> %d", in->config.rate, in->requested_rate);
            ret = -EINVAL;
            goto err;
        }

        ALOGV("create in resampler OK, %d -> %d", in->config.rate, in->requested_rate);
    } else
        ALOGV("do not use in resampler");

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }

    return 0;

err:
    if (in->resampler)
        release_resampler(in->resampler);

    return -1;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    UNUSED(stream);
    UNUSED(rate);
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;

    return get_input_buffer_size(in->requested_rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->config.channels);
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
    UNUSED(stream);
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    UNUSED(stream);
    UNUSED(format);
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct sunxi_stream_in *in)
{
    struct sunxi_audio_device *adev = in->dev;

    if (!in->standby) {
        if(in->pcm){
            pcm_close(in->pcm);
            in->pcm = NULL;
	}

        if(in->sub_pcm){
            pcm_close(in->sub_pcm);
            in->sub_pcm = NULL;
        }

        adev->active_input = 0;

        if (in->resampler) {
            release_resampler(in->resampler);
            in->resampler = NULL;
        }

        in->standby = 1;
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct sunxi_stream_in *in = (struct sunxi_stream_in *)stream;
    int status;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    UNUSED(stream);
    UNUSED(fd);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    UNUSED(stream);
    ALOGV("in_set_parameters: %s", kvpairs);
    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    UNUSED(stream);
    UNUSED(keys);
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    UNUSED(stream);
    UNUSED(gain);
    return 0;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct sunxi_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_in_frame_size(&in->stream)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { .raw = NULL, },
                    .frame_count = frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_in_frame_size(&in->stream),
                        buf.raw,
                        buf.frame_count * audio_stream_in_frame_size(&in->stream));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct sunxi_stream_in *in      = (struct sunxi_stream_in *)stream;
    struct sunxi_audio_device *adev = in->dev;
    size_t frames_rq                = 0;

    if (adev->mode == AUDIO_MODE_IN_CALL) {
        //ALOGD("in call mode, in_read, return ;");
        usleep(10000);
        return 1;
    }

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0)
            in->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

    if (ret < 0)
        goto exit;

    /* place after start_input_stream, because start_input_stream() change frame size */
    frames_rq = bytes / audio_stream_in_frame_size(stream);

    if (in->resampler != NULL) {
        ret = read_frames(in, buffer, frames_rq);
    } else {
        ret = pcm_read(in->pcm, buffer, bytes);
    }

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));

    pthread_mutex_unlock(&in->lock);
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    UNUSED(stream);
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    UNUSED(stream);
    UNUSED(effect);

    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    UNUSED(stream);
    UNUSED(effect);

    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    struct sunxi_audio_device *ladev = (struct sunxi_audio_device *)dev;
    struct sunxi_stream_out *out;
    UNUSED(handle);
    UNUSED(devices);
    UNUSED(address);

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
    out->dev                            = ladev;
    out->standby                        = 1;

    config->format                = out_get_format(&out->stream.common);
    config->channel_mask          = out_get_channels(&out->stream.common);
    config->sample_rate           = out_get_sample_rate(&out->stream.common);

    ALOGV("+++++++++++++++ adev_open_output_stream: req_sample_rate: %d, fmt: %x, channel_count: %d",
        config->sample_rate, config->format, config->channel_mask);

    *stream_out = &out->stream;

    select_output_device(ladev);
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct sunxi_stream_out *out = (struct sunxi_stream_out *)stream;
    struct sunxi_audio_device *adev = out->dev;
    UNUSED(dev);

    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGW("mode in call, do not adev_close_output_stream");
        return ;
    }
	
    out_standby(&stream->common);

    if (out->buffer)
        free(out->buffer);

    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    struct str_parms *parms;
    char value[32];
    int ret;

    ALOGV("adev_set_parameters, %s", kvpairs);

    parms   = str_parms_create_str(kvpairs);

    ret     = str_parms_get_str(parms, AUDIO_HAL_PARAM_OUTPUT_DEVICES, value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_HAL_PARAM_OUTPUT_DEVICES_AUTO) == 0){
            adev->output_active_cards |= AUDIO_CARD_AUTO_DEC;
        }else{
            adev->output_active_cards &= ~AUDIO_CARD_AUTO_DEC;
            if (strcmp(value, AUDIO_HAL_PARAM_OUTPUT_DEVICES_AUDIO_CODEC) == 0){
                adev->output_active_cards &= ~AUDIO_CARD_HDMI;
                adev->output_active_cards |= AUDIO_CARD_CODEC;
            }
            else if (strcmp(value, AUDIO_HAL_PARAM_OUTPUT_DEVICES_HDMI) == 0){
                adev->output_active_cards &= ~AUDIO_CARD_CODEC;
                adev->output_active_cards |= AUDIO_CARD_HDMI;
            }
            else if (strcmp(value, AUDIO_HAL_PARAM_OUTPUT_DEVICES_CODEC_AND_HDMI) == 0){
                adev->output_active_cards |= (AUDIO_CARD_CODEC | AUDIO_CARD_HDMI);
            }
            else{
                adev->output_active_cards |= AUDIO_CARD_CODEC;
            }
        }

	pthread_mutex_lock(&adev->lock);
	set_audio_devices_active(adev, AUDIO_OUT);
	if(adev->active_output)
	    do_output_standby(adev->active_output);
	pthread_mutex_unlock(&adev->lock);
    }

    ret = str_parms_get_str(parms, AUDIO_HAL_PARAM_KEY_POWER_STATE, value, sizeof(value));
    if (ret >= 0) {       
        pthread_mutex_lock(&adev->lock);
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            	adev->stanby = true;
        } else if (strcmp(value, AUDIO_PARAMETER_VALUE_OFF) == 0) {
                adev->stanby = false;
        }
        pthread_mutex_unlock(&adev->lock);
    }
	
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        if(adev->output_active_cards & AUDIO_CARD_AUTO_DEC){
            if(val == AUDIO_DEVICE_OUT_AUX_DIGITAL){
                adev->output_active_cards &= ~AUDIO_CARD_CODEC;
                adev->output_active_cards |= AUDIO_CARD_HDMI;
            }else if(val == AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET){
                adev->output_active_cards |= AUDIO_CARD_SPDIF;
            }
        }

        set_audio_devices_active(adev, AUDIO_OUT);
        pthread_mutex_unlock(&adev->lock);
    }
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        if(adev->output_active_cards & AUDIO_CARD_AUTO_DEC){
            if(val == AUDIO_DEVICE_OUT_AUX_DIGITAL){
                adev->output_active_cards &= ~AUDIO_CARD_HDMI;
                adev->output_active_cards |= AUDIO_CARD_CODEC;
            }else if(val == AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET){
                adev->output_active_cards &= ~AUDIO_CARD_SPDIF;
            }
        }

        set_audio_devices_active(adev, AUDIO_OUT);
        pthread_mutex_unlock(&adev->lock);
    }
    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;
    ALOGV("adev_get_parameters, %s", keys);

    char devices[128];
    memset(devices, 0, sizeof(devices));

    if (!strcmp(keys, AUDIO_HAL_PARAM_OUTPUT_DEVICES)){
        if(adev->output_active_cards & AUDIO_CARD_AUTO_DEC){
            return strdup(AUDIO_HAL_PARAM_OUTPUT_DEVICES_AUTO);
        }else if((adev->output_active_cards & AUDIO_CARD_HDMI)
            && (adev->output_active_cards & AUDIO_CARD_CODEC)){
            return strdup(AUDIO_HAL_PARAM_OUTPUT_DEVICES_CODEC_AND_HDMI);
        }else if(adev->output_active_cards & AUDIO_CARD_CODEC){
            return strdup(AUDIO_HAL_PARAM_OUTPUT_DEVICES_AUDIO_CODEC);
        }else if(adev->output_active_cards & AUDIO_CARD_HDMI){
            return strdup(AUDIO_HAL_PARAM_OUTPUT_DEVICES_HDMI);
        }
    }
    if (!strcmp(keys, AUDIO_PARAMETER_STREAM_ROUTING))
    {
        char prop_value[512];
        int ret = property_get("audio.routing", prop_value, "");
        if (ret > 0)
        {
            return strdup(prop_value);
        }
    }

    if (!strcmp(keys, AUDIO_PARAMETER_DEVICES_IN))
        return strdup(get_audio_devices(adev, AUDIO_IN));

    if (!strcmp(keys, AUDIO_PARAMETER_DEVICES_OUT))
        return strdup(get_audio_devices(adev, AUDIO_OUT));

    if (!strcmp(keys, AUDIO_PARAMETER_DEVICES_IN_ACTIVE))
        if (!get_audio_devices_active(adev, AUDIO_IN, devices))
            return strdup(devices);

    if (!strcmp(keys, AUDIO_PARAMETER_DEVICES_OUT_ACTIVE))
        if (!get_audio_devices_active(adev, AUDIO_OUT, devices))
            return strdup(devices);

    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    UNUSED(dev);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    UNUSED(dev);

    ALOGV("adev_set_voice_volume, volume: %f", volume);

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    UNUSED(dev);
    UNUSED(volume);
    F_LOG;
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    UNUSED(dev);
    UNUSED(volume);
    F_LOG;
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);

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
    UNUSED(dev);
    int channel_count = popcount(config->channel_mask);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                             audio_io_handle_t handle,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char *address,
                             audio_source_t source)
{
    UNUSED(handle);
    UNUSED(flags);
    UNUSED(address);
    UNUSED(source);
    UNUSED(devices);
    struct sunxi_audio_device *ladev = (struct sunxi_audio_device *)dev;
    struct sunxi_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);

    *stream_in = NULL;

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

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

    // default config
    memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
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

    *stream_in 	= &in->stream;
    select_input_device(ladev);
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
    UNUSED(dev);
    struct sunxi_stream_in *in          = (struct sunxi_stream_in *)stream;

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
    UNUSED(device);
    UNUSED(fd);
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct sunxi_audio_device *adev = (struct sunxi_audio_device *)device;
    // free audio route
    if (adev->ar_ahub) {
        audio_route_free(adev->ar_ahub);
        adev->ar_ahub = NULL;
    }
    if (adev->ar_codec) {
        audio_route_free(adev->ar_codec);
        adev->ar_codec = NULL;
    }
    if (adev->mixer_ac107) {
        mixer_close(adev->mixer_ac107);
        adev->mixer_ac107 = NULL;
    }
    
    free(device);

    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct sunxi_audio_device *adev;
    int hdmi_status;

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
    adev->stanby                            = false;
    adev->output_active_cards = OUTPUT_ACTIVE_CARDS;
    adev->input_active_cards = INPUT_ACTIVE_CARDS;

    adev->card_ac107 = adev->card_ahub = adev->card_codec = -1;

    hdmi_status = get_hdmi_status();
    if(hdmi_status < 0){
        ALOGE("get hdmi status error");
    }else if(hdmi_status == 1){
        adev->output_active_cards |= AUDIO_CARD_HDMI;
    }

    init_audio_devices(adev);
    init_audio_devices_active(adev);

    if (!adev->mixer_ac107) {
        free(adev);
        ALOGE("Unable to open ac107 mixer, aborting.");
        return -EINVAL;
    }

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    adev->mode       = AUDIO_MODE_NORMAL;

    // init T501 audio input and output path
    
    set_audio_path(adev, AA_LINEIN_LINEOUT);      //default open linein to lineout path
    set_mixer_value(adev->mixer_ac107, ac107_c2_pga_vol, 12);

    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

    ALOGD("adev_open success ,LINE:%d,FUNC:%s",__LINE__,__FUNCTION__);
    return 0;
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
