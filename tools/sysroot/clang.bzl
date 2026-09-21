# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Macros to make Clang toolchains."""

load("@bazel_skylib//rules:native_binary.bzl", "native_binary")
load("//tools/cc:flags.bzl", "AARCH_FLAGS", "X86_FLAGS")
load("//tools/cc/toolchain:cc_toolchain_config.bzl", "cc_toolchain_config")
load("//tools/rules:cc.bzl", "cc_import", "cc_library", "cc_toolchain")

X86_64 = "x86_64"
AARCH64 = "aarch64"

GCC_VERSION = 10
CLANG_VERSION = 22

def format_versions(input):
    """Format the input string or list with the GCC and Clang versions.

    Args:
      input: A string or list of strings to format with GCC and Clang versions.

    Returns:
      The formatted string or list with GCC and Clang versions substituted.
    """
    if type(input) == "string":
        return input.format(gcc = GCC_VERSION, clang = CLANG_VERSION)
    elif type(input) == "list":
        return [i.format(gcc = GCC_VERSION, clang = CLANG_VERSION) for i in input]
    else:
        message = "Input must be a string or a list of strings. (Got: {})".format(type(input))
        fail(message)

_TOOL_PATHS = {k: format_versions(v) for k, v in {
    "ar": "usr/lib/llvm-{clang}/bin/llvm-ar",
    "as": "usr/lib/llvm-{clang}/bin/llvm-as",
    "cpp": "usr/lib/llvm-{clang}/bin/clang-cpp",
    "dwp": "usr/lib/llvm-{clang}/bin/llvm-dwp",
    "g++": "usr/lib/llvm-{clang}/bin/clang++",
    "gcc": "usr/lib/llvm-{clang}/bin/clang",
    "gcov": "usr/lib/llvm-{clang}/bin/llvm-gcov",
    "ld": "usr/lib/llvm-{clang}/bin/clang++",
    "llvm-cov": "usr/lib/llvm-{clang}/bin/llvm-cov",
    "llvm-profdata": "usr/lib/llvm-{clang}/bin/llvm-profdata",
    "nm": "usr/lib/llvm-{clang}/bin/llvm-nm",
    "objcopy": "usr/lib/llvm-{clang}/bin/llvm-objcopy",
    "objdump": "usr/lib/llvm-{clang}/bin/llvm-objdump",
    "ranlib": "usr/lib/llvm-{clang}/bin/llvm-ranlib",
    "readelf": "usr/lib/llvm-{clang}/bin/llvm-readelf",
    "strip": "usr/lib/llvm-{clang}/bin/llvm-strip",
}.items()}

# To determine these search paths, build the Clang sysroot Dockerfile, then run:
# $ docker run -it --entrypoint='' sysroot-builder:latest bash
# # /usr/lib/llvm-19/bin/clang++ -v -x c++ - < /dev/null
_INCLUDE_DIRS_X86_64 = format_versions([
    "usr/include/c++/{gcc}",
    "usr/include/x86_64-linux-gnu/c++/{gcc}",
    "usr/include/c++/{gcc}/backward",
    "usr/lib/llvm-{clang}/lib/clang/{clang}/include",
    "usr/include/x86_64-linux-gnu",
    "usr/include",
])

# As above, but with:
# # /usr/lib/llvm-19/bin/clang++ -v -x c++ --target=aarch64-unknown-linux-gnu - < /dev/null
_INCLUDE_DIRS_AARCH64 = format_versions([
    "usr/aarch64-linux-gnu/include/c++/{gcc}",
    "usr/aarch64-linux-gnu/include/c++/{gcc}/aarch64-linux-gnu",
    "usr/aarch64-linux-gnu/include/c++/{gcc}/backward",
    "usr/lib/llvm-{clang}/lib/clang/{clang}/include",
    "usr/aarch64-linux-gnu/include",
    "usr/include",
])

def _include_dirs(arch):
    if arch == X86_64:
        return _INCLUDE_DIRS_X86_64
    if arch == AARCH64:
        return _INCLUDE_DIRS_AARCH64
    fail("Unsupported architecture: {}".format(arch))

def libstdcxx_include_dirs(arch):
    """Return the libstdc++ include directories for an architecture."""
    return _include_dirs(arch)[:2]

