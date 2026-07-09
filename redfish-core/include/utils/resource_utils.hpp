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
struct ResourceStatus
{
    bool present = true;
    bool available = true;
    bool functional = true;
    bool enabled = true;
    unsigned char pending = 0;
};

/**
 * @brief Fetches resource status from DBus interfaces
 *
 */
inline void determineResourceState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<ResourceStatus>& status)
{
    BMCWEB_LOG_DEBUG("Determine Resource State");
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
    else if (!status->enabled)
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Disabled;
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

/*
 * @brief Retrieves the status of the resource's state and health
 *
 * Queries three interfaces:
 * - xyz.openbmc_project.Inventory.Item::Present
 * - xyz.openbmc_project.State.Decorator.Availability::Available
 * - xyz.openbmc_project.State.Decorator.OperationalStatus::Functional
 * - xyz.openbmc_project.Object.Enable::Enabled
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
    status->pending = 4;

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [status, asyncResp](const boost::system::error_code& ec, bool present) {
            BMCWEB_LOG_DEBUG("getResourceStatus Present");
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for Present, ec {}",
                                     ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else
            {
                status->present = present;
            }
            determineResourceState(asyncResp, status);
            return;
        });

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.Availability", "Available",
        [status,
         asyncResp](const boost::system::error_code& ec, bool available) {
            BMCWEB_LOG_DEBUG("getResourceStatus Available");
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR(
                        "DBUS response error for Availability, ec {}",
                        ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else
            {
                status->available = available;
            }
            determineResourceState(asyncResp, status);
            return;
        });

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [status,
         asyncResp](const boost::system::error_code& ec, bool functional) {
            BMCWEB_LOG_DEBUG("getResourceStatus Functional");
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for Health, ec {}",
                                     ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else
            {
                status->functional = functional;
            }
            determineResourceState(asyncResp, status);
            return;
        });

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Object.Enable", "Enabled",
        [status, asyncResp](const boost::system::error_code& ec, bool enabled) {
            BMCWEB_LOG_DEBUG("getResourceStatus Enabled");
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for Enabled, ec {}",
                                     ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            else
            {
                status->enabled = enabled;
            }
            determineResourceState(asyncResp, status);
            return;
        });
}

} // namespace redfish
