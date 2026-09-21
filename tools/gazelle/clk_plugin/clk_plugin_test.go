// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package clk_plugin

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/bazelbuild/bazel-gazelle/testtools"
	"github.com/stretchr/testify/require"
	"go.skia.org/infra/bazel/go/bazel"
	"go.skia.org/infra/go/testutils/unittest"
)

// makeBasicWorkspace returns the minimum files necessary for the Gazelle extension to work.
func makeBasicWorkspace() []testtools.FileSpec {
	return []testtools.FileSpec{
		{Path: "MODULE.bazel"}, // Gazelle requires that a MODULE.bazel file exists, even if it's empty.
	}
}

func TestGazelle_NoExistingBuildFile_GeneratesBuildRules(t *testing.T) {
	unittest.BazelOnlyTest(t)

	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/avocado.clk",
			Content: `
#![generate(cpp)]
use src::common_foods::{Beef, Pork, Chicken}
use[] src::exotic_foods::{Armadillo}
use[proto] src::inedible_foods::{Kale}
`,
		}, {
			Path: "src/avocado_util.clk",
			Content: `
#![generate(cpp, py)]
use src::avocado
use src::common_foods
use @external::tools::food_stuff
`,
		}, {
			Path: "src/common_foods.clk",
			Content: `
#![generate(cpp, proto)]
`,
		}, {
			Path: "src/no_generate_attr.clk",
			Content: `
`,
		}, {
			Path: "src/inedible_foods.clk",
			Content: `
#![generate(cpp)]
`,
		},
	}, makeBasicWorkspace()...)

	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "avocado_clk",
    srcs = ["avocado.clk"],
    outs = [
        "avocado_clk_cc.cc",
        "avocado_clk_cc.hh",
        "avocado_clk_cc.inl",
        "avocado_clk_cc_types.cc",
        "avocado_clk_cc_types.hh",
        "avocado_clk_cc_types.inl",
    ],
    cpp_deps = [":common_foods_clk_cc"],
    generate = ["cpp"],
    proto_deps = [":inedible_foods_clk_proto"],
    deps = [
        ":common_foods_clk",
        ":exotic_foods_clk",
        ":inedible_foods_clk",
    ],
)

clk(
    name = "avocado_util_clk",
    srcs = ["avocado_util.clk"],
    outs = [
        "avocado_util_clk_cc.cc",
        "avocado_util_clk_cc.hh",
        "avocado_util_clk_cc.inl",
        "avocado_util_clk_cc_types.cc",
        "avocado_util_clk_cc_types.hh",
        "avocado_util_clk_cc_types.inl",
        "avocado_util_clk_py.py",
    ],
    cpp_deps = [
        ":avocado_clk_cc",
        ":common_foods_clk_cc",
        "@external//tools:food_stuff_clk_cc",
    ],
    generate = [
        "cpp",
        "py",
    ],
    py_deps = [
        ":avocado_clk_py",
        ":common_foods_clk_py",
        "@external//tools:food_stuff_clk_py",
    ],
    deps = [
        ":avocado_clk",
        ":common_foods_clk",
        "@external//tools:food_stuff_clk",
    ],
)

clk(
    name = "common_foods_clk",
    srcs = ["common_foods.clk"],
    outs = [
        "common_foods_clk_cc.cc",
        "common_foods_clk_cc.hh",
        "common_foods_clk_cc.inl",
        "common_foods_clk_cc_types.cc",
        "common_foods_clk_cc_types.hh",
        "common_foods_clk_cc_types.inl",
        "common_foods_clk_proto.proto",
    ],
    generate = [
        "cpp",
        "proto",
    ],
)

