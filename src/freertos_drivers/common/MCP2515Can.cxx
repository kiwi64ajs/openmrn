/** @copyright
 * Copyright (c) 2017 Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
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
 * @file MCP2515Can.cxx
 * This file implements the CAN driver for the MCP2515 CAN Controller.
 *
 * @author Stuart W. Baker
 * @date 3 January 2017
 */

#include "MCP2515Can.hxx"

#include <fcntl.h>
#include <unistd.h>

/*
 * MCP2515Can()
 */
MCP2515Can::MCP2515Can(const char *name, const char *spi_name,
                       void (*interrupt_enable)(void),
                       void (*interrupt_disable)(void))
    : Can(name)
    , OSThread()
    , interrupt_enable(interrupt_enable)
    , interrupt_disable(interrupt_disable)
    , txPending(false)
    , spi(::open(spi_name, O_RDWR))
    , sem()
{
    HASSERT(spi >= 0);
}

/*
 * enable()
 */
void MCP2515Can::enable()
{
    /* there is a mutex lock above us, so the following sequence is atomic */
    if (!is_created())
    {
        /* start the thread */
        /** @todo make this the highest possible thread priority */
        start(name, 0, 1024);
    }

    /* reset device */
    uint8_t reset = RESET;
    ::write(spi, &reset, 1);

    /* setup RX Buf 0 and 1 to receive any message */
    Write rxb0ctrl(RXB0CTRL, 0x60);
    ::write(spi, rxb0ctrl.packet, sizeof(rxb0ctrl.packet));
    Write rxb1ctrl(RXB1CTRL, 0x60);
    ::write(spi, rxb1ctrl.packet, sizeof(rxb1ctrl.packet));
    interrupt_enable();
}

/*
 * disable()
 */
void MCP2515Can::disable()
{
    interrupt_disable();

    /* reset device */
    uint8_t reset = RESET;
    ::write(spi, &reset, 1);
}

/* 
 * tx_msg()
 */
void MCP2515Can::tx_msg()
{
}

/*
 * rx_msg()
 */
void MCP2515Can::rx_msg(int buffer)
{
    ReadRxBuf rx_buf(buffer);
    ::read(spi, rx_buf.packet, sizeof(rx_buf.packet));
    struct can_frame *can_frame;
    if (rxBuf->data_write_pointer(&can_frame))
    {
        can_frame->can_rtr = rx_buf.dlc & 0x40;
        if (can_frame->can_eff == (rx_buf.sidl & 0x08))
        {
            /* extended frame */
            can_frame->can_eff = 1;
            can_frame->can_rtr = rx_buf.dlc & 0x40;
            can_frame->can_id = (((uint32_t)rx_buf.eid0 & 0xFF) <<  0) +
                                (((uint32_t)rx_buf.eid8 & 0xFF) <<  8) +
                                (((uint32_t)rx_buf.sidl & 0x03) << 16) +
                                (((uint32_t)rx_buf.sidl & 0xE0) << 13) +
                                (((uint32_t)rx_buf.sidh & 0xFF) << 19);
        }
        else
        {
            /* standard frame */
            can_frame->can_eff = 0;
            can_frame->can_rtr = rx_buf.sidl & 0x01;
            can_frame->can_id = ((uint32_t)rx_buf.sidl >> 5) +
                                ((uint32_t)rx_buf.sidh << 3);
        }

        can_frame->can_err = 0;
        memcpy(can_frame->data, &rx_buf.d0, 8);
        rxBuf->advance(1);
        ++numReceivedPackets_;
        rxBuf->signal_condition();
    }
    else
    {
        ++overrunCount;
    }
}

/*
 * entry()
 */
void *MCP2515Can::entry()
{
    for ( ; /* forever */ ; )
    {
        sem.wait();

        Read canintf(CANINTF);
        do
        {
            /* read status flags */
            ::read(spi, canintf.packet, sizeof(canintf.packet));

            if (canintf.data & ERRI)
            {
                /* error interrupt active */
            }
            if (canintf.data & RX0I)
            {
                /* receive interrupt active */
                rx_msg(0);
            }

            if (txPending)
            {
            }

            /* refresh status flags just in case RX1 buffer became active
             * before we could finish reading out RX0 buffer
             */
            ::read(spi, canintf.packet, sizeof(canintf.packet));
            if (canintf.data & RX1I)
            {
                /* receive interrupt active */
                rx_msg(1);
            }

        } while (canintf.data);

        interrupt_enable();
    }

    return NULL;
}

/* 
 * interrupt_handler()
 */
void MCP2515Can::interrupt_handler()
{
    int woken = false;
    interrupt_disable();
    sem.post_from_isr(&woken);
    os_isr_exit_yield_test(woken);
}