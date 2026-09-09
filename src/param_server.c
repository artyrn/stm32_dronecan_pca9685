#include <stdint.h>
#include <stdbool.h>

#include "param_server.h"
#include "config.h"

#include "uavcan.protocol.param.GetSet_req.h"
#include "uavcan.protocol.param.GetSet_res.h"
#include "uavcan.protocol.param.ExecuteOpcode_req.h"
#include "uavcan.protocol.param.ExecuteOpcode_res.h"


enum {
    PARAM_NODE_ID = 0,
    PARAM_CAN_BITRATE,
    PARAM_PCA_COUNT,
    PARAM_FS_TIMEOUT,
    PARAM_SYSTEM_COUNT
};

enum {
    OUT_PARAM_TYPE = 0,
    OUT_PARAM_ON,
    OUT_PARAM_OFF,
    OUT_PARAM_FS,
    OUT_PARAM_FS_STATE,
    OUT_PARAM_TIME,
    OUT_PARAM_RETRIG,
    OUT_PARAM_COUNT
};

#define OUTPUT_DEFAULT_TYPE       OUTPUT_PWM
#define OUTPUT_DEFAULT_ON_US      2000U
#define OUTPUT_DEFAULT_OFF_US     1000U
#define OUTPUT_DEFAULT_FS_US      1500U
#define OUTPUT_DEFAULT_FS_STATE   0U
#define OUTPUT_DEFAULT_TIME_MS    1000U
#define OUTPUT_DEFAULT_RETRIG     PULSE_IGNORE

#define OUTPUT_TIME_MIN_MS        1U
#define OUTPUT_TIME_MAX_MS        60000U


static const char param_name_node_id[] =
    "NODE_ID";

static const char param_name_can_bitrate[] =
    "CAN_BITRATE";

static const char param_name_pca_count[] =
    "PCA_COUNT";

static const char param_name_fs_timeout[] =
    "FS_TIMEOUT";

static const char out_suffix_type[] =
    "TYPE";

static const char out_suffix_on[] =
    "ON";

static const char out_suffix_off[] =
    "OFF";

static const char out_suffix_fs[] =
    "FS";

static const char out_suffix_fs_state[] =
    "FS_STATE";

static const char out_suffix_time[] =
    "TIME";

static const char out_suffix_retrig[] =
    "RETRIG";


static void zero_bytes(void *ptr, uint32_t size)
{
    uint8_t *p = (uint8_t *)ptr;

    for (uint32_t i = 0U; i < size; i++) {
        p[i] = 0U;
    }
}


static uint8_t string_length(const char *text)
{
    uint8_t length = 0U;

    if (text == 0) {
        return 0U;
    }

    while ((text[length] != '\0') &&
           (length < 92U)) {
        length++;
    }

    return length;
}


static bool request_name_equal(
    const struct uavcan_protocol_param_GetSetRequest *request,
    const char *name)
{
    const uint8_t length =
        string_length(name);

    if (request->name.len != length) {
        return false;
    }

    for (uint8_t i = 0U; i < length; i++) {
        if (request->name.data[i] !=
            (uint8_t)name[i]) {
            return false;
        }
    }

    return true;
}


static bool request_suffix_equal(
    const struct uavcan_protocol_param_GetSetRequest *request,
    uint8_t offset,
    const char *suffix)
{
    const uint8_t suffix_length =
        string_length(suffix);

    if (request->name.len !=
        (uint8_t)(offset + suffix_length)) {
        return false;
    }

    for (uint8_t i = 0U; i < suffix_length; i++) {
        if (request->name.data[offset + i] !=
            (uint8_t)suffix[i]) {
            return false;
        }
    }

    return true;
}


static uint16_t param_get_count(void)
{
    return (uint16_t)(
        PARAM_SYSTEM_COUNT +
        ((uint16_t)config_get_output_count() * OUT_PARAM_COUNT));
}


