// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package clk_plugin

import (
	gazelle "github.com/bazelbuild/bazel-gazelle/language"

	"github.com/stackav-oss/clockwork/tools/gazelle/clk_plugin/language"
)

// NewLanguage returns an instance of the Gazelle extension that can generate Clockwork rules.
//
// This function is called from the Gazelle binary, but may appear unused by some IDEs.
func NewLanguage() gazelle.Language {
	return &language.ClkLanguage{}
}
