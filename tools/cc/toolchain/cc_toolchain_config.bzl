# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""A cc_toolchain configuration rule for Clang."""

load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES", "ACTION_NAME_GROUPS", "ALL_CC_COMPILE_ACTION_NAMES", "ALL_CC_LINK_ACTION_NAMES", "ALL_CPP_COMPILE_ACTION_NAMES")
load(
    "@rules_cc//cc:cc_toolchain_config_lib.bzl",
    "action_config",
    "feature",
    "feature_set",
    "flag_group",
    "flag_set",
    "tool",
    "tool_path",
    "variable_with_value",
    "with_feature_set",
)
load("@rules_cc//cc:defs.bzl", "CcToolchainConfigInfo")
load("//tools/cc:flags.bzl", "CLANG_CXX_WARNING_FLAGS", "CLANG_C_WARNING_FLAGS", "CLANG_MISC_FLAGS")

all_c_compile_actions = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.assemble,
    ACTION_NAMES.preprocess_assemble,
]

preprocessor_compile_actions = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.preprocess_assemble,
    ACTION_NAMES.cpp_header_parsing,
    ACTION_NAMES.cpp_module_compile,
]

codegen_compile_actions = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.assemble,
    ACTION_NAMES.preprocess_assemble,
    ACTION_NAMES.cpp_module_codegen,
    ACTION_NAMES.lto_backend,
]

def _is_aarch64(ctx):
    """Check if the target CPU is aarch64."""
    return _get_arch(ctx) == "aarch64"

def _is_x86_64(ctx):
    """Check if the target CPU is aarch64."""
    return _get_arch(ctx) == "x86_64"

def _get_arch(ctx):
    return ctx.attr.target.split("-")[0] if ctx.attr.target else "unknown"

