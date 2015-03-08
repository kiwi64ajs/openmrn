/** \copyright
 * Copyright (c) 2014, Stuart W Baker
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
 * \file TivaDev.hxx
 * This file implements the device class prototypes for TivaWare.
 *
 * @author Stuart W. Baker
 * @date 6 May 2014
 */

#ifndef _FREERTOS_DRIVERS_TI_TIVADEV_HXX_
#define _FREERTOS_DRIVERS_TI_TIVADEV_HXX_

#ifndef gcc
#define gcc
#endif

#include <cstdint>

#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/interrupt.h"
#include "driverlib/timer.h"
#include "usblib/usblib.h"
#include "usblib/usbcdc.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdcdc.h"

#include "Serial.hxx"
#include "Can.hxx"
#include "I2C.hxx"

/* This is fixed and equals the USB packet size that the CDC device will
 * advertise to be able to receive. This is a performance parameter, 64 is the
 * largest packet size permitted by USB for virtual serial ports. */
#define TIVA_USB_PACKET_SIZE 64

#define TIVA_USB_BUFFER_SIZE (64 * 4)

/** Private data for this implementation of serial.
 */
class TivaCdc : public USBSerial
{
public:
    /** Constructor.
     * @param name name of this device instance in the file system
     * @param interrupt interrupt number used by the device
     */
    TivaCdc(const char *name, uint32_t interrupt);

    /** Destructor.
     */
    ~TivaCdc()
    {
    }

    /** @todo (Stuart Baker) this should be made private */
    /** handle an interrupt.
     */
    void interrupt_handler();

private:
    /** Read from a file or device.
     * @param file file reference for this device
     * @param buf location to place read data
     * @param count number of bytes to read
     * @return number of bytes read upon success, -1 upon failure with errno containing the cause
     */
    ssize_t read(File *file, void *buf, size_t count) OVERRIDE;

    /** Write to a file or device.
     * @param file file reference for this device
     * @param buf location to find write data
     * @param count number of bytes to write
     * @return number of bytes written upon success, -1 upon failure with errno containing the cause
     */
    ssize_t write(File *file, const void *buf, size_t count) OVERRIDE;

    /** Device select method. Default impementation returns true.
     * @param file reference to the file
     * @param mode FREAD for read active, FWRITE for write active, 0 for
     *        exceptions
     * @return true if active, false if inactive
     */
    bool select(File* file, int mode) OVERRIDE;

    void enable(); /**< function to enable device */
    void disable(); /**< function to disable device */

    static uint32_t control_callback(void *data, unsigned long event, unsigned long msg_param, void *msg_data);

    static uint32_t rx_callback(void *data, unsigned long event, unsigned long msg_param, void *msg_data);

    static uint32_t tx_callback(void *data, unsigned long event, unsigned long msg_param, void *msg_data);

    tUSBDCDCDevice usbdcdcDevice; /**< CDC serial device instance */
    uint32_t interrupt; /**< interrupt number for device */
    bool connected; /**< connection status */
    bool enabled; /**< enabled status */
    int woken; /**< task woken metadata for ISR */
    int waiting;

    tUSBBuffer rxBuffer;
    tUSBBuffer txBuffer;

    tLineCoding lineCoding;

    uint8_t receiveBuffer[TIVA_USB_BUFFER_SIZE];
    uint8_t transmitBuffer[TIVA_USB_BUFFER_SIZE];
    uint8_t receiveBufferWorkspace[USB_BUFFER_WORKSPACE_SIZE];
    uint8_t transmitBufferWorkspace[USB_BUFFER_WORKSPACE_SIZE];

    /** Default constructor.
     */
    TivaCdc();

    DISALLOW_COPY_AND_ASSIGN(TivaCdc);
};

/** Specialization of Serial driver for Tiva UART.
 */
class TivaUart : public Serial
{
public:
    /** Constructor.
     * @param name name of this device instance in the file system
     * @param base base address of this device
     * @param interrupt interrupt number of this device
     */
    TivaUart(const char *name, unsigned long base, uint32_t interrupt);

    /** Destructor.
     */
    ~TivaUart()
    {
    }

    /** @todo (Stuart Baker) this should be made private */
    /** handle an interrupt.
     */
    void interrupt_handler();

private:
    void enable(); /**< function to enable device */
    void disable(); /**< function to disable device */
    void tx_char(); /**< function to try and transmit a character */

    unsigned long base; /**< base address of this device */
    unsigned long interrupt; /**< interrupt of this device */
    bool txPending; /**< transmission currently pending */

    /** Default constructor.
     */
    TivaUart();

    DISALLOW_COPY_AND_ASSIGN(TivaUart);
};

/** Specialization of CAN driver for Tiva CAN.
 */
class TivaCan : public Can
{
public:
    /** Constructor.
     * @param name name of this device instance in the file system
     * @param base base address of this device
     * @param interrupt interrupt number of this device
     */
    TivaCan(const char *name, unsigned long base, uint32_t interrupt);

    /** Destructor.
     */
    ~TivaCan()
    {
    }

    /** @todo (Stuart Baker) this should be made private */
    /** handle an interrupt.
     */
    void interrupt_handler();

private:
    void enable(); /**< function to enable device */
    void disable(); /**< function to disable device */
    void tx_msg(); /**< function to try and transmit a message */

    unsigned long base; /**< base address of this device */
    unsigned long interrupt; /**< interrupt of this device */
    uint8_t data[8]; /**< transmit data */
    bool txPending; /**< transmission currently pending */

    /** Default constructor.
     */
    TivaCan();

    DISALLOW_COPY_AND_ASSIGN(TivaCan);
};

/** Specialization of Serial driver for Tiva UART.
 */
class TivaI2C : public I2C
{
public:
    /** Constructor.
     * @param name name of this device instance in the file system
     * @param base base address of this device
     * @param interrupt interrupt number of this device
     */
    TivaI2C(const char *name, unsigned long base, uint32_t interrupt);

    /** Destructor.
     */
    ~TivaI2C()
    {
    }

    /** @todo (Stuart Baker) this should be made private */
    /** handle an interrupt.
     */
    void interrupt_handler();

private:
    void enable() OVERRIDE {} /**< function to enable device */
    void disable() OVERRIDE {} /**< function to disable device */

    /** Method to transmit/receive the data.
     * @param msg message to transact.
     * @param stop produce a stop condition at the end of the transfer
     * @return bytes transfered upon success or -1 with errno set
     */
    int transfer(struct i2c_msg *msg, bool stop) OVERRIDE;

    unsigned long base; /**< base address of this device */
    unsigned long interrupt; /**< interrupt of this device */
    struct i2c_msg *msg_; /**< message for current transaction */
    bool stop_; /**< current transaction ends in a stop if true */
    int count_; /**< current count index within transaction */

    /** Semaphore to wakeup task level from ISR */
    OSSem sem;

    /** Default constructor.
     */
    TivaI2C();

    DISALLOW_COPY_AND_ASSIGN(TivaI2C);
};

#endif /* _FREERTOS_DRIVERS_TI_TIVADEV_HXX_ */
