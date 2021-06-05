/*
 * Copyright (C) 2011 Freescale Semiconductor Inc.
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "Sensors"
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>

#include <linux/input.h>

#include <utils/Atomic.h>
#include <utils/Log.h>
#include <sensors/sensors2.h>
#include "sensors.h"

#include "AccelSensor.h"
#if defined (MAG_SENSOR_FXOS8700)
#include "MagSensor.h"
#else
#include "MagnetoSensor.h"
#endif
#include "PressSensor.h"
#include "GyroSensor.h"
#include "LightSensor.h"
#include "ProximitySensor.h"
#include "TempSensor.h"

#include <cutils/properties.h>

//using namespace ::android::hardware::sensors::V2_0::implementation;

static sensors_poll_context_t *gContext = nullptr;


static int open_sensors(const struct hw_module_t* module, const char* id,
        struct hw_device_t** device);


static int sensors__get_sensors_list(struct sensors_module_2_t* module,
        struct sensor_t const** list)
{
    int a = 0;
    *list = sSensorList;
    a = ARRAY_SIZE(sSensorList);
    ALOGD("sensors__get_sensors_list sNumber:%d, a:%d\n", sNumber, a);
    return sNumber;
}

static bool checkHasWakeUpSensor() {
    int num = ARRAY_SIZE(sSensorList);
    for (int i = 0; i < num; i++) {
        if (sSensorList[i].flags & SENSOR_FLAG_WAKE_UP)
            return true;
    }
    return false;
}

static int sensors__initialize(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
        const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
        const sp<ISensorsCallback>& sensorsCallback)
{
    Result result = Result::OK;
    int a = ARRAY_SIZE(sSensorList);
    int i = 0;
    for (i = 0; i < a; i++) {
        gContext->activate(sSensorList[i].handle /* handle */, 0 /* disable */);
    }
	result = gContext->setCallback(eventQueueDescriptor, wakeLockDescriptor, sensorsCallback);

    return (int)result;
}

static int sensors_set_operation_mode(uint32_t mode) {
   gContext->setOperationMode((OperationMode)mode);
   return 0;
}

static struct hw_module_methods_t sensors_module_methods = {
open: open_sensors
};

struct sensors_module_2_t HAL_MODULE_INFO_SYM = {
common: {
tag: HARDWARE_MODULE_TAG,
     version_major: 2,
     version_minor: 0,
     id: SENSORS2_HARDWARE_MODULE_ID,
     name: "Sensor module",
     author: "Freescale Semiconductor Inc.",
     methods: &sensors_module_methods,
     dso : NULL,
     reserved : {},
        },
get_sensors_list: sensors__get_sensors_list,
set_operation_mode : sensors_set_operation_mode,
initialize: sensors__initialize,
};

/*****************************************************************************/

sensors_poll_context_t::sensors_poll_context_t()
    :gyro(-1),
    press(-1)
{
    int first = -1;

    if((seStatus[ID_A].isUsed == true) && (seStatus[ID_A].isFound == true)) {
        first = first + 1;
        accel = first;
        mSensors[first] = new AccelSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_A]]);
        mPollFds[first].fd = mSensors[accel]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    if((seStatus[ID_M].isUsed == true) && (seStatus[ID_M].isFound == true)) {
        first = first + 1;
        mag = first;

#if defined (MAG_SENSOR_FXOS8700)
        mSensors[first] = new MagSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_M]]);
        mPollFds[first].fd = mSensors[mag]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
#else
/*
        mSensors[first] = new MagnetoSensor((AccelSensor*)mSensors[accel]);
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_M]]);
        mPollFds[first].fd = mSensors[mag]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
*/
#endif
    }

/*
    if((seStatus[ID_GY].isUsed == true) && (seStatus[ID_GY].isFound == true)) {
        first = first + 1;
        gyro = first;
        mSensors[first] = new GyroSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_GY]]);
        mPollFds[first].fd = mSensors[gyro]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }
*/
    if((seStatus[ID_L].isUsed == true) && (seStatus[ID_L].isFound == true)) {
        first = first + 1;
        light = first;
        mSensors[first] = new LightSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_L]]);
        mPollFds[first].fd = mSensors[light]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    if((seStatus[ID_PX].isUsed == true) && (seStatus[ID_PX].isFound == true)) {
        first = first + 1;
        proximity = first;
        mSensors[first] = new ProximitySensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_PX]]);
        mPollFds[first].fd = mSensors[proximity]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    if((seStatus[ID_T].isUsed == true) && (seStatus[ID_T].isFound == true)) {
        first = first + 1;
        temperature = first;
        mSensors[first] = new TempSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_T]]);
        mPollFds[first].fd = mSensors[temperature]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    int wakeFds[2];
    int result = pipe(wakeFds);
    ALOGE_IF(result<0, "error creating wake pipe (%s)", strerror(errno));
    fcntl(wakeFds[0], F_SETFL, O_NONBLOCK);
    fcntl(wakeFds[1], F_SETFL, O_NONBLOCK);
    mWritePipeFd = wakeFds[1];

    mPollFds[wake].fd = wakeFds[0];
    mPollFds[wake].events = POLLIN;
    mPollFds[wake].revents = 0;
}

