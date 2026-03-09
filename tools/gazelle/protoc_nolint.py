#!/usr/bin/env python3
# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Protoc generator to add NOLINTs to generated cpp files."""

import sys
import textwrap
from pathlib import Path
from typing import Final

import google.protobuf.compiler.plugin_pb2 as plugin
from google.protobuf.descriptor_pb2 import FileDescriptorProto

# "includes" is at the top of the file, right below all of the normal includes.
TOP_INSERTION_POINT: Final = "includes"
# "global_scope" is at the end of the file, just before the close of the header guard.
BOTTOM_INSERTION_POINT: Final = "global_scope"

HH_EDITS: dict[str, str] = {
    # We don't need to insert anything in the header file, because we add a mod in the rule instantiation
    # (see BUILD file) to use awk to add a pragma at the top of the file.
    # Using insertion points didn't work because the first insertion point is after the first clang-tidy error.
}
CC_EDITS = {
    TOP_INSERTION_POINT: textwrap.dedent(
        '''\
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wsign-conversion"''',
    ),
    BOTTOM_INSERTION_POINT: "#pragma GCC diagnostic pop",
}


def process_file(
    proto_file: FileDescriptorProto,
    response: plugin.CodeGeneratorResponse,
) -> None:
    """Process the given proto file.

    This plugin writes the text in INSERTION_POINT_EDITS to the response protobuf.

    Args:
        proto_file: the protobuf file being processed
        response: the response object
    """
    # For each input file, add a corresponding response file.

    # Write to the same filenames that the cpp generator outputs.
    # This tells protoc we want to use insertion points.
    hh_filename = str(Path(proto_file.name).with_suffix(".pb.h"))
    cc_filename = str(Path(proto_file.name).with_suffix(".pb.cc"))

    for insertion_point, text in HH_EDITS.items():
        file = response.file.add()
        file.name = hh_filename
        file.insertion_point = insertion_point
        file.content = text
    for insertion_point, text in CC_EDITS.items():
        file = response.file.add()
        file.name = cc_filename
        file.insertion_point = insertion_point
        file.content = text


def main() -> None:
    """Main function, called when this script is executed.

    Iterates through the files given on stdin and processes them.
    """
    # Protoc sends the input data to stdin. Read it from there.
    # Use sys.stdin.buffer.read to read raw bytes.
    request = plugin.CodeGeneratorRequest.FromString(sys.stdin.buffer.read())

    # Create a response object to fill in in our process_file function.
    response = plugin.CodeGeneratorResponse()

    # Make sure we say we support optional fields, otherwise protoc throws an error if it sees one.
    response.supported_features = plugin.CodeGeneratorResponse.Feature.FEATURE_PROTO3_OPTIONAL

    # We get a list of all files, including transitive ones, but the C++
    # generator only outputs one cc/h pair for the toplevel proto.
    for proto_file in request.proto_file:
        if proto_file.name not in request.file_to_generate:
            continue
        process_file(proto_file, response)

    # Serialize the response object and write it to stdout.
    # Protoc reads the stdout and applies the action specified.
    # Use sys.stdout.buffer.write to write bytes.
    sys.stdout.buffer.write(response.SerializeToString())


if __name__ == "__main__":
    main()
