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

    Ptr<Socket> GetListeningSocket() const;
    std::vector<Ipv4Address> GetPeersAddresses() const;
    void SetPeersAddresses(const std::vector<Ipv4Address>& peers);
    void SetPeersDownloadSpeeds(const std::map<Ipv4Address, double>& peers_download_speeds);
    void SetPeersUploadSpeeds(const std::map<Ipv4Address, double>& peers_upload_speeds);
    void SetNodeInternetSpeeds(const NodeInternetSpeeds& internet_speeds);
    void SetNodeStats(NodeStats* node_stats);
    void SetProtocolType(enum ProtocolType protocol_type);

  protected:
    void DoDispose() override;

    void StartApplication() override;
    void StopApplication() override;

    void HandleRead(Ptr<Socket> socket);
    void HandleAccept(Ptr<Socket> socket, const Address& from);
    void HandlePeerClose(Ptr<Socket> socket);
    void HandlePeerError(Ptr<Socket> socket);

    void ReceivedBlockMessage(std::string& block_info, Address& from);
    virtual void ReceiveBlock(const Block& new_block);
    void ReceivedAntipastResponse(std::string& antipast_info, Address& from);
    void ReceivedTransactionResponse(std::string& transaction_info, Address& from);
    void ReceivedHeadersResponse(std::string& heaedrs_info, Address& from);

    void ReceivedGrapheneBlockMessage(std::string& block_info, Address& from)
    {
        NS_FATAL_ERROR("Not implemented graphene");
    };

    bool AttemptGrapheneReconstruction(const Block& template_block,
                                       int estimated_diff,
                                       std::set<int>& out_missing_txs)
    {
        NS_FATAL_ERROR("Not implemented graphene");
    };

    void SendBlock(std::string packet_info, Address& from);
    void SendMessage(enum Messages received_message,
                     enum Messages response_message,
                     std::string packet,
                     Address& outgoing_address);

    void ValidateBlock(const Block& new_block);
    void AfterBlockValidation(const Block& new_block);

    void Unorphan(const Block& new_block);

    void AdvertiseNewBlock(const Block& new_block);

    void InvTimeoutExpired(std::string block_hash);

    bool ReceivedButNotValidated(std::string block_hash);
    void RemoveReceivedButNotValidated(std::string block_hash);

    bool OnlyHeadersReceived(std::string block_hash);

    void RemoveSendTime();
    void RemoveCompressedBlockSendTime();
    void RemoveReceiveTime();
    void RemoveCompressedBlockReceiveTime();

    // In the case of TCP, each socket accept returns a new socket, so the
    // listening socket is stored separately from the accepted sockets
    Ptr<Socket> m_socket;                 //!< Listening socket
    Address m_local;                      //!< Local address to bind to
    TypeId m_tid;                         //!< Protocol TypeId
    int m_number_of_peers;                //!< Number of node's peers
    double m_mean_block_receive_time;     //!< The mean time interval between two
                                          //!< consecutive blocks
    double m_previous_block_receive_time; //!< The time that the node received the
                                          //!< previous block
    double m_mean_block_propagation_time; //!< The mean time that the node has to wait
                                          //!< in order to receive a newly mined block
    double m_mean_block_size;             //!< The mean block size
    Blockchain m_blockchain;              //!< The node's blockchain
    Mempool m_mempool;
    Time m_inv_timeout_minutes;        //!< The block timeout in minutes
    bool m_is_miner;                   //!< True if the node is also a miner, False otherwise
    double m_download_speed;           //!< The download speed of the node in Bytes/s
    double m_upload_speed;             //!< The upload speed of the node in Bytes/s
    double m_average_transaction_size; //!< The average transaction size. Needed for
                                       //!< compressed blocks
    int m_transaction_index_size;      //!< The transaction index size in bytes. Needed
                                       //!< for compressed blocks
    bool m_graphene;                   //!< True if the graphene mechanism is used, False
                                       //!< otherwise

    std::vector<Ipv4Address> m_peers_addresses; //!< The addresses of peers
    std::map<Ipv4Address, double>
        m_peers_download_speeds;                         //!< The peers_download_speeds of channels
    std::map<Ipv4Address, double> m_peers_upload_speeds; //!< The peers_upload_speeds of channels
    std::map<Ipv4Address, Ptr<Socket>> m_peers_sockets;  //!< The sockets of peers
    std::map<std::string, std::vector<Address>>
        m_queue_inv; //!< map holding the addresses of nodes which sent an INV for
                     //!< a particular block

    std::map<std::string, EventId>
        m_inv_timeouts; //!< map holding the event timeouts of inv messages
    std::map<Address, std::string> m_buffered_data; //!< map holding the buffered data from previous
                                                    //!< handleRead events
    std::map<std::string, Block> m_received_not_validated; //!< vector holding the received but not
                                                           //!< yet validated blocks
    std::map<std::string, Block> m_only_headers_received;  //!< vector holding the blocks that we
                                                           //!< know but not received
    NodeStats* m_node_stats;                               //!< struct holding the node stats
    std::vector<double> m_send_block_times; //!< contains the times of the next send_block events
    std::vector<double> m_send_compressed_block_times; //!< contains the times of the
                                                       //!< next send_block events
    std::vector<double> m_receive_block_times; //!< contains the times of the next send_block events
    std::vector<double> m_receive_compressed_block_times; //!< contains the times of the next
                                                          //!< send_block events
    enum ProtocolType m_protocol_type;                    //!< protocol type

    const int m_ghostdag_port;       //!< 16443
    const int m_seconds_per_min;     //!< 60
    const int m_count_bytes;         //!< The size of count variable in messages
    const int m_message_header_size; //!< The size of the Message Header,
                                     //!< including also
                                     //!< protocol headers (TCP, IP, Ethernet)
    const int m_inventory_size;      //!< The size of inventories in INV messages,
    const int m_get_headers_size;    //!< The size of the GET_HEADERS message,
    const int m_headers_size;
    const int m_block_headers_size;

    TracedCallback<Ptr<const Packet>, const Address&> m_rx_trace;
};

} // namespace ns3
