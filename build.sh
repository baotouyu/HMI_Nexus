#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="${script_dir}"
targets_dir="${project_root}/targets"
projects_file="${targets_dir}/projects.conf"
original_argc=$#

declare -A project_to_cmake_target=()
declare -A project_to_description=()
declare -a project_order=()
declare -a available_targets=()
declare -A target_to_arch=()
declare -A target_to_description=()

target_name=""
project_args=()
build_dir=""
default_build_jobs="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
build_jobs="${default_build_jobs}"
configure_only=0
clean_build=0
sdk_root_override=""
output_dir_override=""
toolchain_bin_override=""
sysroot_override=""
show_help=0
list_targets_only=0
list_projects_only=0
positional_projects=()
interactive_mode=0
prompt_choice_result=0
build_command_preview=""

TARGET_NAME=""
TARGET_ARCH=""
TARGET_DESCRIPTION=""
TARGET_HIDDEN="0"
TARGET_BUILD_PREFIX=""
TARGET_BUILD_DIR_DEFAULT=""
TARGET_TOOLCHAIN_FILE=""
TARGET_SDK_ROOT_VAR=""
TARGET_OUTPUT_DIR_VAR=""
TARGET_OUTPUT_DIR_DEFAULT=""
TARGET_TOOLCHAIN_BIN_VAR=""
TARGET_TOOLCHAIN_BIN_SUFFIX=""
TARGET_SYSROOT_VAR=""
TARGET_SYSROOT_SUFFIX=""
TARGET_NOTES=""

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "${value}"
}

