/** \copyright
 * Copyright (c) 2012, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file nmranet_event.h
 * This file defines the handling of NMRAnet producer/consumer events.
 *
 * @author Stuart W. Baker
 * @date 29 October 2012
 */

#ifndef _nmranet_event_h_
#define _nmranet_event_h_

#include "nmranet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_EMERGENCY_STOP    0x010100000000FFFFULL /**< stop of all operations */
#define EVENT_NEW_LOG_ENTRY     0x010100000000FFF8ULL /**< node recorded a new log entry */
#define EVENT_IDENT_BUTTON      0x010100000000FE00ULL /**< ident button combination pressed */
#define EVENT_DUPLICATE_NODE_ID 0x0101000000000201ULL /**< duplicate Node ID detected */
#define EVENT_DCC_CS_NODE       0x0101000000000301ULL /**< this node is a DCC command station */
#define EVENT_ROSTER_NODE       0x0101000000000302ULL /**< this node is a Roster Node */
#define EVENT_TRAIN_PROXY       0x0101000000000303ULL /**< this node is a Train Proxy */

/** Process an event packet.
 * @param mti Message Type Indicator
 * @param src source node ID, 0 if unavailable
 * @param dst destination node ID, 0 if unavailable
 * @param data NMRAnet packet data
 * @return 0 upon success
 */
int nmranet_event_packet(uint16_t mti, node_id_t src, node_id_t dst, const void *data);

/** Register for the consumption of an event with a given node.
 * @param node to register event to
 * @param event event number to register
 */
void nmranet_event_consumer(node_t node, uint64_t event);

/** Grab an event from the event queue of the node.
 * @param node to grab event from
 * @return 0 if the queue is empty, else return the event number
 */
uint64_t nmranet_event_consume(node_t node);

/** Produce an event from.
 * @param node node to produce event from
 * @param event event to produce
 */
void nmranet_event_produce(node_t node, uint64_t event);

#ifdef __cplusplus
}
#endif

#endif /* _nmranet_event_h_ */
