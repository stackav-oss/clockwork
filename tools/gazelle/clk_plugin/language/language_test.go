// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package language

import (
	"testing"

	"github.com/bazelbuild/bazel-gazelle/rule"
	"github.com/stretchr/testify/assert"

	"github.com/stackav-oss/clockwork/tools/gazelle/clk_plugin/common"
)

func TestMakeRulesFromFiles_Success(t *testing.T) {
	fileNames := []string{"file1.clk", "file2.clk", "file3.clk", "file4.clk", "file5.cc", "file6.proto"}

	rules := makeRulesFromFiles(fileNames)

	assertRulesMatch(t, []*rule.Rule{
		makeClkRule("file1_clk", "file1.clk"),
		makeClkRule("file2_clk", "file2.clk"),
		makeClkRule("file3_clk", "file3.clk"),
		makeClkRule("file4_clk", "file4.clk"),
	}, rules)
}

func TestFindEmptyRules_Success(t *testing.T) {
	rulesInExistingBuildFile := []*rule.Rule{
		makeClkRule("file1_clk", "file1.clk"),
		makeClkRule("file2_clk", "file2.clk"),
		makeClkRule("file3_clk", "file3.clk"),
	}

	rulesFromSourceFiles := []*rule.Rule{
		makeClkRule("file3_clk", "file3.clk"),
		makeClkRule("file4_clk", "file4.clk"),
		makeClkRule("file5_clk", "file5.clk"),
	}

	actualEmpty := findEmptyRules(rulesInExistingBuildFile, rulesFromSourceFiles)

	// The returned rules just have the kind and name set
	assertRulesMatch(t, []*rule.Rule{
		rule.NewRule(common.ClkRule, "file1_clk"),
		rule.NewRule(common.ClkRule, "file2_clk"),
	}, actualEmpty)
}

func makeClkRule(name, source string) *rule.Rule {
	clkRule := rule.NewRule(common.ClkRule, name)
	clkRule.SetAttr("generate", []string{"none"})
	if source != "" {
		clkRule.SetAttr("srcs", []string{source})
	}
	return clkRule
}

type clkRuleAttrs struct {
	name string
	srcs []string
}

func assertRulesMatch(t *testing.T, expected, actual []*rule.Rule) {
	var expectedRules []clkRuleAttrs
	for _, r := range expected {
		r.DelAttr("generate")
		expectedRules = append(expectedRules, clkRuleAttrs{
			name: r.Name(),
			srcs: r.AttrStrings("srcs"),
		})
	}
	var actualRules []clkRuleAttrs
	for _, r := range actual {
		r.DelAttr("generate")
		actualRules = append(actualRules, clkRuleAttrs{
			name: r.Name(),
			srcs: r.AttrStrings("srcs"),
		})
	}
	// This assertion is easier to debug when the two variables do not match.
	assert.ElementsMatch(t, expectedRules, actualRules)
	assert.ElementsMatch(t, expected, actual)
}
