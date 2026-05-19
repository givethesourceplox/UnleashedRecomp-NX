#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd "${script_dir}/.." && pwd)"

image="${UNLEASHED_SWITCH_DOCKER_IMAGE:-switch-nvk-vulkan:local}"
jobs="${JOBS:-8}"
mac_host_build_dir="${UNLEASHED_SWITCH_MAC_HOST_TOOLS_BUILD_DIR:-${root_dir}/build/host-tools-macos}"
linux_host_build_dir="${UNLEASHED_SWITCH_LINUX_HOST_TOOLS_BUILD_DIR:-/project/build/host-tools}"
switch_build_dir="${UNLEASHED_SWITCH_BUILD_DIR:-/project/build/switch-app}"
mesa_nvk_dir="${MESA_NVK_DIR:-$(cd "${root_dir}/.." && pwd)/mesa-clean}"

private_dir="${root_dir}/UnleashedRecompLib/private"
missing_private=()
for private_file in default.xex default.xexp shader.ar; do
  if [[ ! -f "${private_dir}/${private_file}" ]]; then
    missing_private+=("${private_file}")
  fi
done

if (( ${#missing_private[@]} > 0 )); then
  echo "Missing build input(s) in ${private_dir}: ${missing_private[*]}" >&2
  echo "Copy default.xex, default.xexp, and shader.ar from your own installed Xbox 360 copy before building." >&2
  exit 2
fi

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

echo "== Building macOS host shader tool =="
cmake -S "${root_dir}" -B "${mac_host_build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNLEASHED_RECOMP_HOST_TOOLS_ONLY=ON
cmake --build "${mac_host_build_dir}" -j"${jobs}" --target XenosRecomp

echo "== Building Linux host generation tools in Docker =="
docker run "${docker_args[@]}" "${image}" sh -lc \
  "cmake -S /project -B ${linux_host_build_dir} \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNLEASHED_RECOMP_HOST_TOOLS_ONLY=ON && \
   cmake --build ${linux_host_build_dir} -j${jobs} --target file_to_c x_decompress XenonRecomp"

echo "== Generating PPC sources and decompressed shader archive =="
docker run "${docker_args[@]}" "${image}" sh -lc \
  "${linux_host_build_dir}/tools/XenonRecomp/XenonRecomp/XenonRecomp \
    /project/UnleashedRecompLib/config/SWA.toml \
    /project/tools/XenonRecomp/XenonUtils/ppc_context.h && \
   ${linux_host_build_dir}/tools/x_decompress/x_decompress \
    /project/UnleashedRecompLib/private/shader.ar \
    /project/UnleashedRecompLib/private/shader_decompressed.ar"

echo "== Generating shader cache with macOS DXC =="
"${mac_host_build_dir}/tools/XenosRecomp/XenosRecomp/XenosRecomp" \
  "${root_dir}/UnleashedRecompLib/private" \
  "${root_dir}/UnleashedRecompLib/shader/shader_cache.cpp" \
  "${root_dir}/tools/XenosRecomp/XenosRecomp/shader_common.h"

echo "== Generating app shader headers with macOS DXC =="
"${root_dir}/tools/generate-switch-app-shaders.sh"

echo "== Building Switch ELF and packaging NRO =="
docker run "${docker_args[@]}" "${image}" sh -lc \
  "cmake -S /project -B ${switch_build_dir} \
    -DCMAKE_TOOLCHAIN_FILE=/project/toolchains/switch-devkitA64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNLEASHED_RECOMP_SWITCH=ON \
    -DUNLEASHED_RECOMP_SWITCH_USE_PREGENERATED_SHADERS=ON \
    -DUNLEASHED_RECOMP_SWITCH_USE_PREGENERATED_APP_SHADERS=ON \
    -DUNLEASHED_RECOMP_HOST_TOOLS_DIR=${linux_host_build_dir} \
    -DUNLEASHED_RECOMP_HOST_FILE_TO_C=${linux_host_build_dir}/tools/file_to_c/file_to_c \
    -DUNLEASHED_RECOMP_HOST_X_DECOMPRESS=${linux_host_build_dir}/tools/x_decompress/x_decompress \
    -DUNLEASHED_RECOMP_HOST_XENON_RECOMP=${linux_host_build_dir}/tools/XenonRecomp/XenonRecomp/XenonRecomp \
    -DPLUME_PLATFORM_SWITCH=ON \
    ${cmake_nvk_args[*]} && \
   cmake --build ${switch_build_dir} -j${jobs} --target UnleashedRecomp && \
   elf='${switch_build_dir}/UnleashedRecomp/UnleashedRecomp' && \
   test -f \"\${elf}\" && \
   . /project/UnleashedRecomp/res/version.txt && \
   version=\"\${VERSION_MAJOR}.\${VERSION_MINOR}.\${VERSION_REVISION}\" && \
   nacptool --create 'Unleashed Recompiled' 'hedge-dev' \"\${version}\" '${switch_build_dir}/UnleashedRecomp.nacp' && \
   cp \"\${elf}\" '${switch_build_dir}/UnleashedRecomp.debug.elf' && \
   /opt/devkitpro/devkitA64/bin/aarch64-none-elf-strip --strip-all \"\${elf}\" && \
   elf2nro \"\${elf}\" '${switch_build_dir}/UnleashedRecomp.nro' --nacp='${switch_build_dir}/UnleashedRecomp.nacp' --icon=/project/UnleashedRecomp/res/switch_icon.jpg"

echo "== Done =="
echo "NRO: ${root_dir}${switch_build_dir#/project}/UnleashedRecomp.nro"
echo "Debug ELF: ${root_dir}${switch_build_dir#/project}/UnleashedRecomp.debug.elf"
echo "Switch app folder: sdmc:/switch/UnleashedRecomp/"
echo "Installer drop folder: sdmc:/switch/UnleashedRecomp/install/"
