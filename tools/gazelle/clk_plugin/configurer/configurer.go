// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package configurer

import (
	"flag"

	"github.com/bazelbuild/bazel-gazelle/config"
	"github.com/bazelbuild/bazel-gazelle/rule"
)

// ClkConfigurer implements the config.Configurer interface.
type ClkConfigurer struct {
	InClockworkRepo bool
}

// RegisterFlags adds any flags this extension takes.
func (c *ClkConfigurer) RegisterFlags(fs *flag.FlagSet, _ string, _ *config.Config) {
	fs.BoolVar(&c.InClockworkRepo, "clockwork_is_root_repo", false, "Set when building in the clockwork source repo.")
}

// CheckFlags processes the flags defined by this extension.
func (c *ClkConfigurer) CheckFlags(*flag.FlagSet, *config.Config) error {
	return nil
}

// KnownDirectives implements the config.Configurer interface.
//
// Interface documentation:
//
// KnownDirectives returns a list of directive keys that this ClkConfigurer can
// interpret. Gazelle prints errors for directives that are not recognized by
// any ClkConfigurer.
func (c *ClkConfigurer) KnownDirectives() []string {
	return []string{}
}

// Configure implements the config.Configurer interface.
func (c *ClkConfigurer) Configure(*config.Config, string, *rule.File) {}

var _ config.Configurer = &ClkConfigurer{}
