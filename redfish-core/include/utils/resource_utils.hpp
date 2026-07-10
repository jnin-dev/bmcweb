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

namespace redfish
{
namespace resource_utils
{

struct ResourceStatus
{
    bool present = true;
    bool available = true;
    bool functional = true;
    uint8_t pending = 0;
};

/**
 * @brief Fetches resource status from DBus interfaces
 *
 */
inline void determineResourceState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<ResourceStatus>& status)
{
    BMCWEB_LOG_DEBUG("determineResourceState");
    if (--status->pending != 0)
    {
        return;
    }

    // Absent takes priority over unavailable
    if (!status->present)
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Absent;
    }
    else if (!status->available)
    {
        asyncResp->res.jsonValue["Status"]["State"] =
            resource::State::UnavailableOffline;
    }
    else
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
    }

    if (!status->functional)
    {
        asyncResp->res.jsonValue["Status"]["Health"] =
            resource::Health::Critical;
    }
    else
    {
        asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;
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
    const std::string& property,
    std::function<void(ResourceStatus&, bool)>&& callback)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path, interface, property,
        [status, asyncResp, property, callback{std::move(callback)}](
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
            determineResourceState(asyncResp, status);
        });
}

/*
 * @brief Retrieves the status of the resource's state and health
 *
 * Queries three interfaces:
 * - xyz.openbmc_project.Inventory.Item::Present
 * - xyz.openbmc_project.State.Decorator.Availability::Available
 * - xyz.openbmc_project.State.Decorator.OperationalStatus::Functional
 */
inline void getResourceStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    // Default values
    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
    asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;

    BMCWEB_LOG_DEBUG("getResourceStatus");
    auto status = std::make_shared<ResourceStatus>();
    status->pending = 3;

    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.Inventory.Item", "Present",
                      [](ResourceStatus& s, bool val) { s.present = val; });
    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.State.Decorator.Availability",
                      "Available",
                      [](ResourceStatus& s, bool val) { s.available = val; });
    getStatusProperty(asyncResp, status, service, path,
                      "xyz.openbmc_project.State.Decorator.OperationalStatus",
                      "Functional",
                      [](ResourceStatus& s, bool val) { s.functional = val; });
}

} // namespace resource_utils
} // namespace redfish
