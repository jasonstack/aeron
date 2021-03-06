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

#include <string.h>
#include "aeron_system_counters.h"
#include "aeron_alloc.h"

static aeron_system_counter_t system_counters[] =
    {
        { "Bytes sent", AERON_SYSTEM_COUNTER_BYTES_SENT },
        { "Bytes received", AERON_SYSTEM_COUNTER_BYTES_RECEIVED },
        { "Failed offers to ReceiverProxy", AERON_SYSTEM_COUNTER_RECEIVER_PROXY_FAILS },
        { "Failed offers to SenderProxy", AERON_SYSTEM_COUNTER_SENDER_PROXY_FAILS },
        { "Failed offers to DriverConductorProxy", AERON_SYSTEM_COUNTER_CONDUCTOR_PROXY_FAILS },
        { "NAKs sent", AERON_SYSTEM_COUNTER_NAK_MESSAGES_SENT },
        { "NAKs received", AERON_SYSTEM_COUNTER_NAK_MESSAGES_RECEIVED },
        { "Status Messages sent", AERON_SYSTEM_COUNTER_STATUS_MESSAGES_SENT },
        { "Status Messages received", AERON_SYSTEM_COUNTER_STATUS_MESSAGES_RECEIVED },
        { "Heartbeats sent", AERON_SYSTEM_COUNTER_HEARTBEATS_SENT },
        { "Heartbeats received", AERON_SYSTEM_COUNTER_HEARTBEATS_RECEIVED },
        { "Retransmits sent", AERON_SYSTEM_COUNTER_RETRANSMITS_SENT },
        { "Flow control under runs", AERON_SYSTEM_COUNTER_FLOW_CONTROL_UNDER_RUNS },
        { "Flow control over runs", AERON_SYSTEM_COUNTER_FLOW_CONTROL_OVER_RUNS },
        { "Invalid packets", AERON_SYSTEM_COUNTER_INVALID_PACKETS },
        { "Errors", AERON_SYSTEM_COUNTER_ERRORS },
        { "Short sends", AERON_SYSTEM_COUNTER_SHORT_SENDS },
        { "Client keep-alives", AERON_SYSTEM_COUNTER_CLIENT_KEEP_ALIVES },
        { "Sender flow control limits applied", AERON_SYSTEM_COUNTER_SENDER_FLOW_CONTROL_LIMITS },
        { "Unblocked Publications", AERON_SYSTEM_COUNTER_UNBLOCKED_PUBLICATIONS },
        { "Unblocked Control Commands", AERON_SYSTEM_COUNTER_UNBLOCKED_COMMANDS },
        { "Possible TTL Asymmetry", AERON_SYSTEM_COUNTER_POSSIBLE_TTL_ASYMMETRY },
        { "ControllableIdleStrategy status", AERON_SYSTEM_COUNTER_CONTROLLABLE_IDLE_STRATEGY },
        { "Loss gap fills", AERON_SYSTEM_COUNTER_LOSS_GAP_FILLS}
    };

static size_t num_system_counters = sizeof(system_counters)/sizeof(aeron_system_counter_t);

static void system_counter_key_func(uint8_t *key, size_t key_max_length, void *clientd)
{
    int32_t key_value = *(int32_t *)(clientd);

    *(int32_t *)key = key_value;
}

int aeron_system_counters_init(aeron_system_counters_t *counters, aeron_counters_manager_t *manager)
{
    if (NULL == counters || NULL == manager)
    {
        /* TODO: EINVAL */
        return -1;
    }

    counters->manager = manager;
    if (aeron_alloc((void **)&counters->counter_ids, sizeof(int32_t) * num_system_counters) < 0)
    {
        return -1;
    }

    for (int32_t i = 0; i < (int32_t)num_system_counters; i++)
    {
        if ((counters->counter_ids[i] =
            aeron_counters_manager_allocate(
                manager,
                system_counters[i].label,
                strlen(system_counters[i].label),
                AERON_SYSTEM_COUNTER_TYPE_ID,
                system_counter_key_func,
                &(system_counters[i].id))) < 0)
        {
            return -1;
        }
    }

    return 0;
}

void aeron_system_counters_close(aeron_system_counters_t *counters)
{
    for (int32_t i = 0; i < (int32_t)num_system_counters; i++)
    {
        aeron_counters_manager_free(counters->manager, counters->counter_ids[i]);
    }

    aeron_free(counters->counter_ids);
}

extern int64_t *aeron_system_counter_addr(aeron_system_counters_t *counters, aeron_system_counter_enum_t type);
