// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package configurer

import (
	"flag"
	"log"
	"os"
	"strings"

	"go.skia.org/infra/go/skerr"

	"github.com/bazelbuild/bazel-gazelle/config"
	gzflag "github.com/bazelbuild/bazel-gazelle/flag"
	"github.com/bazelbuild/bazel-gazelle/rule"

	"github.com/stackav-oss/clockwork/tools/gazelle/cc_plugin/common"
)

// CppConfigurer implements the config.Configurer interface.
type CppConfigurer struct {
	thirdPartyFile    string
	ThirdPartyFileMap []common.ThirdPartyMapItem
	externalRepoArgs  []string
	ExternalRepos     []common.ExternalRepo
}

// RegisterFlags adds any flags this extension takes.
func (c *CppConfigurer) RegisterFlags(fs *flag.FlagSet, _ string, _ *config.Config) {
	fs.StringVar(&c.thirdPartyFile, "third_party_file_map", "", "This file should be a JSON dictionary that maps include paths to third_party Bazel labels.")
	fs.Var(&gzflag.MultiFlag{Values: &c.externalRepoArgs}, "external_repo", "A prefix name for an include that should have the mappings applied to.")
}

// CheckFlags processes the flags defined by this extension. Concretely, it attempts to read
// in the passed-in third_party_file_map, if one was specified.
func (c *CppConfigurer) CheckFlags(*flag.FlagSet, *config.Config) error {
	if c.thirdPartyFile == "" {
		log.Printf("No third_party_file_map configured")
		c.ThirdPartyFileMap = nil
		return nil
	}
	b, err := os.ReadFile(c.thirdPartyFile)
	if err != nil {
		return skerr.Wrapf(err, "Reading third_party_file_map %s", c.thirdPartyFile)
	}

	// Use custom unmarshaling to preserve order
	thirdPartyItems, err := common.UnmarshalThirdPartyMapItems(b)
	if err != nil {
		return skerr.Wrapf(err, "Parsing JSON in %s", c.thirdPartyFile)
	}

	c.ThirdPartyFileMap = thirdPartyItems

	for _, arg := range c.externalRepoArgs {
		split := strings.Split(arg, ":")
		if len(split) != 2 {
			log.Printf("Invalid external repo arg '%s'.  Expected format is <prefix>:<repo>", arg)
			return nil
		}
		c.ExternalRepos = append(c.ExternalRepos, common.ExternalRepo{Prefix: split[0], Repo: split[1]})
	}

	return nil
}

// KnownDirectives implements the config.Configurer interface.
//
// Interface documentation:
//
// KnownDirectives returns a list of directive keys that this CppConfigurer can
// interpret. Gazelle prints errors for directives that are not recognized by
// any CppConfigurer.
func (c *CppConfigurer) KnownDirectives() []string {
	return []string{}
}

// Configure implements the config.Configurer interface.
func (c *CppConfigurer) Configure(*config.Config, string, *rule.File) {}

var _ config.Configurer = &CppConfigurer{}
