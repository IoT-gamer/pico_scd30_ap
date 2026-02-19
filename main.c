/**
 * Combined SCD30 Access Point for Raspberry Pi Pico W
 */

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

// Include SCD30 Drivers
#include "scd30_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

// Include Network Helpers
#include "dhcpserver.h"
#include "dnsserver.h"

// Configuration
#define AP_SSID "Pico_CO2_Monitor"
#define AP_PASSWORD "12345678" // Must be >= 8 chars or NULL for open
#define TCP_PORT 80
#define LED_GPIO 0

// Global Sensor Data (Updated in main loop, Read in HTTP callback)
typedef struct {
    float co2;
    float temp;
    float hum;
    bool updated;
} sensor_data_t;

volatile sensor_data_t g_sensor_data = {0.0f, 0.0f, 0.0f, false};

// HTTP Response Templates
// Auto-refresh every 3 seconds
#define HTTP_HEADER "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
#define HTML_BODY \
    "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"3\">" \
    "<title>Pico CO2 Monitor</title>" \
    "<style>body{font-family:sans-serif;text-align:center;padding:20px;}" \
    ".val{font-size:2em;font-weight:bold;color:#2c3e50;}" \
    ".label{color:#7f8c8d;}</style></head>" \
    "<body><h1>SCD30 Sensor Reading</h1>" \
    "<div><div class='label'>CO2 Concentration</div><div class='val'>%.2f ppm</div></div><br>" \
    "<div><div class='label'>Temperature</div><div class='val'>%.2f &deg;C</div></div><br>" \
    "<div><div class='label'>Humidity</div><div class='val'>%.2f %%</div></div>" \
    "</body></html>"

// ---------------------------------------------------------------------------
// TCP Server Logic
// ---------------------------------------------------------------------------

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    ip_addr_t gw;
} TCP_SERVER_T;

static err_t tcp_server_close(void *arg, struct tcp_pcb *pcb) {
    tcp_arg(pcb, NULL);
    tcp_close(pcb);
    return ERR_OK;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!p) {
        return tcp_server_close(arg, pcb);
    }

    // Prepare buffer for response
    char response[1024];
    
    // Format the HTML with current global sensor data
    int body_len = snprintf(response, sizeof(response), HTML_BODY, 
                            g_sensor_data.co2, 
                            g_sensor_data.temp, 
                            g_sensor_data.hum);
    
    // Write Header
    tcp_write(pcb, HTTP_HEADER, strlen(HTTP_HEADER), TCP_WRITE_FLAG_COPY);
    
    // Write Body
    tcp_write(pcb, response, body_len, TCP_WRITE_FLAG_COPY);
    
    // Finish
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return tcp_server_close(arg, pcb);
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    if (err != ERR_OK || client_pcb == NULL) return ERR_VAL;
    tcp_recv(client_pcb, tcp_server_recv);
    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err) return false;

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        if (pcb) tcp_close(pcb);
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    stdio_init_all();
    sleep_ms(2000); // Give USB time to settle
    printf("Starting Pico SCD30 Access Point...\n");

    // Initialize Wi-Fi in Threadsafe Background Mode
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return 1;
    }
    
    const char *ap_name = AP_SSID;
    const char *password = AP_PASSWORD;

    cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

    // Setup Network Services (DHCP / DNS)
    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    ip4_addr_t mask;
    IP4_ADDR(ip_2_ip4(&state->gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);

    if (!tcp_server_open(state)) {
        printf("Failed to start Web Server\n");
        return 1;
    }

    printf("Access Point '%s' started on 192.168.4.1\n", ap_name);

    // Initialize SCD30 Sensor
    sensirion_i2c_hal_init();
    
    scd30_init(SCD30_I2C_ADDR_61);
    
    // Set measurement interval (2 seconds)
    scd30_set_measurement_interval(2);
    sleep_ms(200);
    int16_t error = scd30_start_periodic_measurement(0);
    if (error) printf("SCD30 Start Failed: %d\n", error);

    // Main Loop
    while(1) {
        float co2, temp, hum;
        
        // Try reading up to 3 times to handle Wi-Fi interrupt collisions
        for(int i = 0; i < 3; i++) {
            error = scd30_blocking_read_measurement_data(&co2, &temp, &hum);
            if (error == NO_ERROR) {
                break; // Success!
            }
            sleep_ms(10); // Wait 10ms before retrying
        }
        
        if (error == NO_ERROR) {
            // Update global data for the Web Server
            g_sensor_data.co2 = co2;
            g_sensor_data.temp = temp;
            g_sensor_data.hum = hum;
            g_sensor_data.updated = true;
            
            // Debug output to serial
            printf("SCD30: CO2=%.2f, T=%.2f, H=%.2f\n", co2, temp, hum);
            
            // Blink LED to indicate valid read
            bool led_is_on;
            cyw43_gpio_get(&cyw43_state, LED_GPIO, &led_is_on);
            cyw43_gpio_set(&cyw43_state, LED_GPIO, !led_is_on);
        } else {
            // Only print if we failed 3 times in a row
            printf("SCD30 Read Error: %d (Skipping this frame)\n", error);
            
            // Optional: Re-init I2C if the sensor completely hung
            scd30_init(SCD30_I2C_ADDR_61); 
        }
        
        // No sleep needed here as blocking_read handles the 2s timing internally
    }

    // Cleanup (Unreachable in this loop)
    cyw43_arch_deinit();
    return 0;
}