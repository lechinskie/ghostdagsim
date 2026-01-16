#pragma once

#include "ns3/ipv4-address.h"

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

typedef struct
{
    int node_id;
    double mean_block_receive_time;
    double mean_block_propagation_time;
    double mean_block_size;
    int total_blocks;
    int blue_blocks;
    int red_blocks;
    double orphan_rate;
    bool is_miner;
    int miner_generated_blocks;
    double miner_average_block_gen_interval;
    double miner_average_block_size;
    double hash_rate;
    bool attack_success;

    long inv_received_bytes;
    long inv_sent_bytes;
    long get_headers_received_bytes;
    long get_headers_sent_bytes;
    long headers_received_bytes;
    long headers_sent_bytes;
    long get_data_received_bytes;
    long get_data_sent_bytes;
    long block_received_bytes;
    long block_sent_bytes;

    int connections;
    long block_timeouts;

    double total_validation_time;
    int max_dag_width_seen;

    double mempool_similarity_score;
} NodeStats;

enum NodeState
{
    STANDBY,
    SYNCING_HEADERS,
    SYNCING_BLOCKS,
    READY
};

typedef struct
{
    double download_speed;
    double upload_speed;
} NodeInternetSpeeds;

enum Region
{
    NORTH_AMERICA,
    EUROPE,
    SOUTH_AMERICA,
    ASIA_PACIFIC,
    JAPAN,
    AUSTRALIA,
    OTHER
};

enum MinerType
{
    NORMAL_MINER,
    SIMPLE_ATTACKER,
};

enum Messages
{
    PING,
    PONG,
    ADDRESSES,
    REQ_ADDRESSES,

    REQ_HEADERS,
    BLOCK_HEADERS,

    REQ_BLOCK_LOCATOR,
    BLOCK_LOCATOR,
    IDB_BLOCK_LOCATOR,

    REQ_BLOCK_BODIES,
    BLOCK_BODY,
    REQ_IDB_BLOCKS,
    IDB_BLOCK,

    INV_RELAY_BLOCK,
    REQ_RELAY_BLOCK,

    BLOCK,

    INV_TRANSACTIONS,
    REQ_TRANSACTIONS,
    TRANSACTION,

    REQ_ANTIPAST,
};

struct BlockHeader
{
    int block_id;
    int miner_id;
    double time_created;
    std::vector<int> parent_hashes;

    BlockHeader()
        : block_id(0),
          miner_id(0),
          time_created(0)
    {
    }

    int GetSizeInBytes() const
    {
        int base_size = 80;                          // Standard 80-byte header approximation
        int parent_size = parent_hashes.size() * 32; // 32 bytes per hash

        int varint_size = 1;
        if (parent_hashes.size() >= 253)
        {
            varint_size = 3;
        }

        return base_size + varint_size + parent_size;
    }
};

struct Transaction
{
    int tx_id;
    double arrival_time;
    int size_bytes;
};

struct Block
{
    BlockHeader header;
    std::set<Transaction> transactions;
    int size_in_bytes;

    // just metrics popouse, not part of packet
    double time_received;
    ns3::Ipv4Address received_from;
    int hop_count;
    int blue_score;
    bool is_blue;
    int selected_parent;

    Block()
        : size_in_bytes(0),
          time_received(0),
          hop_count(0),
          blue_score(0),
          is_blue(false),
          selected_parent(-1)
    {
    }

    int GetTotalSize() const
    {
        int body_size = transactions.size() * 4;
        return header.GetSizeInBytes() + body_size;
    }
};

struct Mempool
{
    std::unordered_map<int, Transaction> pending_txs;

    void AddTransaction(const Transaction& tx)
    {
        if (pending_txs.find(tx.tx_id) == pending_txs.end())
        {
            pending_txs[tx.tx_id] = tx;
        }
    }

    void RemoveTransactions(const std::set<int>& tx_ids)
    {
        for (int id : tx_ids)
        {
            pending_txs.erase(id);
        }
    }

    std::set<int> GetTransactionIds() const
    {
        std::set<int> ids;
        for (const auto& [id, tx] : pending_txs)
        {
            ids.insert(id);
        }
        return ids;
    }

    int GetSymmetricDifference(const std::set<int>& block_txs) const
    {
        int diff = 0;

        for (int tx_id : block_txs)
        {
            if (pending_txs.find(tx_id) == pending_txs.end())
            {
                diff++;
            }
        }

        for (const auto& [tx_id, tx] : pending_txs)
        {
            if (block_txs.find(tx_id) == block_txs.end())
            {
                diff++;
            }
        }

        return diff;
    }

    int GetIntersectionSize(const std::set<int>& block_txs) const
    {
        int count = 0;
        for (int tx_id : block_txs)
        {
            if (pending_txs.find(tx_id) != pending_txs.end())
            {
                count++;
            }
        }
        return count;
    }

    bool HasTransaction(int tx_id) const
    {
        return pending_txs.find(tx_id) != pending_txs.end();
    }

    int GetTotalSize() const
    {
        int total = 0;
        for (const auto& [id, tx] : pending_txs)
        {
            total += tx.size_bytes;
        }
        return total;
    }

    int GetCount() const
    {
        return static_cast<int>(pending_txs.size());
    }

    void Clear()
    {
        pending_txs.clear();
    }
};

struct Blockchain
{
    Blockchain(int k = 0)
        : ghostdag_k(k),
          next_block_id(0)
    {
        Block genesis;
        genesis.header.block_id = GetNextBlockId();
        genesis.header.miner_id = -1;
        genesis.header.time_created = 0.0;
        genesis.time_received = 0.0;
        genesis.size_in_bytes = 0;
        genesis.blue_score = 1;
        genesis.is_blue = true;
        genesis.selected_parent = -1;

        blocks[genesis.header.block_id] = genesis;
        tips.insert(genesis.header.block_id);
    }

    virtual ~Blockchain()
    {
    }

    int ghostdag_k;
    int next_block_id;

    std::set<int> tips;
    std::map<int, std::set<int>> children;
    std::map<int, Block> blocks;
    std::map<int, Block> orphans;

    int GetDagWidth() const;
    bool HasBlock(int block_id) const;
    bool IsRed(int block_id) const;
    bool IsOrphan(int block_id) const;

    std::vector<const Block*> GetChildrenPointers(const Block& block);
    std::vector<const Block*> GetParentsPointers(const Block& block);

    void AddBlock(const Block& new_block);

    std::set<int> GetPast(int block_id);
    std::set<int> GetFuture(int block_id);
    std::set<int> GetAnticone(int block_id, int other_block_id);

    std::set<int> CalculateBlueSet(int block_id);
    std::set<int> GreedyBlueSet(int block_id);
    int CalculateBlueScore(int block_id, const std::set<int>& blue_set);
    bool IsKCluster(const std::set<int>& blue_set);

    int SelectTip();
    std::vector<int> ComputeGHOSTDAGOrdering();

    int GetNextBlockId()
    {
        return next_block_id++;
    }
};
