#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd "${script_dir}/.." && pwd)"

image="${UNLEASHED_SWITCH_DOCKER_IMAGE:-switch-nvk-vulkan:local}"
jobs="${JOBS:-8}"
mesa_nvk_dir="${MESA_NVK_DIR:-$(cd "${root_dir}/.." && pwd)/mesa-clean}"
build_dir="${UNLEASHED_SWITCH_PLUME_BUILD_DIR:-/project/build/plume-switch}"

docker_args=(
  --rm
  -v "${root_dir}:/project"
  --workdir /project
)

cmake_nvk_args=(
  -DPLUME_SWITCH_NVK_ROOT=/opt/nvk-switch
  -DPLUME_SWITCH_NVK_LIBRARY=/opt/nvk-switch/lib/libvulkan.a
)

if [[ -f "${mesa_nvk_dir}/builddir-switch/src/nouveau/vulkan/libvulkan.a" ]]; then
  docker_args+=(-v "${mesa_nvk_dir}:/mesa-clean:ro")
  cmake_nvk_args=(
    -DPLUME_SWITCH_NVK_ROOT=/mesa-clean
    -DPLUME_SWITCH_NVK_LIBRARY=/mesa-clean/builddir-switch/src/nouveau/vulkan/libvulkan.a
  )
fi

docker run "${docker_args[@]}" \
  "${image}" \
  sh -lc "cmake -S /project/thirdparty/plume -B ${build_dir} \
    -DCMAKE_TOOLCHAIN_FILE=/project/toolchains/switch-devkitA64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DPLUME_PLATFORM_SWITCH=ON \
    ${cmake_nvk_args[*]} && \
    cmake --build ${build_dir} -j${jobs} --target plume"