def _cc_toolchain_config_impl(ctx):
    tool_paths = [tool_path(name = name, path = path) for name, path in ctx.attr.tool_paths.items()]

    action_configs = [
        action_config(action_name = name, enabled = True, tools = [tool(path = ctx.attr.tool_paths["gcc"])])
        for name in all_c_compile_actions
    ] + [
        action_config(action_name = name, enabled = True, tools = [tool(path = ctx.attr.tool_paths["g++"])])
        for name in ALL_CPP_COMPILE_ACTION_NAMES
    ] + [
        action_config(action_name = name, enabled = True, tools = [tool(path = ctx.attr.tool_paths["ld"])])
        for name in ALL_CC_LINK_ACTION_NAMES
    ] + [
        action_config(action_name = name, enabled = True, tools = [tool(path = ctx.attr.tool_paths["ar"])])
        for name in [ACTION_NAMES.cpp_link_static_library]
    ] + [
        action_config(action_name = name, enabled = True, tools = [tool(path = ctx.attr.tool_paths["strip"])])
        for name in [ACTION_NAMES.strip]
    ]

    system_include_paths = []
    for path in ctx.attr.includes:
        system_include_paths.extend(["-isystem", path])

    default_flags_feature = feature(
        name = "default_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CPP_COMPILE_ACTION_NAMES,
                flag_groups = [flag_group(flags = CLANG_CXX_WARNING_FLAGS)],
            ),
            flag_set(
                actions = all_c_compile_actions,
                flag_groups = [flag_group(flags = CLANG_C_WARNING_FLAGS)],
            ),
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = CLANG_MISC_FLAGS)],
            ),
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = [
                            # Point LLVM to a location where it can find GCC
                            "--gcc-toolchain=%{sysroot}/usr",
                        ],
                    ),
                    flag_group(
                        expand_if_available = "output_assembly_file",
                        flags = ["-S"],
                    ),
                    flag_group(
                        expand_if_available = "output_preprocess_file",
                        flags = ["-E"],
                    ),
                    flag_group(
                        flags = ["-MD", "-MF", "%{dependency_file}"],
                        expand_if_available = "dependency_file",
                    ),
                    flag_group(
                        flags = ["-frandom-seed=%{output_file}"],
                        expand_if_available = "output_file",
                    ),
                ],
            ),
            flag_set(
                actions = ALL_CPP_COMPILE_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-std=c++20"])],
            ),
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            "-ffunction-sections",
                            "-fdata-sections",
                        ],
                    ),
                ],
            ),
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [flag_group(flags = ["-fPIC"], expand_if_available = "pic")],
            ),
            flag_set(
                actions = preprocessor_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            # Disable a warning and override builtin macros to
                            # ensure a hermetic build.
                            "-Wno-builtin-macro-redefined",
                            "-D__DATE__=\"redacted\"",
                            "-D__TIMESTAMP__=\"redacted\"",
                            "-D__TIME__=\"redacted\"",
                        ],
                    ),
                    flag_group(
                        flags = ["-D%{preprocessor_defines}"],
                        iterate_over = "preprocessor_defines",
                    ),
                    flag_group(
                        flags = ["-include", "%{includes}"],
                        iterate_over = "includes",
                        expand_if_available = "includes",
                    ),
                    flag_group(
                        flags = ["-iquote", "%{quote_include_paths}"],
                        iterate_over = "quote_include_paths",
                    ),
                    flag_group(
                        flags = ["-I%{include_paths}"],
                        iterate_over = "include_paths",
                    ),
                    flag_group(flags = system_include_paths),
                    flag_group(
                        flags = ["-isystem", "%{system_include_paths}"],
                        iterate_over = "system_include_paths",
                    ),
                ],
            ),
            flag_set(
                actions = [
                    ACTION_NAMES.cpp_link_dynamic_library,
                    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
                    ACTION_NAMES.lto_index_for_dynamic_library,
                    ACTION_NAMES.lto_index_for_nodeps_dynamic_library,
                ],
                flag_groups = [flag_group(flags = ["-shared"])],
            ),
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = ["-Wl,--gdb-index"],
                        expand_if_available = "is_using_fission",
                    ),
                    flag_group(
                        flags = ["-Wl,-S"],
                        expand_if_available = "strip_debug_symbols",
                    ),
                    flag_group(
                        flags = [
                            # Compute build-id from MD5 of inputs
                            "-Wl,--build-id=md5",
                            "-Wl,--hash-style=gnu",
                            # Search paths paths
                            "--gcc-install-dir=" + ctx.attr.gcc_install_dir,
                            "-L%{sysroot}/lib",
                            "-L%{sysroot}/usr/lib/",
                        ] + (["-Wl,--dynamic-linker=" + ctx.attr.dynamic_linker] if ctx.attr.dynamic_linker else []),
                    ),
                    flag_group(
                        flags = ["-L%{library_search_directories}"],
                        iterate_over = "library_search_directories",
                        expand_if_available = "library_search_directories",
                    ),
                    flag_group(
                        iterate_over = "runtime_library_search_directories",
                        flags = ["-Wl,-rpath,$ORIGIN/%{runtime_library_search_directories}"],
                        expand_if_available =
                            "runtime_library_search_directories",
                    ),
                ],
            ),
        ],
    )

    aarch64_link_flags = feature(
        name = "aarch64_link_flags",
        enabled = _is_aarch64(ctx),
        flag_sets = [
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-L%{sysroot}/usr/aarch64-linux-gnu/lib/"])],
            ),
        ],
    )

    x86_64_link_flags = feature(
        name = "x86_64_link_flags",
        enabled = _is_x86_64(ctx),
        flag_sets = [
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-L%{sysroot}/usr/lib/x86_64-linux-gnu/"])],
            ),
        ],
    )

    # Handle different levels of optimization with individual features so that
    # they can be ordered and the defaults can override the minimal settings if
    # both are enabled.
    minimal_optimization_flags = feature(
        name = "minimal_optimization_flags",
        flag_sets = [
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [flag_group(flags = ["-O1"])],
            ),
        ],
    )
    default_optimization_flags = feature(
        name = "default_optimization_flags",
        enabled = True,
        requires = [feature_set(["opt"]), feature_set(["fastbuild"])],
        flag_sets = [
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-DNDEBUG"])],
            ),
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [flag_group(flags = ["-O2"])],
            ),
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-Wl,--gc-sections"])],
            ),
        ],
    )

    target_specific_flags_feature = feature(
        name = "target_specific_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["--target=" + ctx.attr.target] + ctx.attr.target_flags)],
            ),
        ],
    )

    thinlto_feature = feature(
        name = "thin_lto",
        flag_sets = [
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES,
                flag_groups = [
                    flag_group(flags = ["-flto=thin"]),
                    flag_group(
                        expand_if_available = "lto_indexing_bitcode_file",
                        flags = [
                            "-Xclang",
                            "-fthin-link-bitcode=%{lto_indexing_bitcode_file}",
                        ],
                    ),
                ],
            ),
            flag_set(
                actions = [ACTION_NAMES.linkstamp_compile],
                flag_groups = [flag_group(flags = ["-DBUILD_LTO_TYPE=thin"])],
            ),
            flag_set(
                actions = [
                    ACTION_NAMES.lto_index_for_executable,
                    ACTION_NAMES.lto_index_for_dynamic_library,
                    ACTION_NAMES.lto_index_for_nodeps_dynamic_library,
                ],
                flag_groups = [
                    flag_group(flags = [
                        "-flto=thin",
                        "-Wl,-plugin-opt,thinlto-index-only%{thinlto_optional_params_file}",
                        "-Wl,-plugin-opt,thinlto-emit-imports-files",
                        "-Wl,-plugin-opt,thinlto-prefix-replace=%{thinlto_prefix_replace}",
                    ]),
                    flag_group(
                        expand_if_available = "thinlto_object_suffix_replace",
                        flags = ["-Wl,-plugin-opt,thinlto-object-suffix-replace=%{thinlto_object_suffix_replace}"],
                    ),
                    flag_group(
                        expand_if_available = "thinlto_merged_object_file",
                        flags = ["-Wl,-plugin-opt,obj-path=%{thinlto_merged_object_file}"],
                    ),
                ],
            ),
            flag_set(
                actions = [ACTION_NAMES.lto_backend],
                flag_groups = [
                    flag_group(flags = [
                        "-c",
                        "-fthinlto-index=%{thinlto_index}",
                        "-o",
                        "%{thinlto_output_object_file}",
                        "-x",
                        "ir",
                        "%{thinlto_input_bitcode_file}",
                    ]),
                ],
            ),
        ],
    )

    per_object_debug_info_feature = feature(
        name = "per_object_debug_info",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            # Create .dwp and .dwo objects
                            "-gsplit-dwarf",
                            # Generate symbols even if `-c dbg` isn't set to avoid errors like `output '_objs/foo.dwo' was not created`
                            "-ggdb",
                        ],
                        expand_if_available = "per_object_debug_info_file",
                    ),
                ],
            ),
        ],
    )

    # Handle different levels and forms of debug info emission with individual
    # features so that they can be ordered and the defaults can override the
    # minimal settings if both are enabled.
    minimal_debug_info_flags = feature(
        name = "minimal_debug_info_flags",
        flag_sets = [
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [flag_group(flags = ["-g1"])],
            ),
        ],
    )
    default_debug_info_flags = feature(
        name = "default_debug_info_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = codegen_compile_actions,
                flag_groups = [flag_group(flags = [
                    "-ggdb",
                    "-gdwarf-4",
                    "-gz",  # Compress debug information
                ])],
                with_features = [with_feature_set(features = ["dbg"])],
            ),
        ],
    )

    # This feature can be enabled in conjunction with any optimizations to
    # ensure accurate call stacks and backtraces for profilers or errors.
    preserve_call_stacks = feature(
        name = "preserve_call_stacks",
        flag_sets = [flag_set(
            actions = codegen_compile_actions,
            flag_groups = [flag_group(flags = [
                # Ensure good backtraces by preserving frame pointers and
                # disabling tail call elimination.
                "-fno-omit-frame-pointer",
                "-mno-omit-leaf-frame-pointer",
                "-fno-optimize-sibling-calls",
            ])],
        )],
    )

    sysroot_feature = feature(
        name = "sysroot",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = ["--sysroot=%{sysroot}"],
                        expand_if_available = "sysroot",
                    ),
                ],
            ),
        ],
    )

    use_module_maps = feature(
        name = "use_module_maps",
        requires = [feature_set(features = ["module_maps"])],
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                    ACTION_NAMES.cpp_header_parsing,
                    ACTION_NAMES.cpp_module_compile,
                ],
                flag_groups = [
                    # These flag groups are separate so they do not expand to
                    # the cross product of the variables.
                    flag_group(flags = ["-fmodule-name=%{module_name}"]),
                    flag_group(flags = ["-fmodule-map-file=%{module_map_file}"]),
                ],
            ),
        ],
    )

    # Tell bazel we support module maps in general, so they will be generated
    # for all c/c++ rules.
    # Note: not all C++ rules support module maps; thus, do not imply this
    # feature from other features - instead, require it.
    module_maps = feature(
        name = "module_maps",
        enabled = True,
        implies = [
            # "module_map_home_cwd",
            # "module_map_without_extern_module",
            # "generate_submodules",
        ],
    )

    layering_check = feature(
        name = "layering_check",
        implies = ["use_module_maps"],
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                    ACTION_NAMES.cpp_header_parsing,
                    ACTION_NAMES.cpp_module_compile,
                ],
                flag_groups = [
                    flag_group(flags = [
                        "-fmodules-strict-decluse",
                        "-Wprivate-header",
                    ]),
                    flag_group(
                        iterate_over = "dependent_module_map_files",
                        flags = ["-fmodule-map-file=%{dependent_module_map_files}"],
                    ),
                ],
            ),
        ],
    )

    sanitizer_common_flags = feature(
        name = "sanitizer_common_flags",
        implies = ["minimal_optimization_flags", "minimal_debug_info_flags", "preserve_call_stacks"],
        flag_sets = [flag_set(
            actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
            flag_groups = [flag_group(flags = [
                # Compile/link-time ignorelist, mainly to get around 3p violations.
                "-fsanitize-ignorelist=tools/cc/sanitizer/sanitizer_compile_ignorelist.txt",
            ])],
        )],
    )

    # Separated from the feature above so it can only be included on platforms
    # where it is supported. There is no negative flag in Clang so we can't just
    # override it later.
    sanitizer_static_lib_flags = feature(
        name = "sanitizer_static_lib_flags",
        enabled = True,
        requires = [feature_set(["sanitizer_common_flags"])],
        flag_sets = [flag_set(
            actions = ALL_CC_LINK_ACTION_NAMES,
            flag_groups = [flag_group(flags = ["-static-libsan"])],
        )],
    )

    address_and_undefined_behavior_sanitizers_feature = feature(
        name = "address_and_undefined_behavior_sanitizers",
        implies = ["sanitizer_common_flags"],
        flag_sets = [flag_set(
            actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
            flag_groups = [flag_group(flags = [
                "-fsanitize=address,undefined,nullability",
                "-fsanitize-address-use-after-scope",
                # We don't need the recovery behavior of UBSan as we expect
                # builds to be clean. Not recovering is a bit cheaper.
                "-fno-sanitize-recover=undefined",
                # Don't embed the full path name for files. This limits the size
                # and combined with line numbers is unlikely to result in many
                # ambiguities.
                "-fsanitize-undefined-strip-path-components=-1",
                # Needed due to clang AST issues, such as in
                # clang/AST/Redeclarable.h line 199.
                "-fno-sanitize=vptr",
                "-Wno-nullability-extension",  # Required for absl
            ])],
        )],
    )

    thread_sanitizer_feature = feature(
        name = "thread_sanitizer",
        implies = ["sanitizer_common_flags"],
        flag_sets = [flag_set(
            actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
            flag_groups = [flag_group(flags = [
                "-fsanitize=thread",
                "-Wno-nullability-extension",  # Required for absl
            ])],
        )],
    )

    fuzzer = feature(
        name = "fuzzer",
        implies = ["address_and_undefined_behavior_sanitizers"],
        flag_sets = [flag_set(
            actions = ALL_CC_COMPILE_ACTION_NAMES + ALL_CC_LINK_ACTION_NAMES,
            flag_groups = [flag_group(flags = ["-fsanitize=fuzzer-no-link"])],
        )],
    )

    linux_flags_feature = feature(
        name = "linux_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [flag_group(flags = ["-fuse-ld=lld"])],
            ),
            flag_set(
                actions = [ACTION_NAMES.cpp_link_executable, ACTION_NAMES.lto_index_for_executable],
                flag_groups = [
                    flag_group(
                        flags = ["-no-pie"],
                        expand_if_not_available = "force_pic",
                    ),
                    flag_group(
                        flags = ["-pie"],
                        expand_if_available = "force_pic",
                    ),
                ],
            ),
        ],
    )

    default_link_libraries_feature = feature(
        name = "default_link_libraries",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = ["%{linkstamp_paths}"],
                        iterate_over = "linkstamp_paths",
                        expand_if_available = "linkstamp_paths",
                    ),
                    flag_group(
                        iterate_over = "libraries_to_link",
                        flag_groups = [
                            flag_group(
                                flags = ["-Wl,--start-lib"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file_group",
                                ),
                            ),
                            flag_group(
                                flags = ["-Wl,-whole-archive"],
                                expand_if_true =
                                    "libraries_to_link.is_whole_archive",
                            ),
                            flag_group(
                                flags = ["%{libraries_to_link.object_files}"],
                                iterate_over = "libraries_to_link.object_files",
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file_group",
                                ),
                            ),
                            flag_group(
                                flags = ["%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file",
                                ),
                            ),
                            flag_group(
                                flags = ["%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "interface_library",
                                ),
                            ),
                            flag_group(
                                flags = ["%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "static_library",
                                ),
                            ),
                            flag_group(
                                flags = ["-l%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "dynamic_library",
                                ),
                            ),
                            flag_group(
                                flags = ["-l:%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "versioned_dynamic_library",
                                ),
                            ),
                            flag_group(
                                flags = ["-Wl,-no-whole-archive"],
                                expand_if_true = "libraries_to_link.is_whole_archive",
                            ),
                            flag_group(
                                flags = ["-Wl,--end-lib"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file_group",
                                ),
                            ),
                        ],
                        expand_if_available = "libraries_to_link",
                    ),
                    # Note that the params file comes at the end, after the
                    # libraries to link above.
                    flag_group(
                        expand_if_available = "linker_param_file",
                        flags = ["@%{linker_param_file}"],
                    ),
                    flag_group(
                        flags = ["-Wl,@%{thinlto_param_file}"],
                        expand_if_true = "thinlto_param_file",
                    ),
                ],
            ),
        ],
    )

    # Place user provided compile flags after all the features so that these
    # flags can override or customize behavior. The only thing user flags
    # cannot override is the output file as Bazel depends on that.
    #
    # Finally, place the source file (if present) and output file last to make
    # reading the compile command lines easier for humans.
    final_flags_feature = feature(
        name = "final_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ALL_CC_COMPILE_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = ["%{user_compile_flags}"],
                        iterate_over = "user_compile_flags",
                        expand_if_available = "user_compile_flags",
                    ),
                    flag_group(
                        flags = [
                            # Compile actions shouldn't link anything.
                            "-c",
                            "%{source_file}",
                        ],
                        expand_if_available = "source_file",
                    ),
                    flag_group(
                        flags = ["-o", "%{output_file}"],
                        expand_if_available = "output_file",
                    ),
                ],
            ),
            flag_set(
                actions = ALL_CC_LINK_ACTION_NAMES,
                flag_groups = [
                    flag_group(
                        flags = ["%{user_link_flags}"],
                        iterate_over = "user_link_flags",
                        expand_if_available = "user_link_flags",
                    ),
                    flag_group(
                        flags = ["-o", "%{output_execpath}"],
                        expand_if_available = "output_execpath",
                    ),
                ],
            ),
            flag_set(
                actions = [ACTION_NAMES.strip],
                flag_groups = [
                    flag_group(
                        flags = ["%{stripopts}"],
                        iterate_over = "stripopts",
                    ),
                    flag_group(
                        flags = ["-o", "%{output_file}"],
                        expand_if_available = "output_file",
                    ),
                    flag_group(flags = ["%{input_file}"]),
                ],
            ),
        ],
    )

    # Archive actions have an entirely independent set of flags and don't
    # interact with either compiler or link actions.
    default_archiver_flags_feature = feature(
        name = "default_archiver_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [ACTION_NAMES.cpp_link_static_library],
                flag_groups = [
                    flag_group(flags = ["rcsD"]),
                    flag_group(
                        flags = ["%{output_execpath}"],
                        expand_if_available = "output_execpath",
                    ),
                    flag_group(
                        iterate_over = "libraries_to_link",
                        flag_groups = [
                            flag_group(
                                flags = ["%{libraries_to_link.name}"],
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file",
                                ),
                            ),
                            flag_group(
                                flags = ["%{libraries_to_link.object_files}"],
                                iterate_over = "libraries_to_link.object_files",
                                expand_if_equal = variable_with_value(
                                    name = "libraries_to_link.type",
                                    value = "object_file_group",
                                ),
                            ),
                        ],
                        expand_if_available = "libraries_to_link",
                    ),
                    flag_group(
                        expand_if_available = "linker_param_file",
                        flags = ["@%{linker_param_file}"],
                    ),
                ],
            ),
        ],
    )

    coverage_feature = feature(
        name = "coverage",
        flag_sets = [
            flag_set(
                actions = ACTION_NAME_GROUPS.all_cc_compile_actions,
                flag_groups = ([flag_group(flags = ["--coverage"])]),
            ),
            flag_set(
                actions = ACTION_NAME_GROUPS.all_cc_link_actions,
                flag_groups = ([flag_group(flags = ["--coverage"])]),
            ),
        ],
    )

    # Now that we have built up the constituent feature definitions, compose
    # them, including configuration based on the target platform. Currently,
    # the target platform is configured with the "cpu" attribute for legacy
    # reasons. Further, for legacy reasons the default is a Linux OS target and
    # the x88-64 CPU name is "k8".

    # First, define features that are simply used to configure others.
    features = [
        feature(name = "dbg"),
        feature(name = "fastbuild"),
        feature(name = "host"),
        feature(name = "no_legacy_features"),
        feature(name = "nonhost"),
        feature(name = "opt"),
        feature(name = "supports_dynamic_linker", enabled = True),
        feature(name = "supports_fission", enabled = True),
        feature(name = "supports_pic", enabled = True),
        feature(name = "supports_start_end_lib", enabled = True),
    ]

    # The order of the features determines the relative order of flags used.
    # Start off adding the baseline features.
    features += [
        default_flags_feature,
        minimal_optimization_flags,
        default_optimization_flags,
        thinlto_feature,
        per_object_debug_info_feature,  # This feature needs to be before the other debug level features so that the debug level is overridden.
        minimal_debug_info_flags,
        default_debug_info_flags,
        preserve_call_stacks,
        sysroot_feature,
        sanitizer_common_flags,
        address_and_undefined_behavior_sanitizers_feature,
        thread_sanitizer_feature,
        fuzzer,
        layering_check,
        module_maps,
        use_module_maps,
        default_archiver_flags_feature,
        target_specific_flags_feature,
        sanitizer_static_lib_flags,
        linux_flags_feature,
        default_link_libraries_feature,
        final_flags_feature,
        coverage_feature,
        aarch64_link_flags,
        x86_64_link_flags,
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        features = features,
        action_configs = action_configs,
        cxx_builtin_include_directories = ctx.attr.cxx_builtin_include_directories,
        builtin_sysroot = ctx.attr.sysroot,

        # This configuration only supports local non-cross builds so derive
        # everything from the target CPU selected.
        toolchain_identifier = "clang-" + ctx.attr.target,
        host_system_name = "local-k8",
        target_system_name = "local-" + ctx.attr.target,
        # target_cpu is used in the name of the `_solib_<TARGET_CPU>` directory that gets created for shared libaries.
        # Here we use `"local"` to be consistent with our previous naming behavior.
        target_cpu = "local",

        # These attributes aren't meaningful at all so just use placeholder
        # values.
        target_libc = "local",
        compiler = "local",
        abi_version = "local",
        abi_libc_version = "local",

        # We do have to pass in our tool paths.
        tool_paths = tool_paths,
    )

cc_toolchain_config = rule(
    implementation = _cc_toolchain_config_impl,
    attrs = {
        "cxx_builtin_include_directories": attr.string_list(),
        "dynamic_linker": attr.string(),
        "gcc_install_dir": attr.string(),
        "includes": attr.string_list(),
        "sysroot": attr.string(),
        "target": attr.string(),
        "target_flags": attr.string_list(),
        "tool_paths": attr.string_dict(),
    },
    provides = [CcToolchainConfigInfo],
)
