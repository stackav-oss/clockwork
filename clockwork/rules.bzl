# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Definition of custom Clockwork compilation rule."""

# Normally, getting the files attribute of your deps includes all files.  When a
# .clk file depends on other .clk files, we really only need the srcs, so we
# define a custom provider for this.

load("@bazel_lib//lib:diff_test.bzl", "diff_test")
load("@bazel_lib//lib:write_source_files.bzl", "write_source_file")
load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@build_stack_rules_proto//rules:proto_compile.bzl", "proto_compile")
load("@build_stack_rules_proto//rules/cc:proto_cc_library.bzl", "proto_cc_library")
load("@build_stack_rules_proto//rules/go:proto_go_library.bzl", "proto_go_library")
load("@build_stack_rules_proto//rules/py:proto_py_library.bzl", "proto_py_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("//tools/rules:cc.bzl", "cc_binary", "cc_library")
load("//tools/rules:python.bzl", "cc_binary_with_embedded_py", "py_cc_binding", "py_library")

ClkInfo = provider("Collects Clockwork source files", fields = ["src", "srcs", "cache"])

_GENERATED_CODE_CLK_DEPS = [
    "@clockwork//clockwork/dsl/cog:common_cog_event_metrics_clk",
    "@clockwork//clockwork/dsl/cog:common_cog_telemetry_metrics_clk",
    "@clockwork//clockwork/dsl/cog:cog_execution_metrics_signals_clk",
]

_MINIMAL_GENERATED_CODE_CC_DEPS = [
    "@clockwork//clockwork:repr_iface",
    "@clockwork//clockwork:tags",
    "@clockwork//jewels/container:at",
    "@clockwork//jewels/container:compare",
    "@clockwork//jewels/container/tap:bitset",
    "@clockwork//jewels/container/tap:optional",
    "@clockwork//jewels/container/tap:protobuf_to_tap",
    "@clockwork//jewels/container/tap:soa",
    "@clockwork//jewels/container/tap:tap_to_protobuf",
    "@clockwork//jewels/container/tap:tensor",
    "@clockwork//jewels/container/tap:var_array",
    "@clockwork//jewels/container/tap:var_string",
    "@clockwork//jewels/log_cerr:log_cerr",
    "@clockwork//jewels/memory:memory_resource",
    "@clockwork//jewels/memory:pmr_shared_ptr",
    "@clockwork//jewels/memory:pointers",
    "@clockwork//jewels/meta:concepts",
    "@clockwork//jewels/std:expected",
    "@clockwork//jewels/std:span",
    "@clockwork//jewels/time:sync_time",
    "@clockwork//jewels/utility:enum_flags",
    "@clockwork//jewels/utility:string_param",
    "@clockwork//jewels/uuid:uuid",
    "@wise_enum",
]

_GENERATED_CODE_CC_DEPS = [
    "@clockwork//clockwork/cog:include_common",
    "@clockwork//clockwork/common:process_description_clk_cc",
    "@clockwork//clockwork/cog:cog_conditions",
    "@clockwork//clockwork/cog:cog_configs",
    "@clockwork//clockwork/cog:cog_diagnostics",
    "@clockwork//clockwork/cog:cog_inputs",
    "@clockwork//clockwork/cog:cog_memory_resources",
    "@clockwork//clockwork/cog:cog_publishers",
    "@clockwork//clockwork/cog:cog_states",
    "@clockwork//clockwork/cog:cog_statistics",
    "@clockwork//clockwork/cog:cog_timers",
    "@clockwork//clockwork/cog:input_condition",
    "@clockwork//clockwork/cog:input_view",
    "@clockwork//clockwork/cog:simple_cog",
    "@clockwork//clockwork/common:abstract_cog",
    "@clockwork//clockwork/common:abstract_cog_queue",
    "@clockwork//clockwork/diagnostics:reporter",
    "@clockwork//clockwork/diagnostics:report_definitions",
    "@clockwork//clockwork/dial:cond_messages_present",
    "@clockwork//clockwork/dial:cond_time_since_last_exec",
    "@clockwork//clockwork/dial:include_common",
    "@clockwork//clockwork/dial:msg_input",
    "@clockwork//clockwork/pinion:abstract_channel",
    "@clockwork//clockwork/pinion:bidirectional_udp",
    "@clockwork//clockwork/pinion:incoming_udp",
    "@clockwork//clockwork/pinion:outgoing_udp",
    "@clockwork//clockwork/pinion:sock_opt",
    "@clockwork//clockwork/scaffolding:abstract_casing",
    "@clockwork//clockwork/scaffolding:casing",
    "@clockwork//jewels/networking:sock_opt",
    "@clockwork//jewels/networking:socket_endpoint",
]

_GENERATED_CODE_PY_DEPS = [
    "@clockwork//clockwork/dsl/ir:compiler",
    "@clockwork//clockwork/dsl/ir:importer",
    "@clockwork//clockwork/serialization/py:tachyon_dyn",
]

_GENERATED_TEST_CODE_CC_DEPS = [
    "@clockwork//clockwork/cog/wrappers:unit_test_cog",
    "@clockwork//clockwork/cog/wrappers:dummy_cog_queue",
]

_GENERATED_CODE_NANOBIND_CC_DEPS = [
    "@clockwork//jewels/nanobind/clk_bindings/common:common_cc",
    "@clockwork//jewels/nanobind/clk_bindings:bind_fixed_array",
    "@clockwork//jewels/nanobind/clk_bindings:bind_tachyon_serialize",
    "@clockwork//jewels/nanobind/clk_bindings:bind_var_array",
    "@clockwork//jewels/nanobind/clk_bindings:cast_maybe_by_reference",
    "@clockwork//jewels/nanobind/clk_bindings:get_python_type_name",
    "@clockwork//jewels/nanobind:nanobind_tappy_convert",
    "@clockwork//jewels/nanobind:nb_au",
    "@clockwork//jewels/nanobind:nb_byte_array",
    "@clockwork//jewels/nanobind:nb_var_string",
    "@clockwork//jewels/nanobind:uuid_caster",
    "@nanobind",
]

_GENERATED_CODE_NANOBIND_PY_DEPS = ["@clockwork//jewels/nanobind/clk_bindings/common:common_py"]

_GENERATED_CODE_PROTO_DEPS = [
    "@protobuf//:duration_proto",
    "@protobuf//:timestamp_proto",
]

_GENERATED_CODE_GO_PROTO_DEPS = [
    "@org_golang_google_protobuf//reflect/protoreflect",
    "@org_golang_google_protobuf//runtime/protoimpl",
    "@org_golang_google_protobuf//types/known/durationpb",
    "@org_golang_google_protobuf//types/known/timestamppb",
]

_GENERATED_CODE_PY_COG_IMPL_DEPS = [
    "@clockwork//clockwork/python:gil_lock_guard",
    "@clockwork//clockwork/python:python_init",
    "@clockwork//clockwork/python:python_object",
]

def _selected_clkc(ctx):
    return (ctx.executable._clkc, ctx.attr._clkc)

def _clkc_env(clkc_target, use_rust_parser):
    env = dict(clkc_target[RunEnvironmentInfo].environment)
    if use_rust_parser:
        env["CLOCKWORK_PARSER_BACKEND"] = "rust"
    else:
        env["CLOCKWORK_PARSER_BACKEND"] = "python"
    return env

def _clk_deps_with_builtins(deps, minimize_builtin_deps):
    clk_deps = [] + deps
    if not minimize_builtin_deps:
        # Normalize labels for dedup: handle both @clockwork//... and //... forms
        existing = {}
        for d in deps:
            norm = d
            if norm.startswith("@clockwork//"):
                norm = norm[len("@clockwork"):]
            existing[norm] = True
        for d in _GENERATED_CODE_CLK_DEPS:
            norm = d
            if norm.startswith("@clockwork//"):
                norm = norm[len("@clockwork"):]
            if norm not in existing:
                clk_deps.append(d)
    return clk_deps

def _clk_impl(ctx):
    if len(ctx.files.srcs) != 1:
        error = "Must provide exactly one .clk file for srcs {}".format([s.path for s in ctx.files.srcs])
        fail(error)
    src = ctx.files.srcs[0]
    srcs = depset([src], transitive = [dep[ClkInfo].srcs for dep in ctx.attr.deps if ClkInfo in dep])

    use_rust_parser = ctx.attr._use_rust_parser[BuildSettingInfo].value
    marker_files = []
    if ctx.attr.compile:
        if not ctx.outputs.outs:
            marker_files.append(ctx.actions.declare_file(src.basename + "_compile_marker"))

        args = ctx.actions.args()
        args.add("compile-module")
        args.add("--input")
        args.add(src.path)
        args.add("--root")
        compile_outputs = ctx.outputs.outs + marker_files
        args.add(compile_outputs[0].root.path)

        args.add("--repo")
        args.add(ctx.attr.repo)

        if ctx.attr.write_json_files:
            args.add("--write-json-files")
        if marker_files:
            args.add("--marker-output")
            args.add(marker_files[0].path)

        clkc_executable, clkc_target = _selected_clkc(ctx)

        ctx.actions.run(
            inputs = depset(transitive = [srcs, clkc_target[DefaultInfo].default_runfiles.files]),
            outputs = compile_outputs,
            arguments = [args],
            progress_message = "Compiling Clockwork module %s" % ctx.files.srcs[0].short_path,
            mnemonic = "CompileClockworkModule",
            executable = clkc_executable,
            env = _clkc_env(clkc_target, use_rust_parser),
        )

    files = depset(direct = ctx.outputs.outs + [src] + marker_files, transitive = [srcs, ctx.attr._clkc[DefaultInfo].files])
    runfiles = ctx.runfiles(files = ctx.outputs.outs + [src] + marker_files).merge_all([dep[DefaultInfo].default_runfiles for dep in ctx.attr.deps])
    return [
        DefaultInfo(files = files, runfiles = runfiles),
        ClkInfo(src = src, srcs = srcs, cache = depset()),
        OutputGroupInfo(clk_files = srcs),
    ]

_clk = rule(
    implementation = _clk_impl,
    attrs = {
        "compile": attr.bool(default = True),
        "deps": attr.label_list(),
        "outs": attr.output_list(),
        "repo": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = [".clk"]),
        "write_json_files": attr.bool(default = False),
        "_clkc": attr.label(
            cfg = "exec",
            default = Label("//clockwork/dsl:clkc"),
            executable = True,
        ),
        "_use_rust_parser": attr.label(default = Label("//clockwork/dsl:use_rust_parser")),
    },
)

