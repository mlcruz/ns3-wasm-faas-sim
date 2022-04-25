/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/custom-app.h"
#include "ns3/custom-app-helper.h"

// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1
//    point-to-point
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FirstScriptExample");

int
main (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  auto sumWasmBase64 = get_static_module_data (StaticModuleList::WasmSum);
  auto divWasmBase64 = get_static_module_data (StaticModuleList::WasmDiv);

  Time::SetResolution (Time::NS);
  LogComponentEnable ("CustomApp", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  CustomAppHelper wasmFaasHelper = CustomAppHelper (3000);
  ApplicationContainer serverApps = wasmFaasHelper.Install (nodes);

  auto idx = 0;
  std::cout << "[NodeIps] " << std::endl;
  for (auto i = nodes.Begin (); i != nodes.End (); ++i)
    {
      auto node = (*i);
      auto wasmRuntime = node->GetApplication (0)->GetObject<CustomApp> ();
      wasmRuntime->InitRuntime ();

      auto id = wasmRuntime->GetNodeId ();
      std::cout << id << " " << interfaces.GetAddress (idx) << std::endl;

      idx++;
    }

  std::cout << "[Setup] " << std::endl;
  auto node0 = nodes.Get (0)->GetApplication (0)->GetObject<CustomApp> ();
  node0->RegisterWasmModule ((char *) "sum", sumWasmBase64);
  node0->RegisterNode (interfaces.GetAddress (1), 3000);

  auto node1 = nodes.Get (1)->GetApplication (0)->GetObject<CustomApp> ();
  node1->RegisterNode (interfaces.GetAddress (0), 3000);
  node1->RegisterWasmModule ((char *) "div", divWasmBase64);

  Simulator::Schedule (Seconds (3), &CustomApp::ExecuteModule, node0, (char *) "div",
                       (char *) "div", 10, 10);

  std::cout << "[Main] " << std::endl;
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  //   UdpEchoClientHelper echoClient (interfaces.GetAddress (1), 3000);
  //   echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
  //   echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  //   echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  //   ApplicationContainer clientApps = echoClient.Install (nodes.Get (0));
  //   clientApps.Start (Seconds (2.0));
  //   clientApps.Stop (Seconds (10.0));

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
