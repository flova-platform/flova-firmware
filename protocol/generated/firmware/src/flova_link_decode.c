/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 1
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_decode.h"
#include "flova_link_decode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 1
#error "The type file was generated with a different default_max_qty than this file"
#endif

#define log_result(state, result, func) do { \
	if (!result) { \
		zcbor_trace_file(state); \
		zcbor_log("%s error: %s\r\n", func, zcbor_error_str(zcbor_peek_error(state))); \
	} else { \
		zcbor_log("%s success\r\n", func); \
	} \
} while(0)

static bool decode_repeated_ota_desired_ota_target(zcbor_state_t *state, struct ota_desired_ota_target *result);
static bool decode_repeated_ota_desired_ota_release_id(zcbor_state_t *state, struct ota_desired_ota_release_id *result);
static bool decode_repeated_datastream_binding_keys_tstr1_48(zcbor_state_t *state, struct zcbor_string *result);
static bool decode_datastream_binding_keys(zcbor_state_t *state, struct datastream_binding_keys *result);
static bool decode_repeated_ota_profile_ota_boot_state(zcbor_state_t *state, struct ota_profile_ota_boot_state *result);
static bool decode_repeated_ota_profile_ota_rollback_reason(zcbor_state_t *state, struct ota_profile_ota_rollback_reason *result);
static bool decode_ota_profile(zcbor_state_t *state, struct ota_profile *result);
static bool decode_typed_value_fields(zcbor_state_t *state, struct typed_value_fields_r *result);
static bool decode_correlation_id(zcbor_state_t *state, struct correlation_id_r *result);
static bool decode_capabilities(zcbor_state_t *state, struct capabilities *result);
static bool decode_repeated_datastream_bound_ids_compact_id_m(zcbor_state_t *state, uint64_t *result);
static bool decode_datastream_bound_ids(zcbor_state_t *state, struct datastream_bound_ids *result);
static bool decode_command_result_ok(zcbor_state_t *state, struct command_result_ok *result);
static bool decode_command_result_error(zcbor_state_t *state, struct command_result_error *result);
static bool decode_repeated_config_begin_config_command_id(zcbor_state_t *state, struct config_begin_config_command_id *result);
static bool decode_state_reading(zcbor_state_t *state, struct state_reading *result);
static bool decode_state_readings(zcbor_state_t *state, struct state_readings *result);
static bool decode_schedule_action(zcbor_state_t *state, struct schedule_action *result);
static bool decode_schedule_record(zcbor_state_t *state, struct schedule_record *result);
static bool decode_typed_value(zcbor_state_t *state, struct typed_value_fields_r *result);
static bool decode_repeated_datastream_record_datastream_minimum(zcbor_state_t *state, struct datastream_record_datastream_minimum *result);
static bool decode_repeated_datastream_record_datastream_maximum(zcbor_state_t *state, struct datastream_record_datastream_maximum *result);
static bool decode_repeated_datastream_record_datastream_default(zcbor_state_t *state, struct datastream_record_datastream_default *result);
static bool decode_repeated_hardware_mapping_mapping_active_high(zcbor_state_t *state, struct hardware_mapping_mapping_active_high *result);
static bool decode_repeated_hardware_mapping_mapping_pull(zcbor_state_t *state, struct hardware_mapping_mapping_pull *result);
static bool decode_repeated_hardware_mapping_mapping_debounce_ms(zcbor_state_t *state, struct hardware_mapping_mapping_debounce_ms *result);
static bool decode_repeated_hardware_mapping_mapping_sample_ms(zcbor_state_t *state, struct hardware_mapping_mapping_sample_ms *result);
static bool decode_repeated_hardware_mapping_mapping_min_output_ms(zcbor_state_t *state, struct hardware_mapping_mapping_min_output_ms *result);
static bool decode_hardware_mapping(zcbor_state_t *state, struct hardware_mapping *result);
static bool decode_repeated_datastream_record_datastream_mapping(zcbor_state_t *state, struct datastream_record_datastream_mapping *result);
static bool decode_datastream_record(zcbor_state_t *state, struct datastream_record *result);
static bool decode_repeated_system_record_system_heartbeat_ms(zcbor_state_t *state, struct system_record_system_heartbeat_ms *result);
static bool decode_repeated_system_record_system_status_led_pin(zcbor_state_t *state, struct system_record_system_status_led_pin *result);
static bool decode_repeated_system_record_system_status_led_active_low(zcbor_state_t *state, struct system_record_system_status_led_active_low *result);
static bool decode_repeated_system_record_system_batch_flush_ms(zcbor_state_t *state, struct system_record_system_batch_flush_ms *result);
static bool decode_system_record(zcbor_state_t *state, struct system_record *result);
static bool decode_repeated_schedule_occurrence_record_occurrence_values_uint64_m(zcbor_state_t *state, uint64_t *result);
static bool decode_schedule_occurrence_record(zcbor_state_t *state, struct schedule_occurrence_record *result);
static bool decode_repeated_safety_record_safety_minimum(zcbor_state_t *state, struct safety_record_safety_minimum *result);
static bool decode_repeated_safety_record_safety_maximum(zcbor_state_t *state, struct safety_record_safety_maximum *result);
static bool decode_repeated_safety_record_safety_timeout_ms(zcbor_state_t *state, struct safety_record_safety_timeout_ms *result);
static bool decode_safety_record(zcbor_state_t *state, struct safety_record *result);
static bool decode_config_record_body(zcbor_state_t *state, struct config_record_body_r *result);
static bool decode_error(zcbor_state_t *state, struct zcbor_string *result);
static bool decode_schedule_end(zcbor_state_t *state, struct schedule_end *result);
static bool decode_schedule_record_message(zcbor_state_t *state, struct schedule_record_message *result);
static bool decode_config_end(zcbor_state_t *state, struct config_end *result);
static bool decode_config_record(zcbor_state_t *state, struct config_record *result);
static bool decode_config_begin(zcbor_state_t *state, struct config_begin *result);
static bool decode_message_rejected(zcbor_state_t *state, struct zcbor_string *result);
static bool decode_flow_control(zcbor_state_t *state, struct flow_control *result);
static bool decode_time_response(zcbor_state_t *state, struct time_response *result);
static bool decode_schedule_desired(zcbor_state_t *state, struct schedule_desired *result);
static bool decode_ota_desired(zcbor_state_t *state, struct ota_desired *result);
static bool decode_config_desired(zcbor_state_t *state, struct config_desired *result);
static bool decode_ingestion_ack(zcbor_state_t *state, void *result);
static bool decode_command(zcbor_state_t *state, struct command *result);
static bool decode_config_ack(zcbor_state_t *state, struct config_ack *result);
static bool decode_time_request(zcbor_state_t *state, struct time_request *result);
static bool decode_schedule_renew(zcbor_state_t *state, struct schedule_renew *result);
static bool decode_schedule_reported(zcbor_state_t *state, struct schedule_reported *result);
static bool decode_ota_reported(zcbor_state_t *state, struct ota_reported *result);
static bool decode_config_reported(zcbor_state_t *state, struct config_reported *result);
static bool decode_command_result(zcbor_state_t *state, struct command_result_r *result);
static bool decode_state(zcbor_state_t *state, struct state *result);
static bool decode_heartbeat(zcbor_state_t *state, struct heartbeat *result);
static bool decode_datastream_bound(zcbor_state_t *state, struct datastream_bound *result);
static bool decode_datastream_bind(zcbor_state_t *state, struct datastream_bind *result);
static bool decode_bootstrap_error(zcbor_state_t *state, struct zcbor_string *result);
static bool decode_bootstrap_committed(zcbor_state_t *state, struct bootstrap_committed *result);
static bool decode_bootstrap_auth(zcbor_state_t *state, struct bootstrap_auth *result);
static bool decode_pong(zcbor_state_t *state, void *result);
static bool decode_ping(zcbor_state_t *state, void *result);
static bool decode_auth_error(zcbor_state_t *state, struct zcbor_string *result);
static bool decode_auth_ok(zcbor_state_t *state, struct auth_ok *result);
static bool decode_auth(zcbor_state_t *state, struct auth *result);


