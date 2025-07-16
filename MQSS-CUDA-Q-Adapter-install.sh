#/bin/bash

# Parse arguments
while [[ $# -gt 0 ]]; do
  key="$1"
  case $key in
    --cudaq-install-dir)
      CUDAQ_DIR="$2"
      shift
      shift
      ;;
    *)
      echo "Unknown option $1"
      exit 1
      ;;
  esac
done

# Check if --cudaq-install-dir was provided
if [[ -z "$CUDAQ_DIR" ]]; then
  echo "Error: --cudaq-install-dir is required."
  exit 1
fi

# Check if directory exists
if [[ ! -d "$CUDAQ_DIR" ]]; then
  echo "Error: Directory '$CUDAQ_DIR' does not exist."
  exit 1
fi

# Set your filenames (replace with actual filenames)
CUDAQ_COMMON_LIB="./extern/cuda-quantum/build/lib/libcudaq-common.so"
CUDAQ_RUNTIME_LIB="./extern/cuda-quantum/build/lib/libcudaq-mlir-runtime.so"
CUDAQ_REST_LIB="./extern/cuda-quantum/build/lib/libcudaq-rest-qpu.so"
SERVER_HELPER_MQP="./extern/cuda-quantum/build/lib/libcudaq-serverhelper-mqss-mqp.so"
SERVER_HELPER_HPC="./extern/cuda-quantum/build/lib/libcudaq-serverhelper-mqss-hpc.so"
TARGET_MQP="./extern/cuda-quantum/build/targets/mqssMQP.yml"
TARGET_HPC="./extern/cuda-quantum/build/targets/mqssHPC.yml"

# Check if files A and B exist in current directory
if [[ ! -f "$CUDAQ_COMMON_LIB" ]]; then
  echo "Error: File '$CUDAQ_COMMON_LIB' not found in current directory."
  exit 1
fi

if [[ ! -f "$CUDAQ_RUNTIME_LIB" ]]; then
  echo "Error: File '$CUDAQ_RUNTIME_LIB' not found in current directory."
  exit 1
fi

if [[ ! -f "$CUDAQ_REST_LIB" ]]; then
  echo "Error: File '$CUDAQ_REST_LIB' not found in current directory."
  exit 1
fi

if [[ ! -f "$TARGET_MQP" ]]; then
  echo "Error: File '$TARGET_MQP' not found in current directory."
  exit 1
fi

if [[ ! -f "$TARGET_HPC" ]]; then
  echo "Error: File '$TARGET_HPC' not found in current directory."
  exit 1
fi

if [[ ! -f "$SERVER_HELPER_MQP" ]]; then
  echo "Error: File '$SERVER_HELPER_MQP' not found in current directory."
  exit 1
fi

if [[ ! -f "$SERVER_HELPER_HPC" ]]; then
  echo "Error: File '$SERVER_HELPER_HPC' not found in current directory."
  exit 1
fi

# Copy files into the destination directory
#cp "$FILE_A" "$CUDAQ_DIR/"
#cp "$FILE_B" "$CUDAQ_DIR/"
cp "$CUDAQ_COMMON_LIB" "$CUDAQ_DIR/lib"
cp "$CUDAQ_RUNTIME_LIB" "$CUDAQ_DIR/lib"
cp "$CUDAQ_REST_LIB" "$CUDAQ_DIR/lib"
cp "$SERVER_HELPER_MQP" "$CUDAQ_DIR/lib"
cp "$SERVER_HELPER_HPC" "$CUDAQ_DIR/lib"

cp "$TARGET_MQP" "$CUDAQ_DIR/targets"
cp "$TARGET_HPC" "$CUDAQ_DIR/targets"

echo "✅ MQSS CUDA-Q Adapter successfully installed to '$CUDAQ_DIR'."
