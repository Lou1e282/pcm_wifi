#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID          CONFIG_TEST_WIFI_SSID
#define WIFI_PASS          CONFIG_TEST_WIFI_PASS
#define SAMPLE_RATE_HZ     8000    
#define I2S_DMA_BYTES      1024
#define RINGBUF_BYTES      (I2S_DMA_BYTES * 16)
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "pcm_test";

static RingbufHandle_t      rb          = NULL;
static i2s_chan_handle_t    rx_handle   = NULL;
static EventGroupHandle_t   wifi_evt    = NULL;

/* ----------------------------- Wi-Fi Event ----------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START)
            esp_wifi_connect();
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            esp_wifi_connect();
            ESP_LOGW(TAG, "Retrying Wi-Fi connection");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_evt, WIFI_CONNECTED_BIT);
    }
}

/* ----------------------------- Wi-Fi Init ----------------------------- */
static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ----------------------------- I2S RX Task ----------------------------- */
static void i2s_rx_task(void *arg) {
    uint8_t *buf = heap_caps_malloc(I2S_DMA_BYTES, MALLOC_CAP_DEFAULT);
    size_t   bytes_read;
    for (;;) {
        if (i2s_channel_read(rx_handle, buf, I2S_DMA_BYTES, &bytes_read, portMAX_DELAY) == ESP_OK) {
            if (xRingbufferSend(rb, buf, bytes_read, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Ringbuffer full – dropping");
            }
        }
    }
}

/* ----------------------------- I2S Init ----------------------------- */
static void i2s_init(void) {
    i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_0,
        .role          = I2S_ROLE_MASTER,
        .dma_desc_num  = 4,
        .dma_frame_num = I2S_DMA_BYTES / 2,
        .auto_clear    = true
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk         = GPIO_NUM_NC,
            .bclk         = GPIO_NUM_26,
            .ws           = GPIO_NUM_42,
            .dout         = GPIO_NUM_NC,
            .din          = GPIO_NUM_21,
            .invert_flags = { .bclk_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    rb = xRingbufferCreate(RINGBUF_BYTES, RINGBUF_TYPE_BYTEBUF);
    assert(rb);

    xTaskCreatePinnedToCore(i2s_rx_task, "i2s_rx", 4096, NULL, 5, NULL, APP_CPU_NUM);
}

/* ----------------------------- Mock Upload Task ----------------------------- */
// static void mock_send_task(void *arg) {
//     ESP_LOGI(TAG, "mock_send_task started");
//     size_t bytes;
//     uint32_t total = 0;
//     for (;;) {
//         uint8_t *chunk = (uint8_t *)xRingbufferReceive(rb, &bytes, pdMS_TO_TICKS(1000));
//         if (chunk) {
//             total += (uint32_t)bytes;
//             ESP_LOGI(TAG, "Sent %zu bytes (total: %" PRIu32 ")", bytes, total);
//             vRingbufferReturnItem(rb, chunk);
//         } else {
//             ESP_LOGW(TAG, "No PCM received");
//         }
//     }
// }

static void http_post_task(void *arg) {
    ESP_LOGI(TAG, "http_post_task started");

    esp_http_client_config_t config = {
        .url = CONFIG_TEST_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,  // TLS certification
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type",      "application/octet-stream");
    esp_http_client_set_header(client, "Transfer-Encoding", "chunked");

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "HTTP stream open");

    size_t bytes;
    uint32_t total = 0;

    for (;;) {
        uint8_t *chunk = (uint8_t *)xRingbufferReceive(rb, &bytes, pdMS_TO_TICKS(1000));
        if (chunk) {
            int wr = esp_http_client_write(client, (const char *)chunk, bytes);
            vRingbufferReturnItem(rb, chunk);

            if (wr < 0) {
                ESP_LOGE(TAG, "Write error: %d", wr);
                break;  // connection lost
            } else {
                total += bytes;
                ESP_LOGI(TAG, "Uploaded %zu bytes (total: %" PRIu32 ")", bytes, total);
            }
        } else {
            ESP_LOGW(TAG, "No PCM received");
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/* ----------------------------- Main ----------------------------- */
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_evt = xEventGroupCreate();
    wifi_init();
    i2s_init();
    xEventGroupWaitBits(wifi_evt, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    xTaskCreate(http_post_task, "http_tx", 6144, NULL, 5, NULL);
}
