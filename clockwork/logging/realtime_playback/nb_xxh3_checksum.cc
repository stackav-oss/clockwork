// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/xxh3_checksum.hh"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h> // IWYU pragma: keep
#include <xxh3.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace
{

using ByteBuffer = nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>, nanobind::c_contig>;

[[nodiscard]] std::span<const std::byte> as_byte_span(const ByteBuffer& data) noexcept
{
  return std::as_bytes(std::span{data.data(), data.nbytes()});
}

[[nodiscard]] uint64_t compute_checksum(const ByteBuffer& data) noexcept
{
  return clockwork_logging::compute_xxh3_checksum(as_byte_span(data));
}

class Xxh3Checksum final
{
public:
  Xxh3Checksum() noexcept
    : state_(clockwork_logging::init_xxh3_checksum())
  {
  }

  void update(const ByteBuffer& data) noexcept
  {
    clockwork_logging::update_xxh3_checksum(state_, as_byte_span(data));
  }

  [[nodiscard]] uint64_t digest() noexcept
  {
    return clockwork_logging::digest_xxh3_checksum(state_);
  }

private:
  XXH3_state_t state_{};
};

} // namespace

NB_MODULE(nb_xxh3_checksum, mod)
{
  nanobind::set_leak_warnings(false);
  mod.doc() = "Clockwork XXH3 checksum bindings";

  mod.def(
    "compute_xxh3_checksum",
    &compute_checksum,
    nanobind::arg("data"),
    "Compute an unseeded XXH3-64 checksum over a contiguous byte buffer.");

  nanobind::class_<Xxh3Checksum>(mod, "Xxh3Checksum")
    .def(nanobind::init<>())
    .def("update", &Xxh3Checksum::update, nanobind::arg("data"), "Add a contiguous byte buffer to the checksum.")
    .def("digest", &Xxh3Checksum::digest, "Return the current unsigned XXH3-64 checksum.");
}
