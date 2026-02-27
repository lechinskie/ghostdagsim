#include "node.h"

#include "dag.h"
#include "metrics.h"
#include "thirdparty/json.h"

#include "ns3/address-utils.h"
#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <cstdint>
#include <fmt/format.h>
#include <string>

#define MSG_DELIMITER '#'

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GhostDagNode");

NS_OBJECT_ENSURE_REGISTERED(GhostDagNode);

TypeId GhostDagNode::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::GhostDagNode")
          .SetParent<Application>()
          .SetGroupName("Applications")
          .AddConstructor<GhostDagNode>()
          .AddAttribute("Kghostdag",
                        "The K value for dreedy algorithm ghostdag",
                        UintegerValue(10),
                        MakeUintegerAccessor(&GhostDagNode::m_ghostdag_k),
                        MakeUintegerChecker<uint8_t>())
          .AddAttribute("Local", "The Address on which to Bind the rx socket.",
                        AddressValue(),
                        MakeAddressAccessor(&GhostDagNode::m_local),
                        MakeAddressChecker())
          .AddAttribute("InvTimeoutMinutes",
                        "The timeout of inv messages in minutes",
                        TimeValue(Minutes(20)),
                        MakeTimeAccessor(&GhostDagNode::m_inv_timeout_minutes),
                        MakeTimeChecker())
          .AddAttribute("DownloadSpeed",
                        "The download speed of the node in Bytes/s.",
                        DoubleValue(1000000.0),
                        MakeDoubleAccessor(&GhostDagNode::m_download_speed),
                        MakeDoubleChecker<double>())
          .AddAttribute("UploadSpeed",
                        "The upload speed of the node in Bytes/s.",
                        DoubleValue(1000000.0),
                        MakeDoubleAccessor(&GhostDagNode::m_upload_speed),
                        MakeDoubleChecker<double>())
          .AddAttribute(
              "FixedBlockInterval",
              "Fixed interval to generate blocks (seconds).", DoubleValue(1.0),
              MakeDoubleAccessor(&GhostDagNode::m_fixed_block_interval),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "GenerateTransactions", "Whether to generate transactions",
              BooleanValue(true),
              MakeBooleanAccessor(&GhostDagNode::m_generateTransactions),
              MakeBooleanChecker())
          .AddAttribute("TxFeeLambda",
                        "Exponential distribution lambda for tx fees",
                        DoubleValue(150.0),
                        MakeDoubleAccessor(&GhostDagNode::m_txFeeLambda),
                        MakeDoubleChecker<double>())
          .AddAttribute("MempoolSize", "Maximum mempool size",
                        UintegerValue(10000),
                        MakeUintegerAccessor(&GhostDagNode::m_mempoolSize),
                        MakeUintegerChecker<uint32_t>())
          .AddTraceSource("Rx", "A packet has been received",
                          MakeTraceSourceAccessor(&GhostDagNode::m_rx_trace),
                          "ns3::Packet::AddressTracedCallback");
  return tid;
}

GhostDagNode::GhostDagNode()
    : m_mempool(256), m_average_transaction_size(522.4),
      m_transaction_index_size(2), m_generateTransactions(true),
      m_txFeeLambda(150.0), m_mempoolSize(10000), m_txFeeDistribution(150.0),
      m_txsGenerated(0) {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;

  std::random_device rd;
  m_generator.seed(rd() + 1);

  m_ghostdag_port = 16443;
  m_ghostdag_k = 10;
  m_seconds_per_min = 60;
  m_message_header_size = 90;
  m_inventory_size = 36;
  m_headers_size = 81;

  m_tid = TcpSocketFactory::GetTypeId();
}

GhostDagNode::~GhostDagNode() { NS_LOG_FUNCTION(this); }

Ptr<Socket> GhostDagNode::GetListeningSocket() const {
  NS_LOG_FUNCTION(this);
  return m_socket;
}

std::vector<Ipv4Address> GhostDagNode::GetPeersAddresses() const {
  NS_LOG_FUNCTION(this);
  return m_peers_addresses;
}

void GhostDagNode::SetPeersAddresses(const std::vector<Ipv4Address> &peers) {
  NS_LOG_FUNCTION(this);
  m_peers_addresses = peers;
}

