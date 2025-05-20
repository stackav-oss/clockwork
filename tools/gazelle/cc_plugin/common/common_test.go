// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package common

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestGetSuffix_Success(t *testing.T) {
	assert.Equal(t, ".h", getSuffix("foo.h"))
	assert.Equal(t, ".cpp", getSuffix("foo.cpp"))
	assert.Equal(t, "", getSuffix("WORKSPACE"))
}

func TestIsCppHeader_Success(t *testing.T) {
	assert.True(t, IsCppHeader("foo.h"))
	assert.True(t, IsCppHeader("foo.H"))
	assert.True(t, IsCppHeader("foo.hpp"))
	assert.True(t, IsCppHeader("foo.hh"))

	assert.False(t, IsCppHeader("WORKSPACE"))
	assert.False(t, IsCppHeader("foo.cpp"))
	assert.False(t, IsCppHeader("foo.cc"))
	assert.False(t, IsCppHeader("foo.cxx"))
	assert.False(t, IsCppHeader("foo.c"))
}

func TestIsCppSource_Success(t *testing.T) {
	assert.True(t, IsCppSource("foo.cpp"))
	assert.True(t, IsCppSource("foo.cc"))
	assert.True(t, IsCppSource("foo.cxx"))
	assert.True(t, IsCppSource("foo.c"))
	assert.True(t, IsCppSource("foo.C"))

	assert.False(t, IsCppSource("WORKSPACE"))
	assert.False(t, IsCppSource("foo.h"))
	assert.False(t, IsCppSource("foo.H"))
	assert.False(t, IsCppSource("foo.hpp"))
	assert.False(t, IsCppSource("foo.hh"))
}
