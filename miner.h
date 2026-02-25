#pragma once

#include "node.h"

#include <random>

namespace ns3 {

class GhostDagMiner : public GhostDagNode {
public:
  static TypeId GetTypeId();
  GhostDagMiner();
  ~GhostDagMiner() override;

  void SetBlockGenerationInterval(double interval);
  double GetBlockGenerationInterval() const;

  void SetTxSelectionStrategy(int strategy);
  int GetTxSelectionStrategy() const;

  void SetTransactionsPerBlock(int txs);
  int GetTransactionsPerBlock() const;

protected:
  void DoDispose() override;
  void StartApplication() override;
  void StopApplication() override;

  void ScheduleNextMiningEvent();
  void MineBlock();

  std::set<Transaction> SelectTransactions();

  double m_blockGenInterval;
  EventId m_nextMiningEvent;

  int m_txsPerBlock;
  int m_txSelectionStrategy;

  std::mt19937 m_generator;

  int m_minerGeneratedBlocks;
  double m_previousBlockGenerationTime;
  double m_averageBlockGenInterval;
};

} // namespace ns3
