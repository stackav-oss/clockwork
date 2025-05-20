// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package resolver

import (
	"testing"

	"github.com/bazelbuild/bazel-gazelle/label"
	"github.com/bazelbuild/bazel-gazelle/rule"
	"github.com/stretchr/testify/assert"

	"github.com/stackav-oss/clockwork/tools/gazelle/cc_plugin/common"
)

func TestResolveDepsForCImport_RepoFiles_Success(t *testing.T) {
	test := func(repo, inputFile, expectedPackage, expectedRule string) {
		t.Run(inputFile, func(t *testing.T) {
			actual := resolveDepsForCImport(common.Include{Repo: repo, IncludePath: inputFile}, "containing_file")
			expectedLabel := label.New(repo, expectedPackage, expectedRule)
			assert.True(t, expectedLabel.Equal(actual), "%#v != %#v", expectedLabel, actual)
		})
	}
	test("", "include/gpu/GrDirectContext.h", "include/gpu", "GrDirectContext")
	test("", "src/gpu/BaseDevice.h", "src/gpu", "BaseDevice")
	test("", "src/gpu/gl/builders/GrGLProgramBuilder.h", "src/gpu/gl/builders", "GrGLProgramBuilder")
	test("", "src/core/SkUtil.cpp", "src/core", "SkUtil")
	test("repo", "src/core/Time.cpp", "src/core", "Time")
}

func TestSetDeps_IgnoreDuplicates(t *testing.T) {
	testLabel := label.New("@skia", "src/core", "SkFile")

	test := func(name string, newLabels []label.Label, expectedDeps []string) {
		t.Run(name, func(t *testing.T) {
			r := rule.NewRule(common.CCLibraryRule, "SkFile")
			setDeps(r, testLabel, newLabels)
			assert.Equal(t, expectedDeps, r.AttrStrings("deps"))
		})
	}

	test("oneNewDep",
		[]label.Label{label.New("@skia", "include/core", "SkTypes")},
		[]string{"//include/core:SkTypes"})
	test("multipleNewDeps",
		[]label.Label{
			label.New("@skia", "include/core", "SkTypes"),
			label.New("@skia", "include/core", "SkMath"),
			label.New("@skia", "src/core", "SkMacros"),
			label.New("@skia", "include/core", "SkMath"),
			label.New("@skia", "src/gpu", "GrGpu"),
		},
		[]string{
			"//include/core:SkMath",
			"//include/core:SkTypes",
			"//src/gpu:GrGpu",
			":SkMacros",
		})
}

func TestThirdPartyDep_Success(t *testing.T) {
	assert.Equal(t, label.New("", "third_party", "libpng"), thirdPartyDep("//third_party:libpng"))
}

func TestResolve_RespectsIgnorePrefix(t *testing.T) {
	inputRule := rule.NewRule(common.CCLibraryRule, "MyFile")
	inputLabel := label.New("@SomeRepo", "src/core/alpha/beta", "MyFile")
	repoIncludes := []common.Include{common.Include{Repo: "", IncludePath: "include/gamma.h"}, common.Include{Repo: "", IncludePath: "src/core/delta.cpp"}}
	thirdPartyIncludes := []string{"png.h", "arpa/inet.h"}
	imports := common.NewImports(repoIncludes, nil, thirdPartyIncludes, []common.ThirdPartyMapItem{
		{Re: "png.h", Dep: "//third_party:libpng"},
		{Re: "arpa/inet.h", Dep: "SYSTEM HEADER"},
	})

	r := CppResolver{}

	r.Resolve(nil, nil, nil, inputRule, imports, inputLabel)

	assert.Equal(t, []string{
		"//include:gamma",
		"//src/core:delta",
		"//third_party:libpng",
	}, inputRule.AttrStrings("deps"))
}

func TestResolve_DoesntLookAtRepoHeadersOrSystemHeadersForThirdParty(t *testing.T) {
	inputRule := rule.NewRule(common.CCLibraryRule, "MyFile")
	inputLabel := label.New("@SomeRepo", "src/core/alpha/beta", "MyFile")
	repoIncludes := []common.Include{common.Include{Repo: "", IncludePath: "png.h"}, common.Include{Repo: "", IncludePath: "freetype/ftadvanc.h"}}
	systemIncludes := []string{"string", "cmath", "jerror.h", "arpa/inet.h"}
	imports := common.NewImports(repoIncludes, systemIncludes, nil, []common.ThirdPartyMapItem{
		{Re: "png.h", Dep: "//third_party:libpng"},
		{Re: "freetype/ftadvanc.h", Dep: "//third_party:freetype2"},
		{Re: "jerror.h", Dep: "//third_party:libjpeg_turbo"},
		{Re: "arpa/inet.h", Dep: "SYSTEM HEADER - not needed"},
	})

	r := CppResolver{}

	r.Resolve(nil, nil, nil, inputRule, imports, inputLabel)

	assert.Equal(t, []string{
		"//:png",
		"//freetype:ftadvanc",
	}, inputRule.AttrStrings("deps"))
}

func TestResolve_AbsolutePathsAllowedInFileMap(t *testing.T) {
	inputRule := rule.NewRule(common.CCLibraryRule, "MyFile")
	inputLabel := label.New("@SomeRepo", "src/core/alpha/beta", "MyFile")
	thirdPartyIncludes := []string{"png.h", "freetype/ftadvanc.h", "jerror.h"}
	systemIncludes := []string{"string", "cmath"}
	imports := common.NewImports(nil, systemIncludes, thirdPartyIncludes, []common.ThirdPartyMapItem{
		{Re: "png.h", Dep: "@libpng//bazel:settings"},
		{Re: "freetype/ftadvanc.h", Dep: "@freetype2//:freetype2"},
		{Re: "jerror.h", Dep: "@libjpeg_turbo//:jpeg"},
	})

	r := CppResolver{}

	r.Resolve(nil, nil, nil, inputRule, imports, inputLabel)

	assert.Equal(t, []string{
		"@freetype2//:freetype2",
		"@libjpeg_turbo//:jpeg",
		"@libpng//bazel:settings",
	}, inputRule.AttrStrings("deps"))
}
