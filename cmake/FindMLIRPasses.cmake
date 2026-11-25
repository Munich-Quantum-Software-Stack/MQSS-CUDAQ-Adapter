include(FetchContent)

FetchContent_Declare(
  MLIRPasses
  # GIT_REPOSITORY https://github.com/Munich-Quantum-Software-Stack/passes.git
  # GIT_TAG develop # Use the latest tag
  GIT_REPOSITORY git@gitlab.lrz.de:0000000001578E2B/cudaq-custom-mlir-passes.git
  GIT_TAG qmap-integration)

FetchContent_MakeAvailable(MLIRPasses)
