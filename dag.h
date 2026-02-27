#pragma once

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

typedef struct {
  double download_speed;
  double upload_speed;
} NodeInternetSpeeds;

enum Region {
  NORTH_AMERICA,
  EUROPE,
  SOUTH_AMERICA,
  ASIA_PACIFIC,
  JAPAN,
  AUSTRALIA,
  OTHER
};

enum MinerType {
  NORMAL_MINER,
  SIMPLE_ATTACKER,
};

enum Messages {
  NO_MESSAGE,
  PING,
  PONG,

  REQ_HEADERS,
  BLOCK_HEADERS,

  INV_RELAY_BLOCK,
  REQ_RELAY_BLOCK,

  BLOCK,

  INV_TRANSACTIONS,
  REQ_TRANSACTIONS,
  TRANSACTIONS,
};

inline static std::string GetMessageName(Messages msg) {
  switch (msg) {
  case NO_MESSAGE:
    return "NO_MESSAGE";
  case PING:
    return "PING";
  case PONG:
    return "PONG";
  case REQ_HEADERS:
    return "REQ_HEADERS";
  case BLOCK_HEADERS:
    return "BLOCK_HEADERS";
  case INV_RELAY_BLOCK:
    return "INV_RELAY_BLOCK";
  case REQ_RELAY_BLOCK:
    return "REQ_RELAY_BLOCK";
  case BLOCK:
    return "BLOCK";
  case INV_TRANSACTIONS:
    return "INV_TRANSACTIONS";
  case REQ_TRANSACTIONS:
    return "REQ_TRANSACTIONS";
  case TRANSACTIONS:
    return "TRANSACTIONS";
  default:
    return "UNKNOWN_MESSAGE";
  }
}

struct Transaction {
  uint64_t tx_id;
  uint64_t size_bytes;

  bool operator<(const Transaction &other) const { return tx_id < other.tx_id; }
};

struct BlockHeader {
  uint64_t block_id;
  uint64_t miner_id;
  double time_created;
  std::vector<uint64_t> parent_hashes;

  BlockHeader() : block_id(0), miner_id(0), time_created(0) {}

  int GetSizeInBytes() const {
    int base_size = 80; // Standard 80-byte header approximation
    int parent_size = parent_hashes.size() * 32; // 32 bytes per hash

    int varint_size = 1;
    if (parent_hashes.size() >= 253) {
      varint_size = 3;
    }

    return base_size + varint_size + parent_size;
  }
};

struct Block {
  BlockHeader header;
  std::set<Transaction> transactions;
  int size_in_bytes;

  // just metrics porpouse, not part of packet
  double time_received;
  ns3::Ipv4Address received_from;
  uint64_t blue_score;
  bool is_blue;
  std::set<uint64_t> blue_set;
  uint64_t selected_parent;

  Block()
      : size_in_bytes(0), time_received(0), blue_score(0), is_blue(false),
        selected_parent(-1) {}

  int GetTotalSize() const {
    int body_size = transactions.size() * 4;
    return header.GetSizeInBytes() + body_size;
  }
};

struct Blockchain {
  Blockchain(int k = 0) : ghostdag_k(k), next_block_id(1) {
    Block genesis;
    genesis.header.block_id = 0;
    genesis.header.miner_id = -1;
    genesis.header.time_created = 0.0;
    genesis.time_received = 0.0;
    genesis.size_in_bytes = 0;
    genesis.blue_score = 1;
    genesis.is_blue = true;
    genesis.blue_set = {0};
    blocks[genesis.header.block_id] = genesis;
    tips.insert(genesis.header.block_id);
  }
  virtual ~Blockchain() {}

  int ghostdag_k;
  uint64_t next_block_id;
  std::set<uint64_t> tips;
  std::map<uint64_t, std::set<uint64_t>> children;
  std::map<uint64_t, Block> blocks;
  std::map<uint64_t, Block> orphans;

  uint64_t GetDagWidth() const;
  bool HasBlock(uint64_t block_id) const;
  bool IsRed(uint64_t block_id) const;
  bool IsOrphan(uint64_t block_id) const;

  std::vector<const Block *> GetChildrenPointers(const Block &block);
  std::vector<const Block *> GetParentsPointers(const Block &block);

  void AddBlock(const Block &new_block);

  std::set<uint64_t> GetPast(uint64_t block_id);
  std::set<uint64_t> GetFuture(uint64_t block_id) const;
  std::set<uint64_t> GetAnticone(uint64_t block_id, uint64_t other_block_id);

  std::set<uint64_t> CalculateBlueSet(uint64_t block_id);
  std::set<uint64_t> GreedyBlueSet(uint64_t block_id);
  std::set<uint64_t> GreedyBlueSetFromTip(uint64_t tip_id,
                                          const std::set<uint64_t> &past_set);

  int CalculateBlueScore(uint64_t block_id, const std::set<uint64_t> &blue_set);
  bool IsKCluster(const std::set<uint64_t> &blue_set);
  bool IsKClusterSubset(const std::set<uint64_t> &blue_set);

  std::optional<uint64_t> SelectTip();
  std::vector<uint64_t> ComputeGHOSTDAGOrdering();

private:
  std::map<uint64_t, std::set<uint64_t>> past_cache_;

  std::vector<uint64_t> TopologicalSort(const std::set<uint64_t> &subset);
  void ProcessOrphans();
};
