/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 1
 */

#ifndef FLOVA_LINK_DECODE_H__
#define FLOVA_LINK_DECODE_H__

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


int cbor_decode_auth(
		const uint8_t *payload, size_t payload_len,
		struct auth *result,
		size_t *payload_len_out);


int cbor_decode_auth_ok(
		const uint8_t *payload, size_t payload_len,
		struct auth_ok *result,
		size_t *payload_len_out);


int cbor_decode_auth_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out);


int cbor_decode_ping(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out);


int cbor_decode_pong(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out);


int cbor_decode_bootstrap_auth(
		const uint8_t *payload, size_t payload_len,
		struct bootstrap_auth *result,
		size_t *payload_len_out);


int cbor_decode_bootstrap_committed(
		const uint8_t *payload, size_t payload_len,
		struct bootstrap_committed *result,
		size_t *payload_len_out);


int cbor_decode_bootstrap_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out);


int cbor_decode_datastream_bind(
		const uint8_t *payload, size_t payload_len,
		struct datastream_bind *result,
		size_t *payload_len_out);


int cbor_decode_datastream_bound(
		const uint8_t *payload, size_t payload_len,
		struct datastream_bound *result,
		size_t *payload_len_out);


int cbor_decode_heartbeat(
		const uint8_t *payload, size_t payload_len,
		struct heartbeat *result,
		size_t *payload_len_out);


int cbor_decode_state(
		const uint8_t *payload, size_t payload_len,
		struct state *result,
		size_t *payload_len_out);


int cbor_decode_command_result(
		const uint8_t *payload, size_t payload_len,
		struct command_result_r *result,
		size_t *payload_len_out);


int cbor_decode_config_reported(
		const uint8_t *payload, size_t payload_len,
		struct config_reported *result,
		size_t *payload_len_out);


int cbor_decode_ota_reported(
		const uint8_t *payload, size_t payload_len,
		struct ota_reported *result,
		size_t *payload_len_out);


int cbor_decode_schedule_reported(
		const uint8_t *payload, size_t payload_len,
		struct schedule_reported *result,
		size_t *payload_len_out);


int cbor_decode_schedule_renew(
		const uint8_t *payload, size_t payload_len,
		struct schedule_renew *result,
		size_t *payload_len_out);


int cbor_decode_time_request(
		const uint8_t *payload, size_t payload_len,
		struct time_request *result,
		size_t *payload_len_out);


int cbor_decode_config_ack(
		const uint8_t *payload, size_t payload_len,
		struct config_ack *result,
		size_t *payload_len_out);


int cbor_decode_command(
		const uint8_t *payload, size_t payload_len,
		struct command *result,
		size_t *payload_len_out);


int cbor_decode_ingestion_ack(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out);


int cbor_decode_config_desired(
		const uint8_t *payload, size_t payload_len,
		struct config_desired *result,
		size_t *payload_len_out);


int cbor_decode_ota_desired(
		const uint8_t *payload, size_t payload_len,
		struct ota_desired *result,
		size_t *payload_len_out);


int cbor_decode_schedule_desired(
		const uint8_t *payload, size_t payload_len,
		struct schedule_desired *result,
		size_t *payload_len_out);


int cbor_decode_time_response(
		const uint8_t *payload, size_t payload_len,
		struct time_response *result,
		size_t *payload_len_out);


int cbor_decode_flow_control(
		const uint8_t *payload, size_t payload_len,
		struct flow_control *result,
		size_t *payload_len_out);


int cbor_decode_message_rejected(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out);


int cbor_decode_config_begin(
		const uint8_t *payload, size_t payload_len,
		struct config_begin *result,
		size_t *payload_len_out);


int cbor_decode_config_record(
		const uint8_t *payload, size_t payload_len,
		struct config_record *result,
		size_t *payload_len_out);


int cbor_decode_config_end(
		const uint8_t *payload, size_t payload_len,
		struct config_end *result,
		size_t *payload_len_out);


int cbor_decode_schedule_record_message(
		const uint8_t *payload, size_t payload_len,
		struct schedule_record_message *result,
		size_t *payload_len_out);


int cbor_decode_schedule_end(
		const uint8_t *payload, size_t payload_len,
		struct schedule_end *result,
		size_t *payload_len_out);


int cbor_decode_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out);


#ifdef __cplusplus
}
#endif

#endif /* FLOVA_LINK_DECODE_H__ */