void GhostDagNode::SetPeersDownloadSpeeds(
    const std::map<Ipv4Address, double> &peers_download_speeds) {
  NS_LOG_FUNCTION(this);
  m_peers_download_speeds = peers_download_speeds;
}

void GhostDagNode::SetPeersUploadSpeeds(
    const std::map<Ipv4Address, double> &peers_upload_speeds) {
  NS_LOG_FUNCTION(this);
  m_peers_upload_speeds = peers_upload_speeds;
}

void GhostDagNode::SetNodeInternetSpeeds(
    const NodeInternetSpeeds &internet_speeds) {
  NS_LOG_FUNCTION(this);
  m_download_speed = internet_speeds.download_speed * 1000000 / 8;
  m_upload_speed = internet_speeds.upload_speed * 1000000 / 8;
}

/*
 * App life cycle
 * */

void GhostDagNode::DoDispose() {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;
  Application::DoDispose();
}

void GhostDagNode::StartApplication() {
  NS_LOG_FUNCTION(this);

  srand(time(nullptr) + GetNode()->GetId());

  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": download speed = " << m_download_speed << " B/s");
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": upload speed = " << m_upload_speed << " B/s");
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": GHOSTDAG K = " << static_cast<int>(m_ghostdag_k));
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": peers count = " << m_peers_addresses.size());

  if (!m_socket) {
    m_socket = Socket::CreateSocket(GetNode(), m_tid);
    InetSocketAddress local =
        InetSocketAddress(Ipv4Address::GetAny(), m_ghostdag_port);
    m_socket->Bind(local);
    m_socket->Listen();

    if (addressUtils::IsMulticast(m_local)) {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
      if (udpSocket) {
        udpSocket->MulticastJoinGroup(0, local);
      } else {
        NS_FATAL_ERROR("Error: joining multicast on a non-UDP socket");
      }
    }
  }

  m_socket->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
  m_socket->SetAcceptCallback(
      MakeCallback(&GhostDagNode::HandleConnectionRequest, this),
      MakeCallback(&GhostDagNode::HandleAccept, this));

  m_socket->SetCloseCallbacks(
      MakeCallback(&GhostDagNode::HandlePeerClose, this),
      MakeCallback(&GhostDagNode::HandlePeerError, this));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << ": Creating peer sockets");
  for (const auto &peer_addr : m_peers_addresses) {
    m_peers_sockets[peer_addr] =
        Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_peers_sockets[peer_addr]->Connect(
        InetSocketAddress(peer_addr, m_ghostdag_port));
  }

  StartTransactionGeneration();
}

