// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package cc_plugin

import (
	"github.com/bazelbuild/buildtools/tables"
)

// init merges repository-specific buildifier table overrides into the global
// buildtools tables so that BUILD files written by Gazelle use the same
// attribute ordering as direct buildifier invocations.
//
// This must stay in sync with //:.buildifier_tables.json. Without this,
// Gazelle re-orders kwargs alphabetically (e.g. swapping `owners` and
// `file_owners` in `package()` calls) while clk-managed expected BUILD
// files use the buildifier ordering, causing diff_test failures.
func init() {
	tables.MergeTables(
		map[string]bool{ // labelArg
			"cpp_deps":     true,
			"cpp_exe_deps": true,
			"owners":       true,
			"proto_deps":   true,
			"py_deps":      true,
		},
		nil, // denylist
		nil, // listArg
		map[string]bool{ // sortableListArg
			"allowed_dep_trees":     true,
			"allowed_dep_wildcards": true,
			"allowed_deps":          true,
			"generated": true,
			"owners":    true,
		},
		nil, // sortDenylist
		nil, // sortAllowlist
		map[string]int{ // namePriority overrides
			"owners":      1,
			"file_owners": 2,
		},
		false, // stripLabelLeadingSlashes
		false, // shortenAbsoluteLabelsToRelative
		nil,   // symbolLoadLocation
	)
}
