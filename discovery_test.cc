#include "node.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("GhostDagMain");

Ipv4Address
GetNodeIp(Ptr<Node> n)
{
    Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
    return ipv4->GetAddress(1, 0).GetLocal();
}

int
main(int argc, char* argv[])
{
    uint32_t numNodes = 20;
    uint32_t maxPeers = 6;

    CommandLine cmd;
    cmd.AddValue("numNodes", "Number of GhostDag nodes", numNodes);
    cmd.AddValue("maxPeers", "Max peers per node", maxPeers);
    cmd.Parse(argc, argv);

    LogComponentEnable("GhostDagMain", LOG_LEVEL_INFO);
    LogComponentEnable("GhostDagNode", LOG_LEVEL_INFO);

    // ---- Create nodes ----
    NodeContainer nodes;
    nodes.Create(numNodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    // ---- Point-to-point full underlay (IP reachability) ----
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("50Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    for (uint32_t i = 0; i < numNodes; ++i)
    {
        for (uint32_t j = i + 1; j < numNodes; ++j)
        {
            NetDeviceContainer devs = p2p.Install(nodes.Get(i), nodes.Get(j));

            std::ostringstream subnet;
            subnet << "10." << i + 1 << "." << j + 1 << ".0";

            Ipv4AddressHelper ipv4;
            ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
            ipv4.Assign(devs);
        }
    }

    // ---- Install GhostDag apps ----
    std::vector<Ptr<GhostDagNode>> apps;

    for (uint32_t i = 0; i < numNodes; ++i)
    {
        Ptr<GhostDagNode> app = CreateObject<GhostDagNode>();
        app->SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), 16443)));
        app->SetAttribute("MaxPeers", UintegerValue(maxPeers));

        nodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0 + i * 0.05));
        app->SetStopTime(Seconds(60.0));

        apps.push_back(app);
    }

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();

    // ---- Build connected overlay topology ----
    Simulator::Schedule(Seconds(2.0), [&]() {
        NS_LOG_INFO("Building connected P2P topology...");

        // Phase 1: Spanning tree (guarantees connectivity)
        for (uint32_t i = 1; i < numNodes; ++i)
        {
            uint32_t parent = rng->GetInteger(0, i - 1);

            Ipv4Address ipP = GetNodeIp(nodes.Get(parent));
            Ipv4Address ipC = GetNodeIp(nodes.Get(i));

            apps[parent]->ConnectToPeer(ipC, 16443);
            apps[i]->ConnectToPeer(ipP, 16443);

            NS_LOG_INFO("Link: " << parent << " <-> " << i);
        }

        // Phase 2: Extra random links (sparse, not full mesh)
        uint32_t extraEdges = numNodes / 2;

        for (uint32_t k = 0; k < extraEdges; ++k)
        {
            uint32_t a = rng->GetInteger(0, numNodes - 1);
            uint32_t b = rng->GetInteger(0, numNodes - 1);
            if (a == b)
            {
                continue;
            }

            Ipv4Address ipA = GetNodeIp(nodes.Get(a));
            Ipv4Address ipB = GetNodeIp(nodes.Get(b));

            apps[a]->ConnectToPeer(ipB, 16443);
            apps[b]->ConnectToPeer(ipA, 16443);

            NS_LOG_INFO("Extra link: " << a << " <-> " << b);
        }
    });

    Simulator::Stop(Seconds(60.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