void GhostDagNode::StopApplication() {
  NS_LOG_FUNCTION(this);
  for (auto &socket_pair : m_peers_sockets) {
    socket_pair.second->Close();
  }

  if (m_socket) {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  NS_LOG_WARN("\n\nGHOSTDAG NODE " << GetNode()->GetId() << ":");
  NS_LOG_WARN("Total Blocks in DAG = " << m_blockchain.blocks.size());

  StopTransactionGeneration();
}

void GhostDagNode::HandleRead(Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> packet;

  while ((packet = socket->RecvFrom(from))) {
    if (packet->GetSize() == 0) {
      break;
    }

    std::vector<uint8_t> buf(packet->GetSize());
    packet->CopyData(buf.data(), packet->GetSize());
    std::string new_data(buf.begin(), buf.end());

    m_buffered_data[from] += new_data;

    size_t pos = 0;
    while ((pos = m_buffered_data[from].find(MSG_DELIMITER)) !=
           std::string::npos) {
      std::string parsed_packet = m_buffered_data[from].substr(0, pos);

      m_buffered_data[from].erase(0, pos + 1);

      auto data = nlohmann::json::parse(parsed_packet, nullptr, false);
      if (data.is_discarded()) {
        m_buffered_data[from].erase(0, pos + 1);
        continue;
      }

      uint64_t msg_data = data.value("msg", -1);
      if (msg_data != -1) {
        std::cout << "At time " << Simulator::Now().GetSeconds()
                  << "s ghostdag node " << GetNode()->GetId() << " received "
                  << packet->GetSize() << " bytes from "
                  << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                  << InetSocketAddress::ConvertFrom(from).GetPort()
                  << " with info = " << data.dump(4);
        ProcessMessage((enum Messages)msg_data, parsed_packet, from);
      } else {
        m_buffered_data[from].erase(0, pos + 1);
      }
    }
  }
}

void GhostDagNode::SendMessage(enum Messages recv, enum Messages type,
                               std::string payload, Address &to) {
  nlohmann::json d = nlohmann::json::parse(payload);
  d["msg"] = type;
  payload = d.dump();
  Ptr<Packet> packet =
      Create<Packet>((uint8_t *)payload.c_str(), payload.size());
  const uint8_t buf[] = {MSG_DELIMITER};

  InetSocketAddress peer = InetSocketAddress::ConvertFrom(to);
  Ipv4Address ip = peer.GetIpv4();
  auto it = m_peers_sockets.find(ip);

  if (it == m_peers_sockets.end()) {
    m_peers_sockets[ip] =
        Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_peers_sockets[ip]->Connect(InetSocketAddress(ip, m_ghostdag_port));
    return;
  }

  if (m_peers_sockets[ip]->Send(packet) > 0) {
    m_peers_sockets[ip]->Send(buf, 1, 0);
  }
}

void GhostDagNode::ProcessMessage(enum Messages msg_type, std::string payload,
                                  Address &from) {
  switch (msg_type) {
  case INV_RELAY_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received INV_RELAY_BLOCK");
    auto data = nlohmann::json::parse(payload);
    if (data.contains("block_hash")) {
      std::string block_hash = data["block_hash"];
      HandleInvRelayBlock(block_hash, from);
    }
    break;
  }
  case REQ_RELAY_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received REQ_RELAY_BLOCK");
    auto data = nlohmann::json::parse(payload);
    if (data.contains("block_hash")) {
      std::string block_hash = data["block_hash"];
      HandleReqRelayBlock(block_hash, from);
    }
    break;
  }
  case BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received BLOCK");
    auto data = nlohmann::json::parse(payload);
    if (data.contains("block")) {
      Block newBlock;
      auto &blockData = data["block"];
      newBlock.header.block_id = blockData.value("block_id", uint64_t{0});
      newBlock.header.miner_id = blockData.value("miner_id", uint64_t{0});
      newBlock.header.time_created = blockData.value("time_created", 0.0);

      if (blockData.contains("parent_hashes")) {
        for (auto &ph : blockData["parent_hashes"]) {
          newBlock.header.parent_hashes.push_back(ph.get<uint64_t>());
        }
      }

      if (blockData.contains("transactions")) {
        for (auto &txData : blockData["transactions"]) {
          Transaction tx;
          tx.tx_id = txData.value("tx_id", 0);
          tx.size_bytes = txData.value("size_bytes", 522);
          newBlock.transactions.insert(tx);
        }
      }

      HandleBlock(newBlock, from);
    }
    break;
  }
  case INV_TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received INV_TRANSACTIONS");
    auto data = nlohmann::json::parse(payload);
    std::vector<std::string> tx_hashes;
    if (data.contains("tx_hashes")) {
      for (auto &tx_hash : data["tx_hashes"]) {
        tx_hashes.emplace_back(tx_hash);
      }
    }
    HandleInvTransactions(tx_hashes, from);
    break;
  }
  case REQ_TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received REQ_TRANSACTIONS");
    auto data = nlohmann::json::parse(payload);
    std::vector<std::string> tx_hashes;
    if (data.contains("tx_hashes")) {
      for (const auto &h : data["tx_hashes"]) {
        tx_hashes.emplace_back(h.get<std::string>());
      }
    }
    HandleReqTransactions(tx_hashes, from);
    break;
  }
  case TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received TRANSACTIONS");
    auto data = nlohmann::json::parse(payload);
    std::vector<Transaction> txs;
    if (data.contains("transactions")) {
      for (const auto &txData : data["transactions"]) {
        Transaction tx;
        tx.tx_id = txData.value("tx_id", uint64_t{0});
        tx.size_bytes = txData.value("size_bytes", uint32_t{522});
        txs.push_back(tx);
      }
    }
    HandleTransactions(txs, from);
    break;
  }
  default:
    NS_LOG_ERROR("Node: " << GetNode()->GetId()
                          << " Received not know message: " << payload);
    break;
  }
}

