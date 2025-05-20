// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package parsers

import (
	"testing"

	"github.com/stackav-oss/clockwork/tools/gazelle/cc_plugin/common"
	"github.com/stretchr/testify/require"
)

func TestParseCIncludes_Success(t *testing.T) {
	const source = `/* Sample C++ file with imports. */
#include "include/private/SkMacros.h"
	#include "include/private/SkDeque.h"

#include   <vector>

#include	<cstring>
#include  <memory> // This comment should be ignored.
 #include <cstdlib>

 #include <Eigen/Core>
#include <wise_enum.h>

#include "png.h"
#include "dawn/webgpu_cpp.h"

// Even though these are #if guarded, we should still list them.
#if SK_SUPPORT_GPU
#include "include/gpu/GrDirectContext.h"
#include "src/gpu/BaseDevice.h"
#include "src/gpu/SkGr.h"
#if defined(SK_BUILD_FOR_ANDROID_FRAMEWORK)
#   include "src/gpu/GrRenderTarget.h"
#   include "src/gpu/GrRenderTargetProxy.h"
#endif
#endif

// Line comments should be ignored.
//
// #include "experimental/foo.h"

// Block comments should be ignored.
/*
#include "experimental/bar.h"
*/

// It is ok to include cpp files and files with strange capitalization
#include "src/core/alpha.cpp"
#include "src/core/beta.C"
#include "src/core/gamma.H"

// Include path that is in an external repo
#include "zyx/src/util/Time.h"

`

	expectedRepoIncludes := []common.Include{
		common.Include{Repo: "", IncludePath: "dawn/webgpu_cpp.h"},
		common.Include{Repo: "", IncludePath: "include/gpu/GrDirectContext.h"},
		common.Include{Repo: "", IncludePath: "include/private/SkDeque.h"},
		common.Include{Repo: "", IncludePath: "include/private/SkMacros.h"},
		common.Include{Repo: "", IncludePath: "png.h"},
		common.Include{Repo: "", IncludePath: "src/core/alpha.cpp"},
		common.Include{Repo: "", IncludePath: "src/core/beta.C"},
		common.Include{Repo: "", IncludePath: "src/core/gamma.H"},
		common.Include{Repo: "", IncludePath: "src/gpu/BaseDevice.h"},
		common.Include{Repo: "", IncludePath: "src/gpu/GrRenderTarget.h"},
		common.Include{Repo: "", IncludePath: "src/gpu/GrRenderTargetProxy.h"},
		common.Include{Repo: "", IncludePath: "src/gpu/SkGr.h"},
		common.Include{Repo: "repo", IncludePath: "zyx/src/util/Time.h"},
	}

	expectedSystemIncludes := []string{
		"cstdlib",
		"cstring",
		"memory",
		"vector",
	}

	expectedThirdPartyIncludes := []string{
		"Eigen/Core",
		"wise_enum.h",
	}

	externalRepos := []common.ExternalRepo{
		common.ExternalRepo{Prefix: "zyx", Repo: "repo"},
	}

	actualRepoIncludes, actualSystemIncludes, actualThirdPartyIncludes := ParseCIncludes(source, externalRepos)
	require.Equal(t, expectedRepoIncludes, actualRepoIncludes)
	require.Equal(t, expectedSystemIncludes, actualSystemIncludes)
	require.Equal(t, expectedThirdPartyIncludes, actualThirdPartyIncludes)
}