def _clk_compile_benchmark_impl(ctx):
    if len(ctx.files.srcs) != 1:
        error = "Must provide exactly one .clk file for srcs {}".format([s.path for s in ctx.files.srcs])
        fail(error)
    src = ctx.files.srcs[0]
    srcs = depset([src], transitive = [dep[ClkInfo].srcs for dep in ctx.attr.deps if ClkInfo in dep])

    use_rust_parser = ctx.attr._use_rust_parser[BuildSettingInfo].value

    output_dir = ctx.actions.declare_directory(ctx.label.name + "_outputs")
    timing_output = ctx.actions.declare_file(ctx.label.name + ".json")

    args = ctx.actions.args()
    args.add("benchmark-compile-module")
    args.add("--input")
    args.add(src.path)
    args.add("--root")
    args.add(output_dir.path)
    args.add("--repo")
    args.add(ctx.attr.repo)
    args.add("--timing-output")
    args.add(timing_output.path)
    args.add("--nonce")
    args.add(ctx.attr._benchmark_nonce[BuildSettingInfo].value)

    if ctx.attr._profile_benchmark[BuildSettingInfo].value:
        args.add("--profile-output")
        args.add(output_dir.path + "/clkc.cprofile")

    if ctx.attr.write_json_files:
        args.add("--write-json-files")

    clkc_executable, clkc_target = _selected_clkc(ctx)

    ctx.actions.run(
        inputs = depset(transitive = [srcs, clkc_target[DefaultInfo].default_runfiles.files]),
        outputs = [output_dir, timing_output],
        arguments = [args],
        progress_message = "Benchmarking Clockwork module compile %s" % ctx.files.srcs[0].short_path,
        mnemonic = "BenchmarkCompileClockworkModule",
        executable = clkc_executable,
        env = _clkc_env(clkc_target, use_rust_parser),
    )

    return [DefaultInfo(files = depset([timing_output, output_dir]))]

