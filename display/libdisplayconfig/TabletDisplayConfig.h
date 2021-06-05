/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef _TABLET_DISPLAY_CONFIG_H_
#define _TABLET_DISPLAY_CONFIG_H_

#include "DisplayConfigImpl.h"

namespace sunxi {

class TabletDisplayConfig: public DisplayConfigImpl {
public:
    TabletDisplayConfig(IHWCPrivateService& client)
        : mHWComposer(client) { }

    int setDisplayArgs(int display, int cmd1, int cmd2, int data) override {
        return mHWComposer.setDisplayArgs(display, cmd1, cmd2, data);
    }

    int dump(std::string& out) override {
        out += "\n TabletDisplayConfig \n";
        return 0;
    }

private:
    IHWCPrivateService& mHWComposer;
};

} // namespace sunxi
#endif
