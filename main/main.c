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

// Used in debug print
static const char *MESH_TAG = "mesh_main";
static const char *IP_TAG = "wifi_ip";


/*******************************************************
 *                Signatures
 *******************************************************/
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

void app_main(void) {
    // The ESP_ERROR_CHECK() works similarly to an assert: it checks the esp_err_t returned and
    // if it is not equal to ESP_OK it aborts the execution (and an error message is printed)

    //This line will initialize the led handler
    ESP_ERROR_CHECK(mesh_light_init());
    //Turning on the led
    ESP_ERROR_CHECK(mesh_light_set(MESH_LIGHT_ON));

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



}

// I assume this is the sign needed to handle the IP event callback. The code is self explainatory
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(IP_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));

}
