// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Package parsers defines parsers to extract the generated targets and dependencies from clockwork files.
//
// The clockwork language was designed so that regular expressions could be used by the gazelle parser.
package parsers

import (
	"regexp"
	"slices"
	"strings"

	common "github.com/stackav-oss/clockwork/tools/gazelle/clk_plugin/common"
)

// Regular expression to match text before a "//" comment
var textBeforeCommentRegecx = regexp.MustCompile(`^(?P<code>.*)//"`)

// Regular expression to match a generate inner-attribute
var generateRegex = regexp.MustCompile(`\s*#!\[\s*generate\s*\(`)

// Regular expression prefix to match a generate target
const generateTargetRegexPrefix = `\s*#!\[\s*generate.*[\(,]\s*`

// Regular expression suffix to match a generate target
const generateTargetRegexSuffix = `\s*[,\)]`

// Regular expression to match a use statement and extract the namespace identifier
var useRegex = regexp.MustCompile(`^\s*use\s*(?:\[[^\]]*\])?\s*(?P<identifier>@?[a-zA-Z][a-zA-Z0-9_]*(?:\:\:[a-zA-Z][a-zA-Z0-9_]*)*)`)

// Regular expression to match a use statement with no targets
var useDefaultRegex = regexp.MustCompile(`^\s*use\s+@?[a-zA-Z]`)

// Regular expression to match a use[] statement
var useNoneRegex = regexp.MustCompile(`^\s*use\s*\[\s*\]`)

// Regular expression prefix to match a use target
const useTargetRegexPrefix = `^\s*use.*[\[,]\s*`

// Regular expression suffix to match a use target
const useTargetRegexSuffix = `\s*[\],]`

// Regular expression to capture an exe offline attribute
var exeOfflineRegex = regexp.MustCompile(`\s*#!?\[\s*exe\s*\(\s*offline\s*=\s*(?P<offline>[a-z]+)\s*\)\s*\]`)

// Regular expression to capture a cpp type_header attribute
var cppTypeHeaderRegex = regexp.MustCompile(`\s*#!?\[\s*cpp.*[\(,]\s*type_header\s*=\s*"(?P<header>[^"]*)"\s*[\),]`)

// Regular expression to capture a proto go_package attribute
var protoGoPackageRegex = regexp.MustCompile(`\s*#!?\[\s*proto.*[\(,]\s*go_package\s*=\s*(?P<package>[^ ,\)]*)\s*[\),]`)

// Clockwork executable online main target
const clkOnlineMain = "clockwork::scaffolding::online_main"

// Clockwork cpp executable offline main target
const clkOfflineMain = "clockwork::scaffolding::offline_main"

// Clockwork py executable online main target
const clkPythonOnlineMain = "clockwork::scaffolding::python_online_main"

// Clockwork py executable offline main target
const clkPythonOfflineMain = "clockwork::scaffolding::python_offline_main"

