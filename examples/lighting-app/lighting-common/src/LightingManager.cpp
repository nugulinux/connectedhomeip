/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "LightingManager.h"

#include <glib.h>
#include <gio/gio.h>

#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <app-common/zap-generated/attributes/Accessors.h>

#define DBUS_SERVICE_DEVICECTL "com.sktnugu.devicectl"
#define DBUS_PATH_DEVICECTL "/DeviceControl"
#define DBUS_IFACE_DEVICECTL DBUS_SERVICE_DEVICECTL ".DeviceControl"

#define MATTER_LEVEL_MIN 0x01 // 1
#define MATTER_LEVEL_MAX 0xFE // 254
#define NU120_LIGHT_LEVEL_MIN 1 // 1
#define NU120_LIGHT_LEVEL_MAX 5 // 5

#define ENDPOINT_ID 1

using namespace chip::app;
using namespace chip::app::Clusters;

LightingManager LightingManager::sLight;

static GDBusConnection* _conn;

static void on_light_status(GDBusConnection* connection,
    const gchar* sender_name, const gchar* object_path,
    const gchar* interface_name,
    const gchar* signal_name, GVariant* parameters,
    gpointer userdata)
{
    gboolean power = FALSE;
    guint br = 0;
    gboolean auto_br = FALSE;

    g_variant_get(parameters, "(bub)", &power, &br, &auto_br);

    ChipLogProgress(AppServer, "LightingManager::signal received (%d, %d, %d)", power, br, auto_br);
#if 0
    chip::DeviceLayer::SystemLayer().ScheduleLambda([power, br] {
        if (power == TRUE && LightingMgr().IsTurnedOn() == false) {
            ChipLogProgress(AppServer, "LightingManager::set power to true");
            Clusters::OnOff::Attributes::OnOff::Set(ENDPOINT_ID, true);
        } else if (power == FALSE && LightingMgr().IsTurnedOn() == true) {
            ChipLogProgress(AppServer, "LightingManager::set power to false");
            Clusters::OnOff::Attributes::OnOff::Set(ENDPOINT_ID, false);
        }

        uint8_t matter_br;

        matter_br = MATTER_LEVEL_MIN + br * MATTER_LEVEL_MAX / NU120_LIGHT_LEVEL_MAX;
        if (matter_br > MATTER_LEVEL_MAX)
            matter_br = MATTER_LEVEL_MAX;

        ChipLogProgress(AppServer, "LightingManager::Level control (%d -> %d)", LightingMgr().GetLevel(), matter_br);

        if (LightingMgr().GetLevel() != matter_br)
            Clusters::LevelControl::Attributes::CurrentLevel::Set(ENDPOINT_ID, matter_br);
    });
#endif
}

