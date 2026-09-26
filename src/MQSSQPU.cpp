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

#include "MQSSQPU.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mqss/client.h"
#include "mqss/job.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <common/Future.h>
#include <common/SampleResult.h>
#include <cudaq.h>
#include <cudaq/algorithms/sample/policy.h>
#include <cudaq_internal/compiler/Compiler.h>
#include <future>
#include <memory>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

std::vector<std::string>
generateCode(const cudaq::AnyModule& module,
             std::unique_ptr<cudaq::CompileTarget> target,
             cudaq::KernelArgs args) {

  std::vector<std::string> codesToExecute;
  const auto& src = std::get<cudaq::SourceModule>(module);

  auto [moduleOp, context] =
      cudaq_internal::compiler::Compiler::loadQuakeCodeByName(src.getName());

  std::string kernelName = src.getName();
  cudaq_internal::compiler::Compiler compiler(std::move(target));

  auto compiled =
      compiler.runPassPipeline(kernelName, moduleOp, args, true, context);

  auto codes = compiler.emitKernelExecutions(compiled);

  for (auto code : codes) {
    codesToExecute.push_back(code.code);
  }
  return codesToExecute;
}

cudaq::BackendConfig parseBackend(const std::string& backend) {
  auto fields = cudaq::split(backend, ';');
  if (fields.empty() || fields.size() % 2 == 0)
    throw std::runtime_error(
        "Backend config must contain a target and key-value pairs.");

  cudaq::BackendConfig config;
  for (std::size_t index = 1; index < fields.size(); index += 2) {
    auto value = fields[index + 1];
    if (value.starts_with("base64_"))
      value = cudaq::detail::decodeBase64(value.substr(7));
    config.insert_or_assign(fields[index], std::move(value));
  }

  return std::move(config);
}

inline std::string getHostName() {
  size_t max_hostname_size = sysconf(_SC_HOST_NAME_MAX);
  if (max_hostname_size == -1) {
    max_hostname_size = 256;
  }
  char* hostname = new char[max_hostname_size];
  if (gethostname(hostname, max_hostname_size) == 0) {
    std::string result(hostname);
    delete[] hostname;
    return result;
  } else {
    delete[] hostname;
    return "";
  }
}

std::string materializeQirProgram(const std::string& encodedBitcode) {
  try {
    llvm::LLVMContext context;
    const auto bitcode = cudaq::detail::decodeBase64(encodedBitcode);
    const auto buffer = llvm::MemoryBuffer::getMemBufferCopy(bitcode);

    auto module = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
    if (!module)
      throw std::runtime_error("Could not parse CUDA-Q QIR bitcode: " +
                               llvm::toString(module.takeError()));

    llvm::Function* entryPoint = nullptr;
    for (auto& function : **module) {
      if (!function.isDeclaration() &&
          (function.hasFnAttribute("entry_point") ||
           function.hasFnAttribute("EntryPoint"))) {
        if (entryPoint)
          return {};
        entryPoint = &function;
      }
    }

    if (entryPoint && entryPoint->arg_empty() &&
        entryPoint->getReturnType()->isVoidTy()) {
      const auto name = entryPoint->getName().str();
      entryPoint->setName(name + ".cudaq");

      auto* adapter = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getInt64Ty(context), false),
          entryPoint->getLinkage(), name, **module);

      adapter->setCallingConv(entryPoint->getCallingConv());
      entryPoint->removeFnAttr("entry_point");
      entryPoint->removeFnAttr("EntryPoint");

      auto* block = llvm::BasicBlock::Create(context, "entry", adapter);
      llvm::IRBuilder<> builder(block);
      builder.CreateCall(entryPoint);
      builder.CreateRet(
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0));
    }

    std::string program;
    llvm::raw_string_ostream output(program);
    module.get()->print(output, nullptr);
    return program;

  } catch (const std::exception& error) {
    throw std::runtime_error("Could not materialize CUDA-Q QIR program: " +
                             std::string(error.what()));
  }
}
struct ExecutionContextGuard {
  explicit ExecutionContextGuard(cudaq::ExecutionContext* ctx) {
    cudaq::detail::setExecutionContext(ctx);
  }
  ~ExecutionContextGuard() { cudaq::detail::resetExecutionContext(); }
  ExecutionContextGuard(const ExecutionContextGuard&) = delete;
  ExecutionContextGuard& operator=(const ExecutionContextGuard&) = delete;
};

