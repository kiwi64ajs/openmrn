/** \copyright
 * Copyright (c) 2014, Balazs Racz
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
 * \file BootloaderClient.hxx
 *
 * A client library for talking to a bootloader using MemoryConfig
 * protocol::write_stream method.
 *
 * @author Balazs Racz
 * @date 14 Dec 2014
 */

#include "nmranet/DatagramDefs.hxx"
#include "nmranet/StreamDefs.hxx"
#include "nmranet/CanDefs.hxx"
#include "nmranet/MemoryConfig.hxx"

namespace nmranet
{

struct BootloaderResponse
{
    // Response error code. Zero if request successful.
    uint16_t error_code;
    // Human-readable error string.
    string error_details;
};

struct BootloaderRequest
{
    /// Node to send the bootload request to.
    NodeHandle dst;
    /// Memory space ID to write into.
    uint8_t memory_space;
    /// Offset at which to start writing.
    uint32_t offset;
    /// Payload to write.
    string data;
    /// This structure will be filled with the returning error code, or zero if
    /// the bootloading was successful.
    BootloaderResponse *response;
};

extern int g_bootloader_timeout_sec;
int g_bootloader_timeout_sec = 3;

class BootloaderClient : public StateFlow<Buffer<BootloaderRequest>, QList<1>>
{
public:
    BootloaderClient(Node *node, DatagramService *if_datagram_service,
                     IfCan *if_can)
        : StateFlow<Buffer<BootloaderRequest>, QList<1>>(node->interface())
        , node_(node)
        , datagramService_(if_datagram_service)
        , ifCan_(if_can)
    {
    }

    Action entry() override
    {
        return allocate_and_call(STATE(got_dg_client),
                                 datagramService_->client_allocator());
    }

    DatagramService *datagram_service()
    {
        return datagramService_;
    }

    Node *node()
    {
        return node_;
    }

    const NodeHandle &dst()
    {
        return message()->data()->dst;
    }

    void response_datagram_arrived(Buffer<IncomingDatagram> *datagram)
    {
        if (responseDatagram_)
        {
            LOG(ERROR, "Multiple response datagrams arrived from the "
                       "target node.");
            responseDatagram_->unref();
        }
        responseDatagram_ = datagram;
        if (sleeping_)
        {
            timer_.trigger();
        } // else we will be woken up by the datagram client.
    }

private:
    Action got_dg_client()
    {
        dgClient_ =
            full_allocation_result(datagramService_->client_allocator());
        Buffer<NMRAnetMessage> *b;
        mainBufferPool->alloc(&b);
        DatagramPayload payload;
        payload.push_back(DatagramDefs::CONFIGURATION);
        payload.push_back(MemoryConfigDefs::COMMAND_WRITE_STREAM);
        payload.push_back(message()->data()->offset >> 24);
        payload.push_back(message()->data()->offset >> 16);
        payload.push_back(message()->data()->offset >> 8);
        payload.push_back(message()->data()->offset);
        payload.push_back(message()->data()->memory_space);
        localStreamId_ = allocate_local_stream_id();
        payload.push_back(localStreamId_);
        b->data()->reset(Defs::MTI_DATAGRAM, node_->node_id(),
                         message()->data()->dst, payload);
        b->set_done(n_.reset(this));
        dgClient_->write_datagram(b);

        responseDatagram_ = nullptr;
        sleeping_ = false;
        register_write_response_handler();
        return wait_and_call(STATE(write_request_sent));
    }

    class WriteResponseHandler : public DefaultDatagramHandler
    {
    public:
        WriteResponseHandler(BootloaderClient *parent)
            : DefaultDatagramHandler(parent->datagram_service())
            , parent_(parent)
        {
        }

        Action entry() override
        {
            IncomingDatagram *datagram = message()->data();

            if (datagram->dst != parent_->node() ||
                !parent_->node()->interface()->matching_node(parent_->dst(),
                                                             datagram->src) ||
                datagram->payload.size() < 6 ||
                datagram->payload[0] != DatagramDefs::CONFIGURATION ||
                ((datagram->payload[1] & 0xF4) !=
                 MemoryConfigDefs::COMMAND_WRITE_STREAM_REPLY))
            {
                // Uninteresting datagram.
                return respond_reject(DatagramDefs::PERMANENT_ERROR);
            }
            return respond_ok(DatagramDefs::FLAGS_NONE);
        }

        Action ok_response_sent() override
        {
            parent_->response_datagram_arrived(transfer_message());
            return exit();
        }

    private:
        BootloaderClient *parent_;
    };