/*
 * Block Handling Logic
 * */

void GhostDagNode::HandleInvRelayBlock(const std::string &block_hash,
                                       Address &from) {
  NS_LOG_FUNCTION(this << block_hash);

  uint64_t block_id = std::stoul(block_hash);

  if (!m_blockchain.HasBlock(block_id) && !m_blockchain.IsOrphan(block_id)) {
    nlohmann::json req;
    req["block_hash"] = block_hash;

    SendMessage(NO_MESSAGE, REQ_RELAY_BLOCK, req.dump(), from);

    m_queue_inv[block_hash].push_back(from);
    m_inv_timeouts[block_hash] =
        Simulator::Schedule(m_inv_timeout_minutes,
                            &GhostDagNode::InvTimeoutExpired, this, block_hash);
  }
}

void GhostDagNode::HandleReqRelayBlock(const std::string &block_hash,
                                       Address &from) {
  NS_LOG_FUNCTION(this << block_hash);

  uint64_t block_id = std::stoul(block_hash);

  if (m_blockchain.HasBlock(block_id)) {
    const Block &block = m_blockchain.blocks.at(block_id);

    nlohmann::json blockMsg;
    blockMsg["block"]["block_id"] = block.header.block_id;
    blockMsg["block"]["miner_id"] = block.header.miner_id;
    blockMsg["block"]["time_created"] = block.header.time_created;
    blockMsg["block"]["parent_hashes"] = nlohmann::json::array();
    for (uint64_t parent : block.header.parent_hashes) {
      blockMsg["block"]["parent_hashes"].push_back(parent);
    }

    blockMsg["block"]["transactions"] = nlohmann::json::array();
    for (const auto &tx : block.transactions) {
      nlohmann::json txJson;
      txJson["tx_id"] = tx.tx_id;
      txJson["size_bytes"] = tx.size_bytes;
      blockMsg["block"]["transactions"].push_back(txJson);
    }

    SendMessage(NO_MESSAGE, BLOCK, blockMsg.dump(), from);
  }
}

void GhostDagNode::HandleBlock(const Block &new_block, Address &from) {
  NS_LOG_FUNCTION(this << new_block.header.block_id);

  double currentTime = Simulator::Now().GetSeconds();
  Block block = new_block;
  block.time_received = currentTime;

  InetSocketAddress peer = InetSocketAddress::ConvertFrom(from);
  block.received_from = peer.GetIpv4();

  for (const auto &tx : block.transactions) {
    if (m_mempool.size() < (size_t)m_mempoolSize) {
      uint64_t txId = tx.tx_id;
      uint32_t fee = 150;
      m_mempool.insert(GetNode()->GetId(), txId, fee);
    }
  }

  m_blockchain.AddBlock(block);

  std::string blockHash = std::to_string(block.header.block_id);
  BroadcastInvBlock(blockHash);
}

void GhostDagNode::InvTimeoutExpired(std::string block_hash) {
  NS_LOG_FUNCTION(this << block_hash);
  m_queue_inv.erase(block_hash);
  m_inv_timeouts.erase(block_hash);
}

void GhostDagNode::BroadcastInvBlock(const std::string &block_hash) {
  NS_LOG_FUNCTION(this << block_hash);

  nlohmann::json inv;
  inv["block_hash"] = block_hash;

  for (const auto &peer_addr : m_peers_addresses) {
    InetSocketAddress peer = InetSocketAddress(peer_addr, m_ghostdag_port);
    auto addr = Address(peer);
    SendMessage(NO_MESSAGE, INV_RELAY_BLOCK, inv.dump(), addr);
  }
}

/*
 * Transaction Logic
 * */

