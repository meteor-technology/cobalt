// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/application_android.h"
#include "starboard/android/shared/jni_env_ext.h"
#include "starboard/android/shared/jni_utils.h"
#include "starboard/android/shared/log_internal.h"
#include "starboard/common/semaphore.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/command_line.h"
#include "starboard/thread.h"

namespace starboard {
namespace android {
namespace shared {
namespace {

using ::starboard::shared::starboard::CommandLine;
typedef ::starboard::android::shared::ApplicationAndroid::AndroidCommand
    AndroidCommand;

SbThread g_starboard_thread = kSbThreadInvalid;

// Safeguard to avoid sending AndroidCommands either when there is no instance
// of the Starboard application, or after the run loop has exited and the
// ALooper receiving the commands is no longer being polled.
bool g_app_running = false;

std::vector<std::string> GetArgs() {
  std::vector<std::string> args;
  // Fake program name as args[0]
  args.push_back(SbStringDuplicate("android_main"));

  JniEnvExt* env = JniEnvExt::Get();

  ScopedLocalJavaRef<jobjectArray> args_array(
      env->CallStarboardObjectMethodOrAbort("getArgs",
                                            "()[Ljava/lang/String;"));
  jint argc = !args_array ? 0 : env->GetArrayLength(args_array.Get());

  for (jint i = 0; i < argc; i++) {
    ScopedLocalJavaRef<jstring> element(
        env->GetObjectArrayElementOrAbort(args_array.Get(), i));
    args.push_back(env->GetStringStandardUTFOrAbort(element.Get()));
  }

  return args;
}

std::string GetStartDeepLink() {
  JniEnvExt* env = JniEnvExt::Get();
  std::string start_url;

  ScopedLocalJavaRef<jstring> j_url(env->CallStarboardObjectMethodOrAbort(
      "getStartDeepLink", "()Ljava/lang/String;"));
  if (j_url) {
    start_url = env->GetStringStandardUTFOrAbort(j_url.Get());
  }
  SB_LOG(INFO) << "GetStartDeepLink: " << start_url;
  return start_url;
}

void* ThreadEntryPoint(void* context) {
  Semaphore* app_created_semaphore = static_cast<Semaphore*>(context);

  ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  ApplicationAndroid app(looper);

  CommandLine command_line(GetArgs());
  LogInit(command_line);

  // Mark the app running before signaling app created so there's no race to
  // allow sending the first AndroidCommand after onCreate() returns.
  g_app_running = true;

  // Signal ANativeActivity_onCreate() that it may proceed.
  app_created_semaphore->Put();

  // Enter the Starboard run loop until stopped.
  int error_level =
      app.Run(std::move(command_line), GetStartDeepLink().c_str());

  // Mark the app not running before informing StarboardBridge that the app is
  // stopped so that we won't send any more AndroidCommands as a result of
  // shutting down the Activity.
  g_app_running = false;

  // Our launcher.py looks for this to know when the app (test) is done.
  SB_LOG(INFO) << "***Application Stopped*** " << error_level;

  // Inform StarboardBridge that the run loop has exited so it can cleanup and
  // kill the process.
  JniEnvExt* env = JniEnvExt::Get();
  env->CallStarboardVoidMethodOrAbort("afterStopped", "()V");

  return NULL;
}

void OnStart(ANativeActivity* activity) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(AndroidCommand::kStart);
  }
}

void OnResume(ANativeActivity* activity) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(AndroidCommand::kResume);
  }
}

void OnPause(ANativeActivity* activity) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(AndroidCommand::kPause);
  }
}

void OnStop(ANativeActivity* activity) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(AndroidCommand::kStop);
  }
}

void OnWindowFocusChanged(ANativeActivity* activity, int focused) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(focused
        ? AndroidCommand::kWindowFocusGained
        : AndroidCommand::kWindowFocusLost);
  }
}

void OnNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(
        AndroidCommand::kNativeWindowCreated, window);
  }
}

void OnNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(
        AndroidCommand::kNativeWindowDestroyed);
  }
}

void OnInputQueueCreated(ANativeActivity* activity, AInputQueue* queue) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(
        AndroidCommand::kInputQueueChanged, queue);
  }
}

void OnInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue) {
  if (g_app_running) {
    ApplicationAndroid::Get()->SendAndroidCommand(
        AndroidCommand::kInputQueueChanged, NULL);
  }
}

extern "C" SB_EXPORT_PLATFORM void ANativeActivity_onCreate(
    ANativeActivity *activity, void *savedState, size_t savedStateSize) {

  // Start the Starboard thread the first time an Activity is created.
  if (!SbThreadIsValid(g_starboard_thread)) {
    Semaphore semaphore;

    g_starboard_thread = SbThreadCreate(
        0, kSbThreadPriorityNormal, kSbThreadNoAffinity, false,
        "StarboardMain", &ThreadEntryPoint, &semaphore);

    // Wait for the ApplicationAndroid to be created.
    semaphore.Take();
  }

  activity->callbacks->onStart = OnStart;
  activity->callbacks->onResume = OnResume;
  activity->callbacks->onPause = OnPause;
  activity->callbacks->onStop = OnStop;
  activity->callbacks->onWindowFocusChanged = OnWindowFocusChanged;
  activity->callbacks->onNativeWindowCreated = OnNativeWindowCreated;
  activity->callbacks->onNativeWindowDestroyed = OnNativeWindowDestroyed;
  activity->callbacks->onInputQueueCreated = OnInputQueueCreated;
  activity->callbacks->onInputQueueDestroyed = OnInputQueueDestroyed;
  activity->instance = ApplicationAndroid::Get();
}

extern "C" SB_EXPORT_PLATFORM
jboolean Java_dev_cobalt_coat_StarboardBridge_nativeIsReleaseBuild() {
#if defined(COBALT_BUILD_TYPE_GOLD)
  return true;
#else
  return false;
#endif
}

extern "C" SB_EXPORT_PLATFORM
void Java_dev_cobalt_coat_StarboardBridge_nativeInitialize(
    JniEnvExt* env, jobject starboard_bridge) {
  JniEnvExt::Initialize(env, starboard_bridge);
}

}  // namespace
}  // namespace shared
}  // namespace android
}  // namespace starboard
