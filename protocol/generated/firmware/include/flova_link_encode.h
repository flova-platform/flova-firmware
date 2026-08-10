/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 1
 */

#ifndef FLOVA_LINK_ENCODE_H__
#define FLOVA_LINK_ENCODE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "flova_link_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if DEFAULT_MAX_QTY != 1
#error "The type file was generated with a different default_max_qty than this file"
#endif


int cbor_encode_auth(
		uint8_t *payload, size_t payload_len,
		const struct auth *input,
		size_t *payload_len_out);


int cbor_encode_auth_ok(
		uint8_t *payload, size_t payload_len,
		const struct auth_ok *input,
		size_t *payload_len_out);


int cbor_encode_auth_error(
		uint8_t *payload, size_t payload_len,
		const struct zcbor_string *input,
		size_t *payload_len_out);


int cbor_encode_ping(
		uint8_t *payload, size_t payload_len,
		const void *input,
		size_t *payload_len_out);


int cbor_encode_pong(
		uint8_t *payload, size_t payload_len,
		const void *input,
		size_t *payload_len_out);


int cbor_encode_bootstrap_auth(
		uint8_t *payload, size_t payload_len,
		const struct bootstrap_auth *input,
		size_t *payload_len_out);


int cbor_encode_bootstrap_committed(
		uint8_t *payload, size_t payload_len,
		const struct bootstrap_committed *input,
		size_t *payload_len_out);


int cbor_encode_bootstrap_error(
		uint8_t *payload, size_t payload_len,
		const struct zcbor_string *input,
		size_t *payload_len_out);


int cbor_encode_heartbeat(
		uint8_t *payload, size_t payload_len,
		const struct heartbeat *input,
		size_t *payload_len_out);


int cbor_encode_state(
		uint8_t *payload, size_t payload_len,
		const struct state *input,
		size_t *payload_len_out);


int cbor_encode_command_result(
		uint8_t *payload, size_t payload_len,
		const struct command_result_r *input,
		size_t *payload_len_out);


int cbor_encode_config_reported(
		uint8_t *payload, size_t payload_len,
		const struct config_reported *input,
		size_t *payload_len_out);


int cbor_encode_ota_reported(
		uint8_t *payload, size_t payload_len,
		const struct ota_reported *input,
		size_t *payload_len_out);


int cbor_encode_schedule_reported(
		uint8_t *payload, size_t payload_len,
		const struct schedule_reported *input,
		size_t *payload_len_out);


int cbor_encode_schedule_renew(
		uint8_t *payload, size_t payload_len,
		const struct schedule_renew *input,
		size_t *payload_len_out);


int cbor_encode_time_request(
		uint8_t *payload, size_t payload_len,
		const struct time_request *input,
		size_t *payload_len_out);


int cbor_encode_config_ack(
		uint8_t *payload, size_t payload_len,
		const struct config_ack *input,
		size_t *payload_len_out);


int cbor_encode_command(
		uint8_t *payload, size_t payload_len,
		const struct command *input,
		size_t *payload_len_out);


int cbor_encode_ingestion_ack(
		uint8_t *payload, size_t payload_len,
		const void *input,
		size_t *payload_len_out);


int cbor_encode_config_desired(
		uint8_t *payload, size_t payload_len,
		const struct config_desired *input,
		size_t *payload_len_out);


int cbor_encode_ota_desired(
		uint8_t *payload, size_t payload_len,
		const struct ota_desired *input,
		size_t *payload_len_out);


int cbor_encode_schedule_desired(
		uint8_t *payload, size_t payload_len,
		const struct schedule_desired *input,
		size_t *payload_len_out);


int cbor_encode_time_response(
		uint8_t *payload, size_t payload_len,
		const struct time_response *input,
		size_t *payload_len_out);


int cbor_encode_flow_control(
		uint8_t *payload, size_t payload_len,
		const struct flow_control *input,
		size_t *payload_len_out);


int cbor_encode_message_rejected(
		uint8_t *payload, size_t payload_len,
		const struct zcbor_string *input,
		size_t *payload_len_out);


int cbor_encode_config_begin(
		uint8_t *payload, size_t payload_len,
		const struct config_begin *input,
		size_t *payload_len_out);


int cbor_encode_config_record(
		uint8_t *payload, size_t payload_len,
		const struct config_record *input,
		size_t *payload_len_out);


int cbor_encode_config_end(
		uint8_t *payload, size_t payload_len,
		const struct config_end *input,
		size_t *payload_len_out);


int cbor_encode_schedule_record_message(
		uint8_t *payload, size_t payload_len,
		const struct schedule_record_message *input,
		size_t *payload_len_out);


int cbor_encode_schedule_end(
		uint8_t *payload, size_t payload_len,
		const struct schedule_end *input,
		size_t *payload_len_out);


int cbor_encode_error(
		uint8_t *payload, size_t payload_len,
		const struct zcbor_string *input,
		size_t *payload_len_out);


#ifdef __cplusplus
}
#endif

#endif /* FLOVA_LINK_ENCODE_H__ */
