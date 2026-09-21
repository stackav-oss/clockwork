// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package resolver

import (
	"testing"

	"github.com/bazelbuild/bazel-gazelle/label"
	"github.com/bazelbuild/bazel-gazelle/rule"
	"github.com/stretchr/testify/assert"

	"github.com/stackav-oss/clockwork/tools/gazelle/clk_plugin/common"
)

func TestResolveGenerateAll(t *testing.T) {
	inputRule := rule.NewRule(common.ClkRule, "test_file_clk")
	inputLabel := label.New("test_repo", "src/testing/clockwork", "test_file_clk")

	inputGenerate := true
	inputGenerates := []string{"cpp", "cpp_cog", "cpp_test_cog", "cpp_exe", "go_proto", "nanobind", "proto", "proto_conv", "py", "py_cog"}
	inputClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk"},
		common.ClkImport{Repo: "test_repo", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk"},
	}
	inputCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file1_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk_proto_conv"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk_cc"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_cc"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_proto_conv"},
	}
	inputProtoImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk_proto"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk_proto"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_proto"},
	}
	inputPyImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk_py"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk_py"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_nb"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_py"},
	}
	inputCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::online_main"},
	}
	inputGoProtoImportPath := "clockwork.com/src/testing/clockwork/proto"
	inputImports := common.NewImports(
		inputGenerate,
		inputGenerates,
		inputClkImports,
		inputCppImports,
		inputProtoImports,
		inputPyImports,
		inputCppExeImports,
		inputGoProtoImportPath)

	r := ClkResolver{}

	r.Resolve(nil, nil, nil, inputRule, inputImports, inputLabel)

	assert.Equal(t, []string{
		"name",
		"outs",
		"cpp_deps",
		"cpp_exe_deps",
		"generate",
		"go_import_path",
		"proto_deps",
		"py_deps",
		"deps",
	}, inputRule.AttrKeys())

	assert.Equal(t, []string{
		"test_file_clk_cc.cc",
		"test_file_clk_cc.hh",
		"test_file_clk_cc.inl",
		"test_file_clk_cc_cog.cc",
		"test_file_clk_cc_cog.hh",
		"test_file_clk_cc_cog.inl",
		"test_file_clk_cc_dial.cc",
		"test_file_clk_cc_dial.hh",
		"test_file_clk_cc_dial.inl",
		"test_file_clk_cc_impl.cc",
		"test_file_clk_cc_impl.hh",
		"test_file_clk_cc_impl.inl",
		"test_file_clk_cc_test.cc",
		"test_file_clk_cc_test.hh",
		"test_file_clk_cc_test.inl",
		"test_file_clk_cc_types.cc",
		"test_file_clk_cc_types.hh",
		"test_file_clk_cc_types.inl",
		"test_file_clk_exe.cc",
		"test_file_clk_exe.hh",
		"test_file_clk_exe.inl",
		"test_file_clk_nb.cc",
		"test_file_clk_nb.hh",
		"test_file_clk_nb.inl",
		"test_file_clk_proto.proto",
		"test_file_clk_proto_conv.cc",
		"test_file_clk_proto_conv.hh",
		"test_file_clk_proto_conv.inl",
		"test_file_clk_py.py",
		"test_file_clk_py_dial.py",
	}, inputRule.AttrStrings("outs"))

	assert.Equal(t, []string{
		":file1_clk",
		":file2_clk",
		":file3_clk",
		":file4_clk",
		":file5_clk",
		"@other_repo//src/testing/clockwork:file2_clk",
	}, inputRule.AttrStrings("deps"))

	assert.Equal(t, []string{
		":file1_clk_cc",
		":file2_clk_cc",
		":file2_clk_proto_conv",
		":file3_clk_cc",
		"@other_repo//src/testing/clockwork:file2_clk_cc",
		"@other_repo//src/testing/clockwork:file2_clk_proto_conv",
	}, inputRule.AttrStrings("cpp_deps"))

	assert.Equal(t, []string{
		":file3_clk_py",
		":file4_clk_nb",
		":file5_clk_nb",
		":file5_clk_py",
		"@other_repo//src/testing/clockwork:file2_clk_nb",
		"@other_repo//src/testing/clockwork:file2_clk_py",
	}, inputRule.AttrStrings("py_deps"))

	assert.Equal(t, []string{
		":file2_clk_proto",
		":file4_clk_proto",
		"@other_repo//src/testing/clockwork:file2_clk_proto",
	}, inputRule.AttrStrings("proto_deps"))

	assert.Equal(t, []string{
		"//clockwork/scaffolding:online_main",
	}, inputRule.AttrStrings("cpp_exe_deps"))

	assert.Equal(t, "clockwork.com/src/testing/clockwork/proto", inputRule.AttrString("go_import_path"))
}

