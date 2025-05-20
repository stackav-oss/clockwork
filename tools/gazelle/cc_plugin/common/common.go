// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Package common contains any code used by two or more packages. It avoid circular dependencies.
package common

import (
	"encoding/json"
	"regexp"
	"strings"
)

const CCBinaryRule = "cc_binary"
const CCLibraryRule = "cc_library"
const CCTestRule = "cc_test"

// An include parsed from "".  Can optionally provide a repo based on
// a matching prefix with command line arguments.
type Include struct {
	Repo        string
	IncludePath string
}

// Used to map a prefix of an include to a repository.  Specified via
// command line arguments.
type ExternalRepo struct {
	Prefix string
	Repo   string
}

// ImportsParsedFromRuleSources is the "imports" interface returned by Language.GenerateRules(), and
// passed by Gazelle to Resolver.Resolve().
type ImportsParsedFromRuleSources interface {
	// GetRepoIncludes returns the files that are included with "". For the most part, these are within
	// the root bazel module.  However, command line flags can be used to match an external module
	// based on a prefix. These files should be resolved and added to the dependency list for this rule.
	GetRepoIncludes() []Include
	// GetSystemIncludes returns the files that are presumed to be provided via the toolchain.
	// Therefore, they do not need to be added to dependencies. As such, they are currently ignored,
	// but could be useful in the future.
	GetSystemIncludes() []string
	// GetThirdPartyIncludes returns the files that are provided by third-party libraries. The deps
	// for these should be configured in the thirdPartyMap.
	GetThirdPartyIncludes() []string
	// FindThirdPartyDep returns a dep for the given include if one can be found, and a flag showing
	// whether or not a dep was found.
	FindThirdPartyDep(include string) (string, bool)
}

// Holds a regexp and a dep so we can iterate through them
type ThirdPartyMapItem struct {
	Re          string
	Dep         string
	compiled_re *regexp.Regexp
}

// UnmarshalThirdPartyMapItems unmarshals a JSON object into a slice of ThirdPartyMapItem
// while preserving the order of entries as they appear in the JSON.
func UnmarshalThirdPartyMapItems(data []byte) ([]ThirdPartyMapItem, error) {
	var items []ThirdPartyMapItem
	decoder := json.NewDecoder(strings.NewReader(string(data)))

	// Read opening brace
	if _, err := decoder.Token(); err != nil {
		return nil, err
	}

	// Read all key-value pairs in order
	for decoder.More() {
		// Get the key
		token, err := decoder.Token()
		if err != nil {
			return nil, err
		}
		key := token.(string)

		// Get the value
		token, err = decoder.Token()
		if err != nil {
			return nil, err
		}
		value := token.(string)

		// Add to our ordered slice
		items = append(items, ThirdPartyMapItem{
			Re:  key,
			Dep: value,
		})
	}

	return items, nil
}

// importsParsedFromRuleSourcesImpl implements the common.ImportsParsedFromRuleSources interface.
type importsParsedFromRuleSourcesImpl struct {
	repoHeaders       []Include
	systemHeaders     []string
	thirdPartyHeaders []string

	thirdPartyDeps []ThirdPartyMapItem
}

func (i importsParsedFromRuleSourcesImpl) GetRepoIncludes() []Include {
	return i.repoHeaders
}

func (i importsParsedFromRuleSourcesImpl) GetSystemIncludes() []string {
	return i.systemHeaders
}

func (i importsParsedFromRuleSourcesImpl) GetThirdPartyIncludes() []string {
	return i.thirdPartyHeaders
}

func (i importsParsedFromRuleSourcesImpl) FindThirdPartyDep(include string) (string, bool) {
	for _, dep_info := range i.thirdPartyDeps {
		if dep_info.compiled_re.MatchString(include) {
			replacedDep := dep_info.compiled_re.ReplaceAllString(include, dep_info.Dep)
			return replacedDep, true
		}
	}
	return "", false
}

func NewImports(repoHeaders []Include, systemHeaders []string, thirdPartyHeaders []string, thirdPartyMap []ThirdPartyMapItem) ImportsParsedFromRuleSources {
	var thirdPartyDeps []ThirdPartyMapItem
	for _, item := range thirdPartyMap {
		thirdPartyDeps = append(thirdPartyDeps, ThirdPartyMapItem{
			Dep:         item.Dep,
			compiled_re: regexp.MustCompile("^" + item.Re + "$"),
		})
	}
	return importsParsedFromRuleSourcesImpl{
		repoHeaders:       repoHeaders,
		systemHeaders:     systemHeaders,
		thirdPartyHeaders: thirdPartyHeaders,
		thirdPartyDeps:    thirdPartyDeps,
	}
}

var _ ImportsParsedFromRuleSources = importsParsedFromRuleSourcesImpl{}

// TrimCSuffix removes the .h, .cpp, etc suffix from the given file.
func TrimCSuffix(name string) string {
	return strings.TrimSuffix(name, getSuffix(name))
}

// From https://docs.bazel.build/versions/main/be/c-cpp.html#cc_binary.srcs
var cppHeaders = []string{".cuh", ".h", ".hh", ".hpp", ".hxx", ".H"}
var cppSrcs = []string{".c", ".cc", ".cpp", ".cxx", ".cu", ".c++", ".C", ".inl"}

// IsCppHeader returns true if the given file name ends with one of the suffixes that Bazel
// recognizes as a C or C++ header file.
func IsCppHeader(name string) bool {
	suffix := getSuffix(name)
	for _, ext := range cppHeaders {
		if suffix == ext {
			return true
		}
	}
	return false
}

// IsCppCcInl returns true if the given file name ends with ".cc.inl".
func IsCppCcInl(name string) bool {
	return strings.HasSuffix(name, ".cc.inl")
}

// GetCppCcInlStem returns the stem part of stem.cc.inl
func GetCppCcInlStem(name string) string {
	return strings.TrimSuffix(name, ".cc.inl")
}

// IsCppSource returns true if the given file name ends with one of the suffixes that Bazel
// recognizes as a C or C++ source file.
func IsCppSource(name string) bool {
	suffix := getSuffix(name)
	for _, ext := range cppSrcs {
		if suffix == ext {
			return true
		}
	}
	return false
}

func getSuffix(name string) string {
	idx := strings.LastIndex(name, ".")
	if idx < 0 {
		return ""
	}
	return name[idx:]
}
