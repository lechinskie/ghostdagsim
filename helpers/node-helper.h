#pragma once

#include "../dag.h"
#include "../metrics.h"

#include "ns3/application-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/uinteger.h"

namespace ns3 {

class GhostDagNodeHelper {
public:
  GhostDagNodeHelper(Address address, std::vector<Ipv4Address> &peers,
                     std::map<Ipv4Address, double> &peers_download_speeds,
                     std::map<Ipv4Address, double> &peers_upload_speeds,
                     NodeInternetSpeeds &internetSpeeds, NodeStats *stats);

  GhostDagNodeHelper();

  void commonConstructor(Address address, std::vector<Ipv4Address> &peers,
                         std::map<Ipv4Address, double> &peers_download_speeds,
                         std::map<Ipv4Address, double> &peers_upload_speeds,
                         NodeInternetSpeeds &internet_speeds, NodeStats *stats);

  void SetAttribute(std::string name, const AttributeValue &value);

  ApplicationContainer Install(NodeContainer c);
  ApplicationContainer Install(Ptr<Node> node);
  ApplicationContainer Install(std::string node_name);

  void SetPeersAddresses(std::vector<Ipv4Address> &peers_addresses);
  void
  SetPeersDownloadSpeeds(std::map<Ipv4Address, double> &peers_download_speeds);
  void SetPeersUploadSpeeds(std::map<Ipv4Address, double> &peers_upload_speeds);
  void SetNodeInternetSpeeds(NodeInternetSpeeds &internetSpeeds);
  void SetNodeStats(NodeStats *node_stats);

protected:
  virtual Ptr<Application> InstallPriv(Ptr<Node> node);

  ObjectFactory m_factory;
  Address m_address;
  std::vector<Ipv4Address> m_peers_addresses;
  std::map<Ipv4Address, double> m_peers_download_speeds;
  std::map<Ipv4Address, double> m_peers_upload_speeds;
  NodeInternetSpeeds m_internet_speeds;
  NodeStats *m_node_stats;
};

} // namespace ns3
