#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/.." && pwd)"
shader_dir="${root_dir}/UnleashedRecomp/gpu/shader"
dxc_root="${root_dir}/tools/XenosRecomp/thirdparty/dxc-bin"

case "$(uname -m)" in
  arm64|aarch64)
    dxc_arch="arm64"
    ;;
  x86_64|amd64)
    dxc_arch="x64"
    ;;
  *)
    echo "Unsupported host architecture for bundled macOS DXC: $(uname -m)" >&2
    exit 1
    ;;
esac

dxc="${dxc_root}/bin/${dxc_arch}/dxc-macos"
lib_dir="${dxc_root}/lib/${dxc_arch}"

if [[ ! -x "${dxc}" ]]; then
  echo "Missing bundled macOS DXC: ${dxc}" >&2
  exit 1
fi

export DYLD_LIBRARY_PATH="${lib_dir}${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"

compile_shader() {
  local name="$1"
  local target="$2"
  shift 2

  "${dxc}" \
    -T "${target}" \
    -HV 2021 \
    -all-resources-bound \
    -spirv \
    -fvk-use-dx-layout \
    "$@" \
    -Fh "${shader_dir}/${name}.hlsl.spirv.h" \
    "${shader_dir}/${name}.hlsl" \
    -Vn "g_${name}_spirv"
}

compile_vertex_shader() {
  compile_shader "$1" vs_6_0 -fvk-invert-y -DUNLEASHED_RECOMP
}

compile_pixel_shader() {
  compile_shader "$1" ps_6_0 -DUNLEASHED_RECOMP
}

compile_pixel_shader blend_color_alpha_ps
compile_vertex_shader copy_vs
compile_pixel_shader copy_color_ps
compile_pixel_shader copy_depth_ps
compile_pixel_shader csd_filter_ps
compile_vertex_shader csd_no_tex_vs
compile_vertex_shader csd_vs
compile_pixel_shader enhanced_motion_blur_ps
compile_pixel_shader gaussian_blur_3x3
compile_pixel_shader gaussian_blur_5x5
compile_pixel_shader gaussian_blur_7x7
compile_pixel_shader gaussian_blur_9x9
compile_pixel_shader gamma_correction_ps
compile_pixel_shader imgui_ps
compile_vertex_shader imgui_vs
compile_pixel_shader movie_ps
compile_vertex_shader movie_vs
compile_pixel_shader resolve_msaa_color_2x
compile_pixel_shader resolve_msaa_color_4x
compile_pixel_shader resolve_msaa_color_8x
compile_pixel_shader resolve_msaa_depth_2x
compile_pixel_shader resolve_msaa_depth_4x
compile_pixel_shader resolve_msaa_depth_8x
