// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Verge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <pow.h>
#include <random.h>
#include <test/setup_common.h>
#include <validation.h>
#include <validationinterface.h>

BOOST_FIXTURE_TEST_SUITE(validation_block_tests, RegTestingSetup)

struct TestSubscriber : public CValidationInterface {
    uint256 m_expected_tip;

    TestSubscriber(uint256 tip) : m_expected_tip(tip) {}

    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, pindexNew->GetBlockHash());
    }

    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnConflicted) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->hashPrevBlock);
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->pprev->GetBlockHash());

        m_expected_tip = block->GetHash();
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->GetHash());

        m_expected_tip = block->hashPrevBlock;
    }
};

std::shared_ptr<CBlock> Block(const uint256& prev_hash)
{
    static int i = 0;
    static uint64_t time = Params().GenesisBlock().nTime;

    CScript pubKey;
    pubKey << i++ << OP_TRUE;

    auto ptemplate = BlockAssembler(Params()).CreateNewBlock(pubKey, ALGO_SCRYPT, false);
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    pblock->nTime = ++time;

    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vout.resize(1);
    txCoinbase.vin[0].scriptWitness.SetNull();
    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock, bool requirePow = true)
{
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    
    while (requirePow && !CheckProofOfWork(pblock->GetPoWHash(ALGO_SCRYPT), pblock->nBits, Params().GetConsensus())) {
        ++(pblock->nNonce);
    }

    return pblock;
}

// construct a valid block
const std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash)
{
    return FinalizeBlock(Block(prev_hash));
}

// construct an invalid block (but with a valid header)
const std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash)
{
    auto pblock = Block(prev_hash);

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.push_back(CTxIn(COutPoint(pblock->vtx[0]->GetHash(), 0), CScript(), 0));
    coinbase_spend.vout.push_back(pblock->vtx[0]->vout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

void BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (height <= 0 || blocks.size() >= max_size) return;

    bool gen_invalid = GetRand(100) < invalid_rate;
    bool gen_fork = GetRand(100) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root) : GoodBlock(root);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }

    if (gen_fork) {
        blocks.push_back(GoodBlock(root));
        BuildChain(blocks.back()->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }
}

/*BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 50) {
        blocks.clear();
        BuildChain(Params().GenesisBlock().GetHash(), 100, 15, 10, 500, blocks);
    }

    bool ignored;
    CValidationState state;
    std::vector<CBlockHeader> headers;
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(headers), [](std::shared_ptr<const CBlock> b) { return b->GetBlockHeader(); });

    // Process all the headers so we understand the toplogy of the chain
    BOOST_CHECK(ProcessNewBlockHeaders(headers, state, Params()));

    // Connect the genesis block and drain any outstanding events
    ProcessNewBlock(Params(), std::make_shared<CBlock>(Params().GenesisBlock()), true, &ignored);
    SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = chainActive.Tip();
    }
    TestSubscriber sub(initial_tip->GetBlockHash());
    RegisterValidationInterface(&sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    boost::thread_group threads;
    for (int i = 0; i < 10; i++) {
        threads.create_thread([&blocks]() {
            bool ignored;
            for (int i = 0; i < 1000; i++) {
                auto block = blocks[GetRand(blocks.size() - 1)];
                ProcessNewBlock(Params(), block, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (auto block : blocks) {
                if (block->vtx.size() == 1) {
                    bool processed = ProcessNewBlock(Params(), block, true, &ignored);
                    assert(processed);
                }
            }
        });
    }

    threads.join_all();
    while (GetMainSignals().CallbacksPending() > 0) {
        MilliSleep(100);
    }

    UnregisterValidationInterface(&sub);

    BOOST_CHECK_EQUAL(sub.m_expected_tip, chainActive.Tip()->GetBlockHash());
}*/

BOOST_AUTO_TEST_CASE(reject_blocks_with_too_new_timestamps)
{
    bool ignored;
    std::shared_ptr<CBlock> shared_genesisBlock = std::make_shared<CBlock>(Params().GenesisBlock());
    BOOST_CHECK(ProcessNewBlock(Params(), shared_genesisBlock, true, &ignored));

    std::shared_ptr<CBlock> newBlock = Block(shared_genesisBlock->GetPoWHash(ALGO_SCRYPT));

    // Set the transaction to be 5 min in advance of the block
    CMutableTransaction txCoinbase(*newBlock->vtx[0]);
    txCoinbase.nTime = (uint32_t)GetTime() + 1; 
    newBlock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    newBlock = FinalizeBlock(newBlock, false);

    // checking for the current block state (without POW and signing)
    CValidationState state;
    bool ret = CheckBlock(*newBlock.get(), state, Params().GetConsensus(), false, true, false);

    BOOST_CHECK_EQUAL(ret, false);
    BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_INVALID);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "transaction-time-too-new");
}

BOOST_AUTO_TEST_CASE(reject_blocks_with_invalid_signature)
{
    bool ignored;
    std::shared_ptr<CBlock> shared_genesisBlock = std::make_shared<CBlock>(Params().GenesisBlock());
    BOOST_CHECK(ProcessNewBlock(Params(), shared_genesisBlock, true, &ignored));

    std::shared_ptr<CBlock> newBlock = Block(shared_genesisBlock->GetPoWHash(ALGO_SCRYPT));

    CBasicKeyStore keyStore;
    CKey signKey;

    signKey.MakeNewKey(false);
    keyStore.AddKey(signKey);

    // Add a pubkey but do not sign the block!
    CMutableTransaction txCoinbase(*newBlock->vtx[0]);
    txCoinbase.nTime = GetTime() - 1000;
    txCoinbase.vout.resize(2);
    txCoinbase.vout[1].nValue = 0;
    txCoinbase.vout[1].scriptPubKey = GetScriptForRawPubKey(signKey.GetPubKey());

    newBlock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    newBlock->nTime = GetTime();
    newBlock = FinalizeBlock(newBlock, false);
    
    // checking for the current block state (without POW and signing)
    CValidationState state;
    bool ret = CheckBlock(*newBlock.get(), state, Params().GetConsensus(), false, true, true);

    BOOST_CHECK_EQUAL(ret, false);
    BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_INVALID);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-signature");
}

BOOST_AUTO_TEST_SUITE_END()
