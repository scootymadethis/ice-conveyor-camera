#include "command_handler.h"

#include "app_log.h"
#include "capture_service.h"
#include "espnow_service.h"
#include "lidar_protocol.h"
#include "preset_service.h"

static void log_command_once(const String &message)
{
    // The web UI can poll lidar_status frequently. Keep that path quiet by design.
    if (message == "lidar_status")
    {
        return;
    }

    log_infof("command", "received: %s", message.c_str());
}

void handle_client_message(WiFiClient &client, const String &message)
{
    log_command_once(message);

    if (message == "list_presets")
    {
        handle_list_presets(client);
    }
    else if (message.startsWith("preset_save "))
    {
        handle_preset_save(client, message);
    }
    else if (message.startsWith("preset_delete "))
    {
        handle_preset_delete(client, message);
    }
    else if (message.startsWith("preset_get "))
    {
        handle_preset_get(client, message);
    }
    else if (message == "lidar_status")
    {
        handle_lidar_status(client);
    }
    else if (message.startsWith("lidar_base "))
    {
        handle_lidar_base(client, message);
    }
    else if (message == "lidar_base_current")
    {
        handle_lidar_base_current(client);
    }
    else if (message.startsWith("lidar_sample_config "))
    {
        handle_lidar_sample_config(client, message);
    }
    else if (message.startsWith("lidar_post_frame_delay "))
    {
        handle_lidar_post_frame_delay(client, message);
    }
    else if (message.startsWith("lidar_enable "))
    {
        handle_lidar_enable(client, message);
    }
    else if (message == "espnow_send_last")
    {
        handle_espnow_send_last(client);
    }
    else if (message == "camera")
    {
        handle_camera_capture_command(client);
    }
    else if (message.startsWith("config "))
    {
        handle_camera_config_command(client, message);
    }
    else if (message.startsWith("ae_level "))
    {
        handle_ae_level_command(client, message);
    }
    else
    {
        log_warnf("command", "unknown command: %s", message.c_str());
        client.print("UNKNOWN_COMMAND ");
        client.println(message);
    }
}