static bool decode_output_param_index(
    uint16_t index,
    uint8_t *output,
    uint8_t *field)
{
    if (index < PARAM_SYSTEM_COUNT) {
        return false;
    }

    const uint16_t relative =
        (uint16_t)(index - PARAM_SYSTEM_COUNT);

    const uint8_t out =
        (uint8_t)(relative / OUT_PARAM_COUNT);

    const uint8_t fld =
        (uint8_t)(relative % OUT_PARAM_COUNT);

    if (out >= config_get_output_count()) {
        return false;
    }

    *output = out;
    *field = fld;

    return true;
}


static int16_t find_output_param_by_name(
    const struct uavcan_protocol_param_GetSetRequest *request)
{
    /* Exact format: OUT01_TYPE ... OUT32_RETRIG */
    if (request->name.len < 8U) {
        return -1;
    }

    if ((request->name.data[0] != (uint8_t)'O') ||
        (request->name.data[1] != (uint8_t)'U') ||
        (request->name.data[2] != (uint8_t)'T') ||
        (request->name.data[5] != (uint8_t)'_')) {
        return -1;
    }

    const uint8_t tens = request->name.data[3];
    const uint8_t ones = request->name.data[4];

    if ((tens < (uint8_t)'0') ||
        (tens > (uint8_t)'9') ||
        (ones < (uint8_t)'0') ||
        (ones > (uint8_t)'9')) {
        return -1;
    }

    const uint8_t number =
        (uint8_t)(((tens - (uint8_t)'0') * 10U) +
                  (ones - (uint8_t)'0'));

    if ((number == 0U) ||
        (number > config_get_output_count())) {
        return -1;
    }

    uint8_t field;

    if (request_suffix_equal(request, 6U, out_suffix_type)) {
        field = OUT_PARAM_TYPE;
    } else if (request_suffix_equal(request, 6U, out_suffix_on)) {
        field = OUT_PARAM_ON;
    } else if (request_suffix_equal(request, 6U, out_suffix_off)) {
        field = OUT_PARAM_OFF;
    } else if (request_suffix_equal(request, 6U, out_suffix_fs)) {
        field = OUT_PARAM_FS;
    } else if (request_suffix_equal(request, 6U, out_suffix_fs_state)) {
        field = OUT_PARAM_FS_STATE;
    } else if (request_suffix_equal(request, 6U, out_suffix_time)) {
        field = OUT_PARAM_TIME;
    } else if (request_suffix_equal(request, 6U, out_suffix_retrig)) {
        field = OUT_PARAM_RETRIG;
    } else {
        return -1;
    }

    return (int16_t)(
        PARAM_SYSTEM_COUNT +
        (((uint16_t)number - 1U) * OUT_PARAM_COUNT) +
        field);
}


static int16_t param_find(
    const struct uavcan_protocol_param_GetSetRequest *request)
{
    if (request->name.len == 0U) {
        if (request->index < param_get_count()) {
            return (int16_t)request->index;
        }

        return -1;
    }

    if (request_name_equal(request, param_name_node_id)) {
        return PARAM_NODE_ID;
    }

    if (request_name_equal(request, param_name_can_bitrate)) {
        return PARAM_CAN_BITRATE;
    }

    if (request_name_equal(request, param_name_pca_count)) {
        return PARAM_PCA_COUNT;
    }

    if (request_name_equal(request, param_name_fs_timeout)) {
        return PARAM_FS_TIMEOUT;
    }

    return find_output_param_by_name(request);
}