static bool decode_repeated_ota_desired_ota_target(
		zcbor_state_t *state, struct ota_desired_ota_target *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (5))))
	&& (zcbor_tstr_decode(state, (&(*result).ota_desired_ota_target)))
	&& ((((*result).ota_desired_ota_target.len >= 1)
	&& ((*result).ota_desired_ota_target.len <= 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_ota_desired_ota_release_id(
		zcbor_state_t *state, struct ota_desired_ota_release_id *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (6))))
	&& (zcbor_bstr_decode(state, (&(*result).ota_desired_ota_release_id)))
	&& ((((((*result).ota_desired_ota_release_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_binding_keys_tstr1_48(
		zcbor_state_t *state, struct zcbor_string *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_tstr_decode(state, (&(*result))))
	&& ((((*result).len >= 1)
	&& ((*result).len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_datastream_binding_keys(
		zcbor_state_t *state, struct datastream_binding_keys *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 64, &(*result).datastream_binding_keys_tstr1_48_count, (zcbor_decoder_t *)decode_repeated_datastream_binding_keys_tstr1_48, state, (*&(*result).datastream_binding_keys_tstr1_48), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_datastream_binding_keys_tstr1_48(state, (*&(*result).datastream_binding_keys_tstr1_48));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_ota_profile_ota_boot_state(
		zcbor_state_t *state, struct ota_profile_ota_boot_state *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).ota_profile_ota_boot_state)))
	&& ((((*result).ota_profile_ota_boot_state <= 2)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_ota_profile_ota_rollback_reason(
		zcbor_state_t *state, struct ota_profile_ota_rollback_reason *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (5))))
	&& (zcbor_tstr_decode(state, (&(*result).ota_profile_ota_rollback_reason)))
	&& ((((*result).ota_profile_ota_rollback_reason.len >= 1)
	&& ((*result).ota_profile_ota_rollback_reason.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ota_profile(
		zcbor_state_t *state, struct ota_profile *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_decode(state, (&(*result).ota_profile_ota_max_image_bytes)))
	&& ((((((*result).ota_profile_ota_max_image_bytes <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).ota_profile_ota_strategy)))
	&& ((((*result).ota_profile_ota_strategy <= 2)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).ota_profile_ota_boot_layout_version)))
	&& ((((*result).ota_profile_ota_boot_layout_version.len >= 1)
	&& ((*result).ota_profile_ota_boot_layout_version.len <= 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).ota_profile_ota_rollback_capable))))
	&& zcbor_present_decode(&((*result).ota_profile_ota_boot_state_present), (zcbor_decoder_t *)decode_repeated_ota_profile_ota_boot_state, state, (&(*result).ota_profile_ota_boot_state))
	&& zcbor_present_decode(&((*result).ota_profile_ota_rollback_reason_present), (zcbor_decoder_t *)decode_repeated_ota_profile_ota_rollback_reason, state, (&(*result).ota_profile_ota_rollback_reason))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_ota_profile_ota_boot_state(state, (&(*result).ota_profile_ota_boot_state));
		decode_repeated_ota_profile_ota_rollback_reason(state, (&(*result).ota_profile_ota_rollback_reason));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_typed_value_fields(
		zcbor_state_t *state, struct typed_value_fields_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).typed_value_fields_choice, sizeof((*result).typed_value_fields_choice)))) && ((((((*result).typed_value_fields_choice == typed_value_fields_value_bool_type_l_c) && ((((1)
	&& ((zcbor_bool_decode(state, (&(*result).typed_value_fields_value_bool_type_l_value_bool))))))))
	|| (((*result).typed_value_fields_choice == typed_value_fields_value_int_type_l_c) && ((((1)
	&& ((zcbor_int64_decode(state, (&(*result).typed_value_fields_value_int_type_l_value_int))))))))
	|| (((*result).typed_value_fields_choice == typed_value_fields_value_f32_type_l_c) && ((((1)
	&& ((zcbor_float32_decode(state, (&(*result).typed_value_fields_value_f32_type_l_value_f32))))))))
	|| (((*result).typed_value_fields_choice == typed_value_fields_value_f64_type_l_c) && ((((1)
	&& ((zcbor_float64_decode(state, (&(*result).typed_value_fields_value_f64_type_l_value_f64))))))))
	|| (((*result).typed_value_fields_choice == typed_value_fields_value_text_type_l_c) && ((((1)
	&& ((zcbor_tstr_decode(state, (&(*result).typed_value_fields_value_text_type_l_value_text)))
	&& ((((((*result).typed_value_fields_value_text_type_l_value_text.len >= 0)
	&& ((*result).typed_value_fields_value_text_type_l_value_text.len <= 96)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))))))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_correlation_id(
		zcbor_state_t *state, struct correlation_id_r *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_union_start_code(state) && (int_res = ((((zcbor_bstr_decode(state, (&(*result).correlation_id_empty_id_m)))
	&& ((((((*result).correlation_id_empty_id_m.len == 0)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).correlation_id_choice = correlation_id_empty_id_m_c), true))
	|| (zcbor_union_elem_code(state) && (((zcbor_bstr_decode(state, (&(*result).correlation_id_uuid_m)))
	&& ((((((*result).correlation_id_uuid_m.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).correlation_id_choice = correlation_id_uuid_m_c), true)))), zcbor_union_end_code(state), int_res))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_capabilities(
		zcbor_state_t *state, struct capabilities *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_datastream_slots)))
	&& ((((((((*result).capabilities_datastream_slots <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_input_slots)))
	&& ((((((((*result).capabilities_input_slots <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_output_slots)))
	&& ((((((((*result).capabilities_output_slots <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_command_slots)))
	&& ((((((((*result).capabilities_command_slots <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_schedule_slots)))
	&& ((((((((*result).capabilities_schedule_slots <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (5))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_manifest_bytes)))
	&& ((((((((*result).capabilities_manifest_bytes <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (6))))
	&& (zcbor_uint64_decode(state, (&(*result).capabilities_history_bytes)))
	&& ((((((((*result).capabilities_history_bytes <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (7))))
	&& (zcbor_uint64_expect(state, (512))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_bound_ids_compact_id_m(
		zcbor_state_t *state, uint64_t *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_uint64_decode(state, (&(*result))))
	&& ((((((((*result) <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_datastream_bound_ids(
		zcbor_state_t *state, struct datastream_bound_ids *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 64, &(*result).datastream_bound_ids_compact_id_m_count, (zcbor_decoder_t *)decode_repeated_datastream_bound_ids_compact_id_m, state, (*&(*result).datastream_bound_ids_compact_id_m), sizeof(uint64_t))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_datastream_bound_ids_compact_id_m(state, (*&(*result).datastream_bound_ids_compact_id_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_command_result_ok(
		zcbor_state_t *state, struct command_result_ok *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).command_result_ok_result_ok_generation)))
	&& ((((((((*result).command_result_ok_result_ok_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).command_result_ok_result_ok_command_id)))
	&& ((((((*result).command_result_ok_result_ok_command_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_ok_result_ok_compact_id)))
	&& ((((((((*result).command_result_ok_result_ok_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_ok_result_ok_status)))
	&& ((((*result).command_result_ok_result_ok_status <= 2)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_typed_value_fields(state, (&(*result).command_result_ok_typed_value_fields_m))))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_ok_result_ok_version)))
	&& ((((((*result).command_result_ok_result_ok_version <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_correlation_id(state, (&(*result).command_result_ok_result_ok_correlation_id))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_command_result_error(
		zcbor_state_t *state, struct command_result_error *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).command_result_error_result_error_generation)))
	&& ((((((((*result).command_result_error_result_error_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).command_result_error_result_error_command_id)))
	&& ((((((*result).command_result_error_result_error_command_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_error_result_error_compact_id)))
	&& ((((((((*result).command_result_error_result_error_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_error_result_error_status)))
	&& ((((*result).command_result_error_result_error_status >= 3)
	&& ((*result).command_result_error_result_error_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).command_result_error_result_error_code)))
	&& ((((((*result).command_result_error_result_error_code.len >= 1)
	&& ((*result).command_result_error_result_error_code.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).command_result_error_result_error_message)))
	&& ((((((*result).command_result_error_result_error_message.len >= 0)
	&& ((*result).command_result_error_result_error_message.len <= 192)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_result_error_result_error_version)))
	&& ((((((*result).command_result_error_result_error_version <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_correlation_id(state, (&(*result).command_result_error_result_error_correlation_id))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_config_begin_config_command_id(
		zcbor_state_t *state, struct config_begin_config_command_id *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (8))))
	&& (zcbor_bstr_decode(state, (&(*result).config_begin_config_command_id)))
	&& ((((((*result).config_begin_config_command_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_state_reading(
		zcbor_state_t *state, struct state_reading *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).state_reading_reading_compact_id)))
	&& ((((((((*result).state_reading_reading_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_typed_value_fields(state, (&(*result).state_reading_typed_value_fields_m))))
	&& ((zcbor_uint64_decode(state, (&(*result).state_reading_reading_revision)))
	&& ((((((*result).state_reading_reading_revision <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_state_readings(
		zcbor_state_t *state, struct state_readings *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 4, &(*result).state_readings_state_reading_m_count, (zcbor_decoder_t *)decode_state_reading, state, (*&(*result).state_readings_state_reading_m), sizeof(struct state_reading))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_state_reading(state, (*&(*result).state_readings_state_reading_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_action(
		zcbor_state_t *state, struct schedule_action *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_action_action_offset_ms)))
	&& ((((((*result).schedule_action_action_offset_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_action_action_compact_id)))
	&& ((((((((*result).schedule_action_action_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_typed_value_fields(state, (&(*result).schedule_action_typed_value_fields_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_record(
		zcbor_state_t *state, struct schedule_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_expect(state, (2))))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_record_schedule_id)))
	&& ((((((*result).schedule_record_schedule_id <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_bool_decode(state, (&(*result).schedule_record_schedule_enabled))))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_record_schedule_valid_from))))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_record_schedule_valid_until))))
	&& (((zcbor_uint64_expect(state, (5))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 8, &(*result).schedule_record_schedule_actions_schedule_action_m_count, (zcbor_decoder_t *)decode_schedule_action, state, (*&(*result).schedule_record_schedule_actions_schedule_action_m), sizeof(struct schedule_action))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_schedule_action(state, (*&(*result).schedule_record_schedule_actions_schedule_action_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_typed_value(
		zcbor_state_t *state, struct typed_value_fields_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((decode_typed_value_fields(state, (&(*result)))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_record_datastream_minimum(
		zcbor_state_t *state, struct datastream_record_datastream_minimum *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (5))))
	&& (decode_typed_value(state, (&(*result).datastream_record_datastream_minimum)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_record_datastream_maximum(
		zcbor_state_t *state, struct datastream_record_datastream_maximum *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (6))))
	&& (decode_typed_value(state, (&(*result).datastream_record_datastream_maximum)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_record_datastream_default(
		zcbor_state_t *state, struct datastream_record_datastream_default *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (7))))
	&& (decode_typed_value(state, (&(*result).datastream_record_datastream_default)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hardware_mapping_mapping_active_high(
		zcbor_state_t *state, struct hardware_mapping_mapping_active_high *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (2))))
	&& (zcbor_bool_decode(state, (&(*result).hardware_mapping_mapping_active_high)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hardware_mapping_mapping_pull(
		zcbor_state_t *state, struct hardware_mapping_mapping_pull *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (3))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_pull)))
	&& ((((*result).hardware_mapping_mapping_pull <= 3)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hardware_mapping_mapping_debounce_ms(
		zcbor_state_t *state, struct hardware_mapping_mapping_debounce_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_debounce_ms)))
	&& ((((((*result).hardware_mapping_mapping_debounce_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hardware_mapping_mapping_sample_ms(
		zcbor_state_t *state, struct hardware_mapping_mapping_sample_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (5))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_sample_ms)))
	&& ((((((*result).hardware_mapping_mapping_sample_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hardware_mapping_mapping_min_output_ms(
		zcbor_state_t *state, struct hardware_mapping_mapping_min_output_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (6))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_min_output_ms)))
	&& ((((((*result).hardware_mapping_mapping_min_output_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_hardware_mapping(
		zcbor_state_t *state, struct hardware_mapping *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_kind)))
	&& ((((*result).hardware_mapping_mapping_kind <= 3)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).hardware_mapping_mapping_pin)))
	&& ((((((*result).hardware_mapping_mapping_pin <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).hardware_mapping_mapping_active_high_present), (zcbor_decoder_t *)decode_repeated_hardware_mapping_mapping_active_high, state, (&(*result).hardware_mapping_mapping_active_high))
	&& zcbor_present_decode(&((*result).hardware_mapping_mapping_pull_present), (zcbor_decoder_t *)decode_repeated_hardware_mapping_mapping_pull, state, (&(*result).hardware_mapping_mapping_pull))
	&& zcbor_present_decode(&((*result).hardware_mapping_mapping_debounce_ms_present), (zcbor_decoder_t *)decode_repeated_hardware_mapping_mapping_debounce_ms, state, (&(*result).hardware_mapping_mapping_debounce_ms))
	&& zcbor_present_decode(&((*result).hardware_mapping_mapping_sample_ms_present), (zcbor_decoder_t *)decode_repeated_hardware_mapping_mapping_sample_ms, state, (&(*result).hardware_mapping_mapping_sample_ms))
	&& zcbor_present_decode(&((*result).hardware_mapping_mapping_min_output_ms_present), (zcbor_decoder_t *)decode_repeated_hardware_mapping_mapping_min_output_ms, state, (&(*result).hardware_mapping_mapping_min_output_ms))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_hardware_mapping_mapping_active_high(state, (&(*result).hardware_mapping_mapping_active_high));
		decode_repeated_hardware_mapping_mapping_pull(state, (&(*result).hardware_mapping_mapping_pull));
		decode_repeated_hardware_mapping_mapping_debounce_ms(state, (&(*result).hardware_mapping_mapping_debounce_ms));
		decode_repeated_hardware_mapping_mapping_sample_ms(state, (&(*result).hardware_mapping_mapping_sample_ms));
		decode_repeated_hardware_mapping_mapping_min_output_ms(state, (&(*result).hardware_mapping_mapping_min_output_ms));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_datastream_record_datastream_mapping(
		zcbor_state_t *state, struct datastream_record_datastream_mapping *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (8))))
	&& (decode_hardware_mapping(state, (&(*result).datastream_record_datastream_mapping)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_datastream_record(
		zcbor_state_t *state, struct datastream_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_expect(state, (0))))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).datastream_record_datastream_compact_id)))
	&& ((((((((*result).datastream_record_datastream_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_bstr_decode(state, (&(*result).datastream_record_datastream_uuid)))
	&& ((((((*result).datastream_record_datastream_uuid.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_tstr_decode(state, (&(*result).datastream_record_datastream_key)))
	&& ((((*result).datastream_record_datastream_key.len >= 1)
	&& ((*result).datastream_record_datastream_key.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).datastream_record_datastream_value_type)))
	&& ((((*result).datastream_record_datastream_value_type <= 4)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).datastream_record_datastream_minimum_present), (zcbor_decoder_t *)decode_repeated_datastream_record_datastream_minimum, state, (&(*result).datastream_record_datastream_minimum))
	&& zcbor_present_decode(&((*result).datastream_record_datastream_maximum_present), (zcbor_decoder_t *)decode_repeated_datastream_record_datastream_maximum, state, (&(*result).datastream_record_datastream_maximum))
	&& zcbor_present_decode(&((*result).datastream_record_datastream_default_present), (zcbor_decoder_t *)decode_repeated_datastream_record_datastream_default, state, (&(*result).datastream_record_datastream_default))
	&& zcbor_present_decode(&((*result).datastream_record_datastream_mapping_present), (zcbor_decoder_t *)decode_repeated_datastream_record_datastream_mapping, state, (&(*result).datastream_record_datastream_mapping))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_datastream_record_datastream_minimum(state, (&(*result).datastream_record_datastream_minimum));
		decode_repeated_datastream_record_datastream_maximum(state, (&(*result).datastream_record_datastream_maximum));
		decode_repeated_datastream_record_datastream_default(state, (&(*result).datastream_record_datastream_default));
		decode_repeated_datastream_record_datastream_mapping(state, (&(*result).datastream_record_datastream_mapping));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_system_record_system_heartbeat_ms(
		zcbor_state_t *state, struct system_record_system_heartbeat_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).system_record_system_heartbeat_ms)))
	&& ((((((*result).system_record_system_heartbeat_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_system_record_system_status_led_pin(
		zcbor_state_t *state, struct system_record_system_status_led_pin *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (2))))
	&& (zcbor_uint64_decode(state, (&(*result).system_record_system_status_led_pin)))
	&& ((((((*result).system_record_system_status_led_pin <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_system_record_system_status_led_active_low(
		zcbor_state_t *state, struct system_record_system_status_led_active_low *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).system_record_system_status_led_active_low)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_system_record_system_batch_flush_ms(
		zcbor_state_t *state, struct system_record_system_batch_flush_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).system_record_system_batch_flush_ms)))
	&& ((((((*result).system_record_system_batch_flush_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_system_record(
		zcbor_state_t *state, struct system_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_expect(state, (1))))
	&& zcbor_present_decode(&((*result).system_record_system_heartbeat_ms_present), (zcbor_decoder_t *)decode_repeated_system_record_system_heartbeat_ms, state, (&(*result).system_record_system_heartbeat_ms))
	&& zcbor_present_decode(&((*result).system_record_system_status_led_pin_present), (zcbor_decoder_t *)decode_repeated_system_record_system_status_led_pin, state, (&(*result).system_record_system_status_led_pin))
	&& zcbor_present_decode(&((*result).system_record_system_status_led_active_low_present), (zcbor_decoder_t *)decode_repeated_system_record_system_status_led_active_low, state, (&(*result).system_record_system_status_led_active_low))
	&& zcbor_present_decode(&((*result).system_record_system_batch_flush_ms_present), (zcbor_decoder_t *)decode_repeated_system_record_system_batch_flush_ms, state, (&(*result).system_record_system_batch_flush_ms))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_system_record_system_heartbeat_ms(state, (&(*result).system_record_system_heartbeat_ms));
		decode_repeated_system_record_system_status_led_pin(state, (&(*result).system_record_system_status_led_pin));
		decode_repeated_system_record_system_status_led_active_low(state, (&(*result).system_record_system_status_led_active_low));
		decode_repeated_system_record_system_batch_flush_ms(state, (&(*result).system_record_system_batch_flush_ms));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_schedule_occurrence_record_occurrence_values_uint64_m(
		zcbor_state_t *state, uint64_t *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_uint64_decode(state, (&(*result))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_occurrence_record(
		zcbor_state_t *state, struct schedule_occurrence_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_expect(state, (4))))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_occurrence_record_occurrence_schedule_id)))
	&& ((((((*result).schedule_occurrence_record_occurrence_schedule_id <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_occurrence_record_occurrence_chunk_index)))
	&& ((((((*result).schedule_occurrence_record_occurrence_chunk_index <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_uint64_decode(state, (&(*result).schedule_occurrence_record_occurrence_chunk_count)))
	&& ((((((*result).schedule_occurrence_record_occurrence_chunk_count <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 16, &(*result).schedule_occurrence_record_occurrence_values_uint64_m_count, (zcbor_decoder_t *)decode_repeated_schedule_occurrence_record_occurrence_values_uint64_m, state, (*&(*result).schedule_occurrence_record_occurrence_values_uint64_m), sizeof(uint64_t))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_schedule_occurrence_record_occurrence_values_uint64_m(state, (*&(*result).schedule_occurrence_record_occurrence_values_uint64_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_safety_record_safety_minimum(
		zcbor_state_t *state, struct safety_record_safety_minimum *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (3))))
	&& (decode_typed_value(state, (&(*result).safety_record_safety_minimum)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_safety_record_safety_maximum(
		zcbor_state_t *state, struct safety_record_safety_maximum *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (4))))
	&& (decode_typed_value(state, (&(*result).safety_record_safety_maximum)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_safety_record_safety_timeout_ms(
		zcbor_state_t *state, struct safety_record_safety_timeout_ms *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint64_expect(state, (5))))
	&& (zcbor_uint64_decode(state, (&(*result).safety_record_safety_timeout_ms)))
	&& ((((((*result).safety_record_safety_timeout_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_safety_record(
		zcbor_state_t *state, struct safety_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_expect(state, (3))))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).safety_record_safety_compact_id)))
	&& ((((((((*result).safety_record_safety_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_uint64_decode(state, (&(*result).safety_record_safety_policy)))
	&& ((((*result).safety_record_safety_policy <= 4)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).safety_record_safety_minimum_present), (zcbor_decoder_t *)decode_repeated_safety_record_safety_minimum, state, (&(*result).safety_record_safety_minimum))
	&& zcbor_present_decode(&((*result).safety_record_safety_maximum_present), (zcbor_decoder_t *)decode_repeated_safety_record_safety_maximum, state, (&(*result).safety_record_safety_maximum))
	&& zcbor_present_decode(&((*result).safety_record_safety_timeout_ms_present), (zcbor_decoder_t *)decode_repeated_safety_record_safety_timeout_ms, state, (&(*result).safety_record_safety_timeout_ms))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_safety_record_safety_minimum(state, (&(*result).safety_record_safety_minimum));
		decode_repeated_safety_record_safety_maximum(state, (&(*result).safety_record_safety_maximum));
		decode_repeated_safety_record_safety_timeout_ms(state, (&(*result).safety_record_safety_timeout_ms));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_record_body(
		zcbor_state_t *state, struct config_record_body_r *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_union_start_code(state) && (int_res = ((((decode_datastream_record(state, (&(*result).config_record_body_datastream_record_m)))) && (((*result).config_record_body_choice = config_record_body_datastream_record_m_c), true))
	|| (zcbor_union_elem_code(state) && (((decode_system_record(state, (&(*result).config_record_body_system_record_m)))) && (((*result).config_record_body_choice = config_record_body_system_record_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_schedule_record(state, (&(*result).config_record_body_schedule_record_m)))) && (((*result).config_record_body_choice = config_record_body_schedule_record_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_schedule_occurrence_record(state, (&(*result).config_record_body_schedule_occurrence_record_m)))) && (((*result).config_record_body_choice = config_record_body_schedule_occurrence_record_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_safety_record(state, (&(*result).config_record_body_safety_record_m)))) && (((*result).config_record_body_choice = config_record_body_safety_record_m_c), true)))), zcbor_union_end_code(state), int_res))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_error(
		zcbor_state_t *state, struct zcbor_string *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_tstr_decode(state, (&(*result))))
	&& ((((((*result).len >= 1)
	&& ((*result).len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_end(
		zcbor_state_t *state, struct schedule_end *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_end_generation)))
	&& ((((((((*result).schedule_end_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_end_record_count)))
	&& ((((((*result).schedule_end_record_count <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).schedule_end_checksum)))
	&& ((((((*result).schedule_end_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_record_message(
		zcbor_state_t *state, struct schedule_record_message *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_record_message_schedule_message_generation)))
	&& ((((((((*result).schedule_record_message_schedule_message_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_record_message_schedule_message_sequence)))
	&& ((((((*result).schedule_record_message_schedule_message_sequence <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_schedule_record(state, (&(*result).schedule_record_message_schedule_message_record))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_end(
		zcbor_state_t *state, struct config_end *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).config_end_end_generation)))
	&& ((((((((*result).config_end_end_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).config_end_end_record_count)))
	&& ((((((*result).config_end_end_record_count <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).config_end_end_checksum)))
	&& ((((((*result).config_end_end_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_record(
		zcbor_state_t *state, struct config_record *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_decode(state, (&(*result).config_record_record_generation)))
	&& ((((((((*result).config_record_record_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_decode(state, (&(*result).config_record_record_sequence)))
	&& ((((((((*result).config_record_record_sequence <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (decode_config_record_body(state, (&(*result).config_record_record_body))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_begin(
		zcbor_state_t *state, struct config_begin *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_uint64_decode(state, (&(*result).config_begin_config_generation)))
	&& ((((((((*result).config_begin_config_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_bstr_decode(state, (&(*result).config_begin_config_checksum)))
	&& ((((((*result).config_begin_config_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_uint64_decode(state, (&(*result).config_begin_config_record_count)))
	&& ((((((*result).config_begin_config_record_count <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (decode_capabilities(state, (&(*result).config_begin_negotiated_limits))))
	&& (((zcbor_uint64_expect(state, (5))))
	&& (zcbor_bstr_decode(state, (&(*result).config_begin_config_device_id)))
	&& ((((((*result).config_begin_config_device_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (6))))
	&& (zcbor_bstr_decode(state, (&(*result).config_begin_config_template_id)))
	&& ((((((*result).config_begin_config_template_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (7))))
	&& (zcbor_tstr_decode(state, (&(*result).config_begin_config_link_url)))
	&& ((((((*result).config_begin_config_link_url.len >= 1)
	&& ((*result).config_begin_config_link_url.len <= 192)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).config_begin_config_command_id_present), (zcbor_decoder_t *)decode_repeated_config_begin_config_command_id, state, (&(*result).config_begin_config_command_id))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_config_begin_config_command_id(state, (&(*result).config_begin_config_command_id));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_message_rejected(
		zcbor_state_t *state, struct zcbor_string *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_tstr_decode(state, (&(*result))))
	&& ((((((*result).len >= 1)
	&& ((*result).len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_flow_control(
		zcbor_state_t *state, struct flow_control *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).flow_control_flow_retry_after_ms)))
	&& ((((((*result).flow_control_flow_retry_after_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).flow_control_flow_reason)))
	&& ((((((*result).flow_control_flow_reason.len >= 1)
	&& ((*result).flow_control_flow_reason.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).flow_control_flow_capacity)))
	&& ((((((*result).flow_control_flow_capacity <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_time_response(
		zcbor_state_t *state, struct time_response *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).time_response_request_id))))
	&& ((zcbor_uint64_decode(state, (&(*result).time_response_server_utc_ms))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_desired(
		zcbor_state_t *state, struct schedule_desired *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_desired_desired_schedule_generation)))
	&& ((((((((*result).schedule_desired_desired_schedule_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_desired_desired_schedule_revision)))
	&& ((((((*result).schedule_desired_desired_schedule_revision <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_desired_desired_schedule_record_count)))
	&& ((((((*result).schedule_desired_desired_schedule_record_count <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).schedule_desired_desired_schedule_checksum)))
	&& ((((((*result).schedule_desired_desired_schedule_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ota_desired(
		zcbor_state_t *state, struct ota_desired *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_bstr_decode(state, (&(*result).ota_desired_ota_install_id)))
	&& ((((((*result).ota_desired_ota_install_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).ota_desired_ota_version)))
	&& ((((*result).ota_desired_ota_version.len >= 1)
	&& ((*result).ota_desired_ota_version.len <= 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).ota_desired_ota_url)))
	&& ((((((*result).ota_desired_ota_url.len >= 1)
	&& ((*result).ota_desired_ota_url.len <= 256)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_bstr_decode(state, (&(*result).ota_desired_ota_checksum)))
	&& ((((((*result).ota_desired_ota_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (zcbor_uint64_decode(state, (&(*result).ota_desired_ota_size)))
	&& ((((((*result).ota_desired_ota_size <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).ota_desired_ota_target_present), (zcbor_decoder_t *)decode_repeated_ota_desired_ota_target, state, (&(*result).ota_desired_ota_target))
	&& zcbor_present_decode(&((*result).ota_desired_ota_release_id_present), (zcbor_decoder_t *)decode_repeated_ota_desired_ota_release_id, state, (&(*result).ota_desired_ota_release_id))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_ota_desired_ota_target(state, (&(*result).ota_desired_ota_target));
		decode_repeated_ota_desired_ota_release_id(state, (&(*result).ota_desired_ota_release_id));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_desired(
		zcbor_state_t *state, struct config_desired *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).config_desired_desired_config_generation)))
	&& ((((((((*result).config_desired_desired_config_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).config_desired_desired_config_checksum)))
	&& ((((((*result).config_desired_desired_config_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).config_desired_desired_config_record_count)))
	&& ((((((*result).config_desired_desired_config_record_count <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ingestion_ack(
		zcbor_state_t *state, void *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_command(
		zcbor_state_t *state, struct command *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).command_generation)))
	&& ((((((((*result).command_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).command_id)))
	&& ((((((*result).command_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_compact_id)))
	&& ((((((((*result).command_compact_id <= UINT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_typed_value_fields(state, (&(*result).command_typed_value_fields_m))))
	&& ((zcbor_uint64_decode(state, (&(*result).command_desired_version)))
	&& ((((((*result).command_desired_version <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).command_expires_at_utc_ms))))
	&& ((decode_correlation_id(state, (&(*result).command_correlation_id))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_ack(
		zcbor_state_t *state, struct config_ack *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).config_ack_ack_generation)))
	&& ((((((((*result).config_ack_ack_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).config_ack_ack_sequence)))
	&& ((((((((*result).config_ack_ack_sequence <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).config_ack_ack_status)))
	&& ((((*result).config_ack_ack_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).config_ack_ack_reason)))
	&& ((((((*result).config_ack_ack_reason.len >= 1)
	&& ((*result).config_ack_ack_reason.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_time_request(
		zcbor_state_t *state, struct time_request *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).time_request_id))))
	&& ((zcbor_uint64_decode(state, (&(*result).time_request_monotonic_ms))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_renew(
		zcbor_state_t *state, struct schedule_renew *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_renew_renew_generation)))
	&& ((((((((*result).schedule_renew_renew_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_renew_renew_revision)))
	&& ((((((*result).schedule_renew_renew_revision <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_schedule_reported(
		zcbor_state_t *state, struct schedule_reported *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).schedule_reported_reported_schedule_generation)))
	&& ((((((((*result).schedule_reported_reported_schedule_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_reported_reported_schedule_revision)))
	&& ((((((*result).schedule_reported_reported_schedule_revision <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).schedule_reported_reported_schedule_status)))
	&& ((((*result).schedule_reported_reported_schedule_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).schedule_reported_reported_schedule_checksum)))
	&& ((((((*result).schedule_reported_reported_schedule_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ota_reported(
		zcbor_state_t *state, struct ota_reported *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_bstr_decode(state, (&(*result).ota_reported_reported_ota_id)))
	&& ((((((*result).ota_reported_reported_ota_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).ota_reported_reported_ota_status)))
	&& ((((*result).ota_reported_reported_ota_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).ota_reported_reported_ota_error)))
	&& ((((((*result).ota_reported_reported_ota_error.len >= 1)
	&& ((*result).ota_reported_reported_ota_error.len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).ota_reported_reported_ota_progress)))
	&& ((((((*result).ota_reported_reported_ota_progress <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_config_reported(
		zcbor_state_t *state, struct config_reported *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).config_reported_reported_config_generation)))
	&& ((((((((*result).config_reported_reported_config_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).config_reported_reported_config_status)))
	&& ((((*result).config_reported_reported_config_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).config_reported_reported_config_checksum)))
	&& ((((((*result).config_reported_reported_config_checksum.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_command_result(
		zcbor_state_t *state, struct command_result_r *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_union_start_code(state) && (int_res = ((((decode_command_result_ok(state, (&(*result).command_result_ok_m)))) && (((*result).command_result_choice = command_result_ok_m_c), true))
	|| (zcbor_union_elem_code(state) && (((decode_command_result_error(state, (&(*result).command_result_error_m)))) && (((*result).command_result_choice = command_result_error_m_c), true)))), zcbor_union_end_code(state), int_res))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_state(
		zcbor_state_t *state, struct state *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).state_generation)))
	&& ((((((((*result).state_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_state_readings(state, (&(*result).state_values))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_heartbeat(
		zcbor_state_t *state, struct heartbeat *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).heartbeat_generation)))
	&& ((((((((*result).heartbeat_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).heartbeat_uptime_ms))))
	&& ((zcbor_uint64_decode(state, (&(*result).heartbeat_status)))
	&& ((((((*result).heartbeat_status <= UINT8_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).heartbeat_firmware_version)))
	&& ((((*result).heartbeat_firmware_version.len >= 1)
	&& ((*result).heartbeat_firmware_version.len <= 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_tstr_decode(state, (&(*result).heartbeat_firmware_target)))
	&& ((((*result).heartbeat_firmware_target.len >= 1)
	&& ((*result).heartbeat_firmware_target.len <= 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_union_start_code(state) && (int_res = ((((zcbor_bstr_decode(state, (&(*result).heartbeat_running_release_id_empty_id_m)))
	&& ((((((*result).heartbeat_running_release_id_empty_id_m.len == 0)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).heartbeat_running_release_id_choice = heartbeat_running_release_id_empty_id_m_c), true))
	|| (zcbor_union_elem_code(state) && (((zcbor_bstr_decode(state, (&(*result).heartbeat_running_release_id_uuid_m)))
	&& ((((((*result).heartbeat_running_release_id_uuid_m.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).heartbeat_running_release_id_choice = heartbeat_running_release_id_uuid_m_c), true)))), zcbor_union_end_code(state), int_res)))
	&& ((zcbor_union_start_code(state) && (int_res = ((((zcbor_bstr_decode(state, (&(*result).heartbeat_last_install_id_empty_id_m)))
	&& ((((((*result).heartbeat_last_install_id_empty_id_m.len == 0)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).heartbeat_last_install_id_choice = heartbeat_last_install_id_empty_id_m_c), true))
	|| (zcbor_union_elem_code(state) && (((zcbor_bstr_decode(state, (&(*result).heartbeat_last_install_id_uuid_m)))
	&& ((((((*result).heartbeat_last_install_id_uuid_m.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) && (((*result).heartbeat_last_install_id_choice = heartbeat_last_install_id_uuid_m_c), true)))), zcbor_union_end_code(state), int_res)))
	&& ((decode_ota_profile(state, (&(*result).heartbeat_ota_profile))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_datastream_bound(
		zcbor_state_t *state, struct datastream_bound *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).datastream_bound_bound_generation)))
	&& ((((((((*result).datastream_bound_bound_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_datastream_bound_ids(state, (&(*result).datastream_bound_bound_ids))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_datastream_bind(
		zcbor_state_t *state, struct datastream_bind *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).datastream_bind_binding_generation)))
	&& ((((((((*result).datastream_bind_binding_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((decode_datastream_binding_keys(state, (&(*result).datastream_bind_binding_keys))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_bootstrap_error(
		zcbor_state_t *state, struct zcbor_string *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_tstr_decode(state, (&(*result))))
	&& ((((((*result).len >= 1)
	&& ((*result).len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_bootstrap_committed(
		zcbor_state_t *state, struct bootstrap_committed *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_bstr_decode(state, (&(*result).bootstrap_committed_committed_device_id)))
	&& ((((((*result).bootstrap_committed_committed_device_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).bootstrap_committed_committed_generation)))
	&& ((((((((*result).bootstrap_committed_committed_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_decode(state, (&(*result).bootstrap_committed_committed_server_utc_ms))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_bootstrap_auth(
		zcbor_state_t *state, struct bootstrap_auth *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint64_expect(state, (0))))
	&& (zcbor_bstr_decode(state, (&(*result).bootstrap_auth_bootstrap_token)))
	&& ((((((*result).bootstrap_auth_bootstrap_token.len >= 32)
	&& ((*result).bootstrap_auth_bootstrap_token.len <= 64)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (1))))
	&& (zcbor_bstr_decode(state, (&(*result).bootstrap_auth_bootstrap_secret)))
	&& ((((((*result).bootstrap_auth_bootstrap_secret.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).bootstrap_auth_hardware_id)))
	&& ((((*result).bootstrap_auth_hardware_id.len >= 1)
	&& ((*result).bootstrap_auth_hardware_id.len <= 96)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (3))))
	&& (zcbor_tstr_decode(state, (&(*result).bootstrap_auth_firmware_target)))
	&& ((((*result).bootstrap_auth_firmware_target.len >= 1)
	&& ((*result).bootstrap_auth_firmware_target.len <= 64)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint64_expect(state, (4))))
	&& (decode_capabilities(state, (&(*result).bootstrap_auth_bootstrap_capabilities))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_pong(
		zcbor_state_t *state, void *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ping(
		zcbor_state_t *state, void *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_auth_error(
		zcbor_state_t *state, struct zcbor_string *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_tstr_decode(state, (&(*result))))
	&& ((((((*result).len >= 1)
	&& ((*result).len <= 48)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_auth_ok(
		zcbor_state_t *state, struct auth_ok *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_uint64_decode(state, (&(*result).auth_ok_auth_server_utc_ms))))
	&& ((zcbor_uint64_decode(state, (&(*result).auth_ok_auth_heartbeat_ms)))
	&& ((((((*result).auth_ok_auth_heartbeat_ms <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_uint64_expect(state, (512))))
	&& ((zcbor_uint64_decode(state, (&(*result).auth_ok_auth_connection_generation)))
	&& ((((((*result).auth_ok_auth_connection_generation <= UINT32_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_auth(
		zcbor_state_t *state, struct auth *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((((zcbor_bstr_decode(state, (&(*result).auth_device_id)))
	&& ((((((*result).auth_device_id.len == 16)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& ((zcbor_bstr_decode(state, (&(*result).auth_secret)))
	&& ((((((*result).auth_secret.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}



int cbor_decode_auth(
		const uint8_t *payload, size_t payload_len,
		struct auth *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_auth, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_auth_ok(
		const uint8_t *payload, size_t payload_len,
		struct auth_ok *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_auth_ok, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_auth_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_auth_error, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_ping(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_ping, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_pong(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_pong, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_bootstrap_auth(
		const uint8_t *payload, size_t payload_len,
		struct bootstrap_auth *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_bootstrap_auth, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_bootstrap_committed(
		const uint8_t *payload, size_t payload_len,
		struct bootstrap_committed *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_bootstrap_committed, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_bootstrap_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_bootstrap_error, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_datastream_bind(
		const uint8_t *payload, size_t payload_len,
		struct datastream_bind *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_datastream_bind, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_datastream_bound(
		const uint8_t *payload, size_t payload_len,
		struct datastream_bound *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_datastream_bound, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_heartbeat(
		const uint8_t *payload, size_t payload_len,
		struct heartbeat *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_heartbeat, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_state(
		const uint8_t *payload, size_t payload_len,
		struct state *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[6];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_state, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_command_result(
		const uint8_t *payload, size_t payload_len,
		struct command_result_r *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[5];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_command_result, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_reported(
		const uint8_t *payload, size_t payload_len,
		struct config_reported *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_reported, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_ota_reported(
		const uint8_t *payload, size_t payload_len,
		struct ota_reported *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_ota_reported, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_schedule_reported(
		const uint8_t *payload, size_t payload_len,
		struct schedule_reported *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_schedule_reported, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_schedule_renew(
		const uint8_t *payload, size_t payload_len,
		struct schedule_renew *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_schedule_renew, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_time_request(
		const uint8_t *payload, size_t payload_len,
		struct time_request *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_time_request, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_ack(
		const uint8_t *payload, size_t payload_len,
		struct config_ack *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_ack, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_command(
		const uint8_t *payload, size_t payload_len,
		struct command *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_command, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_ingestion_ack(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_ingestion_ack, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_desired(
		const uint8_t *payload, size_t payload_len,
		struct config_desired *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_desired, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_ota_desired(
		const uint8_t *payload, size_t payload_len,
		struct ota_desired *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_ota_desired, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_schedule_desired(
		const uint8_t *payload, size_t payload_len,
		struct schedule_desired *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_schedule_desired, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_time_response(
		const uint8_t *payload, size_t payload_len,
		struct time_response *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_time_response, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_flow_control(
		const uint8_t *payload, size_t payload_len,
		struct flow_control *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_flow_control, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_message_rejected(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_message_rejected, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_begin(
		const uint8_t *payload, size_t payload_len,
		struct config_begin *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_begin, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_record(
		const uint8_t *payload, size_t payload_len,
		struct config_record *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[8];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_record, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_config_end(
		const uint8_t *payload, size_t payload_len,
		struct config_end *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_config_end, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_schedule_record_message(
		const uint8_t *payload, size_t payload_len,
		struct schedule_record_message *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[7];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_schedule_record_message, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_schedule_end(
		const uint8_t *payload, size_t payload_len,
		struct schedule_end *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_schedule_end, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_error(
		const uint8_t *payload, size_t payload_len,
		struct zcbor_string *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_error, sizeof(states) / sizeof(zcbor_state_t), 1);
}
