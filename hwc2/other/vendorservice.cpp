
#include <utils/String16.h>
#include "DisplayConfigService.h"
#include "IHWCPrivateService.h"
#include "snr.h"

#include "../hwc.h"

using sunxi::SNRApi;

class Hwcps: public sunxi::IHWCPrivateService {
public:
    static Hwcps *instantiate();
    void init();
    void dataspaceChangeNotify(int dataspace);

private:
    Hwcps() {}
   ~Hwcps() {};

    /* API define in IHWCPrivateService.h */
    int setDisplayArgs(int display, int cmd1, int cmd2, int data) override;
    int blank(int display, int enable) override;
    int switchDevice(const sunxi::DeviceTable& tables) override;
    int setOutputMode(int display, int type, int mode) override;
    int setMargin(int display, int l, int r, int t, int b) override;
    int setVideoRatio(int display, int ratio) override;
    int set3DMode(int display, int mode) override;
    int setDataspace(int display, int dataspace) override;
    int registerCallback(sunxi::IHWCPrivateService::EventCallback* cb) override;
    void setDebugTag(int32_t tag) override;
    virtual SNRApi* getSNRInterface() override;
    android::status_t dump(int fd, const android::Vector<android::String16>& args);

    static Hwcps* mInstance;
    sunxi::IHWCPrivateService::EventCallback *mEventCallback;
    int mDataspace;

    // Instance of the vendor display hidl service.
    android::sp<sunxi::DisplayConfigService> mDisplayConfigService;
};

Hwcps* Hwcps::mInstance = nullptr;

Hwcps* Hwcps::instantiate()
{
    if (mInstance == nullptr)
        mInstance = new Hwcps();
    return mInstance;
}

int Hwcps::blank(int display, int enable) {
    ALOGD("blank: display[%d], enable[%d]",
          display, enable);
    hwc_setBlank(display, enable);
    return 0;
}

int Hwcps::switchDevice(const sunxi::DeviceTable& tables) {
    ALOGD("switchDevice: phy display count %d",
          tables.mTables.size());
    struct switchdev switchdev[2];
    memset(switchdev, 0, (sizeof(struct switchdev) * 2));

    int displayNum = tables.mTables.size();
    for (int i = 0; i < displayNum; i++) {
	    switchdev[tables.mTables[i].logicalId].display = tables.mTables[i].logicalId;
	    switchdev[tables.mTables[i].logicalId].en = tables.mTables[i].enabled;
	    switchdev[tables.mTables[i].logicalId].type = tables.mTables[i].type;
	    switchdev[tables.mTables[i].logicalId].mode = tables.mTables[i].mode;
    }

    hwc_setSwitchdevice(switchdev);

    return 0;
}

int Hwcps::setOutputMode(int display, int type, int mode) {
    ALOGD("setOutputMode: display[%d] type=%d mode=%d",
          display, type, mode);
    hwc_setOutputMode(display, type, mode);
    return 0;
}

int Hwcps::setMargin(int display, int l, int r, int t, int b) {
    ALOGD("setMargin: display[%d] l=%d r=%d t=%d b=%d",
          display, l, r, t, b);
	hwc_setMargin(display, r, b);
    return 0;
}

int Hwcps::setVideoRatio(int display, int ratio) {
    ALOGD("setVideoRatio: display[%d], ratio=%d",
          display, ratio);
	hwc_setVideoRatio(display, ratio);
    return 0;
}

int Hwcps::set3DMode(int display, int mode) {
    ALOGD("set3DMode: display[%d], mode=%d",
          display, mode);
    hwc_set_3d_mode(display, mode);
    return 0;
}

int Hwcps::setDataspace(int display, int dataspace) {
    ALOGD("setDataspace: display[%d], dataspace=%d",
          display, dataspace);

    if (dataspace == IHWCPrivateService::eDataspaceHdr)
        dataspace = DISPLAY_OUTPUT_DATASPACE_MODE_HDR;
    else if (dataspace == IHWCPrivateService::eDataspaceSdr)
        dataspace = DISPLAY_OUTPUT_DATASPACE_MODE_SDR;
    else
        ALOGD("Hwcps:setDataspace: unknow dataspace mode: %08x", dataspace);

    hwc_setDataSpacemode(display, dataspace);
    return 0;
}

int Hwcps::registerCallback(sunxi::IHWCPrivateService::EventCallback* cb) {
    mEventCallback = cb;
    ALOGD("Callback register from displayd");
    return 0;
}

// vendor command handle function define in hwc.cpp
extern int hwc_set_display_command(int display, int cmd1, int cmd2, int data);

int Hwcps::setDisplayArgs(int display, int cmd1, int cmd2, int data) {
    ALOGD("setDisplayArgs: display=%d args: %d %d %d", display, cmd1, cmd2, data);
    return hwc_set_display_command(display, cmd1, cmd2, data);
}

void Hwcps::setDebugTag(int32_t tag) {
    ALOGD("setDebugTag: tag=%d", tag);
}

android::status_t Hwcps::dump(int fd, const android::Vector<android::String16>& /* args */) {

    android::String8 result;
    result.appendFormat("HWCPrivateService:\n");
    result.appendFormat("  DeviceSlot: none\n");

    write(fd, result.string(), result.size());
    return android::NO_ERROR;
}

void Hwcps::init() {
    /* Register Hwcps */
    mDisplayConfigService = new sunxi::DisplayConfigService(*this);
    android::status_t status = mDisplayConfigService->publish();
    ALOGD("DisplayConfigService init: %s",
            (status == android::OK) ? "ok" : "error");
}

SNRApi* Hwcps::getSNRInterface() {
#if ((TARGET_BOARD_PLATFORM == cupid) && defined(TARGET_PLATFORM_HOMLET))
    return static_cast<SNRApi*>(sunxi::SNRSetting::getInstance());
#endif
    return nullptr;
}

void Hwcps::dataspaceChangeNotify(int dataspace) {
    if (mDataspace == dataspace)
        return;
    if (mEventCallback != nullptr) {
        mEventCallback->onDataspaceChanged(dataspace);
        mDataspace = dataspace;
        ALOGD("Update dataspace(0x%08x) to client", dataspace);
    }
}

void vendorservice_init() {
    Hwcps *privateService = Hwcps::instantiate();
    privateService->init();
    ALOGD("hwc vendor service start.");
}

void homlet_dataspace_change_callback(int dataspace) {
    Hwcps *privateService = Hwcps::instantiate();
    privateService->dataspaceChangeNotify(dataspace);
}

void setup_snr_info(struct disp_snr_info* snr)
{
#if ((TARGET_BOARD_PLATFORM == cupid) && defined(TARGET_PLATFORM_HOMLET))
    sunxi::SNRSetting* handle = sunxi::SNRSetting::getInstance();
    if (handle) {
        handle->convertSnrInfoForDisplay(snr);
    }
#endif
}

