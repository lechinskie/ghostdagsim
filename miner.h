/**
 * @file miner.h
 * @brief GhostDAG honest miner definition (subclass of node)
 *
 * Architecture inspired by Bitcoin-Simulator by Arthur Gervais et al.
 *   https://github.com/arthurgervais/Bitcoin-Simulator
 *   "On the Security and Performance of Proof of Work Blockchains", CCS'16
 *
 * @author Eduardo Lechinski Ramos <lechinski@univali.br>
 * @date 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include "node.h"
#include <cstdint>

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

  std::set<Transaction> SelectTransactions(uint64_t bid);

  double m_blockGenInterval;
  EventId m_nextMiningEvent;

  int m_txsPerBlock;
  int m_txSelectionStrategy;

  int m_minerGeneratedBlocks;
  double m_previousBlockGenerationTime;
  double m_averageBlockGenInterval;
};

} // namespace ns3
