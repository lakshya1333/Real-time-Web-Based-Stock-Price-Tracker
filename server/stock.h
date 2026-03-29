#ifndef STOCK_H
#define STOCK_H

#include "network.h"
#include <pthread.h>

typedef struct {
    client_t *clients;
    int max_clients;
    pthread_mutex_t *clients_mutex;
} sim_args_t;

void init_stock_simulator(void);
void broadcast_prices(client_t *clients, int max_clients, pthread_mutex_t *clients_mutex);
void *stock_simulator_thread(void *arg);

#endif