# As above, but with:
# # echo 'int main() { return 0; }' > test.cc && /usr/lib/llvm-19/bin/clang++ -fuse-ld=lld -fsanitize=address -Wl,--verbose test.cc
_LINK_DIRS_X86_64 = format_versions([
    "usr/lib/llvm-{clang}/lib/clang/{clang}/lib/linux",
    "lib/x86_64-linux-gnu",
    "usr/lib/gcc/x86_64-linux-gnu/{gcc}",
    "usr/lib/x86_64-linux-gnu",
])

# As above, but with:
# # echo 'int main() { return 0; }' > test.cc && /usr/lib/llvm-19/bin/clang++ -fuse-ld=lld -Wl,--verbose --target=aarch64-unknown-linux-gnu test.cc
_LINK_DIRS_AARCH64 = format_versions([
    "usr/aarch64-linux-gnu/lib",
    "usr/lib/gcc-cross/aarch64-linux-gnu/{gcc}",
])

def _libclang_targets():
    # Use defines to avoid ifndef that adds ambiguity in != while iterating. cc_import can't handle that.
    # error: use of overloaded operator '!=' is ambiguous (with operand types 'iterator' (aka 'clang::UnresolvedSetIterator') and 'iterator')
    cc_library(
        name = "libllvm",
        hdrs = native.glob([
            "usr/include/llvm-{}/llvm/**".format(CLANG_VERSION),
            "usr/include/llvm-c-{}/llvm-c/**".format(CLANG_VERSION),
        ], allow_empty = False),
        visibility = ["//visibility:public"],
        includes = [
            "usr/include/llvm-{}".format(CLANG_VERSION),
            "usr/include/llvm-c-{}".format(CLANG_VERSION),
        ],
        deps = [":libllvm_import"],
    )

    cc_import(
        name = "libllvm_import",
        shared_library = "usr/lib/x86_64-linux-gnu/libLLVM.so.{}.{}".format(CLANG_VERSION, 1),
    )

    cc_import(
        name = "libffi.so",
        shared_library = "usr/lib/llvm-{}/lib/libffi.so.7".format(CLANG_VERSION),
    )

    cc_import(
        name = "libclang",
        hdrs = native.glob(["usr/lib/llvm-{}/include/clang-c/**".format(CLANG_VERSION)], allow_empty = False),
        shared_library = "usr/lib/llvm-{clang}/lib/libclang-{clang}.so.{clang}".format(clang = CLANG_VERSION),
        visibility = ["//visibility:public"],
        deps = [
            ":libffi.so",
            ":libllvm",
        ],
    )

    cc_import(
        name = "libclang-cpp.so",
        hdrs = native.glob(["usr/lib/llvm-{}/include/clang/**".format(CLANG_VERSION)], allow_empty = False),
        shared_library = "usr/lib/llvm-{clang}/lib/libclang-cpp.so.{clang}.1".format(clang = CLANG_VERSION),
        deps = [":libclang"],
    )

    # includes don't work right on cc_import, so wrap it in a cc_library
    cc_library(
        name = "libclang-cpp",
        visibility = ["//visibility:public"],
        includes = ["usr/lib/llvm-{}/include".format(CLANG_VERSION)],
        deps = [":libclang-cpp.so"],
    )

