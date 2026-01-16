#pragma once

#include "dag.h"

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <map>

namespace ns3
{
class GhostDagNode : public Application
{
  public:
    static TypeId GetTypeId();
    GhostDagNode();
    ~GhostDagNode() override;

    // --- Standard NS3 Getters/Setters ---
    Ptr<Socket> GetListeningSocket() const;
    std::vector<Ipv4Address> GetPeersAddresses() const;
    void SetPeersAddresses(const std::vector<Ipv4Address>& peers);
    void SetPeersDownloadSpeeds(const std::map<Ipv4Address, double>& peers_download_speeds);
    void SetPeersUploadSpeeds(const std::map<Ipv4Address, double>& peers_upload_speeds);
    void SetNodeInternetSpeeds(const NodeInternetSpeeds& internet_speeds);
    void SetNodeStats(NodeStats* node_stats);
    void ConnectToPeer(Ipv4Address peerIp, uint16_t port);

  protected:
    // --- Application Lifecycle ---
    void DoDispose() override;
    void StartApplication() override;
    void StopApplication() override;

    // --- Socket & Connection Handling ---
    void HandleRead(Ptr<Socket> socket);
    void HandleAccept(Ptr<Socket> socket, const Address& from);
    void HandlePeerClose(Ptr<Socket> socket);
    void HandlePeerError(Ptr<Socket> socket);
    void DiscoverPeers();
    EventId m_pingEvent;
    void PingPeers();

    // --- Message Dispatcher ---
    void ProcessMessage(enum Messages msg_type, std::string payload, Address& from);

    // --- 1. Real-Time Propagation Handlers  ---
    void HandleInvRelayBlock(const std::string& block_hash, Address& from);
    void HandleReqRelayBlock(const std::string& block_hash, Address& from);
    void HandleBlock(const Block& new_block, Address& from);

    // --- 2. Mempool management ---
    void HandleInvTransactions(const std::vector<std::string>& tx_hashes, Address& from);
    void HandleReqTransactions(const std::vector<std::string>& tx_hashes, Address& from);
    void HandleTransaction(const Transaction& tx, Address& from);

    // --- 3. GHOSTDAG Topology Handlers  ---
    void HandleReqAntipast(const std::string& block_hash, Address& from);
    void CheckForMissingParents(const Block& new_block, Address& from);

    // --- 4. IBD / Sync Handlers (Bootstrap) ---
    void HandleReqHeaders(const std::string& locator_hash, Address& from);
    void HandleBlockHeaders(const std::vector<BlockHeader>& headers, Address& from);
    void HandleReqBlockBodies(const std::vector<std::string>& block_hashes, Address& from);
    void HandleBlockBody(const std::set<Transaction>& body, Address& from);

    // --- Sending Helpers ---
    void SendMessage(enum Messages type, std::string payload, Address& to);
    void BroadcastInvBlock(const std::string& block_hash);
    void BroadcastInvTransaction(const std::string& tx_hash);

    // --- Internal Logic & State Management ---
    void ValidateBlock(const Block& new_block);
    void Unorphan(const Block& new_block);
    void AdvertiseNewBlock(const Block& new_block);

    // --- Timeout & Queue Management ---
    void InvTimeoutExpired(std::string block_hash);
    bool ReceivedButNotValidated(std::string block_hash);
    void RemoveReceivedButNotValidated(std::string block_hash);
    bool OnlyHeadersReceived(std::string block_hash);

    // Metrics helpers
    void RemoveSendTime();
    void RemoveReceiveTime();

    Ptr<Socket> m_socket;
    Address m_local;
    TypeId m_tid;
    int m_max_peers;

    EventId m_discoveryEvent;

    // Simulation stats
    double m_mean_block_receive_time;
    double m_previous_block_receive_time;
    double m_mean_block_propagation_time;
    double m_mean_block_size;

    // Core Structures
    Blockchain m_blockchain;
    Mempool m_mempool;
    Time m_inv_timeout_minutes;
    bool m_is_miner;
    bool m_mine_not_synced;

    // Network Params
    double m_download_speed;
    double m_upload_speed;
    double m_average_transaction_size;
    int m_transaction_index_size;

    // Connectivity Maps
    std::vector<Ipv4Address> m_peers_addresses;
    std::map<Ipv4Address, double> m_peers_download_speeds;
    std::map<Ipv4Address, double> m_peers_upload_speeds;
    std::map<Ipv4Address, Ptr<Socket>> m_peers_sockets;

    // State Maps
    std::map<std::string, std::vector<Address>> m_queue_inv;
    std::map<std::string, EventId> m_inv_timeouts;
    std::map<Address, std::string> m_buffered_data;
    std::map<std::string, Block> m_received_not_validated;
    std::map<std::string, Block> m_only_headers_received;

    NodeStats* m_node_stats;
    NodeState m_node_state;
    std::vector<double> m_send_block_times;
    std::vector<double> m_receive_block_times;

    int m_ghostdag_port;
    uint8_t m_ghostdag_k;
    int m_seconds_per_min;
    int m_count_bytes;
    int m_message_header_size;
    int m_inventory_size;
    int m_get_headers_size;
    int m_headers_size;
    int m_block_locator_size;

    TracedCallback<Ptr<const Packet>, const Address&> m_rx_trace;
};

} // namespace ns3
