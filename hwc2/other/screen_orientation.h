
#ifndef HWC_SCREEN_ORIENTATION_H_
#define HWC_SCREEN_ORIENTATION_H_

#ifdef PRIMARY_DISPLAY_ORIENTATION
static_assert(PRIMARY_DISPLAY_ORIENTATION == 0 || PRIMARY_DISPLAY_ORIENTATION == 90 ||
                      PRIMARY_DISPLAY_ORIENTATION == 180 || PRIMARY_DISPLAY_ORIENTATION == 270,
              "Primary display orientation must be 0/90/180/270");
#endif

enum {
    eOrientationDefault = 0,
    eOrientation90 = 1,
    eOrientation180 = 2,
    eOrientation270 = 3,
    eOrientationUnchanged = 4,
    eOrientationSwapMask = 0x01
};

static inline int getFlip(int transform) {
    bool flipH = (transform & HAL_TRANSFORM_FLIP_H) != 0;
    bool flipV = (transform & HAL_TRANSFORM_FLIP_V) != 0;
    int flip = transform & (HAL_TRANSFORM_FLIP_V | HAL_TRANSFORM_FLIP_H);
    if (flipH != flipV) {
        return flip;
    } else {
        return 0;
    }
}

static inline int transformToOrientation(int transform) {
    switch (transform) {
        case HAL_TRANSFORM_ROT_90:
            return eOrientation90;
        case HAL_TRANSFORM_ROT_180:
            return eOrientation180;
        case HAL_TRANSFORM_ROT_270:
            return eOrientation270;
        default:
            return eOrientationDefault;
    }
}

static inline int orientationToTransform(int orientation) {
    switch (orientation) {
        case eOrientationDefault:
            return 0;
        case eOrientation90:
            return HAL_TRANSFORM_ROT_90;
        case eOrientation180:
            return HAL_TRANSFORM_ROT_180;
        case eOrientation270:
            return HAL_TRANSFORM_ROT_270;
        default:
            return 0;
    }
}

// take care of screen physical rotation
inline int calcuTransformFromScreenOrientation(int transform) {
    int orientationInDegree = 0;
    int physicalOrientation = eOrientationDefault;

#ifdef PRIMARY_DISPLAY_ORIENTATION
    orientationInDegree = PRIMARY_DISPLAY_ORIENTATION;
#endif

    switch (orientationInDegree) {
        case 0:
            physicalOrientation = eOrientationDefault;
            break;
        case 90:
            physicalOrientation = eOrientation90;
            break;
        case 180:
            physicalOrientation = eOrientation180;
            break;
        case 270:
            physicalOrientation = eOrientation270;
            break;
        default:
            physicalOrientation = eOrientationDefault;
            break;
    }

    int flip = getFlip(transform);
    if (flip != 0)
        transform &= ~flip;

    int bufferOrientation = transformToOrientation(transform);
    int orientation = (physicalOrientation + bufferOrientation) % (eOrientation270 + 1);
    int finalTransform = orientationToTransform(orientation);

    if (flip != 0) {
        if (finalTransform & HAL_TRANSFORM_ROT_90) {
            if (flip == HAL_TRANSFORM_FLIP_H)
                flip = HAL_TRANSFORM_FLIP_V;
            else
                flip = HAL_TRANSFORM_FLIP_H;
        }
        finalTransform ^= flip;
    }
    return finalTransform;
}

#endif // HWC_SCREEN_ORIENTATION_H_