_clk_compile_benchmark = rule(
    implementation = _clk_compile_benchmark_impl,
    attrs = {
        "deps": attr.label_list(),
        "repo": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = [".clk"]),
        "write_json_files": attr.bool(default = False),
        "_benchmark_nonce": attr.label(default = Label("//clockwork/dsl:clk_compile_benchmark_nonce")),
        "_clkc": attr.label(
            cfg = "exec",
            default = Label("//clockwork/dsl:clkc"),
            executable = True,
        ),
        "_profile_benchmark": attr.label(default = Label("//clockwork/dsl:profile_clk_compile_benchmark")),
        "_use_rust_parser": attr.label(default = Label("//clockwork/dsl:use_rust_parser")),
    },
)

def _collect_clk_files_impl(ctx):
    clk_deps = ctx.attr.clk_target[ClkInfo].srcs
    return DefaultInfo(files = clk_deps, runfiles = ctx.runfiles(clk_deps.to_list()))

_collect_clk_files = rule(
    implementation = _collect_clk_files_impl,
    attrs = {"clk_target": attr.label()},
)

def _update_clk_targets(name, srcs, testonly):
    #  The rule guarantees that srcs is exactly one item.
    clk_file = srcs[0]

    _collect_clk_files(
        name = name + ".clk_files",
        clk_target = name,
        testonly = testonly,
    )
    generated_build_file = "BUILD." + name + ".bazel"
    native.genrule(
        name = name + ".build_gen",
        srcs = [
            clk_file,
            name + ".clk_files",
            "BUILD.bazel",
        ],
        outs = [generated_build_file],
        cmd = "$(location @clockwork//clockwork/dsl/bazel:update_clk_targets) $(location @buildifier_prebuilt//:buildifier) $(location @buildifier_prebuilt//:buildozer) $(location BUILD.bazel) $(location " + clk_file + ") $@ --repo " + native.module_name(),
        tools = [
            "@clockwork//clockwork/dsl/bazel:update_clk_targets",
            "@buildifier_prebuilt//:buildifier",
            "@buildifier_prebuilt//:buildozer",
        ],
        tags = ["no-remote-exec", "clk-deps"],
        testonly = testonly,
    )
    if native.repo_name() == "":
        write_source_file(
            name = name + ".build",
            in_file = generated_build_file,
            out_file = "BUILD.bazel",
            tags = ["clk-deps"],
            #TODO(OI-3066) Resolve write_source_file issue across multiple repos.
            diff_test = True,
            diff_test_failure_message = "The BUILD file {} is out of date.".format(native.package_name() + "/BUILD.bazel"),
        )
    else:
        # write_source_file can't write to external repos, so we just add a diff_test to ensure the file is up to date.
        diff_test(
            name = name + ".build_test",
            file1 = generated_build_file,
            file2 = "BUILD.bazel",
            tags = ["clk-deps"],
            failure_message = "The external BUILD file {} is out of date.".format(native.package_name() + "/BUILD.bazel"),
            diff_args = ["--unified"],
        )

        # Also add an empty filegroup so that the ".build" target also exists outside this repo.
        # Otherwise queries from within and without find different targets, which confuses e.g. bazel-diff.
        native.filegroup(
            name = name + ".build",
            tags = ["clk-deps"],
        )

