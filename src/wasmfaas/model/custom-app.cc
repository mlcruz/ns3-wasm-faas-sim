/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *  Copyright (c) 2007,2008,2009 INRIA, UDCAST
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
 *
 * Author: Amine Ismail <amine.ismail@sophia.inria.fr>
 *                      <amine.ismail@udcast.com>
 */

#include <vector>
#include <unordered_set>
#include <algorithm>

#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/packet-loss-counter.h"

#include "ns3/seq-ts-header.h"
#include "custom-app.h"
#include "libwasmfaas.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CustomApp");

NS_OBJECT_ENSURE_REGISTERED (CustomApp);

TypeId
CustomApp::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::CustomApp")
          .SetParent<Application> ()
          .SetGroupName ("Applications")
          .AddConstructor<CustomApp> ()
          .AddAttribute ("Port", "Port on which we listen for incoming packets.",
                         UintegerValue (100), MakeUintegerAccessor (&CustomApp::m_port),
                         MakeUintegerChecker<uint16_t> ())
          .AddAttribute ("PacketWindowSize",
                         "The size of the window used to compute the packet loss. This value "
                         "should be a multiple of 8.",
                         UintegerValue (32),
                         MakeUintegerAccessor (&CustomApp::GetPacketWindowSize,
                                               &CustomApp::SetPacketWindowSize),
                         MakeUintegerChecker<uint16_t> (8, 256))
          .AddTraceSource ("Rx", "A packet has been received",
                           MakeTraceSourceAccessor (&CustomApp::m_rxTrace),
                           "ns3::Packet::TracedCallback")
          .AddTraceSource ("RxWithAddresses", "A packet has been received",
                           MakeTraceSourceAccessor (&CustomApp::m_rxTraceWithAddresses),
                           "ns3::Packet::TwoAddressTracedCallback");
  return tid;
}

CustomApp::CustomApp () : m_lossCounter (0)
{
  NS_LOG_FUNCTION (this);
  m_received = 0;
  m_runtime_id = 0;
  m_peerAddresses = std::vector<InetSocketAddress> ();
  m_sent = 0;
  m_is_querying_peers_idx = 0; // 0 initial state, 1 querying state
  m_is_querying_peers = false;
  m_module_exec_result = 0;
  m_peer_module_exec_query_state = 0;
  m_has_module_exec_result = false;
  m_peers_queried = std::vector<InetSocketAddress> ();
  m_is_waiting_for_module_load = false;
  m_microseconds_eventloop_interval = 5;
}

CustomApp::~CustomApp ()
{
  NS_LOG_FUNCTION (this);
}

uint16_t
CustomApp::GetPacketWindowSize () const
{
  NS_LOG_FUNCTION (this);
  return m_lossCounter.GetBitMapSize ();
}

void
CustomApp::SetPacketWindowSize (uint16_t size)
{
  NS_LOG_FUNCTION (this << size);
  m_lossCounter.SetBitMapSize (size);
}

uint32_t
CustomApp::GetLost (void) const
{
  NS_LOG_FUNCTION (this);
  return m_lossCounter.GetLost ();
}

uint64_t
CustomApp::GetReceived (void) const
{
  NS_LOG_FUNCTION (this);
  return m_received;
}

void
CustomApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
CustomApp::InitRuntime ()
{
  if (m_runtime_id == 0)
    {
      m_runtime_id = initialize_runtime ();
    }
}