def _filegroups(arch, target_triple, CLANG_VERSION):
    include_dirs = _include_dirs(arch)

    native.filegroup(
        name = "libstdcxx_headers_" + target_triple,
        srcs = native.glob(
            [path + "/**" for path in libstdcxx_include_dirs(arch)],
            allow_empty = False,
        ),
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "llvm_compiler_files_" + target_triple,
        srcs = native.glob(
            [path + "/**" for path in include_dirs[2:] + [
                # Avoids the following build failure, see DX-2729:
                # this rule is missing dependency declarations for the following files included by 'src/liblzma/common/lzip_decoder.c':
                #  'external/clang+/usr/lib/llvm-21/lib/clang/22/share/asan_ignorelist.txt'
                "usr/lib/llvm-{clang}/lib/clang/{clang}/share".format(clang = CLANG_VERSION),
            ]],
            allow_empty = False,
        ) + [
            ":libstdcxx_headers_" + target_triple,
            "usr/lib/llvm-{}/bin/clang".format(CLANG_VERSION),
            "usr/lib/llvm-{}/bin/clang++".format(CLANG_VERSION),
            "usr/lib/llvm-{}/bin/clang-cpp".format(CLANG_VERSION),
            ":llvm_dependencies",
            ":sanitizer_compile_ignorelist",
        ],
    )

    native.filegroup(
        name = "llvm_ar_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/llvm-ar".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_as_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/clang".format(CLANG_VERSION),
            "usr/lib/llvm-{}/bin/llvm-as".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_dwp_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/llvm-dwp".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_gcov_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/llvm-gcov".format(CLANG_VERSION),
            "usr/lib/llvm-{}/bin/llvm-cov".format(CLANG_VERSION),
        ],
    )

    native.filegroup(
        name = "llvm_coverage_files_" + target_triple,
        srcs = [
            ":llvm_gcov_files_" + target_triple,
            "usr/lib/llvm-{}/bin/llvm-profdata".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_linker_files_" + target_triple,
        srcs = native.glob(
            [path + "/**" for path in (_LINK_DIRS_X86_64 if arch == X86_64 else _LINK_DIRS_AARCH64)],
            allow_empty = False,
        ) + [
            "usr/lib/llvm-{}/bin/clang++".format(CLANG_VERSION),
            "usr/lib/llvm-{}/bin/ld.lld".format(CLANG_VERSION),
            ":llvm_dependencies",
            ":sanitizer_compile_ignorelist",
        ],
    )

    native.filegroup(
        name = "llvm_objcopy_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/llvm-objcopy".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_strip_files_" + target_triple,
        srcs = [
            "usr/lib/llvm-{}/bin/llvm-strip".format(CLANG_VERSION),
            ":llvm_dependencies",
        ],
    )

    native.filegroup(
        name = "llvm_all_files_" + target_triple,
        srcs = [
            ":llvm_ar_files_" + target_triple,
            ":llvm_as_files_" + target_triple,
            ":llvm_compiler_files_" + target_triple,
            ":llvm_dwp_files_" + target_triple,
            ":llvm_coverage_files_" + target_triple,
            ":llvm_linker_files_" + target_triple,
            ":llvm_objcopy_files_" + target_triple,
            ":llvm_strip_files_" + target_triple,
        ],
        visibility = ["//visibility:public"],
    )

def _clang_binary_targets():
    native_binary(
        name = "llvm_cxxfilt",
        src = "usr/lib/llvm-{}/bin/llvm-cxxfilt".format(CLANG_VERSION),
        data = [":llvm_runtime_dependencies"],
        # Preserve the binary's location relative to usr/lib/llvm-*/lib because its RPATH is $ORIGIN/../lib.
        out = "usr/lib/llvm-{}/bin/llvm-cxxfilt".format(CLANG_VERSION),
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "llvm_objcopy",
        srcs = ["usr/lib/llvm-{}/bin/llvm-objcopy".format(CLANG_VERSION)],
        data = [":llvm_runtime_dependencies"],
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "llvm_strip",
        srcs = ["usr/lib/llvm-{}/bin/llvm-strip".format(CLANG_VERSION)],
        data = [":llvm_runtime_dependencies"],
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "llvm_symbolizer",
        srcs = ["usr/lib/llvm-{}/bin/llvm-symbolizer".format(CLANG_VERSION)],
        data = [":llvm_runtime_dependencies"],
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "llvm_runtime_dependencies",
        srcs = native.glob([
            "lib/x86_64-linux-gnu/libffi.so*",
            "usr/lib/llvm-{}/lib/libLLVM*".format(CLANG_VERSION),
            "usr/lib/llvm-{}/lib/libffi.so.7".format(CLANG_VERSION),
        ], allow_empty = False),
        visibility = ["//visibility:public"],
    )

def make_clang_targets(name):
    """Define Clang-based toolchains.

    Args:
        name: Unused - this is just to make Buildifier happy.
    """

    native.filegroup(
        name = "clang-tidy",
        srcs = ["usr/lib/llvm-{}/bin/clang-tidy".format(CLANG_VERSION)],
        data = [":llvm_dependencies"],
        visibility = ["//visibility:public"],
    )

    native.filegroup(
        name = "clangd",
        srcs = ["usr/lib/llvm-{}/bin/clangd".format(CLANG_VERSION)],
        data = [":llvm_dependencies"],
        visibility = ["//visibility:public"],
    )

    native_binary(
        name = "clang++",
        out = "clang++",
        src = "usr/lib/llvm-{}/bin/clang++".format(CLANG_VERSION),
        visibility = ["//visibility:public"],
    )

    _libclang_targets()
    _clang_binary_targets()

    native.label_flag(
        name = "sanitizer_compile_ignorelist",
        build_setting_default = ":sanitizer_compile_ignorelist_default",
        visibility = ["//visibility:public"],
    )

    native.filegroup(name = "sanitizer_compile_ignorelist_default")

    native.filegroup(
        name = "llvm_dependencies",
        srcs = native.glob(
            [
                "lib/x86_64-linux-gnu/libffi.so*",
                "lib/x86_64-linux-gnu/libedit.so*",
                "usr/lib/llvm-{}/lib/libclang-*.*".format(CLANG_VERSION),
                "usr/lib/llvm-{}/lib/libclang.*".format(CLANG_VERSION),
                "usr/lib/llvm-{}/lib/libedit.so.2".format(CLANG_VERSION),
                "usr/lib/llvm-{}/lib/libffi.so.7".format(CLANG_VERSION),
                "usr/lib/llvm-{}/lib/libLLVM*".format(CLANG_VERSION),
            ],
            allow_empty = False,
        ),
    )

    for arch in [AARCH64, X86_64]:
        target_triple = arch + "-gnu-linux"
        _filegroups(arch, target_triple, CLANG_VERSION)

        cc_config_name = "{}_cc_toolchain_config".format(target_triple)
        cc_toolchain_name = "_{}_clang_toolchain".format(target_triple)
        cc_toolchain(
            name = cc_toolchain_name,
            all_files = ":llvm_all_files_" + target_triple,
            ar_files = ":llvm_ar_files_" + target_triple,
            as_files = ":llvm_as_files_" + target_triple,
            compiler_files = ":llvm_compiler_files_" + target_triple,
            coverage_files = ":llvm_coverage_files_" + target_triple,
            dwp_files = ":llvm_dwp_files_" + target_triple,
            linker_files = ":llvm_linker_files_" + target_triple,
            objcopy_files = ":llvm_objcopy_files_" + target_triple,
            strip_files = ":llvm_strip_files_" + target_triple,
            supports_param_files = True,
            toolchain_config = cc_config_name,
            toolchain_identifier = "clang-toolchain-" + target_triple,
        )

        include_dirs = _INCLUDE_DIRS_X86_64 if arch == X86_64 else _INCLUDE_DIRS_AARCH64

        cc_toolchain_config(
            name = cc_config_name,
            # Use Ubuntu's dynamic linker on x86_64
            dynamic_linker = "/lib64/ld-linux-x86-64.so.2" if arch == X86_64 else "",
            sysroot = "external/clang+",
            target = arch + "-unknown-linux-gnu",
            target_flags = X86_FLAGS if arch == X86_64 else AARCH_FLAGS,
            tool_paths = _TOOL_PATHS,
            cxx_builtin_include_directories = ["%package(@@clang//)%/" + dir for dir in include_dirs] + ["../clang+/" + dir for dir in include_dirs],
            includes = ["%{sysroot}/" + dir for dir in include_dirs] +
                       [],
            gcc_install_dir = "%{{sysroot}}/usr/lib/gcc/x86_64-linux-gnu/{}".format(GCC_VERSION) if arch == X86_64 else "%{{sysroot}}/usr/lib/gcc-cross/aarch64-linux-gnu/{}".format(GCC_VERSION),
        )

        native.toolchain(
            name = "clang-" + target_triple,
            exec_compatible_with = [
                "@platforms//os:linux",
                "@platforms//cpu:x86_64",
            ],
            target_compatible_with = ["@platforms//cpu:" + arch],
            toolchain = cc_toolchain_name,
            toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
            visibility = ["//visibility:public"],
        )
