#pragma once

#include "DeviceLink/Application/FrameReplayRunner.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"

#include <string>
#include <vector>

namespace DeviceLink::Application
{
class SqliteFrameReplaySource final
{
public:
    explicit SqliteFrameReplaySource(const Infrastructure::SqliteEventRepository& repository);

    [[nodiscard]] std::vector<ReplayFrame> Load(const std::string& deviceId) const;

private:
    const Infrastructure::SqliteEventRepository& m_repository;
};
} // namespace DeviceLink::Application
