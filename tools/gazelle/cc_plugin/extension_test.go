// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package cc_plugin

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
		{Path: "WORKSPACE"}, // Gazelle requires that a WORKSPACE file exists, even if it's empty.
	}
}

func TestGazelle_NoExistingBuildFile_GeneratesBuildRules(t *testing.T) {
	unittest.BazelOnlyTest(t)

	inputFiles := append([]testtools.FileSpec{
		{
			Path: "include/avocado.h",
			Content: `
#define AVOCADO
`,
		}, {
			Path: "include/avocado.cpp",
			Content: `
#include "include/avocado.h"
`,
		}, {
			Path: "include/avocado_util.cpp",
			Content: `
#include "include/avocado.h"
#include "include/common_foods.h"
#include <spirv-tools/libspirv.hpp>
#include <third_party/externals/spirv-cross/spirv_hlsl.hpp>
`,
		}, {
			Path: "include/common_foods.h",
			Content: `
#define COMMON
`,
		},
		{
			Path: "include/avocado_desert.cpp",
			Content: `
#include "include/avocado.h"
#include "experimental/desserts/pudding.h"
#include <png.h>
#include <jpeg.h>
#include <string>
#include <replacement_1.h>
#include <replacement_2.h>
`,
		},
	}, makeBasicWorkspace()...)

	// Note: we see "@spirv_tools" instead of "@spirv_tools//:spirv_tools" (the value specified
	// in test_filemap.json) because the former can be simplified to the latter.
	// "When it matches the last component of the package path, it, and the colon, may be omitted"
	// https://bazel.build/concepts/labels
	// Gazelle does this simplification after generating the file.
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "include/BUILD.bazel",
			Content: `
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "avocado",
    srcs = ["avocado.cpp"],
    hdrs = ["avocado.h"],
)

cc_library(
    name = "avocado_desert",
    srcs = ["avocado_desert.cpp"],
    deps = [
        ":avocado",
        "//experimental/desserts:pudding",
        "//third_party:libpng",
        "@jpeg//:with_special_features",
        "@test//1",
        "@test//2",
    ],
)

cc_library(
    name = "avocado_util",
    srcs = ["avocado_util.cpp"],
    deps = [
        ":avocado",
        ":common_foods",
        "@spirv_cross",
        "@spirv_tools",
    ],
)

cc_library(
    name = "common_foods",
    hdrs = ["common_foods.h"],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_NewIncludesAreAdded(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend two new includes were added to util.cpp
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
	name = "hand_written_rule",
	deps = [
		":that_rule",
		"//experimental:this_rule",
	],
)

cc_library(
    name = "util_hdr",
    hdrs = ["util.h"],
    visibility = ["//:__subpackages__"],
    deps = ["//include:avocado_hdr"],
)

cc_library(
    name = "util_src",
    srcs = ["util_this_is_a_typo.cpp"],
    visibility = ["//:__subpackages__"],
    deps = [":util_hdr"],
)

`,
		},
		{
			Path: "src/util.h",
			Content: `
#include "include/avocado.h"
`,
		},
		{
			Path: "src/util.cpp",
			Content: `
#include "src/util.h"

#include "include/avocado.h"
#include "include/common_foods.h"
`,
		},
	}, makeBasicWorkspace()...)

	// We expect those to be added here.
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "util",
    srcs = ["util.cpp"],
    hdrs = ["util.h"],
    deps = [
        "//include:avocado",
        "//include:common_foods",
    ],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_MissingIncludesAreRemoved(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend one include was removed from util.cpp
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
	name = "hand_written_rule",
	deps = [
		":that_rule",
		"//experimental:this_rule",
	],
)

cc_library(
    name = "util_hdr",
    hdrs = ["util.h"],
    visibility = ["//:__subpackages__"],
    deps = ["//include:avocado_hdr"],
)

cc_library(
    name = "util_src",
    srcs = ["util.cpp"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":util_hdr",
        "//include:common_foods_hdr",
        "//tools/not/there:anymore_hdr",
    ],
)

`,
		},
		{
			Path: "src/util.h",
			Content: `
#include "include/avocado.h"
`,
		},
		{
			Path: "src/util.cpp",
			Content: `
#include "src/util.h"

#include "include/common_foods.h"
`,
		},
	}, makeBasicWorkspace()...)

	// We want that include ("//tools/not/there:anymore_hdr") to not be there anymore
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "util",
    srcs = ["util.cpp"],
    hdrs = ["util.h"],
    deps = [
        "//include:avocado",
        "//include:common_foods",
    ],
)
`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_MissingFilesHaveRulesRemoved(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend util.cpp was deleted.
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
	name = "hand_written_rule",
	deps = [
		":that_rule",
		"//experimental:this_rule",
	],
)

cc_library(
    name = "util_hdr",
    hdrs = ["util.h"],
    visibility = ["//:__subpackages__"],
    deps = ["//include:avocado_hdr"],
)

cc_library(
    name = "util_src",
    srcs = ["util.cpp"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":util_hdr",
        "//include:common_foods_hdr",
    ],
)
`,
		},
		{
			Path: "src/util.h",
			Content: `
#include "include/avocado.h"
`,
		},
	}, makeBasicWorkspace()...)

	// We want that rule to not be there anymore
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "util",
    hdrs = ["util.h"],
    deps = ["//include:avocado"],
)

`,
		},
	}

	test(t, inputFiles, expectedOutputFiles)
}

func TestGazelle_ExistingBuildFiles_OtherRulesUpdated(t *testing.T) {
	unittest.BazelOnlyTest(t)

	// In this testcase, we pretend util.cpp was deleted.
	inputFiles := append([]testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("@rules_cc//cc:defs.bzl", "cc_library")

py_cc_binding(
    name = "util",
    hdrs = ["util.cuh"],
    visibility = ["//:__subpackages__"],
)

py_cc_binding(
    name = "util_src",
    srcs = ["util.cpp"],
    visibility = ["//:__subpackages__"],
    deps = [
        ":util_hdr",
        "//include:common_foods_hdr",
    ],
)
`,
		},
		{
			Path: "src/util.cuh",
			Content: `
#include "include/avocado.h"
`,
		},
	}, makeBasicWorkspace()...)

	// We want that rule to not be there anymore
	expectedOutputFiles := []testtools.FileSpec{
		{
			Path: "src/BUILD.bazel",
			Content: `load("//tools/rules:python.bzl", "py_cc_binding")

py_cc_binding(
    name = "util",
    hdrs = ["util.cuh"],
    visibility = ["//:__subpackages__"],
    deps = ["//include:avocado"],
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
	gazelleAbsPath := fix_runfiles_path("tools/gazelle/cc_plugin/gazelle_cc_test_binary_/gazelle_cc_test_binary")
	filemapAbsPath := fix_runfiles_path("tools/gazelle/cc_plugin/resources/test_filemap.json")
	// Write the input files to a temporary directory.
	dir, cleanup := testtools.CreateFiles(t, inputFiles)
	defer cleanup()

	// Run Gazelle.
	cmd := exec.Command(gazelleAbsPath, "--third_party_file_map", filemapAbsPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Dir = dir
	require.NoError(t, cmd.Run())

	// Assert that Gazelle generated the expected files.
	testtools.CheckFiles(t, dir, expectedOutputFiles)
}