clk(
    name = "inedible_foods_clk",
    srcs = ["inedible_foods.clk"],
    outs = [
        "inedible_foods_clk_cc.cc",
        "inedible_foods_clk_cc.hh",
        "inedible_foods_clk_cc.inl",
        "inedible_foods_clk_cc_types.cc",
        "inedible_foods_clk_cc_types.hh",
        "inedible_foods_clk_cc_types.inl",
    ],
    generate = ["cpp"],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_NewImportsAreAdded(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend two new imports were added to util.clk
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep1_clk",
    srcs = ["dep1.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util_this_is_a_typo.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)
`,
		},
		{
			Path: "src/util.clk",
			Content: `
#![generate()]
use[] src::dep1
use[] src::dep2
`,
		},
		{
			Path: "src/dep1.clk",
			Content: `
#![generate()]
`,
		},
		{
			Path: "src/dep2.clk",
			Content: `
#![generate=()]
`,
		},
	}, makeBasicWorkspace()...)

	// We expect those to be added here.
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep1_clk",
    srcs = ["dep1.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":dep1_clk",
        ":dep2_clk",
    ],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_MissingImportsAreRemoved(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend an imports was removed from util.clk
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep1_clk",
    srcs = ["dep1.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util_this_is_a_typo.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":dep1_clk",
        ":dep2_clk",
    ],
)
`,
		},
		{
			Path: "src/util.clk",
			Content: `
#![generate()]
use[] src::dep2
`,
		},
		{
			Path: "src/dep1.clk",
			Content: `
#![generate()]
`,
		},
		{
			Path: "src/dep2.clk",
			Content: `
#![generate=()]
`,
		},
	}, makeBasicWorkspace()...)

	// We expect those to be added here.
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep1_clk",
    srcs = ["dep1.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
    deps = [":dep2_clk"],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_MissingFilesHaveRulesRemoved(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, test that rules for files that don't exist are removed
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep1_clk",
    srcs = ["dep1.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util_this_is_a_typo.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":dep1_clk",
        ":dep2_clk",
    ],
)
`,
		},
		{
			Path: "src/util.clk",
			Content: `
#![generate()]
use[] src::dep2
`,
		},
		{
			Path: "src/dep2.clk",
			Content: `
#![generate=()]
`,
		},
	}, makeBasicWorkspace()...)

	// We expect those to be removed here.
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
    deps = [":dep2_clk"],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_FilesWithoutGenerateAttrsAreIgnored(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we check that files without generate attributes are ignored
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util_this_is_a_typo.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)
`,
		},
		{
			Path: "src/util.clk",
			Content: `
use[] src::dep1
use[] src::dep2
`,
		},
		{
			Path: "src/dep1.clk",
			Content: `
`,
		},
		{
			Path: "src/dep2.clk",
			Content: `
`,
		},
	}, makeBasicWorkspace()...)

	// We expect that nothing has changed
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `
load("//clockwork:rules.bzl", "clk")

clk(
    name = "dep2_clk",
    srcs = ["dep2.clk"],
    visibility = ["//:__subpackages__"],
)

clk(
    name = "util_clk",
    srcs = ["util_this_is_a_typo.clk"],
    generate = ["none"],
    visibility = ["//:__subpackages__"],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

// Fix the runfiles path
func fix_runfiles_path(path string) string {
	runfilesPath := filepath.Join(bazel.RunfilesDir(), path)
	if _, err := os.Stat(runfilesPath); err == nil {
		return runfilesPath
	}
	// Assume this test is run not as the root module.
	return filepath.Join(bazel.RunfilesDir(), "../clockwork+", path)
}

// test runs Gazelle on a temporary directory with the given input files, and asserts that Gazelle
// generated the expected output files.
func test(t *testing.T, inputFiles, expectedOutputFiles []testtools.FileSpec) {
	// These paths were determined with a bit of trial and error, using --sandbox_debug.
	gazelleAbsPath := fix_runfiles_path("tools/gazelle/clk_plugin/gazelle_clk_test_binary_/gazelle_clk_test_binary")
	// Write the input files to a temporary directory.
	dir, cleanup := testtools.CreateFiles(t, inputFiles)
	defer cleanup()

	// Run Gazelle.
	cmd := exec.Command(gazelleAbsPath, "--clockwork_is_root_repo")
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Dir = dir
	require.NoError(t, cmd.Run())

	// Assert that Gazelle generated the expected files.
	testtools.CheckFiles(t, dir, expectedOutputFiles)
}