func TestResolveGeneratePyExe(t *testing.T) {
	inputRule := rule.NewRule(common.ClkRule, "test_file_clk")
	inputLabel := label.New("test_repo", "src/testing/clockwork", "test_file_clk")

	inputGenerate := true
	inputGenerates := []string{"py", "py_cog", "py_exe"}
	inputClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk"},
		common.ClkImport{Repo: "test_repo", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk"},
	}
	inputCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file1_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk_proto_conv"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk_cc"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_cc"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_proto_conv"},
	}
	inputProtoImports := []common.ClkImport{}
	inputPyImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk_py"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk_py"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_nb"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk_py"},
	}
	inputCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::offline_main"},
	}
	inputGoProtoImportPath := ""
	inputImports := common.NewImports(
		inputGenerate,
		inputGenerates,
		inputClkImports,
		inputCppImports,
		inputProtoImports,
		inputPyImports,
		inputCppExeImports,
		inputGoProtoImportPath)

	r := ClkResolver{}

	r.Resolve(nil, nil, nil, inputRule, inputImports, inputLabel)

	assert.Equal(t, []string{
		"name",
		"outs",
		"cpp_deps",
		"cpp_exe_deps",
		"generate",
		"py_deps",
		"deps",
	}, inputRule.AttrKeys())

	assert.Equal(t, []string{
		"test_file_clk_cc_cog.cc",
		"test_file_clk_cc_cog.hh",
		"test_file_clk_cc_cog.inl",
		"test_file_clk_cc_dial.cc",
		"test_file_clk_cc_dial.hh",
		"test_file_clk_cc_dial.inl",
		"test_file_clk_cc_impl.cc",
		"test_file_clk_cc_impl.hh",
		"test_file_clk_cc_impl.inl",
		"test_file_clk_exe.cc",
		"test_file_clk_exe.hh",
		"test_file_clk_exe.inl",
		"test_file_clk_py.py",
		"test_file_clk_py_dial.py",
	}, inputRule.AttrStrings("outs"))

	assert.Equal(t, []string{
		":file1_clk",
		":file2_clk",
		":file3_clk",
		":file4_clk",
		":file5_clk",
		"@other_repo//src/testing/clockwork:file2_clk",
	}, inputRule.AttrStrings("deps"))

	assert.Equal(t, []string{
		":file1_clk_cc",
		":file2_clk_cc",
		":file2_clk_proto_conv",
		":file3_clk_cc",
		"@other_repo//src/testing/clockwork:file2_clk_cc",
		"@other_repo//src/testing/clockwork:file2_clk_proto_conv",
	}, inputRule.AttrStrings("cpp_deps"))

	assert.Equal(t, []string{
		":file3_clk_py",
		":file4_clk_nb",
		":file5_clk_nb",
		":file5_clk_py",
		"@other_repo//src/testing/clockwork:file2_clk_nb",
		"@other_repo//src/testing/clockwork:file2_clk_py",
	}, inputRule.AttrStrings("py_deps"))

	assert.Equal(t, []string{
		"//clockwork/scaffolding:offline_main",
	}, inputRule.AttrStrings("cpp_exe_deps"))
}

func TestResolveGenerateAlignerWithTestCog(t *testing.T) {
	inputRule := rule.NewRule(common.ClkRule, "test_aligner_clk")
	inputLabel := label.New("test_repo", "src/testing/clockwork", "test_aligner_clk")

	inputGenerate := true
	inputGenerates := []string{"cpp", "cpp_aligner", "cpp_test_cog"}
	inputClkImports := []common.ClkImport{}
	inputCppImports := []common.ClkImport{}
	inputProtoImports := []common.ClkImport{}
	inputPyImports := []common.ClkImport{}
	inputCppExeImports := []common.ClkImport{}
	inputGoProtoImportPath := ""
	inputImports := common.NewImports(
		inputGenerate,
		inputGenerates,
		inputClkImports,
		inputCppImports,
		inputProtoImports,
		inputPyImports,
		inputCppExeImports,
		inputGoProtoImportPath)

	r := ClkResolver{}

	r.Resolve(nil, nil, nil, inputRule, inputImports, inputLabel)

	// cpp_test_cog with cpp_aligner (but no cpp_cog) produces the unified
	// _cc_test files via the umbrella library.
	assert.Equal(t, []string{
		"test_aligner_clk_cc.cc",
		"test_aligner_clk_cc.hh",
		"test_aligner_clk_cc.inl",
		"test_aligner_clk_cc_cog.cc",
		"test_aligner_clk_cc_cog.hh",
		"test_aligner_clk_cc_cog.inl",
		"test_aligner_clk_cc_dial.cc",
		"test_aligner_clk_cc_dial.hh",
		"test_aligner_clk_cc_dial.inl",
		"test_aligner_clk_cc_impl.cc",
		"test_aligner_clk_cc_impl.hh",
		"test_aligner_clk_cc_impl.inl",
		"test_aligner_clk_cc_test.cc",
		"test_aligner_clk_cc_test.hh",
		"test_aligner_clk_cc_test.inl",
		"test_aligner_clk_cc_types.cc",
		"test_aligner_clk_cc_types.hh",
		"test_aligner_clk_cc_types.inl",
	}, inputRule.AttrStrings("outs"))
}

