// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package common

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestImportsParsedFromRuleSources(t *testing.T) {
	import1 := ClkImport{Repo: "clockwork", ImportPath: "aa::bb::cc"}
	import2 := ClkImport{Repo: "", ImportPath: "bb::cc::dd"}
	import4 := ClkImport{Repo: "clockwork", ImportPath: "clockwork::scaffolding::online_main"}
	imports := NewImports(
		true,
		[]string{"cpp", "nanobind"},
		[]ClkImport{import1},
		[]ClkImport{import2},
		[]ClkImport{import1, import2},
		[]ClkImport{import2, import1},
		[]ClkImport{import4},
		"proto/import/path",
	)
	assert.True(t, imports.GetClkGenerate())
	assert.Equal(t, imports.GetClkGenerates(), []string{"cpp", "nanobind"})
	assert.Equal(t, imports.GetClkImports(), []ClkImport{import1})
	assert.Equal(t, imports.GetCppImports(), []ClkImport{import2})
	assert.Equal(t, imports.GetProtoImports(), []ClkImport{import1, import2})
	assert.Equal(t, imports.GetPyImports(), []ClkImport{import2, import1})
	assert.Equal(t, imports.GetCppExeImports(), []ClkImport{import4})
	assert.Equal(t, imports.GetGoProtoImportPath(), "proto/import/path")
}

func TestGetSuffix_Success(t *testing.T) {
	assert.Equal(t, ".h", getSuffix("foo.h"))
	assert.Equal(t, ".clk", getSuffix("foo.clk"))
	assert.Equal(t, "", getSuffix("foo"))
}

func TestIsClkSource_Success(t *testing.T) {
	assert.True(t, IsClkSource("foo.clk"))
	assert.False(t, IsClkSource("foo.hh"))
	assert.False(t, IsClkSource("foo.cc"))
	assert.False(t, IsClkSource("foo.inl"))
	assert.False(t, IsClkSource("foo.proto"))
	assert.False(t, IsClkSource("foo.py"))
	assert.False(t, IsClkSource("foo"))
}

func TestGetClkRuleName(t *testing.T) {
	assert.Equal(t, GetClkRuleName("foo.clk"), "foo_clk")
	assert.Equal(t, GetClkCppRuleName("foo.clk"), "foo_clk_cc")
	assert.Equal(t, GetClkNanobindRuleName("foo.clk"), "foo_clk_nb")
	assert.Equal(t, GetClkProtoRuleName("foo.clk"), "foo_clk_proto")
	assert.Equal(t, GetClkPyRuleName("foo.clk"), "foo_clk_py")
	assert.Equal(t, GetCppHdrRuleName("aa/bb/foo.hh"), "aa::bb::foo")
	assert.Equal(t, GetCppHdrRuleName("a/b/c/foo.pb.h"), "a::b::c::foo_cc_library")
}

func TestGetClockworkLabelString(t *testing.T) {
	assert.Equal(t, GetClockworkLabelString("//label", false), "@clockwork//label")
	assert.Equal(t, GetClockworkLabelString("//label", true), "//label")
}

func TestGetClockworkTargetString(t *testing.T) {
	assert.Equal(t, GetClockworkTargetString("target::string", false), "@clockwork::target::string")
	assert.Equal(t, GetClockworkTargetString("target::string", true), "target::string")
}

func TestSortedStringsFromStringSet(t *testing.T) {
	assert.Equal(t, SortedStringsFromStringSet(map[string]bool{"s1": true, "s2": true}), []string{"s1", "s2"})
}

func TestSortedClkImportsFromStringSet(t *testing.T) {
	import1 := ClkImport{Repo: "", ImportPath: "b::c::d"}
	import2 := ClkImport{Repo: "a", ImportPath: "b::c"}
	assert.Equal(t, SortedClkImportsFromStringSet(map[string]bool{"@a::b::c": true, "b::c::d": true}), []ClkImport{import1, import2})
}
