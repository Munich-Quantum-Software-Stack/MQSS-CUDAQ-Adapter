/*
 * Copyright (c) 2024 - 2026 MQSS Project
 * All rights reserved.
 *
 * Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#pragma once

#include "cudaq/utils/cudaq_utils.h"
#include "mqss/client.h"

#include <cudaq/platform/qpu.h>
#include <memory>

namespace cudaq {

/// The MQSSQPU models a simulated QPU by specifically
/// targeting the QIS ExecutionManager.
class MQSSQPU : public QPU {

private:
  struct CompiledCode {
    std::vector<std::string> codes;
    std::string circuitFormat;
  };

  CompiledCode compileToCodes(const AnyModule& module,
                              const sample_policy& policy, KernelArgs args);

  sample_result submitAndAwait(const std::vector<std::string>& codes,
                               const std::string& circuitFormat,
                               std::size_t shots,
                               std::chrono::seconds timeoutOverride);

public:
  ~MQSSQPU() = default;

  /// @brief the platform file path
  std::filesystem::path platformPath;

  mqss::client::MQSSClient client;

  MQSSQPU() : QPU() {
    std::filesystem::path cudaqLibPath{cudaq::getCUDAQLibraryPath()};
    platformPath = cudaqLibPath.parent_path().parent_path() / "targets";
  }

  void enqueue(QuantumTask& task) override { execution_queue->enqueue(task); }

  /// @brief The target configuration
  cudaq::config::TargetConfig targetConfig;

  /// @brief Mapping of general key-values for backend configuration.
  std::map<std::string, std::string> backendConfig;

  /// @brief The name of the device being targeted
  std::string device;

  bool emulate = false;

  bool runPipeline = true;

  bool isRemote() override { return true; }

  int timeout;

  sample_result launchKernel(const sample_policy& policy,
                             const AnyModule& module, KernelArgs args) override;

  cudaq::async_sample_result launchKernel(const async_sample_policy& policy,
                                          const cudaq::AnyModule& module,
                                          cudaq::KernelArgs args) override;

  cudaq::observe_result launchKernel(const cudaq::observe_policy& policy,
                                     const cudaq::AnyModule& module,
                                     cudaq::KernelArgs args) override;

  cudaq::async_observe_result launchKernel(async_observe_policy& policy,
                                           const cudaq::AnyModule& module,
                                           cudaq::KernelArgs args) override;

  KernelThunkResultType unifiedLaunchModule(const AnyModule& module,
                                            KernelArgs args) override {
    throw std::runtime_error("unifiedLaunchModule.");
  }

  std::unique_ptr<CompileTarget>
  getCompileTarget(const sample_policy& policy) override;

  void setTargetBackend(const std::string& backend) override;
};

CUDAQ_REGISTER_TYPE(QPU, MQSSQPU, mqss)
} // namespace cudaq
