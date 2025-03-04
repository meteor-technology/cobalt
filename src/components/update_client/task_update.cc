// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "components/update_client/task_update.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_engine.h"

namespace update_client {

TaskUpdate::TaskUpdate(scoped_refptr<UpdateEngine> update_engine,
                       bool is_foreground,
                       const std::vector<std::string>& ids,
                       UpdateClient::CrxDataCallback crx_data_callback,
                       Callback callback)
    : update_engine_(update_engine),
      is_foreground_(is_foreground),
      ids_(ids),
      crx_data_callback_(std::move(crx_data_callback)),
      callback_(std::move(callback)) {}

TaskUpdate::~TaskUpdate() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void TaskUpdate::Run() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (ids_.empty()) {
    TaskComplete(Error::INVALID_ARGUMENT);
    return;
  }

#if defined(STARBOARD)
  update_engine_->Update(is_foreground_, ids_, std::move(crx_data_callback_),
                         base::BindOnce(&TaskUpdate::TaskComplete, this),
                         cancelation_closure_);
#else
  update_engine_->Update(is_foreground_, ids_, std::move(crx_data_callback_),
                         base::BindOnce(&TaskUpdate::TaskComplete, this));
#endif
}

void TaskUpdate::Cancel() {
  DCHECK(thread_checker_.CalledOnValidThread());

#if defined(STARBOARD)
  if (cancelation_closure_) {  // The engine's picked up the task.
    std::move(cancelation_closure_).Run();
  }
#endif

  TaskComplete(Error::UPDATE_CANCELED);
}

std::vector<std::string> TaskUpdate::GetIds() const {
  return ids_;
}

void TaskUpdate::TaskComplete(Error error) {
  DCHECK(thread_checker_.CalledOnValidThread());

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback_),
                                scoped_refptr<TaskUpdate>(this), error));
}

}  // namespace update_client
