#include "stock.h"
#include "database.h"
#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *symbols[] = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA"};
static double prices[] = {150.0, 2800.0, 300.0, 3400.0, 700.0};
static int num_symbols = 5;

void init_stock_simulator(void) {
    srand(time(NULL));
}

void broadcast_prices(client_t *clients, int max_clients, pthread_mutex_t *clients_mutex) {
    char json_payload[1024];
    int offset = snprintf(json_payload, sizeof(json_payload), "{\"type\":\"update\",\"data\":[");
    
    for (int i = 0; i < num_symbols; i++) {
        double change = ((double)rand() / RAND_MAX) * 2.0 - 1.0; 
        prices[i] += change;
        if (prices[i] < 1.0) prices[i] = 1.0;
        
        db_record_price(symbols[i], prices[i]);
        
        offset += snprintf(json_payload + offset, sizeof(json_payload) - offset,
                           "{\"symbol\":\"%s\",\"price\":%.2f}%s",
                           symbols[i], prices[i], (i == num_symbols - 1) ? "" : ",");
    }
    snprintf(json_payload + offset, sizeof(json_payload) - offset, "]}");
    
    size_t len = strlen(json_payload);
    printf("Broadcasting live data... len=%zu\n", len);

    pthread_mutex_lock(clients_mutex);
    for (int i = 0; i < max_clients; i++) {
        if (clients[i].fd != -1 && clients[i].is_websocket && clients[i].authenticated) {
            int w = ws_write_frame(clients[i].ssl, json_payload, len, WEBSOCKET_OPCODE_TEXT);
            if (w <= 0) {
                // Connection closed or error, letting epoll socket error handler naturally remove it
            }
        }
    }
    pthread_mutex_unlock(clients_mutex);
}

void *stock_simulator_thread(void *arg) {
    sim_args_t *args = (sim_args_t *)arg;
    init_stock_simulator();
    
    while (1) {
        usleep(500000); // Broadcast every 500ms
        broadcast_prices(args->clients, args->max_clients, args->clients_mutex);
    }
    return NULL;
}