static int64_t param_get_value(uint16_t index)
{
    const config_t *cfg =
        config_get();

    switch (index) {

    case PARAM_NODE_ID:
        return (int64_t)cfg->node_id;

    case PARAM_CAN_BITRATE:
        return (int64_t)cfg->can_bitrate;

    case PARAM_PCA_COUNT:
        return (int64_t)cfg->pca_count;

    case PARAM_FS_TIMEOUT:
        return (int64_t)cfg->failsafe_timeout_ms;

    default:
        break;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return 0;
    }

    const output_config_t *out =
        config_get_output(output);

    if (out == 0) {
        return 0;
    }

    switch (field) {

    case OUT_PARAM_TYPE:
        return (int64_t)out->type;

    case OUT_PARAM_ON:
        return (int64_t)out->on_pwm_us;

    case OUT_PARAM_OFF:
        return (int64_t)out->off_pwm_us;

    case OUT_PARAM_FS:
        return (int64_t)out->failsafe_pwm_us;

    case OUT_PARAM_FS_STATE:
        return (int64_t)out->failsafe_state;

    case OUT_PARAM_TIME:
        return (int64_t)out->pulse_time_ms;

    case OUT_PARAM_RETRIG:
        return (int64_t)out->pulse_retrigger;

    default:
        return 0;
    }
}


static int64_t param_get_default(uint16_t index)
{
    switch (index) {

    case PARAM_NODE_ID:
        return CONFIG_DEFAULT_NODE_ID;

    case PARAM_CAN_BITRATE:
        return CONFIG_DEFAULT_CAN_BITRATE;

    case PARAM_PCA_COUNT:
        return CONFIG_DEFAULT_PCA_COUNT;

    case PARAM_FS_TIMEOUT:
        return CONFIG_DEFAULT_FS_TIMEOUT;

    default:
        break;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return 0;
    }

    (void)output;

    switch (field) {

    case OUT_PARAM_TYPE:
        return OUTPUT_DEFAULT_TYPE;

    case OUT_PARAM_ON:
        return OUTPUT_DEFAULT_ON_US;

    case OUT_PARAM_OFF:
        return OUTPUT_DEFAULT_OFF_US;

    case OUT_PARAM_FS:
        return OUTPUT_DEFAULT_FS_US;

    case OUT_PARAM_FS_STATE:
        return OUTPUT_DEFAULT_FS_STATE;

    case OUT_PARAM_TIME:
        return OUTPUT_DEFAULT_TIME_MS;

    case OUT_PARAM_RETRIG:
        return OUTPUT_DEFAULT_RETRIG;

    default:
        return 0;
    }
}


static int64_t param_get_min(uint16_t index)
{
    switch (index) {

    case PARAM_NODE_ID:
        return 1;

    case PARAM_CAN_BITRATE:
        return 125000;

    case PARAM_PCA_COUNT:
        return 1;

    case PARAM_FS_TIMEOUT:
        return 10;

    default:
        break;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return 0;
    }

    (void)output;

    switch (field) {

    case OUT_PARAM_TYPE:
        return OUTPUT_DISABLED;

    case OUT_PARAM_ON:
    case OUT_PARAM_OFF:
    case OUT_PARAM_FS:
        return CONFIG_PWM_MIN_US;

    case OUT_PARAM_FS_STATE:
    case OUT_PARAM_RETRIG:
        return 0;

    case OUT_PARAM_TIME:
        return OUTPUT_TIME_MIN_MS;

    default:
        return 0;
    }
}


static int64_t param_get_max(uint16_t index)
{
    switch (index) {

    case PARAM_NODE_ID:
        return 125;

    case PARAM_CAN_BITRATE:
        return 1000000;

    case PARAM_PCA_COUNT:
        return CONFIG_MAX_PCA;

    case PARAM_FS_TIMEOUT:
        return 60000;

    default:
        break;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return 0;
    }

    (void)output;

    switch (field) {

    case OUT_PARAM_TYPE:
        return OUTPUT_PULSE;

    case OUT_PARAM_ON:
    case OUT_PARAM_OFF:
    case OUT_PARAM_FS:
        return CONFIG_PWM_MAX_US;

    case OUT_PARAM_FS_STATE:
    case OUT_PARAM_RETRIG:
        return 1;

    case OUT_PARAM_TIME:
        return OUTPUT_TIME_MAX_MS;

    default:
        return 0;
    }
}


