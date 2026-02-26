/*
 * This file is originally created for help create nodes for
 * [Bitcoin-Simulator](https://github.com/arthurgervais/Bitcoin-Simulator) by
 * Arthur Gervais Revised and Re-impl for ghostdag nodes and helpers by
 * Eramoss - <eduardo_ramos@edu.univali.br>
 * */

#include "node-helper.h"

#include "../miner.h"
#include "../node.h"

#include "ns3/inet-socket-address.h"
#include "ns3/names.h"
#include "ns3/string.h"

namespace ns3 {

GhostDagNodeHelper::GhostDagNodeHelper(
    Address address, std::vector<Ipv4Address> &peers,
    std::map<Ipv4Address, double> &peers_download_speeds,
    std::map<Ipv4Address, double> &peers_upload_speeds,
    NodeInternetSpeeds &internet_speeds) {
  m_factory.SetTypeId("ns3::GhostDagNode");
  commonConstructor(address, peers, peers_download_speeds, peers_upload_speeds,
                    internet_speeds);
}

GhostDagNodeHelper::GhostDagNodeHelper() {
  m_factory.SetTypeId("ns3::GhostDagNode");
}

void GhostDagNodeHelper::commonConstructor(
    Address address, std::vector<Ipv4Address> &peers,
    std::map<Ipv4Address, double> &peers_download_speeds,
    std::map<Ipv4Address, double> &peers_upload_speeds,
    NodeInternetSpeeds &internet_speeds) {
  m_address = address;
  m_peers_addresses = peers;
  m_peers_download_speeds = peers_download_speeds;
  m_peers_upload_speeds = peers_upload_speeds;
  m_internet_speeds = internet_speeds;

  m_factory.Set("Local", AddressValue(m_address));
}

void GhostDagNodeHelper::SetAttribute(std::string name,
                                      const AttributeValue &value) {
  m_factory.Set(name, value);
}

ApplicationContainer GhostDagNodeHelper::Install(Ptr<Node> node) {
  return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer GhostDagNodeHelper::Install(std::string node_name) {
  Ptr<Node> node = Names::Find<Node>(node_name);
  return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer GhostDagNodeHelper::Install(NodeContainer c) {
  ApplicationContainer apps;
  for (auto i = c.Begin(); i != c.End(); ++i) {
    apps.Add(InstallPriv(*i));
  }
  return apps;
}

Ptr<Application> GhostDagNodeHelper::InstallPriv(Ptr<Node> node) {
  Ptr<GhostDagNode> app = m_factory.Create<GhostDagNode>();

  app->SetPeersAddresses(m_peers_addresses);
  app->SetPeersDownloadSpeeds(m_peers_download_speeds);
  app->SetPeersUploadSpeeds(m_peers_upload_speeds);
  app->SetNodeInternetSpeeds(m_internet_speeds);

  node->AddApplication(app);

  return app;
}

void GhostDagNodeHelper::SetPeersAddresses(
    std::vector<Ipv4Address> &peers_addresses) {
  m_peers_addresses = peers_addresses;
}

void GhostDagNodeHelper::SetPeersDownloadSpeeds(
    std::map<Ipv4Address, double> &peers_download_speeds) {
  m_peers_download_speeds = peers_download_speeds;
}

void GhostDagNodeHelper::SetPeersUploadSpeeds(
    std::map<Ipv4Address, double> &peers_upload_speeds) {
  m_peers_upload_speeds = peers_upload_speeds;
}

void GhostDagNodeHelper::SetNodeInternetSpeeds(
    NodeInternetSpeeds &internet_speeds) {
  m_internet_speeds = internet_speeds;
}

GhostDagMinerHelper::GhostDagMinerHelper(
    Address address, std::vector<Ipv4Address> &peers,
    std::map<Ipv4Address, double> &peers_download_speeds,
    std::map<Ipv4Address, double> &peers_upload_speeds,
    NodeInternetSpeeds &internet_speeds) {
  m_factory.SetTypeId("ns3::GhostDagMiner");
  commonConstructor(address, peers, peers_download_speeds, peers_upload_speeds,
                    internet_speeds);
}

GhostDagMinerHelper::GhostDagMinerHelper() {
  m_factory.SetTypeId("ns3::GhostDagMiner");
}

Ptr<Application> GhostDagMinerHelper::InstallPriv(Ptr<Node> node) {
  Ptr<GhostDagMiner> app = m_factory.Create<GhostDagMiner>();

  app->SetPeersAddresses(m_peers_addresses);
  app->SetPeersDownloadSpeeds(m_peers_download_speeds);
  app->SetPeersUploadSpeeds(m_peers_upload_speeds);
  app->SetNodeInternetSpeeds(m_internet_speeds);

  node->AddApplication(app);

  return app;
}

} // namespace ns3