void GhostDagNode::HandleInvTransactions(
    const std::vector<std::string> &tx_hashes, Address &from) {

  NS_LOG_FUNCTION(this);

  std::vector<std::string> wanted;

  for (const auto &hash : tx_hashes) {
    uint64_t tx_id = std::stoull(hash);

    if (m_known_txs.count(tx_id)) {
      continue;
    }

    bool first_announcement = m_queue_inv_tx[hash].empty();
    m_queue_inv_tx[hash].push_back(from);

    if (first_announcement) {
      m_inv_tx_timeouts[hash] =
          Simulator::Schedule(m_inv_timeout_minutes,
                              &GhostDagNode::InvTxTimeoutExpired, this, hash);

      wanted.push_back(hash);
    }
  }

  if (wanted.empty()) {
    return;
  }

  nlohmann::json req;
  req["tx_hashes"] = wanted;
  SendMessage(NO_MESSAGE, REQ_TRANSACTIONS, req.dump(), from);

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " requested " << wanted.size()
                       << " txs in one REQ");
}

void GhostDagNode::HandleReqTransactions(
    const std::vector<std::string> &tx_hashes, Address &from) {

  NS_LOG_FUNCTION(this);

  nlohmann::json msg;
  msg["transactions"] = nlohmann::json::array();
  uint64_t found_count = 0;

  for (const auto &hash : tx_hashes) {
    uint64_t tx_id = std::stoull(hash);
    uint32_t miner_id = IdFromTxId(tx_id);

    HtabIterator it = m_mempool.find(miner_id, tx_id);
    if (!it.isValid()) {
      continue; // Evicted or never had it
    }

    nlohmann::json txJson;
    txJson["tx_id"] = it.iterator->txId;
    txJson["size_bytes"] = static_cast<uint32_t>(m_average_transaction_size);
    txJson["fee"] = it.iterator->fee;
    msg["transactions"].push_back(txJson);
    found_count++;
  }

  if (found_count == 0) {
    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " had none of the "
                         << tx_hashes.size() << " requested txs");
    return;
  }

  SendMessage(NO_MESSAGE, TRANSACTIONS, msg.dump(), from);

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " replied with " << found_count
                       << "/" << tx_hashes.size()
                       << " txs in one TRANSACTION message");
}

void GhostDagNode::HandleTransactions(const std::vector<Transaction> &txs,
                                      Address &from) {
  NS_LOG_FUNCTION(this);

  InetSocketAddress sender = InetSocketAddress::ConvertFrom(from);

  for (const auto &tx : txs) {
    std::string hash = std::to_string(tx.tx_id);

    auto t_it = m_inv_tx_timeouts.find(hash);
    if (t_it != m_inv_tx_timeouts.end()) {
      Simulator::Cancel(t_it->second);
      m_inv_tx_timeouts.erase(t_it);
    }
    m_queue_inv_tx.erase(hash);

    if (m_known_txs.count(tx.tx_id)) {
      continue;
    }

    m_known_txs.insert(tx.tx_id);

    if (m_mempool.size() >= static_cast<size_t>(m_mempoolSize)) {
      NS_LOG_DEBUG("Node " << GetNode()->GetId()
                           << " mempool full, dropping tx " << tx.tx_id);
      continue;
    }

    uint32_t miner_id = IdFromTxId(tx.tx_id);
    uint32_t fee = static_cast<uint32_t>(m_txFeeDistribution(m_generator));
    m_mempool.insert(miner_id, tx.tx_id, fee);

    m_pending_inv_tx.push_back(hash);

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " accepted tx " << tx.tx_id
                         << " fee=" << fee);
  }

  if (!m_pending_inv_tx.empty() && !m_invBatchEvent.IsPending()) {
    m_invBatchEvent = Simulator::Schedule(Seconds(INV_BATCH_INTERVAL_S),
                                          &GhostDagNode::FlushInvBatch, this,
                                          sender.GetIpv4());
  }
}

void GhostDagNode::FlushInvBatch(Ipv4Address exclude) {
  NS_LOG_FUNCTION(this);

  if (m_pending_inv_tx.empty()) {
    return;
  }

  std::vector<std::string> batch;
  batch.swap(m_pending_inv_tx);

  BroadcastInvTransactions(batch, exclude);

  NS_LOG_DEBUG("Node " << GetNode()->GetId()
                       << " flushed INV batch: " << batch.size() << " txs");
}

