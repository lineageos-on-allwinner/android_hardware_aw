/*
* Copyright (C) 2014 Invensense, Inc.
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

#ifndef ANDROID_MPL_BUILDER_H
#define ANDROID_MPL_BUILDER_H

#include <stdint.h>

#define WAKE_UP_SENSOR 1

struct mpl_sensor_data {
    int gyro_data[3];
    int accel_data[3];
    int compass_data[3];

    int six_axis_data[4];
    int nine_axis_data[4];
    int six_axis_compass_data[4];

    short gyro_raw_data[3];
    short accel_raw_data[3];

    int gyro_scale;
    int accel_scale;
    int compass_scale;

    int nine_axis_data_scaled[4];
    int six_axis_data_scaled[4];
    int six_axis_compass_data_scaled[4];

    int gyro_matrix_scalar;
    int accel_matrix_scalar;
    int compass_matrix_scalar;

    int compass_soft_iron[9];
    int compass_sens[9];//

    int gyro_bias[3];
    int accel_bias[3];
    int compass_bias[3];

    int gyro_sample_rate;
    int accel_sample_rate;
    int compass_sample_rate;

    int gyro_w_sample_rate;
    int accel_w_sample_rate;
    int compass_w_sample_rate;

    int quat6_sample_rate;
    int quat9_sample_rate;
    int quat6_compass_sample_rate;

    int quat6_w_sample_rate;
    int quat9_w_sample_rate;
    int quat6_compass_w_sample_rate;

    int la_sample_rate;
    int gravity_sample_rate;
    int or_sample_rate;

    int la_w_sample_rate;
    int gravity_w_sample_rate;
    int or_w_sample_rate;

    long long gyro_timestamp;
    long long gyro_raw_timestamp;
    long long accel_timestamp;
    long long compass_timestamp;
    long long compass_raw_timestamp;
    long long gyro_last_timestamp;
    long long gyro_raw_last_timestamp;
    long long accel_last_timestamp;
    long long compass_last_timestamp;
    long long compass_raw_last_timestamp;

    long long gyro_w_timestamp;
    long long accel_w_timestamp;
    long long compass_w_timestamp;
    long long gyro_w_last_timestamp;
    long long accel_w_last_timestamp;
    long long compass_w_last_timestamp;

    long long gyro_raw_w_timestamp;
    long long compass_raw_w_timestamp;
    long long gyro_raw_w_last_timestamp;
    long long compass_raw_w_last_timestamp;

    long long quat6_timestamp;
    long long quat9_timestamp;
    long long quat6_compass_timestamp;
    long long quat6_last_timestamp;
    long long quat9_last_timestamp;
    long long quat6_compass_last_timestamp;
    long long la_timestamp;
    long long la_last_timestamp;
    long long gravity_timestamp;
    long long gravity_last_timestamp;
    long long or_timestamp;
    long long or_last_timestamp;

    long long quat6_w_timestamp;
    long long quat9_w_timestamp;
    long long quat6_compass_w_timestamp;
    long long quat6_w_last_timestamp;
    long long quat9_w_last_timestamp;
    long long quat6_compass_w_last_timestamp;
    long long la_w_timestamp;
    long long la_w_last_timestamp;
    long long gravity_w_timestamp;
    long long gravity_w_last_timestamp;
    long long or_w_timestamp;
    long long or_w_last_timestamp;
};

inline void init_mpl_cal_lib() { return; };
inline int mpl_gyro_reset_timestamp() { return 0; };
inline int mpl_accel_reset_timestamp() { return 0; };
inline int mpl_compass_reset_timestamp() { return 0; };
inline void mpl_build_quat() { return; };
inline void mpl_build_quat9() { return; };
inline void mpl_build_quat_compass() { return; };
inline int mpl_build_accel(int *accel, int status, long long timestamp) { return 0; };
inline int mpl_build_compass(int *accel, int status, long long timestamp) { return 0; };
inline int mpl_build_quat(int *quat, int status, long long timestamp) { return 0; };
inline int mpl_build_gyro(int *accel, int status, long long timestamp) { return 0; };

inline int mpl_get_sensor_type_rotation_vector(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_accelerometer(float *values, int8_t *accuracy,
                                            int64_t * timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_gyroscope(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_gyroscope_raw(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_magnetic_field(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_magnetic_field_raw(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_game_rotation_vector(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_geomagnetic_rotation_vector(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_linear_acceleration(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_gravity(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };
inline int mpl_get_sensor_type_orientation(float *values, int8_t *accuracy,
                                            int64_t *timestamp, int mode) { return 0; };

inline void mpl_set_sample_rate(int sample_rate_us, int id) { return; };
inline int get_effective_sample_rate(int sample_rate_us) { return 0; };
inline void set_compass_sample_rate(int sample_rate_us) { return; };
inline void set_gyro_sample_rate(int sample_rate_us) { return; };
inline void set_accel_sample_rate(int sample_rate_us) { return; } ;

inline void mpl_set_accel_orientation_and_scale(int orientation, int sensitivity)
        { return; };
inline void mpl_set_gyro_orientation_and_scale(int orientation, int sensitivity)
        { return; };
inline void mpl_set_compass_orientation_and_scale(int orientation, int sensitivity, int *softIron)
        { return; };
inline void mpl_set_compass_orientation_and_scale1(int orientation, int scale, int *sensitivity, int *softIron)
        { return; };

inline int inv_get_gravity_6x(int *data) { return 0; };
inline int inv_get_cal_accel(int *data) { return 0; };
inline void inv_get_rotation(float r[3][3]) { return; };
inline void google_orientation(float *g) { return; } ;

inline int mpl_get_mpl_gyro_bias(int *bias) { return 0; };
inline int mpl_set_mpl_gyro_bias(int *bias) { return 0; };
inline int mpl_get_mpl_accel_bias(int *bias) { return 0; };
inline int mpl_set_mpl_accel_bias(int *bias) { return 0; };
inline int mpl_get_mpl_compass_bias(int *bias) { return 0; };
inline int mpl_set_mpl_compass_bias(int *bias, int accuracy) { return 0; };

inline void mpl_set_lib_version(int version_number) { return; };
inline int mpl_get_lib_version() { return 0; };

#endif //  ANDROID_MPL_BUILDER_H
