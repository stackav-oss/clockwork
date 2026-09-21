# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Wrapper macros for Python.
"""

load("@bazel_lib//lib:expand_make_vars.bzl", "expand_variables")
load("@bazel_lib//lib:expand_template.bzl", "expand_template")
load("@rules_python//python:defs.bzl", "PyRuntimeInfo", _py_binary = "py_binary", _py_library = "py_library", _py_test = "py_test")
load("@rules_python//python:packaging.bzl", _py_wheel = "py_wheel")
load("//tools/rules:cc.bzl", "cc_binary")

_DISABLED_TYPE_STUB_ALLOW_LIST = [
    # keep-sorted start
    "//jewels/testing:nb_tmp_directory_guard",
    # keep-sorted end
]

# TODO(OI-3125): De-duplicate rule definitions.

def py_cc_binding(name, srcs, deps, data = [], dynamic_deps = [], py_deps = None, pyi_file = None, use_type_stubs = None, visibility = None, testonly = None, exec_properties = None, imports = [], copts = [], disabled_stubs = _DISABLED_TYPE_STUB_ALLOW_LIST):
    """Create a solib and a Python library wrapping it.

    Args:
        name: The name of the wrapper lib.
        srcs: The C++ srcs for the solib.
        deps: The C++ deps.
        data: The C++ data dependencies.
        dynamic_deps: The cc_shared_library deps.
        py_deps: The python deps.
        pyi_file: Optional manually generated pyi_file for the wrapper lib.
           Use with caution - it's strongly preferred to fix the code generator for all use cases,
           rather than hand-maintaining stubs.
        use_type_stubs: Whether we should run stubgen.
        visibility: The visibility of the resulting wrapper lib.
        testonly: Whether the targets should be marked testonly.
        exec_properties: dict[str, str]
            A dictionary of strings that will be added to the
            exec_properties of a platform selected for this
            target. See exec_properties of the platform rule.
        imports: Import paths
        copts: C++ compiler options for the generated shared library.
        disabled_stubs: List of targets that are allowed to have type stubs disabled.
    """
    if name.endswith(".so"):
        fail("No .so suffix is needed in a py_cc_binding target name.")

    if not (name.startswith("nb_") or name.endswith("_clk_nb")):
        fail("By convention, py_cc_binding target name must start with 'nb_' (handwritten) or end with '_clk_nb' (clockwork-generated). Target '" + name + "' does not.")

    target = "//" + native.package_name() + ":" + name
    autofix_msg = "\nTo fix automatically, run:\n\n\tbuildozer 'remove use_type_stubs' " + target + "\n\n"
    if use_type_stubs == None:
        # If it's None it's unset - default to True
        use_type_stubs = True
    elif use_type_stubs == True:
        # We don't want people to explicitly set True
        msg = "\n\n"
        msg += "use_type_stubs should not be set to explicitly to True (True is the default)\n."
        msg += "It is only used to set to False for a targets in the disabled_stubs allowlist.\n"
        msg += autofix_msg
        fail(msg)
    elif use_type_stubs == False:
        # Only allow this if it's in the allow list.
        if target not in disabled_stubs:
            msg = "\n\n"
            msg += "use_type_stubs should not be set to False.\n"
            msg += "It is only set to False for a targets in the disabled_stubs allowlist.\n"
            msg += autofix_msg
            fail(msg)

    so_name = "{name}.so".format(name = name)

    # Our strategy for clockwork-generated type_casters is to conditionally compile them
    # behind an '#ifdef CLK_ENABLE_NANOBIND_TYPE_CASTER' check.
    # Those targets don't get the "//jewels/nanobind:nanobind_tappy_convert" dependency, so we have to add it
    # here, to all targets.
    #
    # We also define CLK_ENABLE_NANOBIND_TYPE_CASTER here, which works because the type_caster implementation
    # is in an .inl file.
    if "@clockwork//jewels/nanobind:nanobind_tappy_convert" not in deps:
        deps.append("@clockwork//jewels/nanobind:nanobind_tappy_convert")

    cc_binary(
        name = so_name,
        testonly = testonly,
        srcs = srcs,
        data = data,
        defines = ["CLK_ENABLE_NANOBIND_TYPE_CASTER=1"],
        linkopts = [
            "-fvisibility=hidden",
            "-fPIC",
        ],
        dynamic_deps = dynamic_deps,
        linkshared = True,
        # Forward extension-only compile options from generated nanobind bindings.
        copts = copts,
        visibility = ["//visibility:private"],
        deps = deps,
    )

    if pyi_file:
        # make sure the name is correct
        if not pyi_file.endswith(".pyi"):
            fail("Type stubs specified by py_cc_binding(pyi_file=...) must have extension .pyi")
        expected_pyi_name = name + ".pyi"
        if pyi_file not in [expected_pyi_name, ":" + expected_pyi_name]:
            fail("Type stubs specified by py_cc_binding(pyi_file=...) must have same name as target. Expected :{}, got {}".format(expected_pyi_name, pyi_file))
        pyi_data = [pyi_file]
    elif use_type_stubs:
        pyi = nanobind_stubgen(
            name = name + "__stubgen",
            shared_library = so_name,
            testonly = testonly,
            exec_properties = exec_properties,
            py_deps = py_deps or [],
        )
        pyi_data = [pyi]
    else:
        pyi_data = []

    py_library(
        name = name,
        testonly = testonly,
        visibility = visibility,
        data = [so_name] + pyi_data,
        deps = py_deps,
        imports = imports,
    )

def run_python_action(ctx, script, arguments = [], **kwargs):
    """Create an action to run a `py_binary`.

    Args:
        ctx: The rule's ctx.
        script: An executable attribute - e.g. `ctx.attr.some_py_binary`.
        arguments: Arguments to pass to the Python script.
        **kwargs: Arguments to pass to `ctx.actions.run`.
    """
    default_info = script[DefaultInfo]
    runtime_info = script[PyRuntimeInfo]

    ctx.actions.run(
        executable = default_info.files_to_run.executable,
        arguments = arguments,
        tools = [
            default_info.files_to_run,
            depset([default_info.files_to_run.executable], transitive = [runtime_info.files, default_info.default_runfiles.files]),
        ],
        **kwargs
    )

def _py_conditional_genrule_impl(ctx):
    """Run a Python binary via the Python executable so that we can do it in RBE."""
    args = [ctx.expand_location(arg) for arg in ctx.attr.args]
    args = [expand_variables(ctx, arg, outs = ctx.outputs.outs) for arg in args]

    if ctx.attr.condition:
        run_python_action(
            ctx,
            script = ctx.attr.tool,
            inputs = ctx.files.srcs,
            outputs = ctx.outputs.outs,
            mnemonic = "RunPyBinary",
            arguments = args,
            progress_message = "Running " + ctx.attr.tool.label.name,
            resource_set = None,
            env = ctx.attr.tool[RunEnvironmentInfo].environment,
        )
    else:
        for out in ctx.outputs.outs:
            ctx.actions.run_shell(
                outputs = [out],
                command = "touch {target}".format(target = out.path),
            )

    return DefaultInfo(
        files = depset(ctx.outputs.outs),
        runfiles = ctx.runfiles(files = ctx.outputs.outs),
    )

py_conditional_genrule = rule(
    implementation = _py_conditional_genrule_impl,
    attrs = {
        "args": attr.string_list(doc = "Command line arguments of the binary."),
        "condition": attr.bool(mandatory = True, doc = "The condition used to enable the action."),
        "outs": attr.output_list(mandatory = True, doc = "Output files generated by the action."),
        "srcs": attr.label_list(allow_files = True, doc = "Additional inputs of the action."),
        "tool": attr.label(mandatory = True, doc = "The tool to run in the action.", executable = True, cfg = "exec"),
    },
)

def py_test(name, use_pytest = True, **kwargs):
    """Wrapper to handle py test types and pytest requirements.

    Args:
        name: Name of the py_test.
        use_pytest: Check for pytest deps for test targets.
        **kwargs: args to pass to underlying test rule.
    """
    if use_pytest:
        srcs = kwargs.get("srcs", [])
        deps = kwargs.get("deps", [])
        main = kwargs.get("main", "")

        this_label = native.package_relative_label(name)
        test_label = native.package_relative_label(":__test__")
        test_file_label = native.package_relative_label(":__test__.py")

        has_srcs = False
        has_deps = False
        has_main = False

        for src in srcs:
            src_label = native.package_relative_label(src)
            if src_label == test_label or src_label == test_file_label:
                has_srcs = True
                break
        for dep in deps:
            dep_label = native.package_relative_label(dep)
            if dep_label == test_label:
                has_deps = True
                break

        has_main = main and native.package_relative_label(main) == test_file_label

        if not (has_srcs and has_main and has_deps):
            fail("\n".join([
                "",
                "",
                "The py_test target {} is set to run under pytest but is missing dependencies.".format(this_label),
                "",
                "If you have a custom main function and do not want pytest, please explicitly disable pytest and use your main with:",
                "  use_pytest = False",
                '  main = "my_main_file.py",',
                "",
                "Otherwise, to get pytest to work properly, run:",
                "  buildozer 'new_load @aspect_rules_py//py:defs.bzl py_pytest_main' {}".format(this_label),
                "  buildozer 'delete' {}".format(test_label),
                "  buildozer 'new py_pytest_main __test__ after __pkg__' {}".format(this_label),
                "  buildozer 'add deps {}pip//pytest' {}".format("@", test_label),
                "  buildozer 'add srcs :__test__' {}".format(this_label),
                "  buildozer 'add deps :__test__' {}".format(this_label),
                "  buildozer 'set main :__test__.py' {}".format(this_label),
                "",
                "",
            ]))

        exec_properties = kwargs.get("exec_properties")
        tags = kwargs.get("tags", [])
        if exec_properties or "requires-network" in tags:
            # Avoid action conflicts by disabling precompilation of tests that customize exec_properties (and other
            # attributes that surface this issue)
            # https://rules-python.readthedocs.io/en/latest/precompiling.html#known-issues-caveats-and-idiosyncracies
            kwargs["precompile"] = "disabled"

    _py_test(name = name, **kwargs)

def py_binary(env = None, **kwargs):
    # type: (dict[string, string], **Any) -> None
    """https://bazel.build/reference/be/python#py_binary

    Augments py_binary by setting PYTHONUSERSITE env var.

    Args:
      env: Environment dict.
      **kwargs: Additional args to pass to underlying py_binary rule.
    """
    if not env:
        env = {}
    env = env | {
        # Prevent Python packages installed in user site-packages outside of Bazel from being used when running
        # Python binaries under Bazel
        "PYTHONNOUSERSITE": "1",
    }
    _py_binary(env = env, **kwargs)

def nanobind_stubgen(
        name,
        shared_library,
        testonly,
        exec_properties,
        py_deps):
    """Creates a stub file containing Python type annotations for a nanobind extension.

    Args:
        name: str
            Name of this stub generation target; used to make internal target names unique.
        shared_library: Label
            Label of the extension module for which the stub file should be generated.
        testonly: bool
            Whether the extension module is a testonly target.
        exec_properties: dict[str, str]
            A dictionary of strings that will be added to the
            exec_properties of a platform selected for this
            target. See exec_properties of the platform rule.
        py_deps: LabelList
            Python dependencies of the extension module.

    Returns:
        The generated pyi label.
        Add this to the data dependencies of the extension module.
    """

    untyped_so_py_library_name = name + "__stubgen_dummy"

    py_library(
        name = untyped_so_py_library_name,
        testonly = testonly,
        visibility = ["//visibility:private"],
        data = [shared_library],
        deps = py_deps,
    )

    generator_py_binary_name = name + "__generate_stubs_py_binary"
    STUBGEN_WRAPPER = Label("//tools/rules:stubgen_wrapper.py")

    pyi_file = shared_library.replace(".so", ".pyi")
    py_binary(
        name = generator_py_binary_name,
        srcs = [STUBGEN_WRAPPER],
        main = STUBGEN_WRAPPER,
        deps = [
            # Depend on //tools/rules:stubgen_wrapper to let gazelle manange the dependencies.
            Label("//tools/rules:stubgen_wrapper"),
        ] + py_deps,
        data = [untyped_so_py_library_name],
        tags = [
            # Each of these binaries uses the same `src` file, so don't bother running the type-checker for each one.
            # Instead, we have a dedicated target for the `src` file that we type-check.
            "pyrefly-skip",
        ],
    )

    PATTERN_FILE = Label("//tools/rules:stubgen_patterns.txt")
    py_conditional_genrule(
        name = "{}__generate_stubs_genrule".format(name),
        args = ["$(execpath {shared_library})".format(shared_library = shared_library), "$@", "--pattern_file", "$(execpath {pattern_file})".format(pattern_file = PATTERN_FILE), "--bindir", "$(BINDIR)"],
        srcs = [shared_library, PATTERN_FILE],
        outs = [pyi_file],
        exec_properties = exec_properties,
        tool = generator_py_binary_name,
        condition = select({
            "@platforms//cpu:x86_64": True,
            "//conditions:default": False,
        }),
    )

    return pyi_file

def cc_binary_with_embedded_py(name, deps = [], py_deps = [], visibility = None, **kwargs):
    """C/C++ binary with embedded python.

    Augments cc_binary by wrapping the binary in a python wrapper to setup the
    environment for loading python modules from C/C++.

    See https://bazel.build/reference/be/c-cpp#cc_binary.

    Args:
      name: The name of the python wrapper
      deps: C/C++ dependencies
      py_deps: Python dependencies
      visibility: The visibility of the resulting python wrapper.

      **kwargs: Additional args to pass to underlying cc_binary rule.
    """
    cc_binary_name = name + WRAPPED_CC_BINARY_SUFFIX
    python_wrapper_name = name + PYTHON_WRAPPER_SUFFIX
    python_wrapper_source = python_wrapper_name + ".py"

    cc_binary(
        name = cc_binary_name,
        deps = deps + [
            "@rules_python//python/cc:current_py_cc_headers",
            "@rules_python//python/cc:current_py_cc_libs",
        ],
        **kwargs
    )

    expand_template(
        name = python_wrapper_name,
        out = python_wrapper_source,
        substitutions = {"wrapped_cc_binary": "$(rootpath :{cc_binary_name})".format(cc_binary_name = cc_binary_name)},
        template = PY_CC_WRAPPER_TEMPLATE,
        data = [":" + cc_binary_name],
    )

    py_binary(
        name = name,
        srcs = [python_wrapper_source],
        main = python_wrapper_source,
        data = [":" + cc_binary_name],
        deps = py_deps,
        visibility = visibility,
    )

def cc_test_with_embedded_py(name, deps = [], py_deps = [], py_imports = [], tags = [], **kwargs):
    """C/C++ test with embedded python.

    Augments cc_test by wrapping the test in a python wrapper to setup the
    environment for loading python modules from C/C++.

    See https://bazel.build/reference/be/c-cpp#cc_test.

    Args:
      name: The name of the python wrapper
      deps: C/C++ dependencies
      py_deps: Python dependencies
      py_imports: Python imports
      tags: Extra tags applied to the py_test
      **kwargs: Additional args to pass to underlying cc_binary rule.
    """
    cc_binary_name = name + WRAPPED_CC_BINARY_SUFFIX
    python_wrapper_name = name + PYTHON_WRAPPER_SUFFIX
    python_wrapper_source = python_wrapper_name + ".py"

    cc_binary(
        name = cc_binary_name,
        deps = deps + [
            "@rules_python//python/cc:current_py_cc_headers",
            "@rules_python//python/cc:current_py_cc_libs",
        ],
        **kwargs
    )

    expand_template(
        name = python_wrapper_name,
        out = python_wrapper_source,
        substitutions = {"wrapped_cc_binary": "$(rootpath :{cc_binary_name})".format(cc_binary_name = cc_binary_name)},
        template = PY_CC_WRAPPER_TEMPLATE,
        data = [":" + cc_binary_name],
    )

    py_test(
        name = name,
        use_pytest = False,
        srcs = [python_wrapper_source],
        main = python_wrapper_source,
        imports = py_imports,
        data = [":" + cc_binary_name],
        deps = py_deps,
        tags = tags,
    )

py_library = _py_library
py_wheel = _py_wheel

# Python wrapper template for cc targets to setup the environment for running python from C/C++
PY_CC_WRAPPER_TEMPLATE = Label("//tools/rules:py_cc_wrapper.py")

# Suffix for the python wrapper source file
PYTHON_WRAPPER_SUFFIX = "__python_wrapper"

# Suffix for the CC binary wrapped by the python wrapper
WRAPPED_CC_BINARY_SUFFIX = "__wrapped_cc_binary"