void
CustomApp::QueryPeersCallback (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  Address localAddress;
  while ((packet = socket->RecvFrom (from)))
    {
      socket->GetSockName (localAddress);
      m_rxTrace (packet);
      m_rxTraceWithAddresses (packet, from, localAddress);

      if (packet->GetSize () > 0)
        {
          SeqTsHeader seqTs;
          packet->RemoveHeader (seqTs);

          auto packetSize = packet->GetSize ();
          u_int8_t *data = new u_int8_t[packetSize];
          packet->CopyData (data, packetSize);

          // m_peers_queried.erase (std::remove (m_peers_queried.begin (), m_peers_queried.end (),
          //                                     InetSocketAddress::ConvertFrom (from)),
          //                        m_peers_queried.end ());

          const char *strData = (char *) data;

          if (strData[0] == 'r')
            {

              NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                        << " RECEIVED_PACKET_EXECUTE_MODULE_RESULT  "
                                        << InetSocketAddress::ConvertFrom (from).GetIpv4 () << " "
                                        << strData);

              auto s = std::string ((char *) data);

              std::vector<std::string> tokens;
              size_t startPos = 0;
              size_t endPos = 0;
              std::string token;
              while ((endPos = s.find (";", startPos)) != std::string::npos)
                {
                  token = s.substr (startPos, endPos - startPos);
                  tokens.push_back (token);
                  startPos = endPos + 1;
                }

              tokens.push_back (s.substr (startPos));

              auto loadRequest = std::string ("l;");
              loadRequest.append (tokens[1]);
              loadRequest.append (";");

              auto loadRequestStr = loadRequest.c_str ();
              auto resp_len = strlen (loadRequestStr);

              NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                        << " SEND_PACKET_MODULE_LOAD_REQUEST "
                                        << InetSocketAddress::ConvertFrom (from).GetIpv4 () << " "
                                        << loadRequestStr);

              auto p = Create<Packet> ((uint8_t *) loadRequestStr, resp_len);

              SeqTsHeader seqTs;
              seqTs.SetSeq (m_sent);
              p->AddHeader (seqTs);
              socket->SendTo (p, 0, from);
              m_sent++;

              m_module_exec_result = stoi (tokens[2]);
              m_is_querying_peers = false;
              m_is_querying_peers_idx = 255;
              m_is_waiting_for_module_load = true;

              if (m_peer_module_exec_query_state == 1)
                {
                  m_has_module_exec_result = true;
                  m_peer_module_exec_query_state = 2;
                }
            }
          else if (strData[0] == 'c')
            {
              auto s = std::string ((char *) strData);

              std::vector<std::string> tokens;
              size_t startPos = 0;
              size_t endPos = 0;
              std::string token;
              while ((endPos = s.find (";", startPos)) != std::string::npos)
                {
                  token = s.substr (startPos, endPos - startPos);
                  tokens.push_back (token);
                  startPos = endPos + 1;
                }

              tokens.push_back (s.substr (startPos));

              NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                        << " RECEIVED_PACKET_MODULE_LOAD_RESULT "
                                        << InetSocketAddress::ConvertFrom (from).GetIpv4 () << " "
                                        << tokens[1] << " " << s.size ());

              if (!is_module_registered (m_runtime_id, tokens[1].c_str ()))
                {
                  register_module (m_runtime_id, tokens[1].c_str (), tokens[2].c_str ());
                  m_is_waiting_for_module_load = false;

                  NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                            << " REGISTERED MODULE "
                                            << InetSocketAddress::ConvertFrom (from).GetIpv4 ()
                                            << " " << tokens[1]);
                }
            }
          else if (strData[0] == 'n')
            {
              m_is_querying_peers = false;
            }
        }
    }
}

void
CustomApp::QueryPeersForModule (char *name)
{
  NS_LOG_FUNCTION (this);
  if (m_is_querying_peers)
    {
      Simulator::Schedule (MicroSeconds (m_microseconds_eventloop_interval),
                           &CustomApp::QueryPeersForModule, this, name);
      return;
    }

  m_is_querying_peers = true;

  if (m_is_querying_peers_idx == 0)
    {
      NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                << "INIT_QUERY_PEERS_FOR_MODULE " << name << " "
                                << " ");
    }

  std::string s;
  s.append ("e")
      .append (";")
      .append (name)
      .append (";")
      .append (m_query_peers_func_name)
      .append (";")
      .append (std::to_string (m_query_peers_func_args[0]))
      .append (";")
      .append (std::to_string (m_query_peers_func_args[1]))
      .append (";");

  const char *c_str = s.c_str ();

  size_t len = strlen (c_str);

  uint8_t *data = new uint8_t[len];
  memcpy (data, c_str, len);

  if (m_is_querying_peers_idx >= m_peerAddresses.size ())
    {
      m_is_querying_peers = false;
      m_is_querying_peers_idx = 0;

      if (m_peer_module_exec_query_state == 1)
        {
          m_has_module_exec_result = false;
          m_peer_module_exec_query_state = 2;
        }
      return;
    }

  auto peer = m_peerAddresses[m_is_querying_peers_idx];
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  auto outSocket = Socket::CreateSocket (GetNode (), tid);

  if (outSocket->Bind () == -1)
    {
      NS_FATAL_ERROR ("Failed to bind socket");
    }

  if (outSocket->Connect ((InetSocketAddress (peer.GetIpv4 (), peer.GetPort ()))) == -1)
    {
      NS_FATAL_ERROR ("Failed to connect socket");
    }

  auto p = Create<Packet> (data, len);

  SeqTsHeader seqTs;
  seqTs.SetSeq (m_sent);
  p->AddHeader (seqTs);

  NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                            << "SEND_PACKET_EXECUTE_MODULE_REQUEST "
                            << InetSocketAddress::ConvertFrom (peer).GetIpv4 () << " " << data);

  outSocket->Send (p);
  m_peers_queried.push_back (peer);
  m_is_querying_peers_idx++;
  m_sent++;
  Simulator::Schedule (MicroSeconds (m_microseconds_eventloop_interval),
                       &CustomApp::QueryPeersForModule, this, name);
  outSocket->SetRecvCallback (MakeCallback (&CustomApp::QueryPeersCallback, this));
  //}
}

