// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package language

import (
	"testing"

	"github.com/bazelbuild/bazel-gazelle/rule"
	"github.com/stretchr/testify/assert"

	"github.com/stackav-oss/clockwork/tools/gazelle/cc_plugin/common"
)

func TestMakeRulesFromFiles_UsesSuffixToSetAttributes(t *testing.T) {
	fileNames := []string{"alpha.h", "alpha.cpp", "beta.H", "beta.cc", "gamma.hh", "delta.cxx",
		"gamma.c", "delta.hpp", "epsilon.h", "zeta.cpp", "SkTypes.h", "SkUtils.cpp"}

	rules := makeRulesFromFiles(fileNames)

	assertRulesMatch(t, []*rule.Rule{
		library("alpha", "alpha.h", "alpha.cpp"),
		library("beta", "beta.H", "beta.cc"),
		library("gamma", "gamma.hh", "gamma.c"),
		library("delta", "delta.hpp", "delta.cxx"),
		library("epsilon", "epsilon.h", ""),
		library("zeta", "", "zeta.cpp"),
		library("SkTypes", "SkTypes.h", ""),
		library("SkUtils", "", "SkUtils.cpp"),
	}, rules)
}

func TestFindEmptyRules_Success(t *testing.T) {
	rulesInExistingBuildFile := []*rule.Rule{
		library("alpha", "alpha.h", "alpha.cpp"),
		library("SkTypes", "SkTypes.h", ""),
		library("SkUtils", "", "SkUtils.cpp"),
	}

	rulesFromSourceFiles := []*rule.Rule{
		library("Alpha", "Alpha.h", "Alpha.cpp"),
		library("SkTypes", "SkTypes.h", ""),
	}

	actualEmpty := findEmptyRules(rulesInExistingBuildFile, rulesFromSourceFiles)

	// The returned rules just have the kind and name set
	assertRulesMatch(t, []*rule.Rule{
		rule.NewRule(common.CCLibraryRule, "alpha"),
		rule.NewRule(common.CCLibraryRule, "SkUtils"),
	}, actualEmpty)
}

func library(name, header string, source string) *rule.Rule {
	r := rule.NewRule(common.CCLibraryRule, name)
	if header != "" {
		r.SetAttr("hdrs", []string{header})
	}
	if source != "" {
		r.SetAttr("srcs", []string{source})
	}
	return r
}

type ruleAttrs struct {
	name string
	hdrs []string
	srcs []string
}

func assertRulesMatch(t *testing.T, expected, actual []*rule.Rule) {
	var eRules []ruleAttrs
	for _, r := range expected {
		eRules = append(eRules, ruleAttrs{
			name: r.Name(),
			hdrs: r.AttrStrings("hdrs"),
			srcs: r.AttrStrings("srcs"),
		})
	}
	var aRules []ruleAttrs
	for _, r := range actual {
		aRules = append(aRules, ruleAttrs{
			name: r.Name(),
			hdrs: r.AttrStrings("hdrs"),
			srcs: r.AttrStrings("srcs"),
		})
	}
	// This assertion is easier to debug when the two variables do not match.
	assert.ElementsMatch(t, eRules, aRules)
	assert.ElementsMatch(t, expected, actual)
}