static bool can_bitrate_valid(int64_t value)
{
    return
        (value == 125000) ||
        (value == 250000) ||
        (value == 500000) ||
        (value == 1000000);
}


static bool param_set_output_value(
    uint8_t output,
    uint8_t field,
    int64_t value)
{
    output_config_t *out =
        config_get_output_mutable(output);

    if (out == 0) {
        return false;
    }

    output_config_t old = *out;

    switch (field) {

    case OUT_PARAM_TYPE:
        if ((value < OUTPUT_DISABLED) ||
            (value > OUTPUT_PULSE)) {
            return false;
        }
        out->type = (uint8_t)value;
        break;

    case OUT_PARAM_ON:
        if ((value < CONFIG_PWM_MIN_US) ||
            (value > CONFIG_PWM_MAX_US)) {
            return false;
        }
        out->on_pwm_us = (uint16_t)value;
        break;

    case OUT_PARAM_OFF:
        if ((value < CONFIG_PWM_MIN_US) ||
            (value > CONFIG_PWM_MAX_US)) {
            return false;
        }
        out->off_pwm_us = (uint16_t)value;
        break;

    case OUT_PARAM_FS:
        if ((value < CONFIG_PWM_MIN_US) ||
            (value > CONFIG_PWM_MAX_US)) {
            return false;
        }
        out->failsafe_pwm_us = (uint16_t)value;
        break;

    case OUT_PARAM_FS_STATE:
        if ((value < 0) ||
            (value > 1)) {
            return false;
        }
        out->failsafe_state = (uint8_t)value;
        break;

    case OUT_PARAM_TIME:
        if ((value < OUTPUT_TIME_MIN_MS) ||
            (value > OUTPUT_TIME_MAX_MS)) {
            return false;
        }
        out->pulse_time_ms = (uint32_t)value;
        break;

    case OUT_PARAM_RETRIG:
        if ((value < PULSE_IGNORE) ||
            (value > PULSE_RESTART)) {
            return false;
        }
        out->pulse_retrigger = (uint8_t)value;
        break;

    default:
        return false;
    }

    if (config_validate() != 0) {
        *out = old;
        return false;
    }

    return true;
}


static bool param_set_value(
    uint16_t index,
    int64_t value)
{
    config_t *cfg =
        config_get_mutable();

    switch (index) {

    case PARAM_NODE_ID:
    {
        if ((value < 1) ||
            (value > 125)) {
            return false;
        }

        const uint8_t old =
            cfg->node_id;

        cfg->node_id =
            (uint8_t)value;

        if (config_validate() != 0) {
            cfg->node_id = old;
            return false;
        }

        return true;
    }

    case PARAM_CAN_BITRATE:
    {
        if (!can_bitrate_valid(value)) {
            return false;
        }

        const uint32_t old =
            cfg->can_bitrate;

        cfg->can_bitrate =
            (uint32_t)value;

        if (config_validate() != 0) {
            cfg->can_bitrate = old;
            return false;
        }

        return true;
    }

    case PARAM_PCA_COUNT:
    {
        if ((value < 1) ||
            (value > CONFIG_MAX_PCA)) {
            return false;
        }

        const uint8_t old =
            cfg->pca_count;

        cfg->pca_count =
            (uint8_t)value;

        if (config_validate() != 0) {
            cfg->pca_count = old;
            return false;
        }

        return true;
    }

    case PARAM_FS_TIMEOUT:
    {
        if ((value < 10) ||
            (value > 60000)) {
            return false;
        }

        const uint16_t old =
            cfg->failsafe_timeout_ms;

        cfg->failsafe_timeout_ms =
            (uint16_t)value;

        if (config_validate() != 0) {
            cfg->failsafe_timeout_ms = old;
            return false;
        }

        return true;
    }

    default:
        break;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return false;
    }

    return param_set_output_value(output, field, value);
}


static void fill_integer_value(
    struct uavcan_protocol_param_Value *value,
    int64_t number)
{
    value->union_tag =
        UAVCAN_PROTOCOL_PARAM_VALUE_INTEGER_VALUE;

    value->integer_value =
        number;
}