cudaq::config::TargetConfig
loadTargetConfigYml(const std::filesystem::path& configFilePath) {
  std::ifstream configFile(configFilePath);
  if (!configFile)
    throw std::runtime_error("Unable to open backend configuration file: " +
                             configFilePath.string());

  std::string configYmlContents((std::istreambuf_iterator<char>(configFile)),
                                std::istreambuf_iterator<char>());

  cudaq::config::TargetConfig targetConfig;
  cudaq::detail::parseTargetConfigYml(configYmlContents, targetConfig);

  if (!targetConfig.BackendConfig)
    throw std::runtime_error("Backend configuration is missing from: " +
                             configFilePath.string());
  return targetConfig;
}

} // namespace

namespace cudaq {

std::unique_ptr<CompileTarget>
MQSSQPU::getCompileTarget(const sample_policy& /*policy*/) {
  CompileTarget target(targetConfig, backendConfig, emulate);
  target.supportConditionalsOnMeasureResults = !emulate;
  target.pipelineConfig.replaceStateWithKernel = true;
  target.pipelineConfig.addMeasurements = true;
  target.storeReorderIdx = true;
  return std::make_unique<CompileTarget>(target);
}

MQSSQPU::CompiledCode MQSSQPU::compileToCodes(const AnyModule& module,
                                              const sample_policy& policy,
                                              KernelArgs args) {
  const auto& sourceModule = std::get<cudaq::SourceModule>(module);

  if (!runPipeline) {
    // Quake/MLIR passthrough: no compilation pipeline, submit the parsed
    // module text as-is.
    auto [mlirModuleOpaquePtr, mlirContext] =
        cudaq_internal::compiler::Compiler::loadQuakeCodeByName(
            sourceModule.getName());
    auto mod = mlir::ModuleOp::getFromOpaquePointer(mlirModuleOpaquePtr);
    std::string str;
    llvm::raw_string_ostream os(str);
    mod.print(os);
    return {{std::move(str)}, "quake"};
  }

  auto generatedCodes = generateCode(module, getCompileTarget(policy), args);

  if (targetConfig.BackendConfig->CodegenEmission != "qasm2") {
    std::vector<std::string> materialized;
    materialized.reserve(generatedCodes.size());
    for (const auto& generatedCode : generatedCodes)
      materialized.push_back(materializeQirProgram(generatedCode));
    return {std::move(materialized),
            targetConfig.BackendConfig->CodegenEmission};
  }

  return {std::move(generatedCodes), "qasm"};
}

sample_result MQSSQPU::submitAndAwait(const std::vector<std::string>& codes,
                                      const std::string& circuitFormat,
                                      std::size_t shots,
                                      std::chrono::seconds timeoutOverride) {
  std::vector<ExecutionResult> resultsToReturn;

  for (const auto& code : codes) {
    auto job = mqss::client::CircuitJobRequest(code, circuitFormat, device,
                                               shots, false, false);
    auto uuid = client.submitJob(job);
    if (!uuid.has_value())
      throw std::runtime_error("Failed to submit job.");

    std::unique_ptr<mqss::client::JobResult> jobResult =
        client.getJobResult(job, true, timeoutOverride.count());
    if (jobResult == nullptr)
      throw std::runtime_error("Failed to retrieve results");

    for (auto counts : jobResult->getResults()) {
      CountsDictionary _counts = {counts.begin(), counts.end()};
      resultsToReturn.emplace_back(_counts);
    }
  }

  return resultsToReturn;
}

sample_result MQSSQPU::launchKernel(const sample_policy& policy,
                                    const AnyModule& module, KernelArgs args) {
  CUDAQ_INFO("MQSSQPU: launching synchronous kernel");

  auto* ctx = getExecutionContext();
  if (!ctx)
    throw std::runtime_error(
        "MQSSQPU: no execution context available for sync sample.");

  auto [codes, circuitFormat] = compileToCodes(module, policy, args);
  return submitAndAwait(codes, circuitFormat, ctx->shots,
                        std::chrono::seconds(timeout));
}

cudaq::async_sample_result
cudaq::MQSSQPU::launchKernel(const async_sample_policy& policy,
                             const cudaq::AnyModule& module,
                             cudaq::KernelArgs args) {
  CUDAQ_INFO("MQSSQPU: launching asynchronous kernel");

  auto* ctx = getExecutionContext();
  if (!ctx)
    throw std::runtime_error(
        "MQSSQPU: no execution context available for async sample.");

  auto promise = std::make_shared<std::promise<sample_result>>();
  std::future<sample_result> resultFuture = promise->get_future();

  const sample_policy p;

  // Async jobs may legitimately queue longer on the MQSS side than the
  // configured sync timeout allows; give them a longer, local-only budget
  // rather than mutating the shared `timeout` member (that would race
  // with any concurrent sync/async call on this QPU instance).
  constexpr auto asyncTimeout = std::chrono::seconds(100000);

  QuantumTask task = [this, p, module, args, promise, ctx,
                      asyncTimeout]() mutable {
    ExecutionContextGuard guard(ctx);
    try {
      auto [codes, circuitFormat] = compileToCodes(module, p, args);
      auto result =
          submitAndAwait(codes, circuitFormat, ctx->shots, asyncTimeout);
      promise->set_value(std::move(result));
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  };

  enqueue(task);

  return async_sample_result(cudaq::detail::future(std::move(resultFuture)));
}

cudaq::observe_result
cudaq::MQSSQPU::launchKernel(const cudaq::observe_policy& policy,
                             const cudaq::AnyModule& module,
                             cudaq::KernelArgs args) {
  throw std::runtime_error(
      "MQSSQPU does not support launching the observe_result.");
}

cudaq::async_observe_result
cudaq::MQSSQPU::launchKernel(async_observe_policy& policy,
                             const cudaq::AnyModule& module,
                             cudaq::KernelArgs args) {
  throw std::runtime_error(
      "MQSSQPU does not support launching the observe_result.");
}

void MQSSQPU::setTargetBackend(const std::string& backend) {
  CUDAQ_INFO("MQSSQPU platform is targeting {}.", backend);

  backendConfig = parseBackend(backend);

  auto backendName = backend.substr(0, backend.find(';'));
  const auto configFilePath = platformPath / (backendName + ".yml");
  CUDAQ_INFO("Config file path = {}", configFilePath.string());

  targetConfig = loadTargetConfigYml(configFilePath);

  auto getConfig = [&](const std::string& key,
                       const std::string& defaultValue = "",
                       bool lower = true) {
    auto it = backendConfig.find(key);
    auto value = it != backendConfig.end() ? it->second : defaultValue;
    if (lower)
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) { return std::tolower(c); });
    return value;
  };

