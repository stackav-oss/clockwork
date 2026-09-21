// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Package parsers defines parsers for the paths of #include statements from C++ header and
// source files.
//
// It makes use of regular expressions. These should be powerful enough for the job, but not
// too complicated, because the C++ code this extension is meant to work on follows some style
// guidelines which mean we do not necessarily have to use LLVM or similar to properly run the
// preprocessor directives before figuring out the includes.
package parsers

import (
	"regexp"
	"sort"
	"strings"

	common "github.com/stackav-oss/clockwork/tools/gazelle/cc_plugin/common"
	parsers "github.com/stackav-oss/clockwork/tools/gazelle/parser"
)

// First-party headers are anything with quotes.
var repoIncludeRegex = regexp.MustCompile(`^\s*#\s*include\s+"(?P<file>.+)"`)

// System headers have angle brackets and no dots or slashes.
// There are other system headers as well that are handled in the mapping json through the use of a sentinel value.
var systemIncludeRegex = regexp.MustCompile(`^\s*#\s*include\s+<(?P<file>[^./]+)>`)

// Third-party headers are anything else in angle brackets
var thirdPartyIncludeRegex = regexp.MustCompile(`^\s*#\s*include\s+<(?P<file>.+)>`)

func ParseCIncludes(source string, externalRepos []common.ExternalRepo) ([]common.Include, []string, []string) {
	// Remove comments from the source file.
	lines := parsers.SplitLinesAndRemoveComments(source)

	// Extract all imports.
	repoIncludeSet := map[common.Include]bool{}
	systemIncludeSet := map[string]bool{}
	thirdPartyIncludeSet := map[string]bool{}
	for _, line := range lines {
		match := repoIncludeRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			importPath := match[1]
			repo := ""
			// Some includes with "" will be mapped external repos.
			for _, ext_repo := range externalRepos {
				if strings.HasPrefix(importPath, ext_repo.Prefix) {
					repo = ext_repo.Repo
					break
				}
			}

			repoIncludeSet[common.Include{Repo: repo, IncludePath: importPath}] = true
			continue
		}
		match = systemIncludeRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			importPath := match[1]
			systemIncludeSet[importPath] = true
			continue
		}
		match = thirdPartyIncludeRegex.FindStringSubmatch(line)
		if len(match) != 0 {
			importPath := match[1]
			thirdPartyIncludeSet[importPath] = true
			continue
		}
	}

	return setToSliceInclude(repoIncludeSet), setToSliceStr(systemIncludeSet), setToSliceStr(thirdPartyIncludeSet)
}

func setToSliceStr(set map[string]bool) []string {
	var slice []string
	for path := range set {
		slice = append(slice, path)
	}
	sort.Strings(slice)
	return slice
}

func setToSliceInclude(set map[common.Include]bool) []common.Include {
	var slice []common.Include
	for ext_include := range set {
		slice = append(slice, ext_include)
	}
	sort.Slice(slice, func(lhs, rhs int) bool {
		if slice[lhs].Repo != slice[rhs].Repo {
			return slice[lhs].Repo < slice[rhs].Repo
		}
		return slice[lhs].IncludePath < slice[rhs].IncludePath
	})
	return slice
}
