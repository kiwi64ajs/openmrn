#include "utils/async_if_test_helper.hxx"
#include "nmranet/EventManager.hxx"
#include "nmranet/GlobalEventHandler.hxx"
#include "nmranet/GlobalEventHandlerImpl.hxx"
#include "nmranet/NMRAnetEventRegistry.hxx"
#include "nmranet/NMRAnetEventTestHelper.hxx"
#include "nmranet/EndianHelper.hxx"
#include "gmock/gmock.h"

using testing::Eq;
using testing::Field;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::StrictMock;
using testing::WithArg;
using testing::_;
using testing::ElementsAre;

namespace NMRAnet
{

extern void DecodeRange(EventReport *r);

class DecodeRangeTest : public testing::Test
{
protected:
    void ExpectDecode(uint64_t complex, uint64_t exp_event, uint64_t exp_mask)
    {
        EventReport r;
        r.event = complex;
        DecodeRange(&r);
        EXPECT_EQ(exp_event, r.event);
        EXPECT_EQ(exp_mask, r.mask);
    }

    uint64_t eventid_;
    uint64_t mask_;
};

TEST_F(DecodeRangeTest, TrivPositive)
{
    ExpectDecode(0b1100, 0b1100, 0b0011);
}

TEST_F(DecodeRangeTest, TrivNegative)
{
    ExpectDecode(0b110011, 0b110000, 0b0011);
}

TEST_F(DecodeRangeTest, SimplePositive)
{
    ExpectDecode(0b1010101110000, 0b1010101110000, 0b1111);
}

TEST_F(DecodeRangeTest, SimpleNegative)
{
    ExpectDecode(0b101010111000011111, 0b101010111000000000, 0b11111);
}

TEST_F(DecodeRangeTest, LongPositive)
{
    ExpectDecode(0xfffffffffffffff0ULL, 0xfffffffffffffff0ULL, 0xf);
}

TEST_F(DecodeRangeTest, LongNegative)
{
    ExpectDecode(0xffffffffffffff0fULL, 0xffffffffffffff00ULL, 0xf);
}

class AlignMaskTest : public testing::Test
{
protected:
    void ExpectAlign(uint64_t event, unsigned size, unsigned expected_mask)
    {
        tmp_event = event;
        unsigned actual_mask =
            NMRAnetEventRegistry::align_mask(&tmp_event, size);
        string dbg = StringPrintf(
            "event 0x%llx size %u actual event 0x%llx actual mask %u", event,
            size, tmp_event, actual_mask);
        EXPECT_LE(tmp_event, event) << dbg;
        if (actual_mask < 64)
        {
            EXPECT_LE(event + size - 1, tmp_event + ((1ULL << actual_mask) - 1))
                << dbg;
            EXPECT_EQ(0ULL, tmp_event & ((1ULL << actual_mask) - 1)) << dbg;
        }
        else
        {
            EXPECT_EQ(0U, tmp_event);
        }
        EXPECT_EQ(expected_mask, actual_mask) << dbg;
    }

    uint64_t tmp_event;
};

TEST_F(AlignMaskTest, Zeros)
{
    ExpectAlign(0xA, 0, 0);
    ExpectAlign(0xB, 0, 0);
    ExpectAlign(0xF, 0, 0);
    ExpectAlign(0x7FFFFFFFFFFFFFFFULL, 0, 0);

    ExpectAlign(0xA, 1, 0);
    ExpectAlign(0xB, 1, 0);
    ExpectAlign(0xF, 1, 0);
    ExpectAlign(0x7FFFFFFFFFFFFFFFULL, 1, 0);
}

TEST_F(AlignMaskTest, Twos)
{
    ExpectAlign(0xA, 2, 1);
    ExpectAlign(0xB, 2, 3);
    EXPECT_EQ(0x8U, tmp_event);
    ExpectAlign(0xF, 2, 5);
    EXPECT_EQ(0x0U, tmp_event);
    ExpectAlign(0xBF, 2, 7);
    EXPECT_EQ(0x80U, tmp_event);
    ExpectAlign(0x7FFFFFFFFFFFFFFFULL, 2, 64);
    EXPECT_EQ(0ULL, tmp_event);
    ExpectAlign(0xBFFFFFFFFFFFFFFFULL, 2, 63);
    EXPECT_EQ(0x8000000000000000ULL, tmp_event);
    ExpectAlign(0x3FFFFFFFFFFFFFFFULL, 2, 63);
    EXPECT_EQ(0x0000000000000000ULL, tmp_event);
}

TEST_F(AlignMaskTest, Aligned)
{
    ExpectAlign(0x70, 16, 4);
    EXPECT_EQ(0x70U, tmp_event);

    ExpectAlign(0x100, 256, 8);
    EXPECT_EQ(0x100U, tmp_event);
}

TEST_F(AlignMaskTest, Unaligned)
{
    ExpectAlign(0x65, 16, 5);
    EXPECT_EQ(0x60U, tmp_event);

    ExpectAlign(0x55, 8, 4);
    EXPECT_EQ(0x50U, tmp_event);

    ExpectAlign(0x55, 16, 6);
    EXPECT_EQ(0x40U, tmp_event);

    ExpectAlign(0x3F0121, 175, 8);
    EXPECT_EQ(0x3F0100ULL, tmp_event);

    ExpectAlign(0x3F0121, 37, 7);
    EXPECT_EQ(0x3F0100ULL, tmp_event);

    ExpectAlign(0x7FFFFFFFFFFFFFFFULL, 175, 64);
    EXPECT_EQ(0x0ULL, tmp_event);

    ExpectAlign(0x0501010114FF07FFULL, 175, 12);
    EXPECT_EQ(0x0501010114FF0000ULL, tmp_event);
}

static const uint64_t kExitEventId = 0x0808080804040404ULL;
static const uint64_t kTestEventId = 0x0102030405060708ULL;
static const If::MTI kEventReportMti = If::MTI_EVENT_REPORT;
static const If::MTI kProducerIdentifiedResvdMti =
    If::MTI_PRODUCER_IDENTIFIED_RESERVED;
static const If::MTI kGlobalIdentifyEvents = If::MTI_EVENTS_IDENTIFY_GLOBAL;
static const If::MTI kAddressedIdentifyEvents =
    If::MTI_EVENTS_IDENTIFY_ADDRESSED;

class EventHandlerTests : public AsyncIfTest
{
protected:
    EventHandlerTests()
        : service_(ifCan_.get())
    {
    }

