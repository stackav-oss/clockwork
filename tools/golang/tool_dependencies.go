// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

//go:build tools
// +build tools

package support_tools

// These are packages needed by other bazel_deps used in the Clockwork repo.
// They are added here so go mod tidy can detect them and add them to go.mod
import (
	_ "google.golang.org/genproto/googleapis/api"
	_ "google.golang.org/protobuf"
)