void
CustomApp::RegisterNode (Ipv4Address address, uint16_t port)
{
  NS_LOG_FUNCTION (this);

  m_peerAddresses.push_back (InetSocketAddress (address, port));

  NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                            << "REGISTER_NODE " << address << ":" << port);
}

uint64_t
CustomApp::GetNodeId (void)
{
  return m_runtime_id;
}
void
CustomApp::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  InitRuntime ();
  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      if (m_socket->Bind (local) == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&CustomApp::HandleRead, this));
}

double
CustomApp::GetCurrentNodeXPosition (void)
{
  return 0.0;
}

void
CustomApp::StopApplication ()
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
    }
}

void
CustomApp::RegisterWasmModule (char *name, char *data_base64)
{
  NS_LOG_FUNCTION (this);
  InitRuntime ();

  NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                            << "REGISTER_MODULE " << name);
  register_module (m_runtime_id, name, data_base64);
}

int32_t
CustomApp::ExecuteModule (char *module_name, char *func_name, int32_t arg1, int32_t arg2)
{
  NS_LOG_FUNCTION (this);

  NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                            << "INIT_EXECUTE_MODULE_REQUEST " << module_name << " " << func_name
                            << " " << arg1 << " " << arg2);

  m_query_peers_func_name = std::string (func_name);
  m_query_peers_func_args = std::vector<int32_t> (2);
  m_query_peers_func_args[0] = arg1;
  m_query_peers_func_args[1] = arg2;

  if (is_module_registered (m_runtime_id, module_name))
    {
      auto func = WasmFunction{};
      func.name = m_query_peers_func_name.c_str ();

      auto arg1s = std::to_string (m_query_peers_func_args[0]);
      auto arg2s = std::to_string (m_query_peers_func_args[1]);

      func.args[0] = WasmArg{
          arg1s.c_str (),
          ArgType::I32,
      };
      func.args[1] = WasmArg{
          arg2s.c_str (),
          ArgType::I32,
      };

      auto result = execute_module (m_runtime_id, module_name, func);

      NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                << "EXECUTE_MODULE_REQUEST_CACHE_RESULT " << module_name << " "
                                << func_name << " " << arg1 << " " << arg2 << " " << result);
      return result;
    }
  else
    {

      QueryPeersForModule (module_name);
      return 0;
    }
}

void
CustomApp::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  Address localAddress;

  // One means that the query is still running
  if (m_peer_module_exec_query_state == 1)
    {

      Simulator::Schedule (MicroSeconds (m_microseconds_eventloop_interval), &CustomApp::HandleRead,
                           this, socket);
      return;
    }
  // 2 means if has finished running
  else if (m_peer_module_exec_query_state == 2)
    {
      if (m_has_module_exec_result)
        {

          if (m_is_waiting_for_module_load)
            {
              Simulator::Schedule (MicroSeconds (m_microseconds_eventloop_interval),
                                   &CustomApp::HandleRead, this, socket);
              return;
            }
          auto response = std::string ("r;");
          response.append (m_query_peers_func_name);
          response.append (";");
          response.append (std::to_string (m_module_exec_result));
          response.append (";");

          auto response_str = response.c_str ();
          auto resp_len = strlen (response_str);

          NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                    << " SEND_PACKET_EXECUTE_MODULE_RESULT_FROM_PEER "
                                    << response_str);

          auto p = Create<Packet> ((uint8_t *) response_str, resp_len);
          SeqTsHeader seqTs;
          seqTs.SetSeq (m_sent);
          p->AddHeader (seqTs);
          socket->SendTo (p, 0, m_original_module_requester);

          // Cleanup
          m_has_module_exec_result = false;
        }
      else
        {
          auto response = std::string ("n;");
          response.append (m_query_peers_func_name);
          response.append (";");

          auto response_str = response.c_str ();
          auto resp_len = strlen (response_str);

          auto p = Create<Packet> ((uint8_t *) response_str, resp_len);
          SeqTsHeader seqTs;
          seqTs.SetSeq (m_sent);
          p->AddHeader (seqTs);
          socket->SendTo (p, 0, m_original_module_requester);
          m_sent++;

          NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                    << "SENT_PACKET_PEER_MODULE_QUERY_NOT_FOUND");
        }

      m_peer_module_exec_query_state = 0;
      return;
    }

  while ((packet = socket->RecvFrom (from)))
    {
      socket->GetSockName (localAddress);
      m_rxTrace (packet);
      m_rxTraceWithAddresses (packet, from, localAddress);
      m_original_module_requester = from;
      if (packet->GetSize () > 0)
        {
          //   uint32_t receivedSize = packet->GetSize ();
          SeqTsHeader seqTs;
          packet->RemoveHeader (seqTs);
          // uint32_t currentSequenceNumber = seqTs.GetSeq ();

          auto resp = HandlePeerPacket (packet, socket, from);

          if (resp->GetSize () > 0)
            {
              socket->SendTo (resp, 0, from);
              m_sent++;
            }
          // NS_LOG_INFO ("TraceDelay: RX " << receivedSize << " bytes from "
          //                                << InetSocketAddress::ConvertFrom (from).GetIpv4 ()
          //                                << " Sequence Number: " << currentSequenceNumber
          //                                << " Uid: " << packet->GetUid () << " TXtime: "
          //                                << seqTs.GetTs () << " RXtime: " << Simulator::Now ()
          //                                << " Delay: " << Simulator::Now () - seqTs.GetTs ());
        }
    }
}

