#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"
#include "DeviceLink/Infrastructure/AsyncEventStore.h"

#include <cstdint>
#include <functional>

namespace DeviceLink::Application
{
class DeviceEventPersistence final
{
public:
    using TimestampProvider = std::function<std::int64_t()>;

    DeviceEventPersistence(
        Infrastructure::AsyncEventStore& eventStore,
        TimestampProvider timestampProvider);

    void Persist(const DeviceEvent& event) const;

private:
    Infrastructure::AsyncEventStore& m_eventStore;
    TimestampProvider m_timestampProvider;
};
} // namespace DeviceLink::Application
