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

typedef struct
{
    double download_speed;
    double upload_speed;
} NodeInternetSpeeds;

/*
 * The Protocol that the node use to advertise a new block
 * */
enum ProtocolType
{
    STANDARD_PROTOCOL,
    SEND_HEADERS
};

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
    INV_RELAY_BLOCK,  // Notification of a new block hash
    INV_TRANSACTIONS, // Notification of new transactions

    GET_HEADERS,      // Requesting a range of headers
    HEADERS,          // Response containing block headers (including parent lists)
    REQUEST_ANTIPAST, // Specific to GHOSTDAG: fetching blocks in the blue/red sets

    GET_BLOCK_BODY, // Requesting tx data for a known header
    BLOCK_BODY,     // The actual transaction list

    TRANSACTION // Different from linear blockchains, DAGs needs sometimes to receive transaction
                // for fill mempool, since same transaction can be in 1 or more parallel blocks
};

struct Block
{
    int block_id;
    int miner_id;
    double time_created;
    double time_received;
    int size_in_bytes;
    std::vector<int> parent_hashes;
    std::set<int> transactions;
    int blue_score;
    bool is_blue;
    int selected_parent;
    ns3::Ipv4Address received_from;
    int hop_count;

    Block()
        : block_id(0),
          miner_id(0),
          time_created(0),
          time_received(0),
          size_in_bytes(0),
          blue_score(0),
          is_blue(false),
          selected_parent(-1),
          hop_count(0)
    {
    }

    int GetHeaderSize() const
    {
        int base_header = 80;
        int parent_count = parent_hashes.size();
        int parent_hashes_size = parent_count * 32;

        int varint_size = 1;
        if (parent_count >= 253)
        {
            varint_size = 3;
        }

        return base_header + varint_size + parent_hashes_size;
    }
};

struct Transaction
{
    int tx_id;
    double arrival_time;
    int size_bytes;
};

struct Mempool
{
    std::unordered_map<int, Transaction> pending_txs;
    std::map<int, double> tx_arrival_times;

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
        return diff;
    }
};

struct Blockchain
{
    Blockchain(int k)
        : ghostdag_k(k),
          next_block_id(0)
    {
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
    void AddOrphan(const Block& new_block);

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
