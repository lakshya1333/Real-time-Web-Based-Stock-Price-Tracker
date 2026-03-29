#include "stock.h"
#include "database.h"
#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static stock_t global_stocks[MAX_SYMBOLS];
static int global_num_stocks = 0;
static pthread_mutex_t stocks_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_stock_simulator(void) {
    srand(time(NULL));
    global_num_stocks = 0;
}

double fetch_stock_price(const char *symbol) {
    // Basic sanitization to prevent shell injection via malicious WebSocket payload
    for (int i = 0; symbol[i] != '\0'; i++) {
        char c = symbol[i];
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && 
            !(c >= '0' && c <= '9') && c != '.' && c != '-') {
            printf("Warning: Invalid character in stock symbol: %c\n", c);
            return -1.0;
        }
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -sL \"https://query1.finance.yahoo.com/v8/finance/chart/%s\" -H \"User-Agent: Mozilla/5.0\"", symbol);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1.0;
    
    char buffer[4096];
    size_t n = fread(buffer, 1, sizeof(buffer)-1, fp);
    buffer[n] = '\0';
    pclose(fp);
    
    char *match = strstr(buffer, "\"regularMarketPrice\":");
    if (match) {
        match += strlen("\"regularMarketPrice\":");
        double price = strtod(match, NULL);
        if (price > 0.0) return price;
    }
    return -1.0;
}

void add_stock(const char *symbol) {
    pthread_mutex_lock(&stocks_mutex);
    for (int i = 0; i < global_num_stocks; i++) {
        if (strcmp(global_stocks[i].symbol, symbol) == 0) {
            pthread_mutex_unlock(&stocks_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&stocks_mutex);

    double initial_price = fetch_stock_price(symbol);
    if (initial_price < 0.0) {
        initial_price = 100.0 + (rand() % 100); // fallback
    }

    pthread_mutex_lock(&stocks_mutex);
    // Double-check just in case
    for (int i = 0; i < global_num_stocks; i++) {
        if (strcmp(global_stocks[i].symbol, symbol) == 0) {
            pthread_mutex_unlock(&stocks_mutex);
            return;
        }
    }
    if (global_num_stocks < MAX_SYMBOLS) {
        strncpy(global_stocks[global_num_stocks].symbol, symbol, 31);
        global_stocks[global_num_stocks].symbol[31] = '\0';
        global_stocks[global_num_stocks].price = initial_price; 
        global_num_stocks++;
        printf("Added real stock: %s at price %.2f\n", symbol, initial_price);
    }
    pthread_mutex_unlock(&stocks_mutex);
}

void broadcast_prices(client_t *clients, int max_clients, pthread_mutex_t *clients_mutex) {
    pthread_mutex_lock(&stocks_mutex);
    if (global_num_stocks == 0) {
        pthread_mutex_unlock(&stocks_mutex);
        return; 
    }
    
    char json_payload[4096];
    int offset = snprintf(json_payload, sizeof(json_payload), "{\"type\":\"update\",\"data\":[");
    
    for (int i = 0; i < global_num_stocks; i++) {
        // Reduced random change to just a tiny visual jitter since we sync real values periodically
        double change = ((double)rand() / RAND_MAX) * 0.2 - 0.1; 
        global_stocks[i].price += change;
        if (global_stocks[i].price < 1.0) global_stocks[i].price = 1.0;
        
        db_record_price(global_stocks[i].symbol, global_stocks[i].price);
        
        offset += snprintf(json_payload + offset, sizeof(json_payload) - offset,
                           "{\"symbol\":\"%s\",\"price\":%.2f}%s",
                           global_stocks[i].symbol, global_stocks[i].price, (i == global_num_stocks - 1) ? "" : ",");
    }
    snprintf(json_payload + offset, sizeof(json_payload) - offset, "]}");
    
    size_t len = strlen(json_payload);
    pthread_mutex_unlock(&stocks_mutex);
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
    
    int iteration = 0;
    while (1) {
        usleep(500000); // Broadcast every 500ms
        iteration++;
        
        // Every ~10 seconds, sync prices with reality
        if (iteration % 20 == 0) {
            pthread_mutex_lock(&stocks_mutex);
            char symbols_to_fetch[MAX_SYMBOLS][32];
            int num_symbols = global_num_stocks;
            for (int i = 0; i < num_symbols; i++) {
                strcpy(symbols_to_fetch[i], global_stocks[i].symbol);
            }
            pthread_mutex_unlock(&stocks_mutex);
            
            for (int i = 0; i < num_symbols; i++) {
                double new_price = fetch_stock_price(symbols_to_fetch[i]);
                if (new_price > 0) {
                    pthread_mutex_lock(&stocks_mutex);
                    for (int j = 0; j < global_num_stocks; j++) {
                        if (strcmp(global_stocks[j].symbol, symbols_to_fetch[i]) == 0) {
                            // Only update if difference is noticeable or real data moved
                            global_stocks[j].price = new_price;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&stocks_mutex);
                }
            }
        }
        
        broadcast_prices(args->clients, args->max_clients, args->clients_mutex);
    }
    return NULL;
}
