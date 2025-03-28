/*
    This program serves as a future reference for me (or for any interested) on how to create a mesh network with esp32 using espressif. 
    This code is a more commented example inspired by the official wifi mesh network tutorial (https://github.com/espressif/esp-idf/blob/master/examples/mesh/internal_communication/main/mesh_main.c)

    Before starting, let's talk about `idf.py menuconfig`.
    `menuconfig` is the the Graphical Configuration Tool used to configure the esp32. There are many configuration possible, and it is also
    possible to write custom one. A custom configuration is contained in the file main/Kconfig.projbuild. It is taken from their example. 
    Now upon opening menuconfig, a new menu appears and it is possible to set some variables that will be later accessed in the code (SSID and router password, and other configurations). 

    Before running the project it is important to configure those settings beforhand.
*/

#include <string.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_light.h"
#include "nvs_flash.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/

// Pointer where the STA interface will be saved. you have to initialize it to NULL
static esp_netif_t *netif_sta = NULL;

static int mesh_layer = -1;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};


// Used in debug print
static const char *MESH_TAG = "mesh_main";
static const char *IP_TAG = "wifi_ip";


/*******************************************************
 *                Signatures
 *******************************************************/
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

void app_main(void) {
    // The ESP_ERROR_CHECK() works similarly to an assert: it checks the esp_err_t returned and
    // if it is not equal to ESP_OK it aborts the execution (and an error message is printed)

    //This line will initialize the led handler
    ESP_ERROR_CHECK(mesh_light_init());


    
    /************************************
    *           WIFI CONFIGURATION      *
    *************************************/
    //reference: https://docs.espressif.com/projects/esp-idf/en/v4.3.2/esp32/api-reference/storage/nvs_flash.html
    // NVS = non volatile storage. NVS is designed to store key:pair values in a partition of the flash memory throught the esp_partition API.
    // keys are ascii string with max lenght of 15. Values can be of various kind BUT NOT DOUBLE / FLOAT. It is not reccomended to store big variables here.
    // There is the concept of namespaces and much more. I'm pretty sure that the `menuconfig` settings are stored here, so you have to initialize the memory
    ESP_ERROR_CHECK(nvs_flash_init());

    // this line will startup the TCP/IP stack. It must be called once in the setup of the program
    ESP_ERROR_CHECK(esp_netif_init());

    // this function is provided by esp_event.h and it startup a default event handler. Further in the code, using esp_event_handler_register it will
    // handle different events
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    // Creates default STA and AP network interfaces for esp-mesh. It will return the STA interface in the netif_sta pointer.
    // TODO: capire meglio
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    // WIFI initialization. The macro WIFI_INIT_CONFIG_DEFAULT will return default configuration values
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    // Initialize WiFi Allocate resource for WiFi driver, such as WiFi control structure, RX/TX buffer, WiFi NVS structure etc. This WiFi also starts WiFi task.
    // It is important to configure it with the macro defined above to guarantee that all the fields gets the correct value
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    // With esp_event_handler_register we are registering an event. In this case we are registering the callback for
    // IP related event. We are registering them to the function ip_event_handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    // Set the WiFi API configuration storage type to WIFI_STORAGE_FLASH. We are telling the ESP to store all configuration in both memory and flash
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    // Start the wifi according to the configurations. In this case, WIFI_INIT_CONFIG_DEFAULT.
    // I (707) wifi:mode : sta (f4:65:0b:57:21:d8) + softAP (f4:65:0b:57:21:d9)
    // [MY ASSUMPTION] Remember that we have set the esp to create network interfaces for esp-mesh. Only the root node will connect to the router
    // So i'm still not connecting to WIFI, and this makes sense
    ESP_ERROR_CHECK(esp_wifi_start());

    /************************************
     *          MESH CONFIGURATION      *
     ***********************************/

    // Call after wifi initialization, it will initialize the mesh with default values
    ESP_ERROR_CHECK(esp_mesh_init());

    // Register a callback function for all mesh related events 
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));

    // Set mesh topology (I suppose that CONFIG_MESH_TOPOLOGY is taken from menuconfig, in my case chain)
    // This API must be called before starting the mesh
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));

    // Setting max layer (menuconfig, set to 6)
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));

    // Set the treshold to be reached to be a root node (100% of votes)
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));

    // Set the value for the RX queue size. It is the size of the receiver buffer
    // This node can hold up to 128 packets before dropping them
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));

    // PS stands for powersafe. This option is set in menuconfig
    // I disabled powersafe
#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#endif

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
        strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
        strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
    #ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
    #endif

    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d",  esp_get_minimum_free_heap_size(),
            esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
            esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());




}