void GhostDagNode::InvTxTimeoutExpired(std::string tx_hash) {
  NS_LOG_FUNCTION(this << tx_hash);

  auto it = m_queue_inv_tx.find(tx_hash);
  if (it == m_queue_inv_tx.end()) {
    return;
  }

  it->second.erase(it->second.begin());

  if (!it->second.empty()) {
    Address next = it->second.front();

    nlohmann::json req;
    req["tx_hashes"] = std::vector<std::string>{tx_hash};
    SendMessage(NO_MESSAGE, REQ_TRANSACTIONS, req.dump(), next);

    m_inv_tx_timeouts[tx_hash] =
        Simulator::Schedule(m_inv_timeout_minutes,
                            &GhostDagNode::InvTxTimeoutExpired, this, tx_hash);

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " retrying tx " << tx_hash
                         << " from next peer");
  } else {
    m_queue_inv_tx.erase(it);
    m_inv_tx_timeouts.erase(tx_hash);
    NS_LOG_WARN("Node " << GetNode()->GetId() << " gave up fetching tx "
                        << tx_hash);
  }
}

void GhostDagNode::BroadcastInvTransactions(
    const std::vector<std::string> &tx_hashes, Ipv4Address exclude) {

  if (tx_hashes.empty()) {
    return;
  }

  nlohmann::json inv;
  inv["tx_hashes"] = tx_hashes;
  std::string payload = inv.dump();

  for (const auto &peer_addr : m_peers_addresses) {
    if (peer_addr == exclude) {
      continue;
    }
    InetSocketAddress peer(peer_addr, m_ghostdag_port);
    Address addr(peer);
    SendMessage(NO_MESSAGE, INV_TRANSACTIONS, payload, addr);
  }
}

void GhostDagNode::GenerateTransaction() {
  NS_LOG_FUNCTION(this);

  uint64_t txId =
      (static_cast<uint64_t>(GetNode()->GetId()) << 32) | m_txsGenerated;
  auto fee = static_cast<uint32_t>(m_txFeeDistribution(m_generator));

  if (m_mempool.size() < static_cast<size_t>(m_mempoolSize)) {
    m_mempool.insert(GetNode()->GetId(), txId, fee);
    m_known_txs.insert(txId);
    m_txsGenerated++;

    m_pending_inv_tx.push_back(std::to_string(txId));

    if (!m_invBatchEvent.IsPending()) {
      m_invBatchEvent = Simulator::Schedule(Seconds(INV_BATCH_INTERVAL_S),
                                            &GhostDagNode::FlushInvBatch, this,
                                            Ipv4Address());
    }

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " generated tx " << txId
                         << " fee=" << fee);
  } else {
    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " mempool full");
  }

  ScheduleNextTxGeneration();
}

void GhostDagNode::ScheduleNextTxGeneration() {
  std::exponential_distribution<double> txRate(1.0 / 0.1);
  double nextTxTime = txRate(m_generator);

  NS_LOG_DEBUG("Node " << GetNode()->GetId()
                       << " will generate next transaction in " << nextTxTime
                       << "s");

  m_nextTxGenerationEvent = Simulator::Schedule(
      Seconds(nextTxTime), &GhostDagNode::GenerateTransaction, this);
}

void GhostDagNode::StartTransactionGeneration() {
  if (!m_generateTransactions) {
    return;
  }

  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << " starting transaction generation (mempool size: "
                      << m_mempoolSize << ", tx fee lambda: " << m_txFeeLambda
                      << ")");

  ScheduleNextTxGeneration();
}

void GhostDagNode::StopTransactionGeneration() {
  if (m_nextTxGenerationEvent.IsPending()) {
    Simulator::Cancel(m_nextTxGenerationEvent);
  }

  NS_LOG_INFO("Node " << GetNode()->GetId() << " generated " << m_txsGenerated
                      << " transactions");
}

/*
 * ns3 boilerplate
 * */
bool GhostDagNode::HandleConnectionRequest(Ptr<Socket> s, const Address &from) {
  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " accepting connection from "
                       << from);
  return true; // Explicitly accept the connection
}

void GhostDagNode::HandlePeerClose(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
}

void GhostDagNode::HandlePeerError(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
}

void GhostDagNode::HandleAccept(Ptr<Socket> s, const Address &from) {
  NS_LOG_FUNCTION(this << s << from);
  s->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
}

} // namespace ns3
