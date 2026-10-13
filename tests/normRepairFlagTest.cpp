#include "normMessage.h"
#include "normNode.h"
#include "normObject.h"
#include "normSession.h"
#include "protoDispatcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            fprintf(stderr, "TEST FAILED: %s (line %d)\n", #condition, __LINE__); \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

static void TestBlockRepairState()
{
    NormBlock block;
    TEST_ASSERT(block.Init(8));
    NormBlockId blockId(0);

    block.TxInit(blockId, 4, 0);
    TEST_ASSERT(!block.InRepair());
    TEST_ASSERT(block.TxIsOriginalPending(0));
    block.TxAdvanceOriginal(0);
    TEST_ASSERT(!block.TxIsOriginalPending(0));
    TEST_ASSERT(block.TxIsOriginalPending(1));
    block.ClearPending();
    TEST_ASSERT(block.TxUpdate(0, 0, 4, 4, 1));
    TEST_ASSERT(block.InRepair());

    block.TxInit(blockId, 4, 0);
    block.ClearPending();
    TEST_ASSERT(block.HandleSegmentRequest(0, 0, 4, 4, 1));
    TEST_ASSERT(!block.InRepair());
    TEST_ASSERT(block.ActivateRepairs(4));
    TEST_ASSERT(block.InRepair());

    block.TxRecover(blockId, 4, 4);
    TEST_ASSERT(block.InRepair());
    TEST_ASSERT(!block.TxIsOriginalPending(0));
}

static void TestSyncPolicy(NormSession& session)
{
    NormSenderNode sender(session, 2);
    NormDataMsg msg;
    msg.Init();
    msg.SetFecId(129);
    msg.SetFecPayloadId(129, 0, 0, 1, 8);

    sender.SetSyncPolicy(NormSenderNode::SYNC_CURRENT);
    TEST_ASSERT(sender.SyncTest(msg));
    msg.SetFlag(NormObjectMsg::FLAG_REPAIR);
    TEST_ASSERT(!sender.SyncTest(msg));

    sender.SetSyncPolicy(NormSenderNode::SYNC_REPAIR);
    TEST_ASSERT(sender.SyncTest(msg));

    sender.SetSyncPolicy(NormSenderNode::SYNC_ALL);
    TEST_ASSERT(sender.SyncTest(msg));

    NormDataMsg laterBlock;
    laterBlock.Init();
    laterBlock.SetFecId(129);
    laterBlock.SetFecPayloadId(129, 1, 0, 1, 8);
    laterBlock.SetFlag(NormObjectMsg::FLAG_REPAIR);
    sender.SetSyncPolicy(NormSenderNode::SYNC_REPAIR);
    TEST_ASSERT(!sender.SyncTest(laterBlock));
    sender.SetSyncPolicy(NormSenderNode::SYNC_ALL);
    TEST_ASSERT(sender.SyncTest(laterBlock));

    NormInfoMsg repairInfo;
    repairInfo.Init();
    repairInfo.SetFlag(NormObjectMsg::FLAG_REPAIR);
    sender.SetSyncPolicy(NormSenderNode::SYNC_CURRENT);
    TEST_ASSERT(!sender.SyncTest(repairInfo));
    sender.SetSyncPolicy(NormSenderNode::SYNC_REPAIR);
    TEST_ASSERT(sender.SyncTest(repairInfo));
}

static void TestObjectMessageFlags(NormSession& session)
{
    char payload[64];
    memset(payload, 0xa5, sizeof(payload));

    {
        const char info[] = "metadata";
        NormDataObject object(session, NULL, NormObjectId(1), NULL);
        TEST_ASSERT(object.Open(payload, sizeof(payload), false, info, sizeof(info)));

        NormDataMsg msg;
        // An already pending original INFO satisfies a repair request without
        // being reclassified as reactive repair.
        TEST_ASSERT(!object.HandleInfoRequest(false));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::INFO == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        TEST_ASSERT(object.HandleInfoRequest(false));
        TEST_ASSERT(object.ActivateRepairs());
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::INFO == msg.GetType());
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        TEST_ASSERT(object.HandleInfoRequest(true));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::INFO == msg.GetType());
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        TEST_ASSERT(object.TxReset(NormBlockId(0), true));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::INFO == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
    }

    {
        session.SenderSetAutoParity(1);
        NormDataObject object(session, NULL, NormObjectId(2), NULL);
        TEST_ASSERT(object.Open(payload, sizeof(payload), false));

        NormDataMsg msg;
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        NormBlock* block = object.FindBlock(NormBlockId(0));
        TEST_ASSERT(NULL != block);
        TEST_ASSERT(object.TxUpdateBlock(block, 0, 0, 1));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        TEST_ASSERT(object.TxReset(NormBlockId(0), true));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));

        TEST_ASSERT(block->HandleSegmentRequest(0, 0, 1, 4, 1));
        TEST_ASSERT(object.ActivateRepairs());
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        session.SenderSetAutoParity(0);
    }
}

