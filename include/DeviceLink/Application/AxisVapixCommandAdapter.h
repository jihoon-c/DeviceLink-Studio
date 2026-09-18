#pragma once

#include "DeviceLink/Application/GimbalProtocolAdapter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace DeviceLink::Application
{
struct AxisVapixCredentials final
{
    std::string userName;
    std::string password;
};

struct AxisVapixOptions final
{
    std::string hostName;
    std::uint32_t cameraChannel{1};
    std::int32_t scanPanSpeed{25};
    std::optional<AxisVapixCredentials> basicCredentials;
};

struct AxisVapixRequest final
{
    std::string target;
    std::string serializedRequest;
};

struct AxisVapixPosition final
{
    double panDegrees{};
    double tiltDegrees{};
    std::optional<double> zoom;
};

// Maps DeviceLink's semantic gimbal commands to Axis VAPIX PTZ CGI requests.
// It deliberately does not own a socket: raw HTTP I/O belongs to a later Transport/Session integration.
class AxisVapixCommandAdapter final
{
public:
    explicit AxisVapixCommandAdapter(AxisVapixOptions options);

    [[nodiscard]] std::optional<AxisVapixRequest> EncodeCommand(
        const GimbalCommand& command) const;
    [[nodiscard]] static std::optional<AxisVapixPosition> DecodePositionResponse(
        std::int32_t httpStatusCode, std::string_view responseBody) noexcept;

private:
    [[nodiscard]] AxisVapixRequest MakeGetRequest(std::string target) const;

    AxisVapixOptions m_options;
};
} // namespace DeviceLink::Application
