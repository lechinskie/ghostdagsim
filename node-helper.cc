#include "node-helper.h"

#include "node.h"

#include "ns3/inet-socket-address.h"
#include "ns3/names.h"
#include "ns3/string.h"

namespace ns3
{

GhostDagNodeHelper::GhostDagNodeHelper(Address address,
                                       std::vector<Ipv4Address>& peers,
                                       std::map<Ipv4Address, double>& peers_download_speeds,
                                       std::map<Ipv4Address, double>& peers_upload_speeds,
                                       NodeInternetSpeeds& internet_speeds,
                                       NodeStats* stats)
{
    m_factory.SetTypeId("ns3::GhostDagNode");
    commonConstructor(address,
                      peers,
                      peers_download_speeds,
                      peers_upload_speeds,
                      internet_speeds,
                      stats);
}

GhostDagNodeHelper::GhostDagNodeHelper(void)
{
    m_factory.SetTypeId("ns3::GhostDagNode");
}

void
GhostDagNodeHelper::commonConstructor(Address address,
                                      std::vector<Ipv4Address>& peers,
                                      std::map<Ipv4Address, double>& peers_download_speeds,
                                      std::map<Ipv4Address, double>& peers_upload_speeds,
                                      NodeInternetSpeeds& internet_speeds,
                                      NodeStats* stats)
{
    m_address = address;
    m_peers_addresses = peers;
    m_peers_download_speeds = peers_download_speeds;
    m_peers_upload_speeds = peers_upload_speeds;
    m_internet_speeds = internet_speeds;
    m_node_stats = stats;

    // "Local" is likely an Attribute in your GhostDagNode TypeId
    m_factory.Set("Local", AddressValue(m_address));
}

void
GhostDagNodeHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
GhostDagNodeHelper::Install(Ptr<Node> node)
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
GhostDagNodeHelper::Install(std::string node_name)
{
    Ptr<Node> node = Names::Find<Node>(node_name);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
GhostDagNodeHelper::Install(NodeContainer c)
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }
    return apps;
}

Ptr<Application>
GhostDagNodeHelper::InstallPriv(Ptr<Node> node)
{
    Ptr<GhostDagNode> app = m_factory.Create<GhostDagNode>();

    // Manually set variables that are passed via helper methods rather than NS3 Attributes
    app->SetPeersAddresses(m_peers_addresses);
    app->SetPeersDownloadSpeeds(m_peers_download_speeds);
    app->SetPeersUploadSpeeds(m_peers_upload_speeds);
    app->SetNodeInternetSpeeds(m_internet_speeds);
    app->SetNodeStats(m_node_stats);

    node->AddApplication(app);

    return app;
}

// --- Setters for late-binding or topology updates ---

void
GhostDagNodeHelper::SetPeersAddresses(std::vector<Ipv4Address>& peers_addresses)
{
    m_peers_addresses = peers_addresses;
}

void
GhostDagNodeHelper::SetPeersDownloadSpeeds(std::map<Ipv4Address, double>& peers_download_speeds)
{
    m_peers_download_speeds = peers_download_speeds;
}

void
GhostDagNodeHelper::SetPeersUploadSpeeds(std::map<Ipv4Address, double>& peers_upload_speeds)
{
    m_peers_upload_speeds = peers_upload_speeds;
}

void
GhostDagNodeHelper::SetNodeInternetSpeeds(NodeInternetSpeeds& internet_speeds)
{
    m_internet_speeds = internet_speeds;
}

void
GhostDagNodeHelper::SetNodeStats(NodeStats* node_stats)
{
    m_node_stats = node_stats;
}

} // namespace ns3
