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

#define LOG_TAG "SensorsImpl"
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

#include <linux/input.h>

#include <utils/Atomic.h>
#include <utils/Log.h>
#include "SensorsImpl.h"

#include "AccelSensor.h"
#include "MagSensor.h"
#include "PressSensor.h"
#include "GyroSensor.h"
#include "LightSensor.h"
#include "ProximitySensor.h"
#include "TempSensor.h"

#include <cutils/properties.h>

using android::hardware::sensors::V1_0::OperationMode;
int SensorsImpl::get_sensors_list(struct sensor_t const** list) {
    int a = 0;
    *list = sSensorList;
    a = ARRAY_SIZE(sSensorList);
    ALOGD("sensors__get_sensors_list sNumber:%d, a:%d\n", sNumber, a);
    return sNumber;
}

bool SensorsImpl::checkHasWakeUpSensor() {
    int num = ARRAY_SIZE(sSensorList);
    for (int i = 0; i < num; i++) {
        if (sSensorList[i].flags & SENSOR_FLAG_WAKE_UP)
            return true;
    }
    return false;
}

int SensorsImpl::initialize(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
        const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
        const sp<ISensorsCallback>& sensorsCallback)
{
    int result = 0;
    int a = ARRAY_SIZE(sSensorList);
    int i = 0;
    for (i = 0; i < a; i++) {
        activate(sSensorList[i].handle /* handle */, 0 /* disable */);
    }
    result = setCallback(eventQueueDescriptor, wakeLockDescriptor, sensorsCallback);

    return result;
}

int SensorsImpl::set_operation_mode(uint32_t mode) {
    bool hasInject = false;
    for (int i = 0; i < sNumber; i++) {
        if (mSensors[i]->getSensorInfo()->flags & SENSOR_FLAG_DATA_INJECTION) {
            mSensors[i]->set_operation_mode((OperationMode)mode);
            hasInject = true;
        }
    }
    if (!hasInject)
        return android::BAD_VALUE;
    return 0;
}

void SensorsImpl::createSensorDevices()
{
    int first = -1;
    accel = mag = gyro = light = proximity = temperature = press = -1;

    if((seStatus[ID_A].isUsed == true) && (seStatus[ID_A].isFound == true)) {
        first = first + 1;
        accel = first;
        mSensors[first] = new AccelSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_A]]);
        mSensors[first]->setHandle(ID_A);
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
        mSensors[first]->setHandle(ID_M);
        mPollFds[first].fd = mSensors[mag]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
#else
/*
        mSensors[first] = new MagnetoSensor((AccelSensor*)mSensors[accel]);
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_M]]);
        mSensors[first]->setHandle(ID_M);
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
        mSensors[first]->setHandle(ID_GY);
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
        mSensors[first]->setHandle(ID_L);
        mPollFds[first].fd = mSensors[light]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    if((seStatus[ID_PX].isUsed == true) && (seStatus[ID_PX].isFound == true)) {
        first = first + 1;
        proximity = first;
        mSensors[first] = new ProximitySensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_PX]]);
        mSensors[first]->setHandle(ID_PX);
        mPollFds[first].fd = mSensors[proximity]->getFd();
        mPollFds[first].events = POLLIN;
        mPollFds[first].revents = 0;
    }

    if((seStatus[ID_T].isUsed == true) && (seStatus[ID_T].isFound == true)) {
        first = first + 1;
        temperature = first;
        mSensors[first] = new TempSensor();
        mSensors[first]->setSensorInfo(&sSensorList[seSensorIndex[ID_T]]);
        mSensors[first]->setHandle(ID_T);
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

SensorsImpl::~SensorsImpl() {
    for (int i=0 ; i < sNumber ; i++) {
        delete mSensors[i];
    }
    close(mPollFds[wake].fd);
    close(mWritePipeFd);
}

int SensorsImpl::activate(int32_t handle, bool enabled) {

    int index = handleToDriver(handle);
    if (index < 0) return android::BAD_VALUE;
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

int SensorsImpl::setDelay(int handle, int64_t ns) {

    int index = handleToDriver(handle);
    int err = -1;
    if (index < 0)
        return android::BAD_VALUE;

    if(handle == ID_O || handle ==  ID_M){
        mSensors[accel]->setDelay(handle, ns);
    }

    err = mSensors[index]->setDelay(handle, ns);

    return err;
}

int SensorsImpl::setCallback(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
        const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
        const sp<ISensorsCallback>& sensorsCallback)
{
    SensorEventCallback *sCallback = new SensorEventCallback(eventQueueDescriptor, wakeLockDescriptor, sensorsCallback);
    if (!sCallback->isVaild())
        return -EINVAL;
    sCallback->setHasWakeLock(checkHasWakeUpSensor());
    for (int i = 0; i < sNumber; i++) {
        mSensors[i]->setCallback(sCallback);
    }
    return 0;
}

int SensorsImpl::inject_sensor_data(const Event& event) {
    int index = handleToDriver(event.sensorHandle);
    if (index < 0) return index;
    int err = 0 ;
    err = mSensors[index]->injectEvent(event);
    return err;
}

int SensorsImpl::handleToDriver(int handle) const {
    switch (handle) {
        case ID_A:
            return accel;
        case ID_M:
        case ID_O:
            return mag;
        case ID_GY:
            return gyro;
        case ID_L:
            return light;
        case ID_PX:
            return proximity;
        case ID_T:
            return temperature;
        case ID_P:
            return press;
    }
    return -EINVAL;
}

int SensorsImpl::batch(int handle, int64_t sampling_period_ns,
         int64_t max_report_latency_ns) {
    int index = handleToDriver(handle);
    if (index < 0) return android::BAD_VALUE;
    return setDelay(handle,sampling_period_ns);
}

int SensorsImpl::flush(int sensor_handle) {

    ALOGW("AW flush handle %d\n",sensor_handle);
    int index = handleToDriver(sensor_handle);
    if (index < 0) return android::BAD_VALUE;
    return mSensors[index]->flush();
}

int SensorsImpl::register_direct_channel(sensors_direct_mem_t* m, int channel_handle) {
    return 0;
}

int SensorsImpl::config_direct_report(int sensor_handle, int channel_handle, const struct sensors_direct_cfg_t* config) {
    return 0;
}

SensorsImpl::SensorsImpl()
{
    property_set("sys.sensors", "1");/*Modify the  enable and delay interface  group */
    sensorsDetect();/*detect device,filling sensor_t structure */
    createSensorDevices();
}
