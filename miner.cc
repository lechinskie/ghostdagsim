#include "miner.h"

#include "dag.h"
#include "metrics.h"

#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"

#include <cstdint>
#include <string>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GhostDagMiner");

NS_OBJECT_ENSURE_REGISTERED(GhostDagMiner);

TypeId GhostDagMiner::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::GhostDagMiner")
          .SetParent<GhostDagNode>()
          .SetGroupName("Applications")
          .AddConstructor<GhostDagMiner>()
          .AddAttribute("BlockGenInterval",
                        "Mean interval between block generations (seconds)",
                        DoubleValue(20.0),
                        MakeDoubleAccessor(&GhostDagMiner::m_blockGenInterval),
                        MakeDoubleChecker<double>())
          .AddAttribute("TxsPerBlock", "Number of transactions per block",
                        UintegerValue(100),
                        MakeUintegerAccessor(&GhostDagMiner::m_txsPerBlock),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TxSelectionStrategy",
              "Transaction selection strategy: 0=RANDOM, 1=RATIONAL",
              UintegerValue(0),
              MakeUintegerAccessor(&GhostDagMiner::m_txSelectionStrategy),
              MakeUintegerChecker<uint32_t>());
  return tid;
}

GhostDagMiner::GhostDagMiner()
    : m_blockGenInterval(20.0), m_txsPerBlock(100), m_txSelectionStrategy(0) {
  NS_LOG_FUNCTION(this);
  m_generateTransactions = false;
}

GhostDagMiner::~GhostDagMiner() { NS_LOG_FUNCTION(this); }

void GhostDagMiner::SetBlockGenerationInterval(double interval) {
  m_blockGenInterval = interval;
}

double GhostDagMiner::GetBlockGenerationInterval() const {
  return m_blockGenInterval;
}

void GhostDagMiner::SetTxSelectionStrategy(int strategy) {
  m_txSelectionStrategy = strategy;
}

int GhostDagMiner::GetTxSelectionStrategy() const {
  return m_txSelectionStrategy;
}

void GhostDagMiner::SetTransactionsPerBlock(int txs) { m_txsPerBlock = txs; }

int GhostDagMiner::GetTransactionsPerBlock() const { return m_txsPerBlock; }

void GhostDagMiner::DoDispose() {
  NS_LOG_FUNCTION(this);
  if (m_nextMiningEvent.IsPending()) {
    Simulator::Cancel(m_nextMiningEvent);
  }
  GhostDagNode::DoDispose();
}

void GhostDagMiner::StartApplication() {
  NS_LOG_FUNCTION(this);

  GhostDagNode::StartApplication();

  NS_LOG_INFO("Miner " << GetNode()->GetId() << ": block generation interval = "
                       << m_blockGenInterval << "s (Poisson)");
  NS_LOG_INFO("Miner " << GetNode()->GetId()
                       << ": txs per block = " << m_txsPerBlock);
  NS_LOG_INFO("Miner " << GetNode()->GetId() << ": tx selection strategy = "
                       << (m_txSelectionStrategy == 0 ? "RANDOM" : "RATIONAL"));

  ScheduleNextMiningEvent();
}

void GhostDagMiner::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_nextMiningEvent.IsPending()) {
    Simulator::Cancel(m_nextMiningEvent);
  }

  NS_LOG_WARN("Miner " << GetNode()->GetId() << " generated "
                       << m_minerGeneratedBlocks << " blocks");

  GhostDagNode::StopApplication();
}

void GhostDagMiner::ScheduleNextMiningEvent() {
  std::exponential_distribution<double> blockRate(1.0 / m_blockGenInterval);
  double nextBlockTime = blockRate(m_generator);

  NS_LOG_DEBUG("Time " << Simulator::Now().GetSeconds() << ": Miner "
                       << GetNode()->GetId() << " will mine next block in "
                       << nextBlockTime << "s");

  m_nextMiningEvent = Simulator::Schedule(Seconds(nextBlockTime),
                                          &GhostDagMiner::MineBlock, this);
}

void GhostDagMiner::MineBlock() {
  NS_LOG_FUNCTION(this);

  double currentTime = Simulator::Now().GetSeconds();
  int minerId = GetNode()->GetId();

  Block newBlock;
  newBlock.header.block_id =
      (static_cast<uint64_t>(minerId) << 32) | m_blockchain.next_block_id++;
  newBlock.header.miner_id = minerId;
  newBlock.header.time_created = currentTime;

  for (uint64_t tip : m_blockchain.tips) {
    newBlock.header.parent_hashes.push_back(tip);
  }

  newBlock.transactions = SelectTransactions();
  newBlock.size_in_bytes = newBlock.GetTotalSize();
  newBlock.time_received = currentTime;

  m_blockchain.AddBlock(newBlock);

  NS_LOG_INFO("Miner " << minerId << " mined block " << newBlock.header.block_id
                       << " with " << newBlock.transactions.size()
                       << " transactions, "
                       << newBlock.header.parent_hashes.size() << " parents");

  std::string blockHash = std::to_string(newBlock.header.block_id);
  BroadcastInvBlock(blockHash);

  m_averageBlockGenInterval =
      (m_minerGeneratedBlocks /
       static_cast<double>(m_minerGeneratedBlocks + 1)) *
          m_averageBlockGenInterval +
      (currentTime - m_previousBlockGenerationTime) /
          (m_minerGeneratedBlocks + 1);
  m_previousBlockGenerationTime = currentTime;
  m_minerGeneratedBlocks++;

  ScheduleNextMiningEvent();
}

std::set<Transaction> GhostDagMiner::SelectTransactions() {
  NS_LOG_FUNCTION(this);

  std::set<Transaction> selected;

  if (m_txsPerBlock <= 0 || m_mempool.size() == 0) {
    return selected;
  }

  int toSelect = std::min(m_txsPerBlock, static_cast<int>(m_mempool.size()));

  if (m_txSelectionStrategy == 0) {
    for (int i = 0; i < toSelect; ++i) {
      auto it = m_mempool.getRandomTransaction(m_generator);
      if (it.isValid()) {
        Transaction tx;
        tx.tx_id = it.iterator->txId;
        tx.size_bytes = 522;
        selected.insert(tx);
        m_mempool.eraseTransaction(it);
      }
    }
  } else {
    for (int i = 0; i < toSelect; ++i) {
      auto it = m_mempool.getSortedTransactionDescending();
      if (it.isValid()) {
        Transaction tx;
        tx.tx_id = it.iterator->txId;
        tx.size_bytes = 522;
        selected.insert(tx);
        m_mempool.eraseTransaction(it);
      }
    }
  }

  return selected;
}

} // namespace ns3
