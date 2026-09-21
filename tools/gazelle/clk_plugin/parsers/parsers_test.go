// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

package parsers

import (
	"testing"

	"github.com/stackav-oss/clockwork/tools/gazelle/clk_plugin/common"
	"github.com/stretchr/testify/require"
)

func TestParseClkFile_GenerateAll(t *testing.T) {
	const source = `#![ generate ( cpp, cpp_cog, cpp_exe, cpp_test_cog, py, py_cog, nanobind, proto_conv , proto , go_proto ) ]
use [] @repo1::use::none::{a, b,
c};
use @repo2::use::all::{a, b, c};
use [ cpp ] norepo::use::cpp;
use[cpp,py]norepo::use::cpp_py;
use [ py , nanobind ] norepo::use::py_nanobind;
use [ nanobind, proto ] norepo::use::nanobind_proto;
use [ cpp , proto , proto_conv ] norepo::use::cpp_proto_proto_conv;
`

	expectedGenerate := true
	expectedGenerates := []string{"cpp", "cpp_cog", "cpp_exe", "cpp_test_cog", "go_proto", "nanobind", "proto", "proto_conv", "py", "py_cog"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk"},
		common.ClkImport{Repo: "repo1", ImportPath: "use::none_clk"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_proto_conv"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk_cc"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_cc"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_proto_conv"},
	}
	expectedProtoImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_proto"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk_proto"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_proto"},
	}
	expectedPyImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk_py"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk_py"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_nb"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_py"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_py_dial"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk_py_impl"},
	}
	expectedCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::online_main"},
	}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, true)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GenerateNone(t *testing.T) {
	const source = `#![ generate ( ) ]
use [] @repo1::use::none::{a, b,
c};
use @repo2::use::all::{a, b, c};
use [ cpp ] norepo::use::cpp;
use [ cpp, py ] norepo::use::cpp_py;
use [ py , nanobind ] norepo::use::py_nanobind;
use [ nanobind, proto ] norepo::use::nanobind_proto;
use [ cpp , proto , proto_conv ] norepo::use::cpp_proto_proto_conv;
`

	expectedGenerate := true
	expectedGenerates := []string{}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk"},
		common.ClkImport{Repo: "repo1", ImportPath: "use::none_clk"},
		common.ClkImport{Repo: "repo2", ImportPath: "use::all_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_cc"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_proto_conv"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_proto_proto_conv_clk_proto"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk_proto"},
	}
	expectedPyImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_py_clk_py"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::nanobind_proto_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk_nb"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::py_nanobind_clk_py"},
	}
	expectedCppExeImports := []common.ClkImport{}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GenerateCppHdrs(t *testing.T) {
	const source = `#![ generate ( cpp ) ]
use [ cpp ] norepo::use::cpp;
#![ cpp ( type_header = "a/b/c.pb.h" ) ]
#[ cpp ( type_header = "aa/bb/cc.h" ) ]
#[ cpp ( type_header = "aa/bb/dd.h" ) ]
`

	expectedGenerate := true
	expectedGenerates := []string{"cpp"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "a::b::c_cc_library"},
		common.ClkImport{Repo: "", ImportPath: "aa::bb::cc"},
		common.ClkImport{Repo: "", ImportPath: "aa::bb::dd"},
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GenerateCppExeOffline(t *testing.T) {
	const source = `#![ generate ( cpp, cpp_exe ) ]
use [ cpp ] norepo::use::cpp;
#![ exe ( offline = true ) ]
`

	expectedGenerate := true
	expectedGenerates := []string{"cpp", "cpp_exe"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::offline_main"},
	}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, true)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GeneratePyExeOffline(t *testing.T) {
	const source = `#![ generate ( py_exe ) ]
use [ cpp ] norepo::use::cpp;
#![ exe ( offline = true ) ]
`

	expectedGenerate := true
	expectedGenerates := []string{"py_exe"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::python_offline_main"},
	}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, true)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GeneratePyExeOnline(t *testing.T) {
	const source = `#![ generate ( py_exe ) ]
use [ cpp ] norepo::use::cpp;
`

	expectedGenerate := true
	expectedGenerates := []string{"py_exe"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "clockwork::scaffolding::python_online_main"},
	}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, true)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GoProtoImportPath(t *testing.T) {
	const source = `#![ generate ( cpp, cpp_exe ) ]
use [ cpp ] norepo::use::cpp;
#![ exe ( offline = false ) ]
`

	expectedGenerate := true
	expectedGenerates := []string{"cpp", "cpp_exe"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk"},
	}
	expectedCppImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::cpp_clk_cc"},
	}
	expectedProtoImports := []common.ClkImport{}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{
		common.ClkImport{Repo: "clockwork", ImportPath: "clockwork::scaffolding::online_main"},
	}
	expectedGoProtoImportPath := ""
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_GenerateCppExeOnline(t *testing.T) {
	const source = `#![ generate ( proto, go_proto ) ]
use [ proto ] norepo::use::proto;
#![ proto ( package = aa.bb.cc , go_package = aa/bb/cc ) ]
`

	expectedGenerate := true
	expectedGenerates := []string{"go_proto", "proto"}
	expectedClkImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::proto_clk"},
	}
	expectedCppImports := []common.ClkImport{}
	expectedProtoImports := []common.ClkImport{
		common.ClkImport{Repo: "", ImportPath: "norepo::use::proto_clk_proto"},
	}
	expectedPyImports := []common.ClkImport{}
	expectedCppExeImports := []common.ClkImport{}
	expectedGoProtoImportPath := "aa/bb/cc"
	expectedImports := common.NewImports(
		expectedGenerate,
		expectedGenerates,
		expectedClkImports,
		expectedCppImports,
		expectedProtoImports,
		expectedPyImports,
		expectedCppExeImports,
		expectedGoProtoImportPath)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_MultilineCppTypeHeader(t *testing.T) {
	const source = `#![ generate ( cpp ) ]
#[ cpp (
  type_namespace = stack::detection::event_triggers,
  type_header = "autonomy/detection/event_triggers/event_trigger_metrics_state.hh"
) ]
extern_type EventTriggerMetricsState;
`

	const sourceWithTrailingComma = `#![ generate ( cpp ) ]
#[ cpp (
  type_namespace = stack::detection::event_triggers,
  type_header = "autonomy/detection/event_triggers/event_trigger_metrics_state.hh",
) ]
extern_type EventTriggerMetricsState;
`

	expectedImports := common.NewImports(
		true,
		[]string{"cpp"},
		[]common.ClkImport{},
		[]common.ClkImport{
			common.ClkImport{Repo: "", ImportPath: "autonomy::detection::event_triggers::event_trigger_metrics_state"},
		},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		"",
	)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
	actualImports = ParseClkFile(sourceWithTrailingComma, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_MultilineCompactCppTypeHeader(t *testing.T) {
	const source = `#![generate(cpp)]
#[cpp(type_namespace=stack::detection::event_triggers,
type_header="autonomy/detection/event_triggers/event_trigger_metrics_state.hh")]
extern_type EventTriggerMetricsState;
`

	expectedImports := common.NewImports(
		true,
		[]string{"cpp"},
		[]common.ClkImport{},
		[]common.ClkImport{
			common.ClkImport{Repo: "", ImportPath: "autonomy::detection::event_triggers::event_trigger_metrics_state"},
		},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		"",
	)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_MultilineProtoGoPackage(t *testing.T) {
	const source = `#![ generate ( proto, go_proto ) ]
#![ proto (
  package = stack.autonomy.mapping.data_model,
  go_package = github.com/stack-av-llc/av/autonomy/mapping/data_model/enums
) ]
`

	expectedImports := common.NewImports(
		true,
		[]string{"go_proto", "proto"},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		"github.com/stack-av-llc/av/autonomy/mapping/data_model/enums",
	)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}

func TestParseClkFile_CompactProtoGoPackage(t *testing.T) {
	const source = `#![generate(proto, go_proto)]
#![proto(go_package=github.com/stack-av-llc/av/services/swac)]
`

	expectedImports := common.NewImports(
		true,
		[]string{"go_proto", "proto"},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		[]common.ClkImport{},
		"github.com/stack-av-llc/av/services/swac",
	)

	actualImports := ParseClkFile(source, false)
	require.Equal(t, expectedImports, actualImports)
}