// I assume this is the sign needed to handle the IP event callback. The code is self explainatory
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(IP_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));

}


void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;
    //Turning on the LED if i'm root
    if (esp_mesh_is_root())
        mesh_light_set(MESH_LIGHT_ON);
    else
        mesh_light_set(MESH_LIGHT_OFF);


    switch (event_id) {
        case MESH_EVENT_STARTED: {
            esp_mesh_get_id(&id);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
        }
        break;
        case MESH_EVENT_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
        }
        break;
        case MESH_EVENT_CHILD_CONNECTED: {
            printf("CHILD CONNECTED\n");
            mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
            child_connected->aid,
            MAC2STR(child_connected->mac));
        }
        break;
        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
            child_disconnected->aid,
            MAC2STR(child_disconnected->mac));
        }
        break;
        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
            routing_table->rt_size_change,
            routing_table->rt_size_new, mesh_layer);
        }
        break;
        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
            routing_table->rt_size_change,
            routing_table->rt_size_new, mesh_layer);
        }
        break;
        case MESH_EVENT_NO_PARENT_FOUND: {
            mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
            no_parent->scan_times);
        }
        /* TODO handler for the failure */
        break;
        // TODO: uncomment last line?
        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
            esp_mesh_get_id(&id);
            mesh_layer = connected->self_layer;
            memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
            ESP_LOGI(MESH_TAG,
            "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR", duty:%d",
            last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
            esp_mesh_is_root() ? "<ROOT>" :
            (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
            is_mesh_connected = true;
            if (esp_mesh_is_root()) {
                esp_netif_dhcpc_stop(netif_sta);
                esp_netif_dhcpc_start(netif_sta);
            }
            //esp_mesh_comm_p2p_start();
        }
        break;
        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
            ESP_LOGI(MESH_TAG,
            "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
            disconnected->reason);
            is_mesh_connected = false;
            mesh_disconnected_indicator();
            mesh_layer = esp_mesh_get_layer();
            }
        break;
        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
            mesh_layer = layer_change->new_layer;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
            last_layer, mesh_layer,
            esp_mesh_is_root() ? "<ROOT>" :
            (mesh_layer == 2) ? "<layer2>" : "");
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
        }
        break;
        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
            MAC2STR(root_addr->addr));
        }
        break;
        case MESH_EVENT_VOTE_STARTED: {
            printf("Started vote\n");
            mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
            ESP_LOGI(MESH_TAG,
            "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
            vote_started->attempts,
            vote_started->reason,
            MAC2STR(vote_started->rc_addr.addr));
        }
        break;
        case MESH_EVENT_VOTE_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
            break;
        }
        case MESH_EVENT_ROOT_SWITCH_REQ: {
            mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
            ESP_LOGI(MESH_TAG,
            "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
            switch_req->reason,
            MAC2STR( switch_req->rc_addr.addr));
        }
        break;
        case MESH_EVENT_ROOT_SWITCH_ACK: {
            /* new root */
            printf("New root\n");
            mesh_layer = esp_mesh_get_layer();
            esp_mesh_get_parent_bssid(&mesh_parent_addr);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
        }
        break;
        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
        }
        break;
        case MESH_EVENT_ROOT_FIXED: {
            printf("Root fixed\n");
            mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
            root_fixed->is_fixed ? "fixed" : "not fixed");
        }
        break;
        case MESH_EVENT_ROOT_ASKED_YIELD: {
            mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
            ESP_LOGI(MESH_TAG,
            "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
            MAC2STR(root_conflict->addr),
            root_conflict->rssi,
            root_conflict->capacity);
        }
        break;
        case MESH_EVENT_CHANNEL_SWITCH: {
            mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
        }
        break;
            case MESH_EVENT_SCAN_DONE: {
            mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
            scan_done->number);
        }
        break;
        case MESH_EVENT_NETWORK_STATE: {
            mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
            network_state->is_rootless);
        }
        break;
            case MESH_EVENT_STOP_RECONNECTION: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
        }
        break;
        case MESH_EVENT_FIND_NETWORK: {
            mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
            find_network->channel, MAC2STR(find_network->router_bssid));
        }
        break;
        case MESH_EVENT_ROUTER_SWITCH: {
            mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
            router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
        }
        break;
        case MESH_EVENT_PS_PARENT_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
        }
        break;
        case MESH_EVENT_PS_CHILD_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
            MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
        }
        break;
        default:
            ESP_LOGI(MESH_TAG, "unknown id:%" PRId32 "", event_id);
        break;
        }
}