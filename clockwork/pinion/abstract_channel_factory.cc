// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/abstract_channel_factory.hh"

namespace clockwork::pinion
{

AbstractChannelFactory::~AbstractChannelFactory() = default;

jewels::expected<std::shared_ptr<AbstractChannel>, AbstractChannel::Error> AbstractChannelFactory::open(
  AbstractChannel::Role role,
  std::string_view identifier,
  std::string_view name,
  const BufferLayout& layout,
  size_t max_subscribers)
{
  switch (role)
  {
  case AbstractChannel::Role::publisher:
    return open_publisher(identifier, name, layout, max_subscribers);
  case AbstractChannel::Role::subscriber:
    return open_subscriber(identifier, name, layout, max_subscribers);
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): RAII managed trivial type
ChannelFactoryContext* ChannelFactoryContext::current_ = nullptr;

ChannelFactoryContext::ChannelFactoryContext(AbstractChannelFactory& factory)
  : factory_(factory), previous_(current_)
{
  current_ = this;
}

ChannelFactoryContext::~ChannelFactoryContext()
{
  current_ = previous_;
}

AbstractChannelFactory* ChannelFactoryContext::get()
{
  if (current_ != nullptr)
  {
    return &(current_->factory_);
  }
  return nullptr;
}

} // namespace clockwork::pinion