load_projects() {
    if [[ ! -f "${projects_file}" ]]; then
        echo "Missing projects file: ${projects_file}" >&2
        exit 1
    fi

    while IFS='|' read -r raw_name raw_target raw_description; do
        local name
        name="$(trim "${raw_name:-}")"
        if [[ -z "${name}" || "${name}" == \#* ]]; then
            continue
        fi

        local cmake_target
        cmake_target="$(trim "${raw_target:-}")"
        local description
        description="$(trim "${raw_description:-}")"

        project_order+=("${name}")
        project_to_cmake_target["${name}"]="${cmake_target}"
        project_to_description["${name}"]="${description}"
    done < "${projects_file}"
}

load_targets_catalog() {
    available_targets=()
    target_to_arch=()
    target_to_description=()

    local name
    local arch
    local description
    while IFS='|' read -r name arch description; do
        if [[ -z "${name}" ]]; then
            continue
        fi

        available_targets+=("${name}")
        target_to_arch["${name}"]="${arch}"
        target_to_description["${name}"]="${description}"
    done < <(
        while IFS= read -r -d '' target_conf; do
            (
                # shellcheck disable=SC1090
                source "${target_conf}"
                if [[ "${TARGET_HIDDEN:-0}" == "1" ]]; then
                    exit 0
                fi
                printf '%s|%s|%s\n' \
                    "${TARGET_NAME}" \
                    "${TARGET_ARCH}" \
                    "${TARGET_DESCRIPTION}"
            )
        done < <(find "${targets_dir}" -mindepth 2 -maxdepth 2 -name target.conf -print0 | sort -z)
    )
}

list_available_targets() {
    local target
    load_targets_catalog
    for target in "${available_targets[@]}"; do
        printf '  %-16s [%s] %s\n' \
            "${target}" \
            "${target_to_arch[${target}]}" \
            "${target_to_description[${target}]}"
    done
}

list_available_projects() {
    local name
    for name in "${project_order[@]}"; do
        printf '  %-16s -> %-28s %s\n' \
            "${name}" \
            "${project_to_cmake_target[${name}]}" \
            "${project_to_description[${name}]}"
    done
    printf '  %-16s -> %-28s %s\n' "all" "all" "构建当前目标下的全部可执行项目"
}

usage() {
    cat <<'EOF'
用法:
  ./build.sh
  ./build.sh --target TARGET [options]
  ./build.sh --d211 [PROJECT] [options]
  ./build.sh --f133 [PROJECT] [options]
  ./build.sh --linux [PROJECT] [options]

常用选项:
  --target NAME         构建目标平台，例如 linux-host / d211-riscv64
  --d211                等价于 --target d211-riscv64
  --f133                等价于 --target f133-riscv64
  --linux               等价于 --target linux-host
  --project NAME        指定要编译的工程，可重复传入，也支持逗号分隔
                        也支持位置参数，例如 ./build.sh --d211 wifi_example
                        默认是 all
  --build-dir PATH      自定义构建目录，默认自动生成到 build/<目标>-<工程>
  --jobs N              并行编译线程数，默认取 nproc
  --configure-only      只执行 CMake 配置
  --clean               配置前删除当前构建目录

交叉编译相关:
  --sdk-root PATH       SDK 根目录
  --output-dir NAME     SDK 内的 output 子目录
  --toolchain-bin PATH  手动指定交叉编译工具 bin 目录
  --sysroot PATH        手动指定 sysroot

辅助选项:
  --list-targets        只列出当前可用目标
  --list-projects       只列出当前可用工程
  -h, --help            打印帮助

说明:
  不带参数执行时，会进入交互式菜单，依次选择平台和工程后开始构建
EOF

    echo
    echo "当前可用目标:"
    list_available_targets
    echo
    echo "当前可用工程:"
    list_available_projects
}

command_to_string() {
    local rendered=""
    printf -v rendered '%q ' "$@"
    printf '%s' "${rendered% }"
}

print_command() {
    echo "    $(command_to_string "$@")"
}

prompt_numbered_choice() {
    local prompt="$1"
    local -n labels_ref="$2"
    local choice=""
    local index=1
    local label

    while true; do
        echo "${prompt}"
        index=1
        for label in "${labels_ref[@]}"; do
            printf '  %2d) %s\n' "${index}" "${label}"
            ((index++))
        done

        if ! read -r -p "请输入序号: " choice; then
            echo "Interactive selection cancelled." >&2
            exit 1
        fi

        choice="$(trim "${choice}")"
        if [[ "${choice}" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#labels_ref[@]} )); then
            prompt_choice_result=$((choice - 1))
            return 0
        fi

        echo "请输入 1-${#labels_ref[@]} 之间的数字。"
        echo
    done
}

prompt_target_selection() {
    local target
    local target_labels=()

    load_targets_catalog
    if [[ ${#available_targets[@]} -eq 0 ]]; then
        echo "No visible targets found under ${targets_dir}." >&2
        exit 1
    fi

    for target in "${available_targets[@]}"; do
        target_labels+=("${target} [${target_to_arch[${target}]}] ${target_to_description[${target}]}")
    done

    prompt_numbered_choice "请选择编译平台:" target_labels
    target_name="${available_targets[${prompt_choice_result}]}"
    echo
}

prompt_project_selection() {
    local project
    local project_labels=()

    for project in "${project_order[@]}"; do
        project_labels+=(
            "${project} -> ${project_to_cmake_target[${project}]} ${project_to_description[${project}]}"
        )
    done
    project_labels+=("all -> all 构建当前目标下的全部可执行项目")

    prompt_numbered_choice "请选择工程:" project_labels
    if [[ ${prompt_choice_result} -eq ${#project_order[@]} ]]; then
        project_args=("all")
    else
        project_args=("${project_order[${prompt_choice_result}]}")
    fi
    echo
}

prompt_sdk_root_if_needed() {
    if [[ -z "${TARGET_SDK_ROOT_VAR}" ]]; then
        return
    fi

    local current_sdk_root="${sdk_root_override:-${!TARGET_SDK_ROOT_VAR:-}}"
    if [[ -n "${current_sdk_root}" ]]; then
        return
    fi

    if [[ -n "${TARGET_NOTES}" ]]; then
        echo "${TARGET_NOTES}"
    fi

    while true; do
        if ! read -r -p "请输入 ${TARGET_SDK_ROOT_VAR}: " sdk_root_override; then
            echo "Interactive selection cancelled." >&2
            exit 1
        fi

        sdk_root_override="$(trim "${sdk_root_override}")"
        if [[ -n "${sdk_root_override}" ]]; then
            return 0
        fi

        echo "${TARGET_SDK_ROOT_VAR} 不能为空。"
    done
}

resolve_output_path() {
    local cmake_target_name="$1"
    local candidate=""
    local candidates=(
        "${resolved_build_dir}/${cmake_target_name}"
        "${resolved_build_dir}/bin/${cmake_target_name}"
        "${resolved_build_dir}/${cmake_target_name}.exe"
        "${resolved_build_dir}/bin/${cmake_target_name}.exe"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -f "${candidate}" ]]; then
            printf '%s' "${candidate}"
            return 0
        fi
    done

    candidate="$(
        find "${resolved_build_dir}" -maxdepth 3 -type f \
            \( -name "${cmake_target_name}" -o -name "${cmake_target_name}.exe" \) \
            | sort | head -n 1
    )"
    if [[ -n "${candidate}" ]]; then
        printf '%s' "${candidate}"
        return 0
    fi

    return 1
}

show_output_paths() {
    local -a report_targets=()
    local target=""
    local output_path=""

    if [[ ${selected_all} -eq 1 || ${#selected_cmake_targets[@]} -eq 0 ]]; then
        local project
        for project in "${project_order[@]}"; do
            report_targets+=("${project_to_cmake_target[${project}]}")
        done
    else
        report_targets=("${selected_cmake_targets[@]}")
    fi

    echo "==> Output paths"
    echo "    Build dir     : ${resolved_build_dir}"
    for target in "${report_targets[@]}"; do
        output_path="$(resolve_output_path "${target}" || true)"
        if [[ -n "${output_path}" ]]; then
            echo "    Executable    : ${output_path}"
        fi
    done
}

prepare_build_command_preview() {
    local preview_cmd=("./build.sh" "--target" "${target_name}")
    local project

    if [[ ${#resolved_projects[@]} -eq 0 ]]; then
        preview_cmd+=("--project" "all")
    else
        for project in "${resolved_projects[@]}"; do
            preview_cmd+=("--project" "${project}")
        done
    fi

    if [[ -n "${build_dir}" ]]; then
        preview_cmd+=("--build-dir" "${build_dir}")
    fi
    if [[ "${build_jobs}" != "${default_build_jobs}" ]]; then
        preview_cmd+=("--jobs" "${build_jobs}")
    fi
    if [[ "${configure_only}" == "1" ]]; then
        preview_cmd+=("--configure-only")
    fi
    if [[ "${clean_build}" == "1" ]]; then
        preview_cmd+=("--clean")
    fi
    if [[ -n "${sdk_root_override}" ]]; then
        preview_cmd+=("--sdk-root" "${sdk_root_override}")
    fi
    if [[ -n "${output_dir_override}" ]]; then
        preview_cmd+=("--output-dir" "${output_dir_override}")
    fi
    if [[ -n "${toolchain_bin_override}" ]]; then
        preview_cmd+=("--toolchain-bin" "${toolchain_bin_override}")
    fi
    if [[ -n "${sysroot_override}" ]]; then
        preview_cmd+=("--sysroot" "${sysroot_override}")
    fi

    build_command_preview="$(command_to_string "${preview_cmd[@]}")"
}

parse_project_args() {
    local raw
    for raw in "${project_args[@]}"; do
        IFS=',' read -r -a split_projects <<< "${raw}"
        local item
        for item in "${split_projects[@]}"; do
            item="$(trim "${item}")"
            if [[ -n "${item}" ]]; then
                resolved_projects+=("${item}")
            fi
        done
    done
}

load_target() {
    local target_conf="${targets_dir}/${target_name}/target.conf"
    if [[ ! -f "${target_conf}" ]]; then
        echo "Unknown target: ${target_name}" >&2
        echo >&2
        usage >&2
        exit 1
    fi

    # shellcheck disable=SC1090
    source "${target_conf}"
}

resolve_target_build_dir() {
    if [[ -n "${build_dir}" ]]; then
        resolved_build_dir="${build_dir}"
        return
    fi

    if [[ -n "${TARGET_BUILD_DIR_DEFAULT}" ]]; then
        resolved_build_dir="${project_root}/${TARGET_BUILD_DIR_DEFAULT}"
        return
    fi

    local build_prefix="${TARGET_BUILD_PREFIX:-${TARGET_NAME}}"
    local build_project_token="all"

    if [[ ${selected_all} -eq 1 ]]; then
        build_project_token="all"
    elif [[ ${#selected_cmake_targets[@]} -eq 1 ]]; then
        build_project_token="${selected_cmake_targets[0]}"
    elif [[ ${#selected_cmake_targets[@]} -gt 1 ]]; then
        build_project_token="multi"
    fi

    resolved_build_dir="${project_root}/build/${build_prefix}-${build_project_token}"
}

append_cross_compile_args() {
    if [[ -z "${TARGET_SDK_ROOT_VAR}" ]]; then
        return
    fi

    local sdk_root="${sdk_root_override:-${!TARGET_SDK_ROOT_VAR:-}}"
    if [[ -z "${sdk_root}" ]]; then
        echo "Target ${TARGET_NAME} requires --sdk-root or environment ${TARGET_SDK_ROOT_VAR}." >&2
        exit 1
    fi

    local output_dir="${output_dir_override:-}"
    if [[ -z "${output_dir}" && -n "${TARGET_OUTPUT_DIR_VAR}" ]]; then
        output_dir="${!TARGET_OUTPUT_DIR_VAR:-}"
    fi
    if [[ -z "${output_dir}" ]]; then
        output_dir="${TARGET_OUTPUT_DIR_DEFAULT}"
    fi

    local toolchain_bin="${toolchain_bin_override:-}"
    if [[ -z "${toolchain_bin}" && -n "${TARGET_TOOLCHAIN_BIN_VAR}" ]]; then
        toolchain_bin="${!TARGET_TOOLCHAIN_BIN_VAR:-}"
    fi
    if [[ -z "${toolchain_bin}" && -n "${TARGET_TOOLCHAIN_BIN_SUFFIX}" ]]; then
        toolchain_bin="${sdk_root}/${output_dir}/${TARGET_TOOLCHAIN_BIN_SUFFIX}"
    fi

    local sysroot="${sysroot_override:-}"
    if [[ -z "${sysroot}" && -n "${TARGET_SYSROOT_VAR}" ]]; then
        sysroot="${!TARGET_SYSROOT_VAR:-}"
    fi
    if [[ -z "${sysroot}" && -n "${TARGET_SYSROOT_SUFFIX}" ]]; then
        sysroot="${sdk_root}/${output_dir}/${TARGET_SYSROOT_SUFFIX}"
    fi

    cmake_args+=(
        "-D${TARGET_SDK_ROOT_VAR}=${sdk_root}"
    )

    if [[ -n "${TARGET_OUTPUT_DIR_VAR}" ]]; then
        cmake_args+=("-D${TARGET_OUTPUT_DIR_VAR}=${output_dir}")
    fi
    if [[ -n "${TARGET_TOOLCHAIN_BIN_VAR}" && -n "${toolchain_bin}" ]]; then
        cmake_args+=("-D${TARGET_TOOLCHAIN_BIN_VAR}=${toolchain_bin}")
    fi
    if [[ -n "${TARGET_SYSROOT_VAR}" && -n "${sysroot}" ]]; then
        cmake_args+=("-D${TARGET_SYSROOT_VAR}=${sysroot}")
    fi

    build_summary+=("SDK root      : ${sdk_root}")
    if [[ -n "${output_dir}" ]]; then
        build_summary+=("Output dir    : ${output_dir}")
    fi
    if [[ -n "${toolchain_bin}" ]]; then
        build_summary+=("Toolchain bin : ${toolchain_bin}")
    fi
    if [[ -n "${sysroot}" ]]; then
        build_summary+=("Sysroot       : ${sysroot}")
    fi
}

resolve_projects() {
    if [[ ${#resolved_projects[@]} -eq 0 ]]; then
        resolved_projects=("all")
        return
    fi

    local project
    local cmake_target
    for project in "${resolved_projects[@]}"; do
        if [[ "${project}" == "all" ]]; then
            selected_all=1
            continue
        fi

        if [[ -n "${project_to_cmake_target[${project}]:-}" ]]; then
            cmake_target="${project_to_cmake_target[${project}]}"
        else
            cmake_target="${project}"
        fi

        selected_projects+=("${project}")
        selected_cmake_targets+=("${cmake_target}")
    done

    if [[ ${selected_all} -eq 1 ]]; then
        selected_projects=("all")
        selected_cmake_targets=()
    fi
}

show_build_summary() {
    echo "==> Build summary"
    local line
    for line in "${build_summary[@]}"; do
        echo "    ${line}"
    done
}

load_projects

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            target_name="$2"
            shift 2
            ;;
        --d211)
            target_name="d211-riscv64"
            shift
            ;;
        --f133)
            target_name="f133-riscv64"
            shift
            ;;
        --linux)
            target_name="linux-host"
            shift
            ;;
        --project)
            project_args+=("$2")
            shift 2
            ;;
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        --jobs)
            build_jobs="$2"
            shift 2
            ;;
        --configure-only)
            configure_only=1
            shift
            ;;
        --clean)
            clean_build=1
            shift
            ;;
        --sdk-root)
            sdk_root_override="$2"
            shift 2
            ;;
        --output-dir)
            output_dir_override="$2"
            shift 2
            ;;
        --toolchain-bin)
            toolchain_bin_override="$2"
            shift 2
            ;;
        --sysroot)
            sysroot_override="$2"
            shift 2
            ;;
        --list-targets)
            list_targets_only=1
            shift
            ;;
        --list-projects)
            list_projects_only=1
            shift
            ;;
        -h|--help)
            show_help=1
            shift
            ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                positional_projects+=("$1")
                shift
            done
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo >&2
            usage >&2
            exit 1
            ;;
        *)
            positional_projects+=("$1")
            shift
            ;;
    esac
done

if [[ ${#positional_projects[@]} -gt 0 ]]; then
    project_args+=("${positional_projects[@]}")
fi

if [[ ${show_help} -eq 1 ]]; then
    usage
    exit 0
fi

if [[ ${list_targets_only} -eq 1 ]]; then
    list_available_targets
    exit 0
fi

if [[ ${list_projects_only} -eq 1 ]]; then
    list_available_projects
    exit 0
fi

if [[ ${original_argc} -eq 0 ]]; then
    interactive_mode=1
    echo "==> 进入交互式构建"
    echo
    prompt_target_selection
    prompt_project_selection
fi

if [[ -z "${target_name}" ]]; then
    echo "Missing --target." >&2
    echo >&2
    usage >&2
    exit 1
fi

load_target

if [[ ${interactive_mode} -eq 1 ]]; then
    prompt_sdk_root_if_needed
fi

declare -a resolved_projects=()
declare -a selected_projects=()
declare -a selected_cmake_targets=()
declare -a cmake_args=()
declare -a build_summary=()
selected_all=0

parse_project_args
resolve_projects
resolve_target_build_dir
prepare_build_command_preview

if [[ "${clean_build}" == "1" ]]; then
    rm -rf "${resolved_build_dir}"
fi

cmake_args=(
    -S "${project_root}"
    -B "${resolved_build_dir}"
)

if [[ -n "${TARGET_TOOLCHAIN_FILE}" ]]; then
    cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=${project_root}/${TARGET_TOOLCHAIN_FILE}")
fi

append_cross_compile_args

build_summary+=("Target        : ${TARGET_NAME} (${TARGET_ARCH})")
build_summary+=("Build dir     : ${resolved_build_dir}")
if [[ ${#selected_projects[@]} -eq 0 || ${selected_all} -eq 1 ]]; then
    build_summary+=("Projects      : all")
else
    build_summary+=("Projects      : ${selected_projects[*]}")
fi
build_summary+=("Jobs          : ${build_jobs}")
if [[ -n "${TARGET_NOTES}" ]]; then
    build_summary+=("Notes         : ${TARGET_NOTES}")
fi

show_build_summary
echo "==> Build command"
echo "    ${build_command_preview}"

echo "==> Configuring"
print_command cmake "${cmake_args[@]}"
cmake "${cmake_args[@]}"

if [[ ${configure_only} -eq 1 ]]; then
    exit 0
fi

echo "==> Building"
if [[ ${selected_all} -eq 1 || ${#selected_cmake_targets[@]} -eq 0 ]]; then
    build_cmd=(cmake --build "${resolved_build_dir}" -j"${build_jobs}")
    print_command "${build_cmd[@]}"
    "${build_cmd[@]}"
else
    for cmake_target_name in "${selected_cmake_targets[@]}"; do
        echo "    -> ${cmake_target_name}"
        build_cmd=(
            cmake --build "${resolved_build_dir}" -j"${build_jobs}" --target "${cmake_target_name}"
        )
        print_command "${build_cmd[@]}"
        "${build_cmd[@]}"
    done
fi

show_output_paths