sensors_poll_context_t::~sensors_poll_context_t() {
    for (int i=0 ; i < 10 ; i++) {
        delete mSensors[i];
    }
    close(mPollFds[wake].fd);
    close(mWritePipeFd);
}

int sensors_poll_context_t::activate(int handle, int enabled) {

    int index = handleToDriver(handle);
    if (index < 0) return index;
    int err = 0 ;
	// if handle == orientaion or magnetic ,please enable ACCELERATE Sensor
    if(handle == ID_O || handle ==  ID_M){
        err =  mSensors[accel]->setEnable(handle, enabled);
        if(err)
            return err;
    }
    err |=  mSensors[index]->setEnable(handle, enabled);

    if (enabled && !err) {
        const char wakeMessage(WAKE_MESSAGE);
        int result = write(mWritePipeFd, &wakeMessage, 1);
        ALOGE_IF(result<0, "error sending wake message (%s)", strerror(errno));
    }
    return err;
}

int sensors_poll_context_t::setDelay(int handle, int64_t ns) {

    int index = handleToDriver(handle);
    int err = -1;
    if (index < 0)
        return index;

    if(handle == ID_O || handle ==  ID_M){
        mSensors[accel]->setDelay(handle, ns);
    }

    err = mSensors[index]->setDelay(handle, ns);

    return err;
}

Result sensors_poll_context_t::setCallback(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
        const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
        const sp<ISensorsCallback>& sensorsCallback)
{
    SensorEventCallback *sCallback = new SensorEventCallback(eventQueueDescriptor, wakeLockDescriptor, sensorsCallback);
    if (!sCallback->isVaild())
        return Result::BAD_VALUE;
    sCallback->setHasWakeLock(checkHasWakeUpSensor());
    for (int i = 0; i < sNumber; i++) {
        mSensors[i]->setCallback(sCallback);
	}
    return Result::OK;
}

int sensors_poll_context_t::flush(int sensor_handle) {
    ALOGW("AW flush handle %d\n",sensor_handle);
    int index = handleToDriver(sensor_handle);
    if (index < 0) return index;
    mSensors[index]->flush();
    return 0;
}

void sensors_poll_context_t::setOperationMode(OperationMode mode) {
    for (int i = 0; i < sNumber; i++) {
        gContext->getSensor(i)->set_operation_mode(mode);
    }
}

int sensors_poll_context_t::injectSensorData(int handle, const Event& event) {
    int index = handleToDriver(handle);
    if (index < 0) return index;
    int err = 0 ;
    err = mSensors[index]->injectEvent(event);
    return err;
}

/*****************************************************************************/

static int poll__batch(struct sensors_poll_device_2* dev,
		  int handle, int flags, int64_t sampling_period_ns,
		  int64_t max_report_latency_ns)
{
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
	return ctx->setDelay(handle,sampling_period_ns);
}


static int poll__close(struct hw_device_t *dev)
{
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;

    if (ctx) {
        delete ctx;
    }
    return 0;
}

static int poll__activate(struct sensors_poll_device2_t *dev,
        int handle, int enabled) {

    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->activate(handle, enabled);
}

static int poll__setDelay(struct sensors_poll_device2_t *dev,
        int handle, int64_t ns) {

    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->setDelay(handle, ns);
}

static int poll__flush(struct sensors_poll_device_2 *dev,
		int handle) {

    ALOGW("AW poll__flush handle %d\n",handle);
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->flush(handle);
}

static int poll__inject_sensor_data(struct sensors_poll_device_2 *dev,
        const Event& event) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->injectSensorData(event.sensorHandle, event);
}

/*****************************************************************************/

/** Open a new instance of a sensor device using name */
static int open_sensors(const struct hw_module_t* module, const char* id,
                        struct hw_device_t** device)
{
    //insmodDevice();/*Automatic detection, loading device drivers */
    property_set("sys.sensors", "1");/*Modify the  enable and delay interface  group */
    sensorsDetect();/*detect device,filling sensor_t structure */
    sensors_poll_context_t *dev = new sensors_poll_context_t();
    memset(&dev->device, 0, sizeof(sensors_poll_device_2_t));

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version  = SENSORS_DEVICE_API_VERSION_2_0;
    dev->device.common.module   = const_cast<hw_module_t*>(module);
    dev->device.common.close    = poll__close;
    dev->device.activate        = poll__activate;
    dev->device.setDelay        = poll__setDelay;
    dev->device.batch			= poll__batch;
    dev->device.flush			= poll__flush;
    dev->device.inject_sensor_data = poll__inject_sensor_data;
    dev->device.register_direct_channel = NULL;

    *device = &dev->device.common;
    gContext = dev;

    return 0;
}