Ptr<Packet>
CustomApp::HandlePeerPacket (Ptr<Packet> packet, Ptr<Socket> socket, Address from)
{
  NS_LOG_FUNCTION (this);

  auto packetSize = packet->GetSize ();
  u_int8_t *data = new u_int8_t[packetSize];
  packet->CopyData (data, packetSize);

  std::vector<std::string> tokens;
  switch ((char) data[0])
    {

      // load module handler
      case 'l': {
        NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                  << "RECEIVED_PACKET_MODULE_LOAD_REQUEST"
                                  << " " << data);

        auto moduleName = std::string ((char *) data + 2);
        moduleName.pop_back ();

        auto base64_data = get_runtime_module_base64_data (m_runtime_id, moduleName.c_str ());

        auto respStr = std::string ("c;");
        respStr.append (moduleName);
        respStr.append (";");
        respStr.append (base64_data);
        respStr.append (";");
        free_ffi_string ((char *) base64_data);

        NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                  << "SEND_PACKET_MODULE_LOAD_RESPONSE"
                                  << " " << moduleName << " " << respStr.size ());

        auto resp = Create<Packet> ((uint8_t *) respStr.c_str (), respStr.size ());

        SeqTsHeader seqTs;
        seqTs.SetSeq (m_sent);
        resp->AddHeader (seqTs);
        return resp;

        break;
      }
      // execute module handler
      case 'e': {
        NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds () << " "
                                  << "RECEIVED_PACKET_EXECUTE_MODULE_REQUEST"
                                  << " " << data);

        auto s = std::string ((char *) data);

        size_t startPos = 0;
        size_t endPos = 0;
        std::string token;
        while ((endPos = s.find (";", startPos)) != std::string::npos)
          {
            token = s.substr (startPos, endPos - startPos);
            tokens.push_back (token);
            startPos = endPos + 1;
          }

        tokens.push_back (s.substr (startPos));

        bool isModuleRegistered = is_module_registered (m_runtime_id, tokens[1.0].c_str ());
        m_query_peers_func_name = std::string (tokens[2].c_str ());
        m_query_peers_func_args = std::vector<int32_t> (2);
        m_query_peers_func_args[0] = std::stoi (tokens[3]);
        m_query_peers_func_args[1] = std::stoi (tokens[4]);

        if (isModuleRegistered)
          {

            auto func = WasmFunction{};

            func.name = m_query_peers_func_name.c_str ();
            auto arg1s = std::to_string (m_query_peers_func_args[0]);
            auto arg2s = std::to_string (m_query_peers_func_args[1]);

            func.args[0] = WasmArg{
                arg1s.c_str (),
                ArgType::I32,
            };

            func.args[1] = WasmArg{
                arg2s.c_str (),
                ArgType::I32,
            };

            auto result = execute_module (m_runtime_id, tokens[1].c_str (), func);

            auto response = std::string ("r;");
            response.append (tokens[1]);
            response.append (";");
            response.append (std::to_string (result));
            response.append (";");

            auto response_str = response.c_str ();
            auto resp_len = strlen (response_str);

            NS_LOG_INFO (m_runtime_id << " " << Simulator::Now ().GetMilliSeconds ()
                                      << " SEND_PACKET_EXECUTE_MODULE_RESULT " << response_str);

            auto p = Create<Packet> ((uint8_t *) response_str, resp_len);
            SeqTsHeader seqTs;
            seqTs.SetSeq (m_sent);
            p->AddHeader (seqTs);
            return p;
          }
        else
          {
            m_peer_module_exec_query_state = 1;
            QueryPeersForModule ((char *) tokens[1].c_str ());
            Simulator::Schedule (MicroSeconds (m_microseconds_eventloop_interval),
                                 &CustomApp::HandleRead, this, socket);
          }
        break;
      }

    default:
      break;
    }

  uint8_t *default_resp = new uint8_t[1];
  default_resp[0] = '0';
  auto p = Create<Packet> (default_resp, 1);
  SeqTsHeader seqTs;
  seqTs.SetSeq (m_sent);
  p->AddHeader (seqTs);
  return p;
}

} // Namespace ns3