def clk_compile_benchmark(
        name,
        srcs,
        deps = [],
        minimize_builtin_deps = False,
        write_json_files = False,
        testonly = None,
        **kwargs):
    """Benchmark a single Clockwork compile action into a tree artifact.

    Args:
        name: The name of the benchmark target.
        srcs: A list containing a single .clk file.
        deps: A list of dependencies for the .clk file.
        minimize_builtin_deps: A flag to minimize the dependencies pulled into the benchmark action.
        write_json_files: Whether to write JSON versions of the config or not.
        testonly: Whether this is a test-only target.
        **kwargs: Additional arguments passed through to the underlying benchmark rule.
    """
    _clk_compile_benchmark(
        name = name,
        srcs = srcs,
        repo = native.module_name(),
        deps = _clk_deps_with_builtins(deps, minimize_builtin_deps),
        write_json_files = write_json_files,
        testonly = testonly,
        **kwargs
    )

def clk(
        name,
        srcs,
        outs = [],
        data = [],
        deps = [],
        compile = True,
        generate = None,
        minimize_builtin_deps = False,
        cpp_deps = [],
        cpp_exe_deps = [],
        cpp_combo_test_aligner_deps = [],
        nanobind_cpp_deps = [],
        nanobind_copts = [],
        py_deps = [],
        proto_deps = [],
        go_import_path = None,
        write_json_files = False,
        testonly = None,
        **kwargs):
    """Compile a clk file.

    Args:
        name: The name of the clk target.
        srcs: A list containing a single .clk file.
        outs: A list of generated output files.
        data: A list of dependencies for data files.
        deps: A list of dependencies for the .clk file.
        compile: Whether to run the compiler or not (False essentially turns this into a filegroup).
        generate: A list of the targets generated by the clk file.
        minimize_builtin_deps: A flag to minimize the dependencies pulled into the generated code.
                       Use this to work around cicrular dependencies from clockwork files that
                       are needed by the generated code.
        cpp_deps: A list of dependencies for the generated cpp targets (cpp, cpp_cog, py_cog, cpp_exe, py_exe, nanobind)
        cpp_exe_deps: A list of dependencies for the generated cpp_exe targets.
        cpp_combo_test_aligner_deps: A list of aligner test wrapper targets needed by the combo test wrapper.
        nanobind_cpp_deps: A list of C++ dependencies for the generated nanobind target.
        nanobind_copts: A list of C++ compiler options for the generated nanobind target.
        py_deps: A list of dependencies for the generated py targets (py, py_cog, py_exe, nanobind).
        proto_deps: A list of protobuf dependencies for the generated targets.
        go_import_path: Import path string for the proto go library.
        write_json_files: Whether to write JSON versions of the config or not.
        testonly: Whether this is a test-only target.
        **kwargs: Additional arguments passed through to the underlying clk rule.
    """

    clk_deps = _clk_deps_with_builtins(deps, minimize_builtin_deps)

    _clk(
        name = name,
        srcs = srcs,
        repo = native.module_name(),
        outs = outs,
        deps = clk_deps,
        compile = compile,
        write_json_files = write_json_files,
        testonly = testonly,
        **kwargs
    )

    if generate == None:
        if compile:
            _update_clk_targets(name, srcs, testonly)
        return

    generated_code_cc_deps = [] + _MINIMAL_GENERATED_CODE_CC_DEPS
    if not minimize_builtin_deps:
        generated_code_cc_deps.extend(_GENERATED_CODE_CC_DEPS)

    proto_cpp_deps = []
    proto_py_deps = []
    proto_go_deps = []
    for proto_dep in proto_deps:
        proto_cpp_deps.append(proto_dep + "_cc_library")
        proto_py_deps.append(proto_dep + "_py_library")
        proto_go_deps.append(proto_dep + "_go_library")

    if "cpp" in generate:
        # Whether this module declares any cog or aligner. The umbrella
        # ``_cc`` library is alwayslink for these so the registration
        # statics in ``_cc.cc`` are pulled into the consumer's link.
        cog_kind_present = (
            "cpp_cog" in generate or
            "py_cog" in generate or
            "cpp_aligner" in generate
        )

        # Schemas / enums / tags / interfaces / converters / etc. Always
        # emitted for any cpp-generating module.
        cc_library(
            name = name + "_cc_types",
            srcs = [
                name + "_cc_types.cc",
                name + "_cc_types.inl",
            ],
            hdrs = [name + "_cc_types.hh"],
            data = data + [":" + name],
            deps = cpp_deps + proto_cpp_deps + generated_code_cc_deps,
            testonly = testonly,
            **kwargs
        )

        cpp_cog_opts = dict(kwargs)
        cog_kind_deps = []
        if cog_kind_present:
            cog_kind_deps = [
                ":" + name + "_cc_cog",
                ":" + name + "_cc_dial",
                ":" + name + "_cc_impl",
            ]
            cpp_cog_opts["alwayslink"] = True

        cc_library(
            name = name + "_cc",
            srcs = [
                name + "_cc.cc",
                name + "_cc.inl",
            ],
            hdrs = [name + "_cc.hh"],
            data = data + [":" + name],
            deps = [":" + name + "_cc_types"] + cog_kind_deps + cpp_deps + proto_cpp_deps + generated_code_cc_deps,
            testonly = testonly,
            **cpp_cog_opts
        )

    if "py" in generate:
        py_library(
            name = name + "_py",
            srcs = [name + "_py.py"],
            data = data + [":" + name],
            deps = py_deps + proto_py_deps + _GENERATED_CODE_PY_DEPS,
            testonly = testonly,
            **kwargs
        )

    if "nanobind" in generate:
        # Keep nanobind-only type caster deps and force-includes out of the generated C++ library.
        py_cc_binding(
            name = name + "_nb",
            srcs = [
                name + "_nb.cc",
                name + "_nb.hh",
                name + "_nb.inl",
            ],
            data = data + [":" + name],
            deps = [":" + name + "_cc"] + cpp_deps + proto_cpp_deps + nanobind_cpp_deps + _GENERATED_CODE_NANOBIND_CC_DEPS,
            py_deps = py_deps + proto_py_deps + _GENERATED_CODE_NANOBIND_PY_DEPS,
            copts = nanobind_copts,
            testonly = testonly,
            **kwargs
        )

    if "proto" in generate:
        if "go_proto" in generate:
            go_output_name = name + "_proto.pb.go"
            go_output_mappings = [go_output_name + "=" + go_import_path + "/" + go_output_name]
            go_outputs = [go_output_name]
            go_plugins = ["@build_stack_rules_proto//plugin/golang/protobuf:protoc-gen-go"]
        else:
            go_output_mappings = []
            go_outputs = []
            go_plugins = []

        proto_library(
            name = name + "_proto",
            srcs = [name + "_proto.proto"],
            deps = proto_deps + _GENERATED_CODE_PROTO_DEPS,
            testonly = testonly,
            **kwargs
        )

        proto_cc_library(
            name = name + "_proto_cc_library",
            srcs = [name + "_proto.pb.cc"],
            hdrs = [name + "_proto.pb.h"],
            deps = ["@protobuf"] + proto_cpp_deps,
            testonly = testonly,
            **kwargs
        )

        proto_compile(
            name = name + "_proto_compile",
            output_mappings = go_output_mappings,
            outputs = [
                name + "_proto.pb.h",
                name + "_proto.pb.cc",
                name + "_proto_pb2.py",
                name + "_proto_pb2.pyi",
            ] + go_outputs,
            plugins = [
                "@build_stack_rules_proto//plugin/builtin:cpp",
                "@build_stack_rules_proto//plugin/builtin:pyi",
                "@build_stack_rules_proto//plugin/builtin:python",
                "@clockwork//tools/gazelle:protoc-gen-nolint",
            ] + go_plugins,
            proto = name + "_proto",
            testonly = testonly,
            **kwargs
        )

        proto_py_library(
            name = name + "_proto_py_library",
            srcs = [name + "_proto_pb2.py"],
            data = [name + "_proto_pb2.pyi"],
            deps = ["@protobuf//:protobuf_python"] + proto_py_deps,
            testonly = testonly,
            **kwargs
        )

        if "go_proto" in generate:
            proto_go_library(
                name = name + "_proto_go_library",
                srcs = go_outputs,
                importpath = go_import_path,
                deps = proto_go_deps + _GENERATED_CODE_GO_PROTO_DEPS,
                testonly = testonly,
                **kwargs
            )

    if "proto_conv" in generate:
        cc_library(
            name = name + "_proto_conv",
            srcs = [
                name + "_proto_conv.cc",
                name + "_proto_conv.inl",
            ],
            hdrs = [name + "_proto_conv.hh"],
            data = [":" + name],
            deps = [
                name + "_cc",
                name + "_proto_cc_library",
            ] + generated_code_cc_deps,
            alwayslink = True,
            testonly = testonly,
            **kwargs
        )

    if "cpp_cog" in generate or "py_cog" in generate or "cpp_aligner" in generate:
        cc_library(
            name = name + "_cc_dial",
            srcs = [
                name + "_cc_dial.cc",
                name + "_cc_dial.inl",
            ],
            hdrs = [name + "_cc_dial.hh"],
            data = [":" + name],
            deps = [":" + name + "_cc_types"] + cpp_deps + proto_cpp_deps + generated_code_cc_deps,
            testonly = testonly,
            **kwargs
        )

        cc_library(
            name = name + "_cc_cog",
            srcs = [
                name + "_cc_cog.cc",
                name + "_cc_cog.inl",
            ],
            hdrs = [name + "_cc_cog.hh"],
            data = [":" + name],
            deps = [":" + name + "_cc_dial", ":" + name + "_cc_types"] + cpp_deps + proto_cpp_deps + generated_code_cc_deps,
            testonly = testonly,
            **kwargs
        )

        if "cpp_test_cog" in generate:
            combo_deps = []
            if "cpp_combo_test" in generate:
                combo_deps = cpp_combo_test_aligner_deps
            cc_library(
                name = name + "_cc_test",
                srcs = [
                    name + "_cc_test.cc",
                    name + "_cc_test.inl",
                ],
                hdrs = [name + "_cc_test.hh"],
                data = [":" + name],
                deps = [":" + name + "_cc"] + cpp_deps + proto_cpp_deps + generated_code_cc_deps + _GENERATED_TEST_CODE_CC_DEPS + combo_deps,
                testonly = True,
                **kwargs
            )

    # Generated _cc_impl is emitted by the codegen for py_cog and cpp_aligner;
    # for plain cpp_cog the user hand-writes _cc_impl.cc and the
    # corresponding cc_library in their BUILD.bazel.
    if "py_cog" in generate or "cpp_aligner" in generate:
        impl_deps = [":" + name + "_cc_dial", ":" + name + "_cc_types"] + cpp_deps + proto_cpp_deps + generated_code_cc_deps
        if "py_cog" in generate:
            impl_deps = impl_deps + _GENERATED_CODE_PY_COG_IMPL_DEPS
        cc_library(
            name = name + "_cc_impl",
            srcs = [
                name + "_cc_impl.cc",
                name + "_cc_impl.inl",
            ],
            hdrs = [name + "_cc_impl.hh"],
            data = [":" + name],
            deps = impl_deps,
            testonly = testonly,
            **kwargs
        )

    if "cpp_exe" in generate:
        cpp_exe_deps = cpp_deps + proto_cpp_deps + cpp_exe_deps + generated_code_cc_deps
        if "cpp" in generate:
            cpp_exe_deps.append(":" + name + "_cc")

        cc_binary(
            name = name + "_exe",
            srcs = [
                name + "_exe.cc",
                name + "_exe.hh",
                name + "_exe.inl",
            ],
            data = data + [":" + name],
            deps = cpp_exe_deps,
            testonly = testonly,
            **kwargs
        )

    if "py_cog" in generate:
        py_dial_deps = py_deps + proto_py_deps + _GENERATED_CODE_PY_DEPS
        if "py" in generate:
            py_dial_deps.append(":" + name + "_py")

        py_library(
            name = name + "_py_dial",
            srcs = [name + "_py_dial.py"],
            data = [":" + name],
            deps = py_dial_deps,
            testonly = testonly,
            **kwargs
        )

    if "py_exe" in generate:
        cpp_exe_deps = cpp_deps + proto_cpp_deps + cpp_exe_deps + generated_code_cc_deps
        if "cpp" in generate:
            cpp_exe_deps.append(":" + name + "_cc")

        py_exe_deps = py_deps
        if "py" in generate:
            py_exe_deps.append(":" + name + "_py")
        if "py_cog" in generate:
            py_exe_deps.append(":" + name + "_py_dial")
            py_exe_deps.append(":" + name + "_py_impl")

        cc_binary_with_embedded_py(
            name = name + "_exe",
            srcs = [
                name + "_exe.cc",
                name + "_exe.hh",
                name + "_exe.inl",
            ],
            data = data + [":" + name],
            deps = cpp_exe_deps,
            py_deps = py_exe_deps,
            testonly = testonly,
            **kwargs
        )
