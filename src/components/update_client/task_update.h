// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_UPDATE_CLIENT_TASK_UPDATE_H_
#define COMPONENTS_UPDATE_CLIENT_TASK_UPDATE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "components/update_client/task.h"
#include "components/update_client/update_client.h"

namespace update_client {

class UpdateEngine;
enum class Error;

// Defines a specialized task for updating a group of CRXs.
class TaskUpdate : public Task {
 public:
  using Callback =
      base::OnceCallback<void(scoped_refptr<Task> task, Error error)>;

  // |update_engine| is injected here to handle the task.
  // |is_foreground| is true when the update task is initiated by the user.
  // |ids| represents the CRXs to be updated by this task.
  // |crx_data_callback| is called to get update data for the these CRXs.
  // |callback| is called to return the execution flow back to creator of
  //    this task when the task is done.
  TaskUpdate(scoped_refptr<UpdateEngine> update_engine,
             bool is_foreground,
             const std::vector<std::string>& ids,
             UpdateClient::CrxDataCallback crx_data_callback,
             Callback callback);

  void Run() override;

  void Cancel() override;

  std::vector<std::string> GetIds() const override;

 private:
  ~TaskUpdate() override;

  // Called when the task has completed either because the task has run or
  // it has been canceled.
  void TaskComplete(Error error);

  base::ThreadChecker thread_checker_;
  scoped_refptr<UpdateEngine> update_engine_;
  const bool is_foreground_;
  const std::vector<std::string> ids_;
  UpdateClient::CrxDataCallback crx_data_callback_;
  Callback callback_;
#if defined(STARBOARD)
  base::OnceClosure cancelation_closure_;
#endif

  DISALLOW_COPY_AND_ASSIGN(TaskUpdate);
};

}  // namespace update_client

#endif  // COMPONENTS_UPDATE_CLIENT_TASK_UPDATE_H_