static void TestMidFirstPassRepairFlags(NormSession& session)
{
    char payload[4 * 64];
    memset(payload, 0xa5, sizeof(payload));
    NormSenderNode syncNode(session, 3);
    syncNode.SetSyncPolicy(NormSenderNode::SYNC_CURRENT);

    session.SenderSetAutoParity(0);
    {
        NormDataObject object(session, NULL, NormObjectId(3), NULL);
        TEST_ASSERT(object.Open(payload, sizeof(payload), false));

        NormDataMsg msg;

        // Start the original pass, leaving source symbols 1 through 3 unsent.
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(NormMsg::DATA == msg.GetType());
        TEST_ASSERT(0 == msg.GetFecSymbolId(8));
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(syncNode.SyncTest(msg));

        // Simulate a NACK arriving during the original pass.  Symbol 0 is now
        // pending again as reactive repair, ahead of the untouched source data.
        NormBlock* block = object.FindBlock(NormBlockId(0));
        TEST_ASSERT(NULL != block);
        TEST_ASSERT(object.TxUpdateBlock(block, 0, 0, 1));

        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(0 == msg.GetFecSymbolId(8));
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(!syncNode.SyncTest(msg));

        // The repair state must not leak onto source symbols that are still
        // being transmitted for the first time.
        for (UINT16 symbolId = 1; symbolId < 4; ++symbolId)
        {
            TEST_ASSERT(object.NextSenderMsg(&msg));
            TEST_ASSERT(symbolId == msg.GetFecSymbolId(8));
            TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
            TEST_ASSERT(syncNode.SyncTest(msg));
        }
    }

    {
        NormDataObject object(session, NULL, NormObjectId(4), NULL);
        TEST_ASSERT(object.Open(payload, sizeof(payload), false));

        NormDataMsg msg;
        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(0 == msg.GetFecSymbolId(8));
        TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(syncNode.SyncTest(msg));

        NormBlock* block = object.FindBlock(NormBlockId(0));
        TEST_ASSERT(NULL != block);

        // Request one fresh parity symbol while source symbols 1 through 3
        // remain in their original pass.
        TEST_ASSERT(object.TxUpdateBlock(block, 4, 4, 1));
        for (UINT16 symbolId = 1; symbolId < 4; ++symbolId)
        {
            TEST_ASSERT(object.NextSenderMsg(&msg));
            TEST_ASSERT(symbolId == msg.GetFecSymbolId(8));
            TEST_ASSERT(!msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
            TEST_ASSERT(syncNode.SyncTest(msg));
        }

        TEST_ASSERT(object.NextSenderMsg(&msg));
        TEST_ASSERT(4 == msg.GetFecSymbolId(8));
        TEST_ASSERT(msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR));
        TEST_ASSERT(!syncNode.SyncTest(msg));
    }
}

int main(int, char**)
{
    ProtoDispatcher dispatcher;
    NormSessionMgr manager(dispatcher, dispatcher, &dispatcher);
    NormSession* session = manager.NewSession("224.1.2.3", 0, 1);
    TEST_ASSERT(NULL != session);
    TEST_ASSERT(session->StartSender(1, 1024 * 1024, 64, 4, 4, 129));

    TestBlockRepairState();
    TestSyncPolicy(*session);
    TestObjectMessageFlags(*session);
    TestMidFirstPassRepairFlags(*session);

    session->StopSender();
    manager.DeleteSession(session);
    printf("normRepairFlagTest: PASSED\n");
    return 0;
}
