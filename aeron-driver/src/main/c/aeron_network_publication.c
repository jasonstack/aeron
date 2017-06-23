/*
 * Copyright 2014 - 2017 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <string.h>
#include "util/aeron_netutil.h"
#include "util/aeron_error.h"
#include "aeron_network_publication.h"
#include "aeron_alloc.h"
#include "media/aeron_send_channel_endpoint.h"

int aeron_network_publication_create(
    aeron_network_publication_t **publication,
    aeron_send_channel_endpoint_t *endpoint,
    aeron_driver_context_t *context,
    int64_t registration_id,
    int32_t session_id,
    int32_t stream_id,
    int32_t initial_term_id,
    size_t mtu_length,
    aeron_position_t *pub_lmt_position,
    aeron_position_t *snd_pos_position,
    aeron_position_t *snd_lmt_position,
    size_t term_buffer_length,
    bool is_exclusive,
    aeron_system_counters_t *system_counters)
{
    char path[AERON_MAX_PATH];
    int path_length =
        aeron_network_publication_location(
            path,
            sizeof(path),
            context->aeron_dir,
            endpoint->conductor_fields.udp_channel->canonical_form,
            session_id,
            stream_id,
            registration_id);
    aeron_network_publication_t *_pub = NULL;
    const uint64_t usable_fs_space = context->usable_fs_space_func(context->aeron_dir);
    const uint64_t log_length = AERON_LOGBUFFER_COMPUTE_LOG_LENGTH(term_buffer_length);
    const int64_t now_ns = context->nano_clock();

    *publication = NULL;

    if (usable_fs_space < log_length)
    {
        aeron_set_err(ENOSPC, "Insufficient usable storage for new log of length=%d in %s", log_length, context->aeron_dir);
        return -1;
    }

    if (aeron_alloc((void **)&_pub, sizeof(aeron_network_publication_t)) < 0)
    {
        aeron_set_err(ENOMEM, "%s", "Could not allocate network publication");
        return -1;
    }

    _pub->log_file_name = NULL;
    if (aeron_alloc((void **)(&_pub->log_file_name), (size_t)path_length) < 0)
    {
        aeron_free(_pub);
        aeron_set_err(ENOMEM, "%s", "Could not allocate network publication log_file_name");
        return -1;
    }

    if (context->map_raw_log_func(&_pub->mapped_raw_log, path, context->term_buffer_sparse_file, term_buffer_length) < 0)
    {
        aeron_free(_pub->log_file_name);
        aeron_free(_pub);
        aeron_set_err(aeron_errcode(), "error mapping network raw log %s: %s", path, aeron_errmsg());
        return -1;
    }
    _pub->map_raw_log_close_func = context->map_raw_log_close_func;

    strncpy(_pub->log_file_name, path, path_length);
    _pub->log_file_name_length = (size_t)path_length;
    _pub->log_meta_data = (aeron_logbuffer_metadata_t *)(_pub->mapped_raw_log.log_meta_data.addr);

    _pub->log_meta_data->term_tail_counters[0] = (int64_t)initial_term_id << 32;
    _pub->log_meta_data->initialTerm_id = initial_term_id;
    _pub->log_meta_data->mtu_length = (int32_t)mtu_length;
    _pub->log_meta_data->correlation_id = registration_id;
    _pub->log_meta_data->time_of_last_status_message = 0;
    aeron_logbuffer_fill_default_header(
        _pub->mapped_raw_log.log_meta_data.addr, session_id, stream_id, initial_term_id);

    _pub->endpoint = endpoint;
    _pub->conductor_fields.subscribeable.array = NULL;
    _pub->conductor_fields.subscribeable.length = 0;
    _pub->conductor_fields.subscribeable.capacity = 0;
    _pub->conductor_fields.managed_resource.registration_id = registration_id;
    _pub->conductor_fields.managed_resource.clientd = _pub;
    _pub->conductor_fields.managed_resource.incref = aeron_network_publication_incref;
    _pub->conductor_fields.managed_resource.decref = aeron_network_publication_decref;
    _pub->conductor_fields.has_reached_end_of_life = false;
    _pub->conductor_fields.cleaning_position = 0;
    //_pub->conductor_fields.trip_limit = 0;
    //_pub->conductor_fields.consumer_position = 0;
    _pub->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_ACTIVE;
    _pub->conductor_fields.refcnt = 1;
    _pub->session_id = session_id;
    _pub->stream_id = stream_id;
    _pub->pub_lmt_position.counter_id = pub_lmt_position->counter_id;
    _pub->pub_lmt_position.value_addr = pub_lmt_position->value_addr;
    _pub->snd_pos_position.counter_id = snd_pos_position->counter_id;
    _pub->snd_pos_position.value_addr = snd_pos_position->value_addr;
    _pub->snd_lmt_position.counter_id = snd_lmt_position->counter_id;
    _pub->snd_lmt_position.value_addr = snd_lmt_position->value_addr;
    _pub->initial_term_id = initial_term_id;
    _pub->term_length_mask = (int32_t)term_buffer_length - 1;
    _pub->position_bits_to_shift = (size_t)aeron_number_of_trailing_zeroes((int32_t)term_buffer_length);
    _pub->mtu_length = mtu_length;
    _pub->term_window_length = (int64_t)aeron_network_publication_term_window_length(context, term_buffer_length);
    //_pub->trip_gain = _pub->term_window_length / 8;
    _pub->linger_timeout_ns = (int64_t)context->publication_linger_timeout_ns;
    _pub->time_of_last_send_or_heartbeat_ns = now_ns - AERON_NETWORK_PUBLICATION_HEARTBEAT_TIMEOUT_NS - 1;
    _pub->time_of_last_setup_ns = now_ns - AERON_NETWORK_PUBLICATION_SETUP_TIMEOUT_NS - 1;
    _pub->is_exclusive = is_exclusive;
    _pub->should_send_setup_frame = true;
    _pub->is_connected = false;

    //_pub->conductor_fields.consumer_position = aeron_ipc_publication_producer_position(_pub);

    _pub->short_sends_counter = aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_SHORT_SENDS);

    *publication = _pub;
    return 0;
}

void aeron_network_publication_close(aeron_counters_manager_t *counters_manager, aeron_network_publication_t *publication)
{
    aeron_subscribeable_t *subscribeable = &publication->conductor_fields.subscribeable;

    aeron_counters_manager_free(counters_manager, (int32_t)publication->pub_lmt_position.counter_id);

    for (size_t i = 0, length = subscribeable->length; i < length; i++)
    {
        aeron_counters_manager_free(counters_manager, (int32_t)subscribeable->array[i].counter_id);
    }

    if (NULL != publication)
    {
        publication->map_raw_log_close_func(&publication->mapped_raw_log);
        aeron_free(publication->log_file_name);
    }

    aeron_free(publication);
}

void aeron_network_publication_incref(void *clientd)
{
    aeron_network_publication_t *publication = (aeron_network_publication_t *)clientd;

    publication->conductor_fields.refcnt++;
}

void aeron_network_publication_decref(void *clientd)
{
    aeron_network_publication_t *publication = (aeron_network_publication_t *)clientd;
    int32_t ref_count = --publication->conductor_fields.refcnt;

    if (0 == ref_count)
    {
        //publication->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_INACTIVE;
        //AERON_PUT_ORDERED(publication->log_meta_data->end_of_stream_position, aeron_ipc_publication_producer_position(publication));
    }
}

int aeron_network_publication_setup_message_check(
    aeron_network_publication_t *publication, int64_t now_ns, int32_t active_term_id, int32_t term_offset)
{
    int result = 0;

    if (now_ns > (publication->time_of_last_setup_ns + AERON_NETWORK_PUBLICATION_SETUP_TIMEOUT_NS))
    {
        uint8_t setup_buffer[sizeof(aeron_setup_header_t)];
        aeron_setup_header_t *setup_header = (aeron_setup_header_t *)setup_buffer;
        struct iovec iov;
        struct msghdr msghdr;

        setup_header->frame_header.frame_length = sizeof(aeron_setup_header_t);
        setup_header->frame_header.version = AERON_FRAME_HEADER_VERSION;
        setup_header->frame_header.flags = 0;
        setup_header->frame_header.type = AERON_HDR_TYPE_SETUP;
        setup_header->term_offset = term_offset;
        setup_header->session_id = publication->session_id;
        setup_header->stream_id = publication->stream_id;
        setup_header->initial_term_id = publication->initial_term_id;
        setup_header->active_term_id = active_term_id;
        setup_header->term_length = publication->term_length_mask + 1;
        setup_header->mtu = (int32_t)publication->mtu_length;
        setup_header->ttl = publication->endpoint->conductor_fields.udp_channel->multicast_ttl;

        iov.iov_base = setup_buffer;
        iov.iov_len = sizeof(aeron_setup_header_t);
        msghdr.msg_iov = &iov;
        msghdr.msg_iovlen = 1;
        msghdr.msg_flags = 0;

        if ((result = aeron_send_channel_sendmsg(publication->endpoint, &msghdr)) != (int)iov.iov_len)
        {
            if (result >= 0)
            {
                aeron_counter_increment(publication->short_sends_counter, 1);
            }
        }

        publication->time_of_last_setup_ns = now_ns;
        publication->time_of_last_send_or_heartbeat_ns = now_ns;

        bool is_connected;
        AERON_GET_VOLATILE(is_connected, publication->is_connected);
        if (is_connected)
        {
            publication->should_send_setup_frame = false;
        }
    }

    return result;
}

int aeron_network_publication_send_data(
    aeron_network_publication_t *publication, int64_t now_ns, int64_t snd_pos, int32_t term_offset)
{
    int bytes_sent = 0;
    int32_t available_window = (int32_t)(aeron_counter_get(publication->snd_lmt_position.value_addr) - snd_pos);
    if (available_window > 0)
    {
        /* TODO: finish */
    }

    return bytes_sent;
}

int aeron_network_publication_send(aeron_network_publication_t *publication, int64_t now_ns)
{
    int64_t snd_pos = aeron_counter_get(publication->snd_pos_position.value_addr);
    int32_t active_term_id =
        aeron_logbuffer_compute_term_id_from_position(
            snd_pos, publication->position_bits_to_shift, publication->initial_term_id);
    int32_t term_offset = (int32_t)snd_pos & publication->term_length_mask;

    if (publication->should_send_setup_frame)
    {
        if (aeron_network_publication_setup_message_check(publication, now_ns, active_term_id, term_offset) < 0)
        {
            return -1;
        }
    }

    int bytes_sent = aeron_network_publication_send_data(publication, now_ns, snd_pos, term_offset);
    if (bytes_sent < 0)
    {
        return -1;
    }

    /* TODO: finish */

    return 0;
}