  const auto accessMode = getConfig("access_mode", "remote");
  const auto token = getConfig("token", "", false);
  device = getConfig("device", "EQE1", false);
  const auto endpoint =
      getConfig("endpoint",
                accessMode == "remote"
                    ? "https://portal.quantum.lrz.de:4000/v1/"
                    : "qoffload_api_request_reception_queue_" + getHostName());
  const auto exchangeFormat = getConfig("exchange_format", "qasm2");
  timeout = std::atoi(getConfig("timeout", "60").c_str());

  if (accessMode != "remote" && accessMode != "hpc") {
    throw std::runtime_error(
        "Only available access modes are 'remote' and 'hpc'");
  }

  if (accessMode == "remote" && token.empty()) {
    throw std::runtime_error("Token is mandatory when access_mode is 'remote'");
  }

  if (exchangeFormat != "qir-base" && exchangeFormat != "qir-adaptive" &&
      exchangeFormat != "qasm2" && exchangeFormat != "quake" &&
      exchangeFormat != "mlir") {
    throw std::runtime_error(
        "Invalid exchange format. Valid exchange formats are: 'qir-base', "
        "'qir-adaptive', 'qasm2', 'quake' and 'mlir'");
  }

  // Update the backend configuration using the parsed backend settings.
  auto targetBackend = *targetConfig.BackendConfig;
  if (exchangeFormat != "quake" && exchangeFormat != "mlir") {
    targetBackend.CodegenEmission = exchangeFormat;
    CUDAQ_INFO("Codegen emission: {}", targetBackend.CodegenEmission);
  } else {
    runPipeline = false;
  }

  targetConfig.BackendConfig = std::move(targetBackend);
  client = mqss::client::MQSSClient(token, endpoint, accessMode == "hpc");
}

} // namespace cudaq