    Action write_request_sent()
    {
        uint32_t dg_result = dgClient_->result();
        datagramService_->client_allocator()->typed_insert(dgClient_);

        if (responseDatagram_)
        {
            // Response has already arrived.
            return call_immediately(STATE(wait_for_response_dg));
        }
        else if (dg_result & DatagramClient::OPERATION_SUCCESS)
        {
            sleeping_ = true;
            return sleep_and_call(&timer_,
                                  SEC_TO_NSEC(g_bootloader_timeout_sec),
                                  STATE(wait_for_response_dg));
        }
        else
        {
            // Some error happened on sending the datagram.
            string error_details;
            if (dg_result & DatagramClient::DST_NOT_FOUND)
            {
                error_details = "Destination node not found.";
            }
            else if (dg_result & DatagramClient::TIMEOUT)
            {
                error_details = "Timeout waiting for destination node to ACK "
                                "the write request datagram.";
            }
            else if (dg_result & DatagramClient::PERMANENT_ERROR)
            {
                error_details = "Write request: Permanent error.";
            }
            else if (dg_result & DatagramClient::RESEND_OK)
            {
                error_details =
                    "Write request: Temporary error, please try again.";
            }
            return return_error(dg_result & 0xffff, error_details);
        }
    }

    Action wait_for_response_dg()
    {
        sleeping_ = false;
        if (!responseDatagram_)
        {
            return return_error(DatagramClient::RESEND_OK,
                                "Timed out waiting for response datagram.");
        }
        unregister_write_response_handler();
        const auto &payload = responseDatagram_->data()->payload;
        if ((payload[1] & 0xFC) ==
            MemoryConfigDefs::COMMAND_WRITE_STREAM_FAILED)
        {
            // Write error.
            uint16_t error_code = DatagramClient::PERMANENT_ERROR;
            unsigned error_ofs = 6;
            if (payload[1] == MemoryConfigDefs::COMMAND_WRITE_STREAM_FAILED)
            {
                ++error_ofs;
            }
            error_code = payload[error_ofs] << 8 | payload[error_ofs + 1];
            error_ofs += 2;
            return return_error(error_code,
                                "Write rejected " + payload.substr(error_ofs));
        }
        else if ((payload[1] & 0xFC) ==
                 MemoryConfigDefs::COMMAND_WRITE_STREAM_REPLY)
        {
            // Write OK. proceed to stream acquisition.
            responseDatagram_->unref();
            return call_immediately(STATE(initiate_stream));
        }
        else
        {
            // don't know what happened.
            return return_error(DatagramClient::PERMANENT_ERROR,
                                "internal inconsistency.");
        }
    }

    uint8_t allocate_local_stream_id()
    {
        return 0x6F;
    }

    Action return_error(uint16_t error_code, const string &error_details)
    {
        unregister_write_response_handler();
        message()->data()->response->error_code = error_code;
        message()->data()->response->error_details = error_details;
        if (responseDatagram_)
        {
            responseDatagram_->unref();
            responseDatagram_ = nullptr;
        }
        return release_and_exit();
    }

    void register_write_response_handler()
    {
        datagramService_->registry()->insert(node_, DatagramDefs::CONFIGURATION,
                                             &writeResponseHandler_);
        writeResponseRegistered_ = true;
    }

    void unregister_write_response_handler()
    {
        if (writeResponseRegistered_)
        {
            writeResponseRegistered_ = false;
            datagramService_->registry()->erase(
                node_, DatagramDefs::CONFIGURATION, &writeResponseHandler_);
        }
    }

    Action initiate_stream()
    {
        return allocate_and_call(
            node_->interface()->addressed_message_write_flow(),
            STATE(send_init_stream));
    }

    Action send_init_stream()
    {
        auto *b = get_allocation_result(
            node_->interface()->addressed_message_write_flow());
        b->data()->reset(Defs::MTI_STREAM_INITIATE_REQUEST, node_->node_id(),
                         message()->data()->dst,
                         StreamDefs::create_initiate_request(
                             StreamDefs::MAX_PAYLOAD, false, localStreamId_));
        node_->interface()->addressed_message_write_flow()->send(b);
        sleeping_ = true;
        node_->interface()->dispatcher()->register_handler(
            &streamInitiateReplyHandler_, Defs::MTI_STREAM_INITIATE_REPLY,
            Defs::MTI_EXACT);
        return sleep_and_call(&timer_, SEC_TO_NSEC(g_bootloader_timeout_sec),
                              STATE(received_init_stream));
    }

    void stream_initiate_replied(Buffer<NMRAnetMessage> *message)
    {
        if (message->data()->dstNode != node_ ||
            !node_->interface()->matching_node(dst(), message->data()->src))
        {
            // Not for me.
            return message->unref();
        }
        const auto &payload = message->data()->payload;
        if (payload.size() < 6 || payload[4] != localStreamId_)
        {
            // Talking about another stream or incorrect data.
            return message->unref();
        }
        remoteStreamId_ = payload[5];
        streamFlags_ = payload[2];
        streamAdditionalFlags_ = payload[3];
        // We save the remote alias here if we haven't got any yet.
        if (message->data()->src.alias)
        {
            this->message()->data()->dst.alias = message->data()->src.alias;
        }
        message->unref();
        timer_.trigger();
    }