func ParseClkFile(source string, inClockworkRepo bool) common.ImportsParsedFromRuleSources {
	generate := false
	generates := map[string]bool{}
	defaultImports := map[string]bool{}
	clkImports := map[string]bool{}
	cppImports := map[string]bool{}
	protoImports := map[string]bool{}
	pyImports := map[string]bool{}
	cppExeImports := map[string]bool{}
	goProtoImportPath := ""

	lines := strings.Split(source, "\n")
	for _, line := range lines {
		match := textBeforeCommentRegecx.FindStringSubmatch(line)
		if len(match) != 0 {
			line = match[1]
		}

		if generateRegex.MatchString(line) {
			generate = true
			generates, defaultImports = parseGenerateLine(line)
		}

		match = useRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			importPath := match[1]
			clkImports[common.GetClkRuleName(importPath)] = true
			imports := parseUseImports(line, defaultImports)
			for imprt := range imports {
				if imprt == common.ClkCppTarget {
					cppImports[common.GetClkCppRuleName(importPath)] = true
				} else if imprt == common.ClkNanobindTarget {
					pyImports[common.GetClkNanobindRuleName(importPath)] = true
				} else if imprt == common.ClkProtoTarget {
					protoImports[common.GetClkProtoRuleName(importPath)] = true
				} else if imprt == common.ClkProtoConvTarget {
					cppImports[common.GetClkProtoConvRuleName(importPath)] = true
				} else if imprt == common.ClkPyTarget {
					pyImports[common.GetClkPyRuleName(importPath)] = true
				} else if imprt == common.ClkPyCogTarget {
					pyImports[common.GetClkPyDialRuleName(importPath)] = true
					pyImports[common.GetClkPyImplRuleName(importPath)] = true
				}
			}
		}

		match = cppTypeHeaderRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			cppImports[common.GetCppHdrRuleName(match[1])] = true
		}

		match = exeOfflineRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			if match[1] == "true" {
				if _, generateCppExe := generates[common.ClkCppExeTarget]; generateCppExe {
					cppExeImports = map[string]bool{common.GetClockworkTargetString(clkOfflineMain, inClockworkRepo): true}
				} else {
					cppExeImports = map[string]bool{common.GetClockworkTargetString(clkPythonOfflineMain, inClockworkRepo): true}
				}

			} else {
				if _, generateCppExe := generates[common.ClkCppExeTarget]; generateCppExe {
					cppExeImports = map[string]bool{common.GetClockworkTargetString(clkOnlineMain, inClockworkRepo): true}
				} else {
					cppExeImports = map[string]bool{common.GetClockworkTargetString(clkPythonOnlineMain, inClockworkRepo): true}
				}
			}
		}

		match = protoGoPackageRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			goProtoImportPath = match[1]
		}
	}

	if len(cppExeImports) == 0 {
		if _, generateCppExe := generates[common.ClkCppExeTarget]; generateCppExe {
			cppExeImports[common.GetClockworkTargetString(clkOnlineMain, inClockworkRepo)] = true
		} else if _, generatePyExe := generates[common.ClkPyExeTarget]; generatePyExe {
			cppExeImports[common.GetClockworkTargetString(clkPythonOnlineMain, inClockworkRepo)] = true
		}
	}

	return common.NewImports(
		generate,
		common.SortedStringsFromStringSet(generates),
		common.SortedClkImportsFromStringSet(clkImports),
		common.SortedClkImportsFromStringSet(cppImports),
		common.SortedClkImportsFromStringSet(protoImports),
		common.SortedClkImportsFromStringSet(pyImports),
		common.SortedClkImportsFromStringSet(cppExeImports),
		goProtoImportPath,
	)
}

func parseGenerateLine(line string) (map[string]bool, map[string]bool) {
	generates := map[string]bool{}
	defaultImports := map[string]bool{}
	for _, target := range common.AllGenerateTargets {
		generateTargetRegex := regexp.MustCompile(generateTargetRegexPrefix + target + generateTargetRegexSuffix)
		if generateTargetRegex.MatchString(line) {
			generates[target] = true
			if slices.Contains(common.AllUseTargets, target) {
				defaultImports[target] = true
			}
		}
	}
	if _, generatesCppExe := generates[common.ClkCppExeTarget]; generatesCppExe {
		defaultImports[common.ClkCppTarget] = true
	}
	if _, generatesPyExe := generates[common.ClkPyExeTarget]; generatesPyExe {
		defaultImports[common.ClkCppTarget] = true
	}
	return generates, defaultImports
}

func parseUseImports(line string, defaultImports map[string]bool) map[string]bool {
	if useDefaultRegex.MatchString(line) {
		return defaultImports
	}
	imports := map[string]bool{}
	if useNoneRegex.MatchString(line) {
		return imports
	}
	for _, target := range common.AllUseTargets {
		useTargetRegex := regexp.MustCompile(useTargetRegexPrefix + target + useTargetRegexSuffix)
		if useTargetRegex.MatchString(line) {
			imports[target] = true
		}
	}
	return imports
}