    ~EventHandlerTests()
    {
        wait();
    }

    void wait()
    {
        while (service_.event_processing_pending())
        {
            usleep(100);
        }
        AsyncIfTest::wait();
    }

    void send_message(If::MTI mti, uint64_t event)
    {
        auto *b = ifCan_->dispatcher()->alloc();
        b->data()->reset(mti, 0, {0, 0}, EventIDToPayload(event));
        ifCan_->dispatcher()->send(b);
    }

    GlobalEventService service_;
    StrictMock<MockEventHandler> h1_;
    StrictMock<MockEventHandler> h2_;
    StrictMock<MockEventHandler> h3_;
    StrictMock<MockEventHandler> h4_;
};

TEST_F(EventHandlerTests, SimpleRunTest)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    NMRAnetEventRegistry::instance()->register_handlerr(&h2_, 0, 64);
    EXPECT_CALL(h1_, HandleEventReport(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleEventReport(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    send_message(kEventReportMti, kTestEventId);
    wait();
}

TEST_F(EventHandlerTests, SimpleRunTest2)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    NMRAnetEventRegistry::instance()->register_handlerr(&h2_, 0, 64);
    EXPECT_CALL(h1_, HandleEventReport(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleEventReport(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    send_packet(":X195B4111N0102030405060708;");
    wait();
}

TEST_F(EventHandlerTests, Run100EventsTest)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    NMRAnetEventRegistry::instance()->register_handlerr(&h2_, 0, 64);
    EXPECT_CALL(h1_, HandleEventReport(_, _)).Times(100).WillRepeatedly(
        WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleEventReport(_, _)).Times(100).WillRepeatedly(
        WithArg<1>(Invoke(&InvokeNotification)));
    for (int i = 0; i < 100; i++)
    {
        send_message(kEventReportMti, 0x0102030405060708ULL);
    }
    wait();
}

TEST_F(EventHandlerTests, EventsOrderTest)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    {
        InSequence s;

        EXPECT_CALL(h1_, HandleEventReport(
                             Field(&EventReport::event, kTestEventId + 0), _))
            .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
        EXPECT_CALL(h1_, HandleEventReport(
                             Field(&EventReport::event, kTestEventId + 1), _))
            .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
        EXPECT_CALL(h1_, HandleEventReport(
                             Field(&EventReport::event, kTestEventId + 2), _))
            .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    }
    BlockExecutor block(nullptr);
    for (int i = 0; i < 3; i++)
    {
        send_message(kEventReportMti, kTestEventId + i);
    }
    block.release_block();
    wait();
}

TEST_F(EventHandlerTests, GlobalRunTest1)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    NMRAnetEventRegistry::instance()->register_handlerr(&h2_, 0, 64);
    EXPECT_CALL(h1_, HandleIdentifyGlobal(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleIdentifyGlobal(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    auto *b = ifCan_->dispatcher()->alloc();
    b->data()->reset(kGlobalIdentifyEvents, 0, "");
    ifCan_->dispatcher()->send(b);
    wait();
}

TEST_F(EventHandlerTests, GlobalAndLocal)
{
    NMRAnetEventRegistry::instance()->register_handlerr(&h1_, 0, 64);
    NMRAnetEventRegistry::instance()->register_handlerr(&h2_, 0, 64);
    EXPECT_CALL(h1_, HandleIdentifyGlobal(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleIdentifyGlobal(_, _))
        .WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h1_, HandleEventReport(_, _)).Times(100).WillRepeatedly(
        WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(h2_, HandleEventReport(_, _)).Times(100).WillRepeatedly(
        WithArg<1>(Invoke(&InvokeNotification)));
    send_packet(":X19970111N;");
    for (int i = 0; i < 100; i++)
    {
        send_message(kEventReportMti, 0x0102030405060708ULL);
    }
    wait();
}

} // namespace NMRAnet