    Action received_init_stream()
    {
        sleeping_ = false;
        node_->interface()->dispatcher()->unregister_handler(
            &streamInitiateReplyHandler_, Defs::MTI_STREAM_INITIATE_REPLY,
            Defs::MTI_EXACT);
        if (!(streamFlags_ & StreamDefs::FLAG_ACCEPT))
        {
            if (streamFlags_ & StreamDefs::FLAG_PERMANENT_ERROR)
            {
                return return_error(
                    DatagramDefs::PERMANENT_ERROR | streamAdditionalFlags_,
                    "Stream initiate request was denied (permanent error).");
            }
            else
            {
                return return_error(
                    Defs::ERROR_TEMPORARY | streamAdditionalFlags_,
                    "Stream initiate request was denied (temporary error).");
            }
        }
        if (!maxBufferSize_)
        {
            return return_error(DatagramDefs::PERMANENT_ERROR,
                                "Inconsistency: zero buffer length but "
                                "accepted stream request.");
        }
        availableBufferSize_ = maxBufferSize_;
        bufferOffset_ = 0;
        node_->interface()->dispatcher()->register_handler(
            &streamProceedHandler_, Defs::MTI_STREAM_PROCEED, Defs::MTI_EXACT);
        return call_immediately(STATE(send_stream_data));
    }

    Action send_stream_data()
    {
        if (bufferOffset_ >= message()->data()->data.size())
        {
            return call_immediately(STATE(close_stream));
        }
        return allocate_and_call(ifCan_->frame_write_flow(),
                                 STATE(fill_outgoing_stream_frame));
    }

    Action fill_outgoing_stream_frame()
    {
        auto *b = get_allocation_result(ifCan_->frame_write_flow());
        uint32_t can_id;
        NodeAlias local_alias =
            ifCan_->local_aliases()->lookup(node()->node_id());
        NodeAlias remote_alias = dst().alias;
        CanDefs::set_datagram_fields(&can_id, local_alias, remote_alias,
                                     CanDefs::STREAM_DATA);
        auto *frame = b->data()->mutable_frame();
        SET_CAN_FRAME_ID_EFF(*frame, can_id);
        unsigned len = 7;
        if (availableBufferSize_ < len)
        {
            len = availableBufferSize_;
        }
        frame->can_dlc = len + 1;
        frame->data[0] = remoteStreamId_;
        memcpy(&frame->data[1], &message()->data()->data[bufferOffset_], len);
        bufferOffset_ += len;
        availableBufferSize_ -= len;
        b->set_done(n_.reset(this));
        ifCan_->frame_write_flow()->send(b);

        if (availableBufferSize_)
        {
            return wait_and_call(STATE(send_stream_data));
        }
        else
        {
            return wait_and_call(STATE(wait_for_stream_proceed));
        }
    }

    Action wait_for_stream_proceed()
    {
        sleeping_ = true;
        return sleep_and_call(&timer_, SEC_TO_NSEC(g_bootloader_timeout_sec),
                              STATE(stream_proceed_timeout));
    }

    void stream_proceed_received(Buffer<NMRAnetMessage> *message)
    {
        if (message->data()->dstNode != node_ ||
            !node_->interface()->matching_node(dst(), message->data()->src))
        {
            // Not for me.
            return message->unref();
        }
        const auto &payload = message->data()->payload;
        if (payload.size() < 2 || payload[0] != localStreamId_)
        {
            // Talking about another stream or incorrect data.
            return message->unref();
        }
        availableBufferSize_ += maxBufferSize_;
        message->unref();
        if (sleeping_)
        {
            timer_.trigger();
        }
    }

    Action stream_proceed_timeout()
    {
        sleeping_ = false;
        if (!availableBufferSize_) // no proceed arrived
        {
            ///@TODO(balazs.racz) somehow merge these two actions: remember
            /// that we timed out and close the stream.
            return return_error(
                Defs::ERROR_TEMPORARY,
                "Times out waiting for stream proceed message.");
            return call_immediately(STATE(close_stream));
        }
        return call_immediately(STATE(send_stream_data));
    }

    Action close_stream()
    {
        node_->interface()->dispatcher()->unregister_handler(
            &streamProceedHandler_, Defs::MTI_STREAM_PROCEED, Defs::MTI_EXACT);
        return release_and_exit();
    }

private:
    Node *node_;
    DatagramService *datagramService_;
    IfCan *ifCan_;
    DatagramClient *dgClient_ = nullptr;
    Buffer<IncomingDatagram> *responseDatagram_ = nullptr;
    uint8_t localStreamId_;
    uint8_t remoteStreamId_;
    // maximum size of pending stream data.
    uint16_t maxBufferSize_;
    // Flags from the stream initiate response.
    uint8_t streamFlags_;
    // Additional flags from the initiate response.
    uint8_t streamAdditionalFlags_;

    // How many bytes can we send before needing to wait for stream data
    // proceed message.
    uint32_t availableBufferSize_;
    //
    size_t bufferOffset_;

    WriteResponseHandler writeResponseHandler_{this};
    bool writeResponseRegistered_ = false;
    MessageHandler::GenericHandler streamInitiateReplyHandler_{
        this, &BootloaderClient::stream_initiate_replied};
    MessageHandler::GenericHandler streamProceedHandler_{
        this, &BootloaderClient::stream_proceed_received};
    StateFlowTimer timer_{this};
    // true if we are waiting for a timeout, false if we haven't started
    // sleeping yet.
    bool sleeping_ = false;
    BarrierNotifiable n_;
};

} // namespace nmranet
