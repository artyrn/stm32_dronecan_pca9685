#ifndef PARAM_SERVER_H
#define PARAM_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "canard.h"

bool param_server_should_accept(
    uint64_t *out_data_type_signature,
    uint16_t data_type_id,
    CanardTransferType transfer_type);

bool param_server_handle(
    CanardInstance *canard,
    CanardRxTransfer *transfer);

#endif
