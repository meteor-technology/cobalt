// Copyright 2021 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/contrib/stadia/x11/application_stadia_x11.h"

#include "starboard/contrib/stadia/services/vendor/linux/public/stadia_lifecycle.h"
#include "starboard/shared/x11/window_internal.h"

namespace starboard {
namespace contrib {
namespace stadia {
namespace x11 {

using ::starboard::shared::dev_input::DevInput;

constexpr char kAppId[] = "com.google.stadia.linux";

SbWindow ApplicationStadiaX11::CreateWindow(const SbWindowOptions* options) {
  SbWindow window =
      starboard::shared::x11::ApplicationX11::CreateWindow(options);
  StadiaInitialize();
  return window;
}

}  // namespace x11
}  // namespace stadia
}  // namespace contrib
}  // namespace starboard