static void fill_integer_numeric(
    struct uavcan_protocol_param_NumericValue *value,
    int64_t number)
{
    value->union_tag =
        UAVCAN_PROTOCOL_PARAM_NUMERICVALUE_INTEGER_VALUE;

    value->integer_value =
        number;
}


static const char *output_field_suffix(uint8_t field)
{
    switch (field) {

    case OUT_PARAM_TYPE:
        return out_suffix_type;

    case OUT_PARAM_ON:
        return out_suffix_on;

    case OUT_PARAM_OFF:
        return out_suffix_off;

    case OUT_PARAM_FS:
        return out_suffix_fs;

    case OUT_PARAM_FS_STATE:
        return out_suffix_fs_state;

    case OUT_PARAM_TIME:
        return out_suffix_time;

    case OUT_PARAM_RETRIG:
        return out_suffix_retrig;

    default:
        return 0;
    }
}


static bool fill_param_name(
    uint16_t index,
    struct uavcan_protocol_param_GetSetResponse *response)
{
    const char *system_name = 0;

    switch (index) {

    case PARAM_NODE_ID:
        system_name = param_name_node_id;
        break;

    case PARAM_CAN_BITRATE:
        system_name = param_name_can_bitrate;
        break;

    case PARAM_PCA_COUNT:
        system_name = param_name_pca_count;
        break;

    case PARAM_FS_TIMEOUT:
        system_name = param_name_fs_timeout;
        break;

    default:
        break;
    }

    if (system_name != 0) {
        const uint8_t length =
            string_length(system_name);

        response->name.len = length;

        for (uint8_t i = 0U; i < length; i++) {
            response->name.data[i] =
                (uint8_t)system_name[i];
        }

        return true;
    }

    uint8_t output;
    uint8_t field;

    if (!decode_output_param_index(index, &output, &field)) {
        return false;
    }

    const char *suffix =
        output_field_suffix(field);

    if (suffix == 0) {
        return false;
    }

    const uint8_t number =
        (uint8_t)(output + 1U);

    uint8_t pos = 0U;

    response->name.data[pos++] = (uint8_t)'O';
    response->name.data[pos++] = (uint8_t)'U';
    response->name.data[pos++] = (uint8_t)'T';
    response->name.data[pos++] =
        (uint8_t)('0' + (number / 10U));
    response->name.data[pos++] =
        (uint8_t)('0' + (number % 10U));
    response->name.data[pos++] = (uint8_t)'_';

    const uint8_t suffix_length =
        string_length(suffix);

    for (uint8_t i = 0U; i < suffix_length; i++) {
        response->name.data[pos++] =
            (uint8_t)suffix[i];
    }

    response->name.len = pos;

    return true;
}


static void fill_param_response(
    uint16_t index,
    struct uavcan_protocol_param_GetSetResponse *response)
{
    fill_integer_value(
        &response->value,
        param_get_value(index));

    fill_integer_value(
        &response->default_value,
        param_get_default(index));

    fill_integer_numeric(
        &response->min_value,
        param_get_min(index));

    fill_integer_numeric(
        &response->max_value,
        param_get_max(index));

    (void)fill_param_name(index, response);
}


