/**
 * @file node-helper.h
 * @brief Node creation helper for P2P blockchain simulation
 * @author Arthur Gervais (original Bitcoin-Simulator)
 * @see https://github.com/arthurgervais/Bitcoin-Simulator
 * @author Eduardo Ramos <eduardo_ramos@edu.univali.br> (GHOSTDAG adaptations)
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

#include "../dag.h"

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
                     NodeInternetSpeeds &internetSpeeds);

  GhostDagNodeHelper();

  void commonConstructor(Address address, std::vector<Ipv4Address> &peers,
                         std::map<Ipv4Address, double> &peers_download_speeds,
                         std::map<Ipv4Address, double> &peers_upload_speeds,
                         NodeInternetSpeeds &internet_speeds);

  void SetAttribute(std::string name, const AttributeValue &value);

  ApplicationContainer Install(NodeContainer c);
  ApplicationContainer Install(Ptr<Node> node);
  ApplicationContainer Install(std::string node_name);

  void SetPeersAddresses(std::vector<Ipv4Address> &peers_addresses);
  void
  SetPeersDownloadSpeeds(std::map<Ipv4Address, double> &peers_download_speeds);
  void SetPeersUploadSpeeds(std::map<Ipv4Address, double> &peers_upload_speeds);
  void SetNodeInternetSpeeds(NodeInternetSpeeds &internetSpeeds);

protected:
  virtual Ptr<Application> InstallPriv(Ptr<Node> node);

  ObjectFactory m_factory;
  Address m_address;
  std::vector<Ipv4Address> m_peers_addresses;
  std::map<Ipv4Address, double> m_peers_download_speeds;
  std::map<Ipv4Address, double> m_peers_upload_speeds;
  NodeInternetSpeeds m_internet_speeds;
};

class GhostDagMinerHelper : public GhostDagNodeHelper {
public:
  GhostDagMinerHelper(Address address, std::vector<Ipv4Address> &peers,
                      std::map<Ipv4Address, double> &peers_download_speeds,
                      std::map<Ipv4Address, double> &peers_upload_speeds,
                      NodeInternetSpeeds &internetSpeeds);

  GhostDagMinerHelper();

protected:
  Ptr<Application> InstallPriv(Ptr<Node> node) override;
};

} // namespace ns3