func TestResolveGenerateConsumerWithComboTest(t *testing.T) {
	inputRule := rule.NewRule(common.ClkRule, "consumer_clk")
	inputLabel := label.New("test_repo", "src/testing/clockwork", "consumer_clk")

	inputGenerate := true
	inputGenerates := []string{"cpp", "cpp_cog", "cpp_test_cog", "cpp_combo_test"}
	inputClkImports := []common.ClkImport{}
	inputCppImports := []common.ClkImport{}
	inputProtoImports := []common.ClkImport{}
	inputPyImports := []common.ClkImport{}
	inputCppExeImports := []common.ClkImport{}
	inputGoProtoImportPath := ""
	inputImports := common.NewImports(
		inputGenerate,
		inputGenerates,
		inputClkImports,
		inputCppImports,
		inputProtoImports,
		inputPyImports,
		inputCppExeImports,
		inputGoProtoImportPath)

	r := ClkResolver{}

	r.Resolve(nil, nil, nil, inputRule, inputImports, inputLabel)

	// cpp_combo_test adds no extra outs beyond what cpp_cog + cpp_test_cog produce
	assert.Equal(t, []string{
		"consumer_clk_cc.cc",
		"consumer_clk_cc.hh",
		"consumer_clk_cc.inl",
		"consumer_clk_cc_cog.cc",
		"consumer_clk_cc_cog.hh",
		"consumer_clk_cc_cog.inl",
		"consumer_clk_cc_dial.cc",
		"consumer_clk_cc_dial.hh",
		"consumer_clk_cc_dial.inl",
		"consumer_clk_cc_test.cc",
		"consumer_clk_cc_test.hh",
		"consumer_clk_cc_test.inl",
		"consumer_clk_cc_types.cc",
		"consumer_clk_cc_types.hh",
		"consumer_clk_cc_types.inl",
	}, inputRule.AttrStrings("outs"))
}

func TestResolveGenerateNone(t *testing.T) {
	inputRule := rule.NewRule(common.ClkRule, "test_file_clk")
	inputLabel := label.New("test_repo", "src/testing/clockwork", "test_file_clk")

	inputGenerate := true
	inputGenerates := []string{}
	inputClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file2_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file3_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file4_clk"},
		common.ClkImport{Repo: "", ImportPath: "src::testing::clockwork::file5_clk"},
		common.ClkImport{Repo: "test_repo", ImportPath: "src::testing::clockwork::file1_clk"},
		common.ClkImport{Repo: "other_repo", ImportPath: "src::testing::clockwork::file2_clk"},
	}
	inputCppImports := []common.ClkImport{}
	inputProtoImports := []common.ClkImport{}
	inputPyImports := []common.ClkImport{}
	inputCppExeImports := []common.ClkImport{}
	inputGoProtoImportPath := ""
	inputImports := common.NewImports(
		inputGenerate,
		inputGenerates,
		inputClkImports,
		inputCppImports,
		inputProtoImports,
		inputPyImports,
		inputCppExeImports,
		inputGoProtoImportPath)

	r := ClkResolver{}

	r.Resolve(nil, nil, nil, inputRule, inputImports, inputLabel)

	assert.Equal(t, []string{
		"name",
		"generate",
		"deps",
	}, inputRule.AttrKeys())

	assert.Equal(t, []string{"none"}, inputRule.AttrStrings("generate"))

	assert.Equal(t, []string{
		":file1_clk",
		":file2_clk",
		":file3_clk",
		":file4_clk",
		":file5_clk",
		"@other_repo//src/testing/clockwork:file2_clk",
	}, inputRule.AttrStrings("deps"))
}
