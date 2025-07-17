/*
 * JackBridge for DPF
 * Copyright (C) 2013-2025 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "jackbridge.h"

#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

#include <dlfcn.h>
#include <errno.h>

typedef unsigned long ulong;

// --------------------------------------------------------------------------------------------------------------------

typedef void (*JackSymLatencyCallback)(jack_latency_callback_mode_t, void*);
typedef int  (*JackSymProcessCallback)(jack_nframes_t, void*);
typedef void (*JackSymThreadInitCallback)(void*);
typedef int  (*JackSymGraphOrderCallback)(void*);
typedef int  (*JackSymXRunCallback)(void*);
typedef int  (*JackSymBufferSizeCallback)(jack_nframes_t, void*);
typedef int  (*JackSymSampleRateCallback)(jack_nframes_t, void*);
typedef void (*JackSymPortRegistrationCallback)(jack_port_id_t, int, void*);
typedef void (*JackSymClientRegistrationCallback)(const char*, int, void*);
typedef void (*JackSymPortConnectCallback)(jack_port_id_t, jack_port_id_t, int, void*);
typedef void (*JackSymPortRenameCallback)(jack_port_id_t, const char*, const char*, void*);
typedef void (*JackSymFreewheelCallback)(int, void*);
typedef void (*JackSymShutdownCallback)(void*);
typedef void (*JackSymInfoShutdownCallback)(jack_status_t, const char*, void*);
typedef int  (*JackSymSyncCallback)(jack_transport_state_t, jack_position_t*, void*);
typedef void (*JackSymTimebaseCallback)(jack_transport_state_t, jack_nframes_t, jack_position_t*, int, void*);
typedef void (*JackSymSessionCallback)(jack_session_event_t*, void*);
typedef void (*JackSymPropertyChangeCallback)(jack_uuid_t, const char*, jack_property_change_t, void*);
typedef void* (*JackSymThreadCallback)(void*);

typedef void        (*jacksym_get_version)(int*, int*, int*, int*);
typedef const char* (*jacksym_get_version_string)(void);

typedef jack_client_t* (*jacksym_client_open)(const char*, jack_options_t, jack_status_t*, ...);
typedef int            (*jacksym_client_close)(jack_client_t*);

typedef int   (*jacksym_client_name_size)(void);
typedef char* (*jacksym_get_client_name)(jack_client_t*);

typedef char* (*jacksym_client_get_uuid)(jack_client_t*);
typedef char* (*jacksym_get_uuid_for_client_name)(jack_client_t*, const char*);
typedef char* (*jacksym_get_client_name_by_uuid)(jack_client_t*, const char*);

typedef int  (*jacksym_uuid_parse)(const char*, jack_uuid_t*);
typedef void (*jacksym_uuid_unparse)(jack_uuid_t, char buf[JACK_UUID_STRING_SIZE]);

typedef int (*jacksym_activate)(jack_client_t*);
typedef int (*jacksym_deactivate)(jack_client_t*);
typedef int (*jacksym_is_realtime)(jack_client_t*);

typedef int  (*jacksym_set_thread_init_callback)(jack_client_t*, JackSymThreadInitCallback, void*);
typedef void (*jacksym_on_shutdown)(jack_client_t*, JackSymShutdownCallback, void*);
typedef void (*jacksym_on_info_shutdown)(jack_client_t*, JackSymInfoShutdownCallback, void*);
typedef int  (*jacksym_set_process_callback)(jack_client_t*, JackSymProcessCallback, void*);
typedef int  (*jacksym_set_freewheel_callback)(jack_client_t*, JackSymFreewheelCallback, void*);
typedef int  (*jacksym_set_buffer_size_callback)(jack_client_t*, JackSymBufferSizeCallback, void*);
typedef int  (*jacksym_set_sample_rate_callback)(jack_client_t*, JackSymSampleRateCallback, void*);
typedef int  (*jacksym_set_client_registration_callback)(jack_client_t*, JackSymClientRegistrationCallback, void*);
typedef int  (*jacksym_set_port_registration_callback)(jack_client_t*, JackSymPortRegistrationCallback, void*);
typedef int  (*jacksym_set_port_rename_callback)(jack_client_t*, JackSymPortRenameCallback, void*);
typedef int  (*jacksym_set_port_connect_callback)(jack_client_t*, JackSymPortConnectCallback, void*);
typedef int  (*jacksym_set_graph_order_callback)(jack_client_t*, JackSymGraphOrderCallback, void*);
typedef int  (*jacksym_set_xrun_callback)(jack_client_t*, JackSymXRunCallback, void*);
typedef int  (*jacksym_set_latency_callback)(jack_client_t*, JackSymLatencyCallback, void*);

typedef int (*jacksym_set_freewheel)(jack_client_t*, int);
typedef int (*jacksym_set_buffer_size)(jack_client_t*, jack_nframes_t);

typedef jack_nframes_t (*jacksym_get_sample_rate)(jack_client_t*);
typedef jack_nframes_t (*jacksym_get_buffer_size)(jack_client_t*);
typedef float          (*jacksym_cpu_load)(jack_client_t*);

typedef jack_port_t* (*jacksym_port_register)(jack_client_t*, const char*, const char*, ulong, ulong);
typedef int          (*jacksym_port_unregister)(jack_client_t*, jack_port_t*);
typedef void*        (*jacksym_port_get_buffer)(jack_port_t*, jack_nframes_t);

typedef const char*  (*jacksym_port_name)(const jack_port_t*);
typedef jack_uuid_t  (*jacksym_port_uuid)(const jack_port_t*);
typedef const char*  (*jacksym_port_short_name)(const jack_port_t*);
typedef int          (*jacksym_port_flags)(const jack_port_t*);
typedef const char*  (*jacksym_port_type)(const jack_port_t*);
typedef int          (*jacksym_port_is_mine)(const jack_client_t*, const jack_port_t*);
typedef int          (*jacksym_port_connected)(const jack_port_t*);
typedef int          (*jacksym_port_connected_to)(const jack_port_t*, const char*);
typedef const char** (*jacksym_port_get_connections)(const jack_port_t*);
typedef const char** (*jacksym_port_get_all_connections)(const jack_client_t*, const jack_port_t*);

typedef int (*jacksym_port_rename)(jack_client_t*, jack_port_t*, const char*);
typedef int (*jacksym_port_set_name)(jack_port_t*, const char*);
typedef int (*jacksym_port_set_alias)(jack_port_t*, const char*);
typedef int (*jacksym_port_unset_alias)(jack_port_t*, const char*);
typedef int (*jacksym_port_get_aliases)(const jack_port_t*, char* const aliases[2]);

typedef int (*jacksym_port_request_monitor)(jack_port_t*, int);
typedef int (*jacksym_port_request_monitor_by_name)(jack_client_t*, const char*, int);
typedef int (*jacksym_port_ensure_monitor)(jack_port_t*, int);
typedef int (*jacksym_port_monitoring_input)(jack_port_t*);

typedef int (*jacksym_connect)(jack_client_t*, const char*, const char*);
typedef int (*jacksym_disconnect)(jack_client_t*, const char*, const char*);
typedef int (*jacksym_port_disconnect)(jack_client_t*, jack_port_t*);

typedef int    (*jacksym_port_name_size)(void);
typedef int    (*jacksym_port_type_size)(void);
typedef size_t (*jacksym_port_type_get_buffer_size)(jack_client_t*, const char*);

typedef void (*jacksym_port_get_latency_range)(jack_port_t*, jack_latency_callback_mode_t, jack_latency_range_t*);
typedef void (*jacksym_port_set_latency_range)(jack_port_t*, jack_latency_callback_mode_t, jack_latency_range_t*);
typedef int  (*jacksym_recompute_total_latencies)(jack_client_t*);

typedef const char** (*jacksym_get_ports)(jack_client_t*, const char*, const char*, ulong);
typedef jack_port_t* (*jacksym_port_by_name)(jack_client_t*, const char*);
typedef jack_port_t* (*jacksym_port_by_id)(jack_client_t*, jack_port_id_t);

typedef void (*jacksym_free)(void*);

typedef uint32_t (*jacksym_midi_get_event_count)(void*);
typedef int      (*jacksym_midi_event_get)(jack_midi_event_t*, void*, uint32_t);
typedef void     (*jacksym_midi_clear_buffer)(void*);
typedef int      (*jacksym_midi_event_write)(void*, jack_nframes_t, const jack_midi_data_t*, size_t);
typedef jack_midi_data_t* (*jacksym_midi_event_reserve)(void*, jack_nframes_t, size_t);

typedef int (*jacksym_release_timebase)(jack_client_t*);
typedef int (*jacksym_set_sync_callback)(jack_client_t*, JackSymSyncCallback, void*);
typedef int (*jacksym_set_sync_timeout)(jack_client_t*, jack_time_t);
typedef int (*jacksym_set_timebase_callback)(jack_client_t*, int, JackSymTimebaseCallback, void*);
typedef int (*jacksym_transport_locate)(jack_client_t*, jack_nframes_t);

typedef jack_transport_state_t (*jacksym_transport_query)(const jack_client_t*, jack_position_t*);
typedef jack_nframes_t         (*jacksym_get_current_transport_frame)(const jack_client_t*);

typedef int  (*jacksym_transport_reposition)(jack_client_t*, const jack_position_t*);
typedef void (*jacksym_transport_start)(jack_client_t*);
typedef void (*jacksym_transport_stop)(jack_client_t*);

typedef int  (*jacksym_set_property)(jack_client_t*, jack_uuid_t, const char*, const char*, const char*);
typedef int  (*jacksym_get_property)(jack_uuid_t, const char*, char**, char**);
typedef void (*jacksym_free_description)(jack_description_t*, int);
typedef int  (*jacksym_get_properties)(jack_uuid_t, jack_description_t*);
typedef int  (*jacksym_get_all_properties)(jack_description_t**);
typedef int  (*jacksym_remove_property)(jack_client_t*, jack_uuid_t, const char*);
typedef int  (*jacksym_remove_properties)(jack_client_t*, jack_uuid_t);
typedef int  (*jacksym_remove_all_properties)(jack_client_t*);
typedef int  (*jacksym_set_property_change_callback)(jack_client_t*, JackSymPropertyChangeCallback, void*);

typedef int  (*jacksym_thread_creator_t)(pthread_t*, const pthread_attr_t*, void *(*)(void*), void*);
typedef void (*jacksym_set_thread_creator)(jacksym_thread_creator_t);

typedef bool           (*jacksym_set_process_thread)(jack_client_t*, JackSymThreadCallback callback, void*);
typedef jack_nframes_t (*jacksym_cycle_wait)(jack_client_t*);
typedef void           (*jacksym_cycle_signal)(jack_client_t*, int);

typedef jack_nframes_t (*jacksym_port_get_latency)(jack_port_t*);
typedef jack_nframes_t (*jacksym_frame_time)(const jack_client_t*);

// --------------------------------------------------------------------------------------------------------------------

typedef struct JackBridge {
    void* lib;

    jacksym_get_version get_version_ptr;
    jacksym_get_version_string get_version_string_ptr;

    jacksym_client_open client_open_ptr;
    jacksym_client_close client_close_ptr;

    jacksym_client_name_size client_name_size_ptr;
    jacksym_get_client_name get_client_name_ptr;

    jacksym_client_get_uuid client_get_uuid_ptr;
    jacksym_get_uuid_for_client_name get_uuid_for_client_name_ptr;
    jacksym_get_client_name_by_uuid get_client_name_by_uuid_ptr;

    jacksym_uuid_parse uuid_parse_ptr;
    jacksym_uuid_unparse uuid_unparse_ptr;

    jacksym_activate activate_ptr;
    jacksym_deactivate deactivate_ptr;
    jacksym_is_realtime is_realtime_ptr;

    jacksym_set_thread_init_callback set_thread_init_callback_ptr;
    jacksym_on_shutdown on_shutdown_ptr;
    jacksym_on_info_shutdown on_info_shutdown_ptr;
    jacksym_set_process_callback set_process_callback_ptr;
    jacksym_set_freewheel_callback set_freewheel_callback_ptr;
    jacksym_set_buffer_size_callback set_buffer_size_callback_ptr;
    jacksym_set_sample_rate_callback set_sample_rate_callback_ptr;
    jacksym_set_client_registration_callback set_client_registration_callback_ptr;
    jacksym_set_port_registration_callback set_port_registration_callback_ptr;
    jacksym_set_port_rename_callback set_port_rename_callback_ptr;
    jacksym_set_port_connect_callback set_port_connect_callback_ptr;
    jacksym_set_graph_order_callback set_graph_order_callback_ptr;
    jacksym_set_xrun_callback set_xrun_callback_ptr;
    jacksym_set_latency_callback set_latency_callback_ptr;

    jacksym_set_freewheel set_freewheel_ptr;
    jacksym_set_buffer_size set_buffer_size_ptr;

    jacksym_get_sample_rate get_sample_rate_ptr;
    jacksym_get_buffer_size get_buffer_size_ptr;
    jacksym_cpu_load cpu_load_ptr;

    jacksym_port_register port_register_ptr;
    jacksym_port_unregister port_unregister_ptr;
    jacksym_port_get_buffer port_get_buffer_ptr;

    jacksym_port_name port_name_ptr;
    jacksym_port_uuid port_uuid_ptr;
    jacksym_port_short_name port_short_name_ptr;
    jacksym_port_flags port_flags_ptr;
    jacksym_port_type port_type_ptr;
    jacksym_port_is_mine port_is_mine_ptr;
    jacksym_port_connected port_connected_ptr;
    jacksym_port_connected_to port_connected_to_ptr;
    jacksym_port_get_connections port_get_connections_ptr;
    jacksym_port_get_all_connections port_get_all_connections_ptr;

    jacksym_port_rename port_rename_ptr;
    jacksym_port_set_name port_set_name_ptr;
    jacksym_port_set_alias port_set_alias_ptr;
    jacksym_port_unset_alias port_unset_alias_ptr;
    jacksym_port_get_aliases port_get_aliases_ptr;

    jacksym_port_request_monitor port_request_monitor_ptr;
    jacksym_port_request_monitor_by_name port_request_monitor_by_name_ptr;
    jacksym_port_ensure_monitor port_ensure_monitor_ptr;
    jacksym_port_monitoring_input port_monitoring_input_ptr;

    jacksym_connect connect_ptr;
    jacksym_disconnect disconnect_ptr;
    jacksym_port_disconnect port_disconnect_ptr;

    jacksym_port_name_size port_name_size_ptr;
    jacksym_port_type_size port_type_size_ptr;
    jacksym_port_type_get_buffer_size port_type_get_buffer_size_ptr;

    jacksym_port_get_latency_range port_get_latency_range_ptr;
    jacksym_port_set_latency_range port_set_latency_range_ptr;
    jacksym_recompute_total_latencies recompute_total_latencies_ptr;

    jacksym_get_ports get_ports_ptr;
    jacksym_port_by_name port_by_name_ptr;
    jacksym_port_by_id port_by_id_ptr;

    jacksym_free free_ptr;

    jacksym_midi_get_event_count midi_get_event_count_ptr;
    jacksym_midi_event_get midi_event_get_ptr;
    jacksym_midi_clear_buffer midi_clear_buffer_ptr;
    jacksym_midi_event_write midi_event_write_ptr;
    jacksym_midi_event_reserve midi_event_reserve_ptr;

    jacksym_release_timebase release_timebase_ptr;
    jacksym_set_sync_callback set_sync_callback_ptr;
    jacksym_set_sync_timeout set_sync_timeout_ptr;
    jacksym_set_timebase_callback set_timebase_callback_ptr;
    jacksym_transport_locate transport_locate_ptr;

    jacksym_transport_query transport_query_ptr;
    jacksym_get_current_transport_frame get_current_transport_frame_ptr;

    jacksym_transport_reposition transport_reposition_ptr;
    jacksym_transport_start transport_start_ptr;
    jacksym_transport_stop transport_stop_ptr;

    jacksym_set_property set_property_ptr;
    jacksym_get_property get_property_ptr;
    jacksym_free_description free_description_ptr;
    jacksym_get_properties get_properties_ptr;
    jacksym_get_all_properties get_all_properties_ptr;
    jacksym_remove_property remove_property_ptr;
    jacksym_remove_properties remove_properties_ptr;
    jacksym_remove_all_properties remove_all_properties_ptr;
    jacksym_set_property_change_callback set_property_change_callback_ptr;

    jacksym_set_thread_creator set_thread_creator_ptr;

    jacksym_set_process_thread set_process_thread_ptr;
    jacksym_cycle_wait cycle_wait_ptr;
    jacksym_cycle_signal cycle_signal_ptr;

    jacksym_port_get_latency port_get_latency_ptr;
    jacksym_frame_time frame_time_ptr;
} JackBridge;

static void jackbridge_init(JackBridge* const bridge)
{
    void* lib;
    memset(bridge, 0, sizeof(*bridge));

    lib = dlopen("libjack.so.0", RTLD_NOW);
    if (lib == NULL)
    {
        fprintf(stderr, "[wineasio] Failed to load libjack.so.0: '%s'\n", dlerror());
        return;
    }
    else
    {
        fprintf(stdout, "[wineasio] libjack.so.0 loaded successfully!\n");
    }

    #define JOIN(a, b) a ## b
    #define LIB_SYMBOL(NAME) bridge->JOIN(NAME, _ptr) = (jacksym_##NAME)dlsym(lib, "jack_" #NAME);

    LIB_SYMBOL(get_version)
    LIB_SYMBOL(get_version_string)

    LIB_SYMBOL(client_open)
    LIB_SYMBOL(client_close)

    LIB_SYMBOL(client_name_size)
    LIB_SYMBOL(get_client_name)

    LIB_SYMBOL(client_get_uuid)
    LIB_SYMBOL(get_uuid_for_client_name)
    LIB_SYMBOL(get_client_name_by_uuid)

    LIB_SYMBOL(uuid_parse)
    LIB_SYMBOL(uuid_unparse)

    LIB_SYMBOL(activate)
    LIB_SYMBOL(deactivate)
    LIB_SYMBOL(is_realtime)

    LIB_SYMBOL(set_thread_init_callback)
    LIB_SYMBOL(on_shutdown)
    LIB_SYMBOL(on_info_shutdown)
    LIB_SYMBOL(set_process_callback)
    LIB_SYMBOL(set_freewheel_callback)
    LIB_SYMBOL(set_buffer_size_callback)
    LIB_SYMBOL(set_sample_rate_callback)
    LIB_SYMBOL(set_client_registration_callback)
    LIB_SYMBOL(set_port_registration_callback)
    LIB_SYMBOL(set_port_rename_callback)
    LIB_SYMBOL(set_port_connect_callback)
    LIB_SYMBOL(set_graph_order_callback)
    LIB_SYMBOL(set_xrun_callback)
    LIB_SYMBOL(set_latency_callback)

    LIB_SYMBOL(set_freewheel)
    LIB_SYMBOL(set_buffer_size)

    LIB_SYMBOL(get_sample_rate)
    LIB_SYMBOL(get_buffer_size)
    LIB_SYMBOL(cpu_load)

    LIB_SYMBOL(port_register)
    LIB_SYMBOL(port_unregister)
    LIB_SYMBOL(port_get_buffer)

    LIB_SYMBOL(port_name)
    LIB_SYMBOL(port_uuid)
    LIB_SYMBOL(port_short_name)
    LIB_SYMBOL(port_flags)
    LIB_SYMBOL(port_type)
    LIB_SYMBOL(port_is_mine)
    LIB_SYMBOL(port_connected)
    LIB_SYMBOL(port_connected_to)
    LIB_SYMBOL(port_get_connections)
    LIB_SYMBOL(port_get_all_connections)

    LIB_SYMBOL(port_rename)
    LIB_SYMBOL(port_set_name)
    LIB_SYMBOL(port_set_alias)
    LIB_SYMBOL(port_unset_alias)
    LIB_SYMBOL(port_get_aliases)

    LIB_SYMBOL(port_request_monitor)
    LIB_SYMBOL(port_request_monitor_by_name)
    LIB_SYMBOL(port_ensure_monitor)
    LIB_SYMBOL(port_monitoring_input)

    LIB_SYMBOL(connect)
    LIB_SYMBOL(disconnect)
    LIB_SYMBOL(port_disconnect)

    LIB_SYMBOL(port_name_size)
    LIB_SYMBOL(port_type_size)
    LIB_SYMBOL(port_type_get_buffer_size)

    LIB_SYMBOL(port_get_latency_range)
    LIB_SYMBOL(port_set_latency_range)
    LIB_SYMBOL(recompute_total_latencies)

    LIB_SYMBOL(get_ports)
    LIB_SYMBOL(port_by_name)
    LIB_SYMBOL(port_by_id)

    LIB_SYMBOL(free)

    LIB_SYMBOL(midi_get_event_count)
    LIB_SYMBOL(midi_event_get)
    LIB_SYMBOL(midi_clear_buffer)
    LIB_SYMBOL(midi_event_write)
    LIB_SYMBOL(midi_event_reserve)

    LIB_SYMBOL(release_timebase)
    LIB_SYMBOL(set_sync_callback)
    LIB_SYMBOL(set_sync_timeout)
    LIB_SYMBOL(set_timebase_callback)
    LIB_SYMBOL(transport_locate)
    LIB_SYMBOL(transport_query)
    LIB_SYMBOL(get_current_transport_frame)
    LIB_SYMBOL(transport_reposition)
    LIB_SYMBOL(transport_start)
    LIB_SYMBOL(transport_stop)

    LIB_SYMBOL(set_property)
    LIB_SYMBOL(get_property)
    LIB_SYMBOL(free_description)
    LIB_SYMBOL(get_properties)
    LIB_SYMBOL(get_all_properties)
    LIB_SYMBOL(remove_property)
    LIB_SYMBOL(remove_properties)
    LIB_SYMBOL(remove_all_properties)
    LIB_SYMBOL(set_property_change_callback)

    LIB_SYMBOL(set_thread_creator)

    LIB_SYMBOL(set_process_thread)
    LIB_SYMBOL(cycle_wait)
    LIB_SYMBOL(cycle_signal)

    LIB_SYMBOL(port_get_latency)
    LIB_SYMBOL(frame_time)

    #undef JOIN
    #undef LIB_SYMBOL

    bridge->lib = lib;
}

__attribute__((destructor))
static void jackbridge_destroy(JackBridge* const bridge)
{
    if (bridge->lib != NULL)
    {
        dlclose(bridge->lib);
        bridge->lib = NULL;
    }
}

static JackBridge* jackbridge_instance()
{
    static JackBridge bridge;
    static bool init = false;
    if (! init)
    {
        init = true;
        jackbridge_init(&bridge);
    }
    return &bridge;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_is_ok()
{
    return jackbridge_instance()->lib != NULL;
}

// --------------------------------------------------------------------------------------------------------------------

void jackbridge_get_version(int* major_ptr, int* minor_ptr, int* micro_ptr, int* proto_ptr)
{
    if (jackbridge_instance()->get_version_ptr != NULL)
        return jackbridge_instance()->get_version_ptr(major_ptr, minor_ptr, micro_ptr, proto_ptr);
    if (major_ptr != NULL)
        *major_ptr = 0;
    if (minor_ptr != NULL)
        *minor_ptr = 0;
    if (micro_ptr != NULL)
        *micro_ptr = 0;
    if (proto_ptr != NULL)
        *proto_ptr = 0;
}

const char* jackbridge_get_version_string()
{
    if (jackbridge_instance()->get_version_string_ptr != NULL)
        return jackbridge_instance()->get_version_string_ptr();
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

jack_client_t* jackbridge_client_open(const char* client_name, uint32_t options, jack_status_t* status)
{
    if (jackbridge_instance()->client_open_ptr != NULL)
        return jackbridge_instance()->client_open_ptr(client_name, (jack_options_t)options, status);
    if (status != NULL)
        *status = JackServerError;
    return NULL;
}

bool jackbridge_client_close(jack_client_t* client)
{
    if (jackbridge_instance()->client_close_ptr != NULL)
        return (jackbridge_instance()->client_close_ptr(client) == 0);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

int jackbridge_client_name_size()
{
    if (jackbridge_instance()->client_name_size_ptr != NULL)
        return jackbridge_instance()->client_name_size_ptr();
    return 33;
}

const char* jackbridge_get_client_name(jack_client_t* client)
{
    if (jackbridge_instance()->get_client_name_ptr != NULL)
        return jackbridge_instance()->get_client_name_ptr(client);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

char* jackbridge_client_get_uuid(jack_client_t* client)
{
    if (jackbridge_instance()->client_get_uuid_ptr != NULL)
        return jackbridge_instance()->client_get_uuid_ptr(client);
    return NULL;
}

char* jackbridge_get_uuid_for_client_name(jack_client_t* client, const char* name)
{
    if (jackbridge_instance()->get_uuid_for_client_name_ptr != NULL)
        return jackbridge_instance()->get_uuid_for_client_name_ptr(client, name);
    return NULL;
}

char* jackbridge_get_client_name_by_uuid(jack_client_t* client, const char* uuid)
{
    if (jackbridge_instance()->get_client_name_by_uuid_ptr != NULL)
        return jackbridge_instance()->get_client_name_by_uuid_ptr(client, uuid);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_uuid_parse(const char* buf, jack_uuid_t* uuid)
{
    if (jackbridge_instance()->uuid_parse_ptr != NULL)
        return jackbridge_instance()->uuid_parse_ptr(buf, uuid) == 0;
    return false;
}

void jackbridge_uuid_unparse(jack_uuid_t uuid, char buf[JACK_UUID_STRING_SIZE])
{
    if (jackbridge_instance()->uuid_unparse_ptr != NULL)
        return jackbridge_instance()->uuid_unparse_ptr(uuid, buf);
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_activate(jack_client_t* client)
{
    if (jackbridge_instance()->activate_ptr != NULL)
        return (jackbridge_instance()->activate_ptr(client) == 0);
    return false;
}

bool jackbridge_deactivate(jack_client_t* client)
{
    if (jackbridge_instance()->deactivate_ptr != NULL)
        return (jackbridge_instance()->deactivate_ptr(client) == 0);
    return false;
}

bool jackbridge_is_realtime(jack_client_t* client)
{
    if (jackbridge_instance()->is_realtime_ptr != NULL)
        return jackbridge_instance()->is_realtime_ptr(client);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_set_thread_init_callback(jack_client_t* client, JackThreadInitCallback thread_init_callback, void* arg)
{
    if (jackbridge_instance()->set_thread_init_callback_ptr != NULL)
        return (jackbridge_instance()->set_thread_init_callback_ptr(client, thread_init_callback, arg) == 0);
    return false;
}

void jackbridge_on_shutdown(jack_client_t* client, JackShutdownCallback shutdown_callback, void* arg)
{
    if (jackbridge_instance()->on_shutdown_ptr != NULL)
        jackbridge_instance()->on_shutdown_ptr(client, shutdown_callback, arg);
}

void jackbridge_on_info_shutdown(jack_client_t* client, JackInfoShutdownCallback shutdown_callback, void* arg)
{
    if (jackbridge_instance()->on_info_shutdown_ptr != NULL)
        jackbridge_instance()->on_info_shutdown_ptr(client, shutdown_callback, arg);
}

bool jackbridge_set_process_callback(jack_client_t* client, JackProcessCallback process_callback, void* arg)
{
    if (jackbridge_instance()->set_process_callback_ptr != NULL)
        return (jackbridge_instance()->set_process_callback_ptr(client, process_callback, arg) == 0);
    return false;
}

bool jackbridge_set_freewheel_callback(jack_client_t* client, JackFreewheelCallback freewheel_callback, void* arg)
{
    if (jackbridge_instance()->set_freewheel_callback_ptr != NULL)
        return (jackbridge_instance()->set_freewheel_callback_ptr(client, freewheel_callback, arg) == 0);
    return false;
}

bool jackbridge_set_buffer_size_callback(jack_client_t* client, JackBufferSizeCallback bufsize_callback, void* arg)
{
    if (jackbridge_instance()->set_buffer_size_callback_ptr != NULL)
        return (jackbridge_instance()->set_buffer_size_callback_ptr(client, bufsize_callback, arg) == 0);
    return false;
}

bool jackbridge_set_sample_rate_callback(jack_client_t* client, JackSampleRateCallback srate_callback, void* arg)
{
    if (jackbridge_instance()->set_sample_rate_callback_ptr != NULL)
        return (jackbridge_instance()->set_sample_rate_callback_ptr(client, srate_callback, arg) == 0);
    return false;
}

bool jackbridge_set_client_registration_callback(jack_client_t* client, JackClientRegistrationCallback registration_callback, void* arg)
{
    if (jackbridge_instance()->set_client_registration_callback_ptr != NULL)
        return (jackbridge_instance()->set_client_registration_callback_ptr(client, registration_callback, arg) == 0);
    return false;
}

bool jackbridge_set_port_registration_callback(jack_client_t* client, JackPortRegistrationCallback registration_callback, void *arg)
{
    if (jackbridge_instance()->set_port_registration_callback_ptr != NULL)
        return (jackbridge_instance()->set_port_registration_callback_ptr(client, registration_callback, arg) == 0);
    return false;
}

bool jackbridge_set_port_rename_callback(jack_client_t* client, JackPortRenameCallback rename_callback, void* arg)
{
    if (jackbridge_instance()->set_port_rename_callback_ptr != NULL)
        return (jackbridge_instance()->set_port_rename_callback_ptr(client, rename_callback, arg) == 0);
    return false;
}

bool jackbridge_set_port_connect_callback(jack_client_t* client, JackPortConnectCallback connect_callback, void* arg)
{
    if (jackbridge_instance()->set_port_connect_callback_ptr != NULL)
        return (jackbridge_instance()->set_port_connect_callback_ptr(client, connect_callback, arg) == 0);
    return false;
}

bool jackbridge_set_graph_order_callback(jack_client_t* client, JackGraphOrderCallback graph_callback, void* arg)
{
    if (jackbridge_instance()->set_graph_order_callback_ptr != NULL)
        return (jackbridge_instance()->set_graph_order_callback_ptr(client, graph_callback, arg) == 0);
    return false;
}

bool jackbridge_set_xrun_callback(jack_client_t* client, JackXRunCallback xrun_callback, void* arg)
{
    if (jackbridge_instance()->set_xrun_callback_ptr != NULL)
        return (jackbridge_instance()->set_xrun_callback_ptr(client, xrun_callback, arg) == 0);
    return false;
}

bool jackbridge_set_latency_callback(jack_client_t* client, JackLatencyCallback latency_callback, void* arg)
{
    if (jackbridge_instance()->set_latency_callback_ptr != NULL)
        return (jackbridge_instance()->set_latency_callback_ptr(client, latency_callback, arg) == 0);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_set_freewheel(jack_client_t* client, bool onoff)
{
    if (jackbridge_instance()->set_freewheel_ptr != NULL)
        return jackbridge_instance()->set_freewheel_ptr(client, onoff);
    return false;
}

bool jackbridge_set_buffer_size(jack_client_t* client, jack_nframes_t nframes)
{
    if (jackbridge_instance()->set_buffer_size_ptr != NULL)
        return jackbridge_instance()->set_buffer_size_ptr(client, nframes);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

jack_nframes_t jackbridge_get_sample_rate(jack_client_t* client)
{
    if (jackbridge_instance()->get_sample_rate_ptr != NULL)
        return jackbridge_instance()->get_sample_rate_ptr(client);
    return 0;
}

jack_nframes_t jackbridge_get_buffer_size(jack_client_t* client)
{
    if (jackbridge_instance()->get_buffer_size_ptr != NULL)
        return jackbridge_instance()->get_buffer_size_ptr(client);
    return 0;
}

float jackbridge_cpu_load(jack_client_t* client)
{
    if (jackbridge_instance()->cpu_load_ptr != NULL)
        return jackbridge_instance()->cpu_load_ptr(client);
    return 0.0f;
}

// --------------------------------------------------------------------------------------------------------------------

jack_port_t* jackbridge_port_register(jack_client_t* client, const char* port_name, const char* type, uint64_t flags, uint64_t buffer_size)
{
    if (jackbridge_instance()->port_register_ptr != NULL)
        return jackbridge_instance()->port_register_ptr(client, port_name, type, (ulong)flags, (ulong)buffer_size);
    return NULL;
}

bool jackbridge_port_unregister(jack_client_t* client, jack_port_t* port)
{
    if (jackbridge_instance()->port_unregister_ptr != NULL)
        return (jackbridge_instance()->port_unregister_ptr(client, port) == 0);
    return false;
}

void* jackbridge_port_get_buffer(jack_port_t* port, jack_nframes_t nframes)
{
    if (jackbridge_instance()->port_get_buffer_ptr != NULL)
        return jackbridge_instance()->port_get_buffer_ptr(port, nframes);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

const char* jackbridge_port_name(const jack_port_t* port)
{
    if (jackbridge_instance()->port_name_ptr != NULL)
        return jackbridge_instance()->port_name_ptr(port);
    return NULL;
}

jack_uuid_t jackbridge_port_uuid(const jack_port_t* port)
{
    if (jackbridge_instance()->port_uuid_ptr != NULL)
        return jackbridge_instance()->port_uuid_ptr(port);
    return 0;
}

const char* jackbridge_port_short_name(const jack_port_t* port)
{
    if (jackbridge_instance()->port_short_name_ptr != NULL)
        return jackbridge_instance()->port_short_name_ptr(port);
    return NULL;
}

int jackbridge_port_flags(const jack_port_t* port)
{
    if (jackbridge_instance()->port_flags_ptr != NULL)
        return jackbridge_instance()->port_flags_ptr(port);
    return 0x0;
}

const char* jackbridge_port_type(const jack_port_t* port)
{
    if (jackbridge_instance()->port_type_ptr != NULL)
        return jackbridge_instance()->port_type_ptr(port);
    return NULL;
}

bool jackbridge_port_is_mine(const jack_client_t* client, const jack_port_t* port)
{
    if (jackbridge_instance()->port_is_mine_ptr != NULL)
        return jackbridge_instance()->port_is_mine_ptr(client, port);
    return false;
}

int jackbridge_port_connected(const jack_port_t* port)
{
    if (jackbridge_instance()->port_connected_ptr != NULL)
        return jackbridge_instance()->port_connected_ptr(port);
    return 0;
}

bool jackbridge_port_connected_to(const jack_port_t* port, const char* port_name)
{
    if (jackbridge_instance()->port_connected_to_ptr != NULL)
        return jackbridge_instance()->port_connected_to_ptr(port, port_name);
    return false;
}

const char** jackbridge_port_get_connections(const jack_port_t* port)
{
    if (jackbridge_instance()->port_get_connections_ptr != NULL)
        return jackbridge_instance()->port_get_connections_ptr(port);
    return NULL;
}

const char** jackbridge_port_get_all_connections(const jack_client_t* client, const jack_port_t* port)
{
    if (jackbridge_instance()->port_get_all_connections_ptr != NULL)
        return jackbridge_instance()->port_get_all_connections_ptr(client, port);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_port_rename(jack_client_t* client, jack_port_t* port, const char* port_name)
{
    // Try new API first
    if (jackbridge_instance()->port_rename_ptr != NULL)
        return (jackbridge_instance()->port_rename_ptr(client, port, port_name) == 0);
    // Try old API if using JACK2
    if (jackbridge_instance()->get_version_string_ptr != NULL && jackbridge_instance()->port_set_name_ptr != NULL)
        return (jackbridge_instance()->port_set_name_ptr(port, port_name) == 0);
    return false;
}

bool jackbridge_port_set_alias(jack_port_t* port, const char* alias)
{
    if (jackbridge_instance()->port_set_alias_ptr != NULL)
        return (jackbridge_instance()->port_set_alias_ptr(port, alias) == 0);
    return false;
}

bool jackbridge_port_unset_alias(jack_port_t* port, const char* alias)
{
    if (jackbridge_instance()->port_unset_alias_ptr != NULL)
        return (jackbridge_instance()->port_unset_alias_ptr(port, alias) == 0);
    return false;
}

int jackbridge_port_get_aliases(const jack_port_t* port, char* const aliases[2])
{
    if (jackbridge_instance()->port_get_aliases_ptr != NULL)
        return jackbridge_instance()->port_get_aliases_ptr(port, aliases);
    return 0;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_port_request_monitor(jack_port_t* port, bool onoff)
{
    if (jackbridge_instance()->port_request_monitor_ptr != NULL)
        return (jackbridge_instance()->port_request_monitor_ptr(port, onoff) == 0);
    return false;
}

bool jackbridge_port_request_monitor_by_name(jack_client_t* client, const char* port_name, bool onoff)
{
    if (jackbridge_instance()->port_request_monitor_by_name_ptr != NULL)
        return (jackbridge_instance()->port_request_monitor_by_name_ptr(client, port_name, onoff) == 0);
    return false;
}

bool jackbridge_port_ensure_monitor(jack_port_t* port, bool onoff)
{
    if (jackbridge_instance()->port_ensure_monitor_ptr != NULL)
        return (jackbridge_instance()->port_ensure_monitor_ptr(port, onoff) == 0);
    return false;
}

bool jackbridge_port_monitoring_input(jack_port_t* port)
{
    if (jackbridge_instance()->port_monitoring_input_ptr != NULL)
        return jackbridge_instance()->port_monitoring_input_ptr(port);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_connect(jack_client_t* client, const char* source_port, const char* destination_port)
{
    if (jackbridge_instance()->connect_ptr != NULL)
    {
        const int ret = jackbridge_instance()->connect_ptr(client, source_port, destination_port);
        return ret == 0 || ret == EEXIST;
    }
    return false;
}

bool jackbridge_disconnect(jack_client_t* client, const char* source_port, const char* destination_port)
{
    if (jackbridge_instance()->disconnect_ptr != NULL)
        return (jackbridge_instance()->disconnect_ptr(client, source_port, destination_port) == 0);
    return false;
}

bool jackbridge_port_disconnect(jack_client_t* client, jack_port_t* port)
{
    if (jackbridge_instance()->port_disconnect_ptr != NULL)
        return (jackbridge_instance()->port_disconnect_ptr(client, port) == 0);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

int jackbridge_port_name_size()
{
    if (jackbridge_instance()->port_name_size_ptr != NULL)
        return jackbridge_instance()->port_name_size_ptr();
    return 256;
}

int jackbridge_port_type_size()
{
    if (jackbridge_instance()->port_type_size_ptr != NULL)
        return jackbridge_instance()->port_type_size_ptr();
    return 32;
}

uint32_t jackbridge_port_type_get_buffer_size(jack_client_t* client, const char* port_type)
{
    if (jackbridge_instance()->port_type_get_buffer_size_ptr != NULL)
        return (uint32_t)jackbridge_instance()->port_type_get_buffer_size_ptr(client, port_type);
    return 0;
}

// --------------------------------------------------------------------------------------------------------------------

void jackbridge_port_get_latency_range(jack_port_t* port, uint32_t mode, jack_latency_range_t* range)
{
    if (jackbridge_instance()->port_get_latency_range_ptr != NULL)
        return jackbridge_instance()->port_get_latency_range_ptr(port, (jack_latency_callback_mode_t)mode, range);
    range->min = 0;
    range->max = 0;
}

void jackbridge_port_set_latency_range(jack_port_t* port, uint32_t mode, jack_latency_range_t* range)
{
    if (jackbridge_instance()->port_set_latency_range_ptr != NULL)
        jackbridge_instance()->port_set_latency_range_ptr(port, (jack_latency_callback_mode_t)mode, range);
}

bool jackbridge_recompute_total_latencies(jack_client_t* client)
{
    if (jackbridge_instance()->recompute_total_latencies_ptr != NULL)
        return (jackbridge_instance()->recompute_total_latencies_ptr(client) == 0);
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

const char** jackbridge_get_ports(jack_client_t* client, const char* port_name_pattern, const char* type_name_pattern, uint64_t flags)
{
    if (jackbridge_instance()->get_ports_ptr != NULL)
        return jackbridge_instance()->get_ports_ptr(client, port_name_pattern, type_name_pattern, (ulong)flags);
    return NULL;
}

jack_port_t* jackbridge_port_by_name(jack_client_t* client, const char* port_name)
{
    if (jackbridge_instance()->port_by_name_ptr != NULL)
        return jackbridge_instance()->port_by_name_ptr(client, port_name);
    return NULL;
}

jack_port_t* jackbridge_port_by_id(jack_client_t* client, jack_port_id_t port_id)
{
    if (jackbridge_instance()->port_by_id_ptr != NULL)
        return jackbridge_instance()->port_by_id_ptr(client, port_id);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

void jackbridge_free(void* ptr)
{
    if (jackbridge_instance()->free_ptr != NULL)
        return jackbridge_instance()->free_ptr(ptr);
    free(ptr);
}

// --------------------------------------------------------------------------------------------------------------------

uint32_t jackbridge_midi_get_event_count(void* port_buffer)
{
    if (jackbridge_instance()->midi_get_event_count_ptr != NULL)
        return jackbridge_instance()->midi_get_event_count_ptr(port_buffer);
    return 0;
}

bool jackbridge_midi_event_get(jack_midi_event_t* event, void* port_buffer, uint32_t event_index)
{
    if (jackbridge_instance()->midi_event_get_ptr != NULL)
        return (jackbridge_instance()->midi_event_get_ptr(event, port_buffer, event_index) == 0);
    return false;
}

void jackbridge_midi_clear_buffer(void* port_buffer)
{
    if (jackbridge_instance()->midi_clear_buffer_ptr != NULL)
        jackbridge_instance()->midi_clear_buffer_ptr(port_buffer);
}

bool jackbridge_midi_event_write(void* port_buffer, jack_nframes_t time, const jack_midi_data_t* data, uint32_t data_size)
{
    if (jackbridge_instance()->midi_event_write_ptr != NULL)
        return (jackbridge_instance()->midi_event_write_ptr(port_buffer, time, data, data_size) == 0);
    return false;
}

jack_midi_data_t* jackbridge_midi_event_reserve(void* port_buffer, jack_nframes_t time, uint32_t data_size)
{
    if (jackbridge_instance()->midi_event_reserve_ptr != NULL)
        return jackbridge_instance()->midi_event_reserve_ptr(port_buffer, time, data_size);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_release_timebase(jack_client_t* client)
{
    if (jackbridge_instance()->release_timebase_ptr != NULL)
        return (jackbridge_instance()->release_timebase_ptr(client) == 0);
    return false;
}

bool jackbridge_set_sync_callback(jack_client_t* client, JackSyncCallback sync_callback, void* arg)
{
    if (jackbridge_instance()->set_sync_callback_ptr != NULL)
        return (jackbridge_instance()->set_sync_callback_ptr(client, sync_callback, arg) == 0);
    return false;
}

bool jackbridge_set_sync_timeout(jack_client_t* client, jack_time_t timeout)
{
    if (jackbridge_instance()->set_sync_timeout_ptr != NULL)
        return (jackbridge_instance()->set_sync_timeout_ptr(client, timeout) == 0);
    return false;
}

bool jackbridge_set_timebase_callback(jack_client_t* client, bool conditional, JackTimebaseCallback timebase_callback, void* arg)
{
    if (jackbridge_instance()->set_timebase_callback_ptr != NULL)
        return (jackbridge_instance()->set_timebase_callback_ptr(client, conditional, timebase_callback, arg) == 0);
    return false;
}

bool jackbridge_transport_locate(jack_client_t* client, jack_nframes_t frame)
{
    if (jackbridge_instance()->transport_locate_ptr != NULL)
        return (jackbridge_instance()->transport_locate_ptr(client, frame) == 0);
    return false;
}

uint32_t jackbridge_transport_query(const jack_client_t* client, jack_position_t* pos)
{
    if (jackbridge_instance()->transport_query_ptr != NULL)
        return jackbridge_instance()->transport_query_ptr(client, pos);
    if (pos != NULL)
    {
        // invalidate
        memset(pos, 0, sizeof(*pos));
        pos->unique_1 = 0;
        pos->unique_2 = 1;
    }
    return JackTransportStopped;
}

jack_nframes_t jackbridge_get_current_transport_frame(const jack_client_t* client)
{
    if (jackbridge_instance()->get_current_transport_frame_ptr != NULL)
        return jackbridge_instance()->get_current_transport_frame_ptr(client);
    return 0;
}

bool jackbridge_transport_reposition(jack_client_t* client, const jack_position_t* pos)
{
    if (jackbridge_instance()->transport_reposition_ptr != NULL)
        return (jackbridge_instance()->transport_reposition_ptr(client, pos) == 0);
    return false;
}

void jackbridge_transport_start(jack_client_t* client)
{
    if (jackbridge_instance()->transport_start_ptr != NULL)
        jackbridge_instance()->transport_start_ptr(client);
}

void jackbridge_transport_stop(jack_client_t* client)
{
    if (jackbridge_instance()->transport_stop_ptr != NULL)
        jackbridge_instance()->transport_stop_ptr(client);
}

// --------------------------------------------------------------------------------------------------------------------

bool jackbridge_set_property(jack_client_t* client, jack_uuid_t subject, const char* key, const char* value, const char* type)
{
    if (jackbridge_instance()->set_property_ptr != NULL)
        return (jackbridge_instance()->set_property_ptr(client, subject, key, value, type) == 0);
    return false;
}

bool jackbridge_get_property(jack_uuid_t subject, const char* key, char** value, char** type)
{
    if (jackbridge_instance()->get_property_ptr != NULL)
        return (jackbridge_instance()->get_property_ptr(subject, key, value, type) == 0);
    return false;
}

void jackbridge_free_description(jack_description_t* desc, bool free_description_itself)
{
    if (jackbridge_instance()->free_description_ptr != NULL)
        jackbridge_instance()->free_description_ptr(desc, free_description_itself);
}

bool jackbridge_get_properties(jack_uuid_t subject, jack_description_t* desc)
{
    if (jackbridge_instance()->get_properties_ptr != NULL)
        return (jackbridge_instance()->get_properties_ptr(subject, desc) == 0);
    return false;
}

bool jackbridge_get_all_properties(jack_description_t** descs)
{
    if (jackbridge_instance()->get_all_properties_ptr != NULL)
        return (jackbridge_instance()->get_all_properties_ptr(descs) == 0);
    return false;
}

bool jackbridge_remove_property(jack_client_t* client, jack_uuid_t subject, const char* key)
{
    if (jackbridge_instance()->remove_property_ptr != NULL)
        return (jackbridge_instance()->remove_property_ptr(client, subject, key) == 0);
    return false;
}

int jackbridge_remove_properties(jack_client_t* client, jack_uuid_t subject)
{
    if (jackbridge_instance()->remove_properties_ptr != NULL)
        return jackbridge_instance()->remove_properties_ptr(client, subject);
    return 0;
}

bool jackbridge_remove_all_properties(jack_client_t* client)
{
    if (jackbridge_instance()->remove_all_properties_ptr != NULL)
        return (jackbridge_instance()->remove_all_properties_ptr(client) == 0);
    return false;
}

bool jackbridge_set_property_change_callback(jack_client_t* client, JackPropertyChangeCallback callback, void* arg)
{
    if (jackbridge_instance()->set_property_change_callback_ptr != NULL)
        return (jackbridge_instance()->set_property_change_callback_ptr(client, callback, arg) == 0);
    return false;
}

void jackbridge_set_thread_creator(JackThreadCreator creator)
{
    if (jackbridge_instance()->set_thread_creator_ptr != NULL)
        jackbridge_instance()->set_thread_creator_ptr(creator);
}

bool jackbridge_set_process_thread(jack_client_t* client, JackThreadCallback callback, void* arg)
{
    if (jackbridge_instance()->set_process_thread_ptr != NULL)
        return (jackbridge_instance()->set_process_thread_ptr(client, callback, arg) == 0);
    return false;
}

jack_nframes_t jackbridge_cycle_wait(jack_client_t* client)
{
    if (jackbridge_instance()->cycle_wait_ptr != NULL)
        return jackbridge_instance()->cycle_wait_ptr(client);
    return 0;
}

void jackbridge_cycle_signal(jack_client_t* client, int status)
{
    if (jackbridge_instance()->cycle_signal_ptr != NULL)
        jackbridge_instance()->cycle_signal_ptr(client, status);
}

jack_nframes_t jackbridge_port_get_latency(jack_port_t* port)
{
    if (jackbridge_instance()->port_get_latency_ptr != NULL)
        jackbridge_instance()->port_get_latency_ptr(port);
    return 0;
}

jack_nframes_t jackbridge_frame_time(const jack_client_t* client)
{
    if (jackbridge_instance()->frame_time_ptr != NULL)
        jackbridge_instance()->frame_time_ptr(client);
    return 0;
}