static void handle_getset(
    CanardInstance *canard,
    CanardRxTransfer *transfer)
{
    static struct
        uavcan_protocol_param_GetSetRequest request;

    static struct
        uavcan_protocol_param_GetSetResponse response;

    static uint8_t
        buffer[UAVCAN_PROTOCOL_PARAM_GETSET_RESPONSE_MAX_SIZE];

    zero_bytes(
        &request,
        sizeof(request));

    zero_bytes(
        &response,
        sizeof(response));

    if (uavcan_protocol_param_GetSetRequest_decode(
            transfer,
            &request)) {

        canardReleaseRxTransferPayload(
            canard,
            transfer);

        return;
    }

    const int16_t found =
        param_find(&request);

    if (found >= 0) {

        const uint16_t index =
            (uint16_t)found;

        if (request.value.union_tag ==
            UAVCAN_PROTOCOL_PARAM_VALUE_INTEGER_VALUE) {

            (void)param_set_value(
                index,
                request.value.integer_value);
        }

        fill_param_response(
            index,
            &response);
    }

    const uint32_t payload_length =
        uavcan_protocol_param_GetSetResponse_encode(
            &response,
            buffer);

    uint8_t transfer_id =
        transfer->transfer_id;

    const uint8_t destination_node_id =
        transfer->source_node_id;

    canardReleaseRxTransferPayload(
        canard,
        transfer);

    (void)canardRequestOrRespond(
        canard,
        destination_node_id,
        UAVCAN_PROTOCOL_PARAM_GETSET_RESPONSE_SIGNATURE,
        UAVCAN_PROTOCOL_PARAM_GETSET_RESPONSE_ID,
        &transfer_id,
        CANARD_TRANSFER_PRIORITY_LOW,
        CanardResponse,
        buffer,
        (uint16_t)payload_length);
}


static void handle_execute_opcode(
    CanardInstance *canard,
    CanardRxTransfer *transfer)
{
    static struct
        uavcan_protocol_param_ExecuteOpcodeRequest request;

    static struct
        uavcan_protocol_param_ExecuteOpcodeResponse response;

    static uint8_t
        buffer[
            UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_RESPONSE_MAX_SIZE];

    zero_bytes(
        &request,
        sizeof(request));

    zero_bytes(
        &response,
        sizeof(response));

    if (uavcan_protocol_param_ExecuteOpcodeRequest_decode(
            transfer,
            &request)) {

        canardReleaseRxTransferPayload(
            canard,
            transfer);

        return;
    }

    response.argument =
        request.argument;

    response.ok = false;

    if (request.opcode ==
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_REQUEST_OPCODE_SAVE) {

        response.ok =
            (config_save() == 0);

    } else if (
        request.opcode ==
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_REQUEST_OPCODE_ERASE) {

        config_load_defaults();

        response.ok =
            (config_save() == 0);
    }

    const uint32_t payload_length =
        uavcan_protocol_param_ExecuteOpcodeResponse_encode(
            &response,
            buffer);

    uint8_t transfer_id =
        transfer->transfer_id;

    const uint8_t destination_node_id =
        transfer->source_node_id;

    canardReleaseRxTransferPayload(
        canard,
        transfer);

    (void)canardRequestOrRespond(
        canard,
        destination_node_id,
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_RESPONSE_SIGNATURE,
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_RESPONSE_ID,
        &transfer_id,
        CANARD_TRANSFER_PRIORITY_LOW,
        CanardResponse,
        buffer,
        (uint16_t)payload_length);
}


bool param_server_should_accept(
    uint64_t *out_data_type_signature,
    uint16_t data_type_id,
    CanardTransferType transfer_type)
{
    if (transfer_type !=
        CanardTransferTypeRequest) {

        return false;
    }

    if (data_type_id ==
        UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_ID) {

        *out_data_type_signature =
            UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_SIGNATURE;

        return true;
    }

    if (data_type_id ==
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_REQUEST_ID) {

        *out_data_type_signature =
            UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_REQUEST_SIGNATURE;

        return true;
    }

    return false;
}


bool param_server_handle(
    CanardInstance *canard,
    CanardRxTransfer *transfer)
{
    if (transfer->transfer_type !=
        CanardTransferTypeRequest) {

        return false;
    }

    if (transfer->data_type_id ==
        UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_ID) {

        handle_getset(
            canard,
            transfer);

        return true;
    }

    if (transfer->data_type_id ==
        UAVCAN_PROTOCOL_PARAM_EXECUTEOPCODE_REQUEST_ID) {

        handle_execute_opcode(
            canard,
            transfer);

        return true;
    }

    return false;
}
