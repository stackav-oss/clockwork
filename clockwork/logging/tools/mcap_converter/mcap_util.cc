// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/tools/mcap_converter/mcap_util.hh"

#include <mcap/types.hpp>
#include <mcap/writer.hpp>

namespace clockwork_logging
{

[[nodiscard]] mcap::McapWriterOptions make_offload_mcap_writer_options()
{
  // Create the merged output log writer.
  auto writer_opts = mcap::McapWriterOptions("");
  writer_opts.noChunkCRC = true;
  writer_opts.noAttachmentCRC = true;
  writer_opts.enableDataCRC = false;
  writer_opts.noSummaryCRC = true;
  writer_opts.noChunking = false;
  writer_opts.noMessageIndex = false;
  writer_opts.noSummary = false;
  writer_opts.chunkSize = mcap::DefaultChunkSize;
  writer_opts.compression = mcap::Compression::Zstd;
  writer_opts.compressionLevel = mcap::CompressionLevel::Default;
  writer_opts.forceCompression = false;
  writer_opts.noRepeatedSchemas = false;
  writer_opts.noRepeatedChannels = false;
  writer_opts.noAttachmentIndex = false;
  writer_opts.noMetadataIndex = false;
  writer_opts.noChunkIndex = false;
  writer_opts.noStatistics = false;
  writer_opts.noSummaryOffsets = false;
  return writer_opts;
}

} // namespace clockwork_logging
