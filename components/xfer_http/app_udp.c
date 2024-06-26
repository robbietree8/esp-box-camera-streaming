#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "errno.h"
#include "app_wifi.h"


#define HOST_IP_ADDR CONFIG_UDP_PUSH_SERVER_IP
#define PORT CONFIG_UDP_PUSH_SERVER_PORT

static const char *TAG = "camera_udp";
static const size_t chunk_length = 1024;
static int sock;
static struct sockaddr_in dest_addr;


static void send_packet_data(uint8_t *buf, size_t len, size_t chunkLength)
{
    uint8_t buffer[chunkLength];
    size_t blen = sizeof(buffer);
    size_t rest = len % blen;

    for(uint8_t i = 0; i < len / blen; i++) {
        memcpy(buffer, buf + i * blen, blen);
        sendto(sock, buffer, blen, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    if(rest) {
        memcpy(buffer, buf + (len - rest), rest);
        sendto(sock, buffer, rest, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
}

static void udp_client_task(void *pvParameters)
{
    camera_fb_t *fb = NULL;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;

    while (1) {
        if(!is_wifi_connected()) {
            ESP_LOGE(TAG, "wifi not connected");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        
        dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, PORT);

        while (1) {
            fb = esp_camera_fb_get();
            if (!fb) {
                ESP_LOGE(TAG, "Camera capture failed");
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
                ESP_LOGI(TAG, "JPG Image size: %u, format: %d", _jpg_buf_len, fb->format);
            }

            if (_jpg_buf_len > 0) {
                send_packet_data(_jpg_buf, _jpg_buf_len, chunk_length);
            }

            if (fb) {
                esp_camera_fb_return(fb);
                fb = NULL;
                _jpg_buf = NULL;
            } else if (_jpg_buf) {
                _jpg_buf = NULL;
            }

            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_udp_main()
{
    xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
}