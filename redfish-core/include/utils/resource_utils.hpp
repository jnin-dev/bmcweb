// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "logging.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace redfish
{
namespace resource_utils
{

struct ResourceStatus
{
    bool present = true;
    bool available = true;
    bool functional = true;
    bool enabled = true;
    uint8_t pending = 0;
};

/**
 * @brief Fetches resource status from DBus interfaces
 *
 */
inline void determineResourceState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<ResourceStatus>& status,
    const nlohmann::json::json_pointer& jsonPtr)
{
    BMCWEB_LOG_DEBUG("determineResourceState");
    if (--status->pending != 0)
    {
        return;
    }

    nlohmann::json& statusJson = asyncResp->res.jsonValue[jsonPtr]["Status"];

    // Absent takes priority over unavailable
    if (!status->present)
    {
        statusJson["State"] = resource::State::Absent;
    }
    else if (!status->available)
    {
        statusJson["State"] = resource::State::UnavailableOffline;
    }
    else if (!status->enabled)
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Disabled;
    }
    else
    {
        statusJson["State"] = resource::State::Enabled;
    }

    if (!status->functional)
    {
        statusJson["Health"] = resource::Health::Critical;
    }
    else
    {
        statusJson["Health"] = resource::Health::OK;
    }
}

/**
 * @brief Helper to fetch boolean property and update status
 *
 */
inline void getStatusProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<ResourceStatus>& status, const std::string& service,
    const std::string& path, const std::string& interface,
    const std::string& property, const nlohmann::json::json_pointer& jsonPtr,
    std::function<void(ResourceStatus&, bool)>&& callback)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path, interface, property,
        [status, asyncResp, property, jsonPtr, callback{std::move(callback)}](
            const boost::system::error_code& ec, bool value) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for {}, ec {}",
                                     property, ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else
            {
                callback(*status, value);
            }
            determineResourceState(asyncResp, status, jsonPtr);
        });
}

/*
 * @brief Retrieves the status of the resource's state and health
 *
 * Queries four interfaces:
 * - xyz.openbmc_project.Inventory.Item::Present
 * - xyz.openbmc_project.State.Decorator.Availability::Available
 * - xyz.openbmc_project.State.Decorator.OperationalStatus::Functional
 * - xyz.openbmc_project.Object.Enable::Enabled
 */
inline void getResourceStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const nlohmann::json::json_pointer& jsonPtr)
{
    // Default values
    asyncResp->res.jsonValue[jsonPtr]["Status"]["State"] =
        resource::State::Enabled;
    asyncResp->res.jsonValue[jsonPtr]["Status"]["Health"] =
        resource::Health::OK;

    BMCWEB_LOG_DEBUG("getResourceStatus");
    auto status = std::make_shared<ResourceStatus>();
    status->pending = 4;

    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.Inventory.Item", "Present", jsonPtr,
                      [](ResourceStatus& s, bool val) { s.present = val; });
    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.State.Decorator.Availability",
                      "Available", jsonPtr,
                      [](ResourceStatus& s, bool val) { s.available = val; });
    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.State.Decorator.OperationalStatus",
                      "Functional", jsonPtr,
                      [](ResourceStatus& s, bool val) { s.functional = val; });
    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.Object.Enable", "Enabled", jsonPtr,
                      [](ResourceStatus& s, bool val) { s.enabled = val; });
}

inline void getResourceStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    getResourceStatus(asyncResp, service, path, ""_json_pointer);
}

} // namespace resource_utils
} // namespace redfish