CHIP_ERROR InitOnGLibMatterContext(LightingManager* userdata)
{
    guint id;
    GError* error = nullptr;
    GVariant* ret;

    _conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        ChipLogError(AppServer, "g_bus_get_sync() failed.");
        g_error_free(error);
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogProgress(AppServer, "LightingManager::dbus get ok");

    id = g_dbus_connection_signal_subscribe(
        _conn, DBUS_SERVICE_DEVICECTL, DBUS_IFACE_DEVICECTL, "onLightChanged",
        DBUS_PATH_DEVICECTL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_light_status, NULL, NULL);
    if (id == 0) {
        ChipLogError(AppServer, "g_dbus_connection_signal_subscribe() failed.");
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogProgress(AppServer, "LightingManager::signal subscribed");

    ret = g_dbus_connection_call_sync(
        _conn, DBUS_SERVICE_DEVICECTL, DBUS_PATH_DEVICECTL, DBUS_IFACE_DEVICECTL,
        "getLightStates", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
        1000, NULL, &error);
    if (error) {
        ChipLogError(AppServer, "g_dbus_connection_call_sync() failed: %s", error->message);
        g_error_free(error);
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogProgress(AppServer, "LightingManager::dbus method call success");

    if (ret) {
        gboolean power;
        guint br;
        gboolean auto_br;

        g_variant_get(ret, "(bub)", &power, &br, &auto_br);

        br = br * MATTER_LEVEL_MAX / NU120_LIGHT_LEVEL_MAX;
        ChipLogProgress(AppServer, "LightingManager::power=%d, br=%d", power, br);

        g_variant_unref(ret);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR SetOnGLibMatterContext(LightingManager* userdata)
{
    GError* error = nullptr;
    GVariant* ret;
    gboolean power;
    gint br;

    power = userdata->IsTurnedOn();
    br = NU120_LIGHT_LEVEL_MIN + userdata->GetLevel() * NU120_LIGHT_LEVEL_MAX / MATTER_LEVEL_MAX;
    if (br > NU120_LIGHT_LEVEL_MAX)
        br = NU120_LIGHT_LEVEL_MAX;

    ChipLogProgress(AppServer, "LightingManager::dbus method call (power=%d, br=%d)", power, br);

    ret = g_dbus_connection_call_sync(
        _conn, DBUS_SERVICE_DEVICECTL, DBUS_PATH_DEVICECTL, DBUS_IFACE_DEVICECTL,
        "setLightPower", g_variant_new("(bi)", power, br), NULL, G_DBUS_CALL_FLAGS_NONE,
        1000, NULL, &error);
    if (error) {
        ChipLogError(AppServer, "g_dbus_connection_call_sync() failed: %s", error->message);
        g_error_free(error);
        return CHIP_ERROR_INTERNAL;
    }

    if (ret)
        g_variant_unref(ret);

    ChipLogProgress(AppServer, "LightingManager::dbus method call success");

    return CHIP_NO_ERROR;
}

CHIP_ERROR LightingManager::Init()
{
    CHIP_ERROR ret;

    ChipLogProgress(AppServer, "LightingManager::1");

    ret = chip::DeviceLayer::PlatformMgrImpl().GLibMatterContextInvokeSync(InitOnGLibMatterContext, this);

    ChipLogProgress(AppServer, "LightingManager::2");

    mState = kState_On;
    mLevel = 0;

    return ret;
}

bool LightingManager::IsTurnedOn()
{
    return mState == kState_On;
}

void LightingManager::SetCallbacks(LightingCallback_fn aActionInitiated_CB, LightingCallback_fn aActionCompleted_CB)
{
    mActionInitiated_CB = aActionInitiated_CB;
    mActionCompleted_CB = aActionCompleted_CB;
}

bool LightingManager::InitiateAction(Action_t aAction)
{
    // TODO: this function is called InitiateAction because we want to implement some features such as ramping up here.
    bool action_initiated = false;
    State_t new_state;

    switch (aAction)
    {
    case ON_ACTION:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(ON_ACTION)");
        break;
    case OFF_ACTION:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(OFF_ACTION)");
        break;
    default:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(unknown)");
        break;
    }

    // Initiate On/Off Action only when the previous one is complete.
    if (mState == kState_Off && aAction == ON_ACTION)
    {
        action_initiated = true;
        new_state        = kState_On;
    }
    else if (mState == kState_On && aAction == OFF_ACTION)
    {
        action_initiated = true;
        new_state        = kState_Off;
    }

    if (action_initiated)
    {
        if (mActionInitiated_CB)
        {
            mActionInitiated_CB(aAction);
        }

        Set(new_state == kState_On);

        if (mActionCompleted_CB)
        {
            mActionCompleted_CB(aAction);
        }
    }

    return action_initiated;
}

void LightingManager::Set(bool aOn)
{
    if (aOn)
    {
        mState = kState_On;
    }
    else
    {
        mState = kState_Off;
    }

    ChipLogProgress(AppServer, "LightingManager::Set(%d)", mState);
    chip::DeviceLayer::PlatformMgrImpl().GLibMatterContextInvokeSync(SetOnGLibMatterContext, this);
}

uint8_t LightingManager::GetLevel()
{
    return mLevel;
}

void LightingManager::SetLevel(uint8_t value)
{
    if (value == mLevel)
        return;

    mLevel = value;

    ChipLogProgress(AppServer, "LightingManager::SetLevel(%d)", mLevel);

    if (IsTurnedOn() == false)
        ChipLogProgress(AppServer, "LightingManager::skip due to power-off status");

    chip::DeviceLayer::PlatformMgrImpl().GLibMatterContextInvokeSync(SetOnGLibMatterContext, this);
}
