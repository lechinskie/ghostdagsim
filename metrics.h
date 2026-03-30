/**
 * @file metrics.cc
 * @brief Data collector definitions, macros and core metrics structures
 * @author Eduardo Ramos <eduardo_ramos@edu.univali.br>
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

#include "thirdparty/json.h"
#include <filesystem>
#include <fstream>
#include <string>

class EventLogger {
public:
  static EventLogger &Get() {
    static EventLogger instance;
    return instance;
  }

  void Init(const std::string &output_dir, uint32_t rank) {
    std::string path =
        output_dir + "/rank" + std::to_string(rank) + "/events.jsonl";
    std::filesystem::create_directories(output_dir + "/rank" +
                                        std::to_string(rank));
    m_file.open(path, std::ios::out | std::ios::app);
    m_file.rdbuf()->pubsetbuf(m_io_buffer, sizeof(m_io_buffer));

    m_buffer.reserve(FLUSH_THRESHOLD);

    m_enabled = m_file.is_open();
  }

  void Write(nlohmann::json &obj) {
    if (!m_enabled)
      return;
    m_buffer += obj.dump();
    m_buffer += '\n';
    if (m_buffer.size() >= FLUSH_THRESHOLD)
      flush();
  }

  void Close() {
    flush();
    if (m_file.is_open())
      m_file.close();
    m_enabled = false;
  }

  bool IsEnabled() const { return m_enabled; }

private:
  void flush() {
    if (m_buffer.empty())
      return;
    m_file.write(m_buffer.data(),
                 static_cast<std::streamsize>(m_buffer.size()));
    m_buffer.clear();
  }

  EventLogger() : m_enabled(false) {}
  ~EventLogger() { Close(); }
  EventLogger(const EventLogger &) = delete;
  EventLogger &operator=(const EventLogger &) = delete;

  static constexpr size_t FLUSH_THRESHOLD = 1 * 1024 * 1024;

  std::ofstream m_file;
  std::string m_buffer;
  char m_io_buffer[4 * 1024 * 1024];
  bool m_enabled;
};

#ifdef GHOSTDAGSIM_METRICS

#define LOG_FIELD(key, value) obj[(key)] = (value);

#define LOG_EVENT(event_name, ...)                                             \
  do {                                                                         \
    if (!EventLogger::Get().IsEnabled())                                       \
      break;                                                                   \
    nlohmann::json obj;                                                        \
    obj["event"] = (event_name);                                               \
    obj["t"] = ns3::Simulator::Now().GetSeconds();                             \
    __VA_ARGS__                                                                \
    EventLogger::Get().Write(obj);                                             \
  } while (0)

#else

#define LOG_FIELD(key, value)
#define LOG_EVENT(event_name, ...)                                             \
  do {                                                                         \
  } while (0)

#endif

// clang-format off

// --- Block lifecycle --------------------------------------------------------

#define EVENT_BLOCK_MINED(node, block, parent_hashes, parent_count)    \
  LOG_EVENT("block_mined",                                             \
    LOG_FIELD("node",         (uint64_t)(node))                        \
    LOG_FIELD("block",        (uint64_t)(block))                       \
    LOG_FIELD("parents",      (parent_hashes))                         \
    LOG_FIELD("parent_count", (uint32_t)(parent_count))                \
  )

#define EVENT_BLOCK_RECEIVED(node, block, from, size_bytes,            \
                             total_txs, already_known, parent_count)   \
  LOG_EVENT("block_received",                                          \
    LOG_FIELD("node",          (uint64_t)(node))                       \
    LOG_FIELD("block",         (uint64_t)(block))                      \
    LOG_FIELD("from",          (std::string)(from))                    \
    LOG_FIELD("size_bytes",    (uint32_t)(size_bytes))                 \
    LOG_FIELD("total_txs",     (uint32_t)(total_txs))                  \
    LOG_FIELD("already_known", (uint32_t)(already_known))              \
    LOG_FIELD("overlap_ratio", (total_txs) > 0                         \
                                 ? (double)(already_known)/(total_txs) \
                                 : 1.0)                                \
    LOG_FIELD("parent_count",  (uint32_t)(parent_count))               \
  )

#define EVENT_BLOCK_ORPHANED(node, block, missing_parents)  \
  LOG_EVENT("block_orphaned",                               \
    LOG_FIELD("node",            (uint64_t)(node))          \
    LOG_FIELD("block",           (uint64_t)(block))         \
    LOG_FIELD("missing_parents", (missing_parents))         \
  )

#define EVENT_BLOCK_UNORPHANED(node, block)  \
  LOG_EVENT("block_unorphaned",              \
    LOG_FIELD("node",  (uint64_t)(node))     \
    LOG_FIELD("block", (uint64_t)(block))    \
  )

#define EVENT_BLOCK_COLORED(node, block, is_blue, blue_score, dag_width)  \
  LOG_EVENT("block_colored",                                              \
    LOG_FIELD("node",       (uint64_t)(node))                             \
    LOG_FIELD("block",      (uint64_t)(block))                            \
    LOG_FIELD("is_blue",    (bool)(is_blue))                              \
    LOG_FIELD("blue_score", (uint64_t)(blue_score))                       \
    LOG_FIELD("dag_width",  (uint64_t)(dag_width))                        \
  )

// --- Network messages -------------------------------------------------------

#define EVENT_MSG_SENT(node, peer, msg_type, block, bytes)  \
  LOG_EVENT("msg_sent",                                     \
    LOG_FIELD("node",     (uint64_t)(node))                 \
    LOG_FIELD("peer",     (std::string)(peer))              \
    LOG_FIELD("msg_type", (std::string)(msg_type))          \
    LOG_FIELD("block",    (uint64_t)(block))                \
    LOG_FIELD("bytes",    (uint32_t)(bytes))                \
  )

#define EVENT_MSG_RECV(node, peer, msg_type, block, bytes)  \
  LOG_EVENT("msg_recv",                                     \
    LOG_FIELD("node",     (uint64_t)(node))                 \
    LOG_FIELD("peer",     (std::string)(peer))              \
    LOG_FIELD("msg_type", (std::string)(msg_type))          \
    LOG_FIELD("block",    (uint64_t)(block))                \
    LOG_FIELD("bytes",    (uint32_t)(bytes))                \
  )

// --- Transactions -----------------------------------------------------------

#define EVENT_TX_GENERATED(node, tx_id, fee)  \
  LOG_EVENT("tx_generated",                   \
    LOG_FIELD("node",  (uint64_t)(node))      \
    LOG_FIELD("tx_id", (uint64_t)(tx_id))     \
    LOG_FIELD("fee",   (uint32_t)(fee))       \
  )

#define EVENT_TX_CONFIRMED(node, tx_id, block, gen_t, is_blue)  \
  LOG_EVENT("tx_confirmed",                                      \
    LOG_FIELD("node",    (uint64_t)(node))                       \
    LOG_FIELD("tx_id",   (uint64_t)(tx_id))                      \
    LOG_FIELD("block",   (uint64_t)(block))                      \
    LOG_FIELD("gen_t",   (double)(gen_t))                        \
    LOG_FIELD("latency", ns3::Simulator::Now().GetSeconds()      \
                           - (double)(gen_t))                    \
    LOG_FIELD("is_blue", (bool)(is_blue))                        \
  )

// --- Graphene ---------------------------------------------------------------

#define EVENT_GRAPHENE_REQ_SENT(node, block, peer)  \
  LOG_EVENT("graphene_req_sent",                    \
    LOG_FIELD("node",  (uint64_t)(node))            \
    LOG_FIELD("block", (uint64_t)(block))           \
    LOG_FIELD("peer",  (std::string)(peer))         \
  )

#define EVENT_GRAPHENE_BLOCK_RECV(node, block, iblt_ok,       \
                                  bytes_total, bytes_saved)   \
  LOG_EVENT("graphene_block_recv",                            \
    LOG_FIELD("node",         (uint64_t)(node))               \
    LOG_FIELD("block",        (uint64_t)(block))              \
    LOG_FIELD("iblt_success", (bool)(iblt_ok))                \
    LOG_FIELD("bytes_total",  (uint32_t)(bytes_total))        \
    LOG_FIELD("bytes_saved",  (uint32_t)(bytes_saved))        \
  )

#define EVENT_GRAPHENE_FALLBACK(node, block, missing_count)  \
  LOG_EVENT("graphene_fallback",                             \
    LOG_FIELD("node",          (uint64_t)(node))             \
    LOG_FIELD("block",         (uint64_t)(block))            \
    LOG_FIELD("missing_count", (uint32_t)(missing_count))    \
  )

// --- Compact blocks ---------------------------------------------------------

#define EVENT_COMPACT_SENT(node, block, peer,           \
                           full_txs, short_ids, bytes)  \
  LOG_EVENT("compact_sent",                             \
    LOG_FIELD("node",      (uint64_t)(node))            \
    LOG_FIELD("block",     (uint64_t)(block))           \
    LOG_FIELD("peer",      (std::string)(peer))         \
    LOG_FIELD("full_txs",  (uint32_t)(full_txs))        \
    LOG_FIELD("short_ids", (uint32_t)(short_ids))       \
    LOG_FIELD("bytes",     (uint32_t)(bytes))           \
  )

#define EVENT_COMPACT_MISSING(node, block, missing_count)  \
  LOG_EVENT("compact_missing",                             \
    LOG_FIELD("node",          (uint64_t)(node))           \
    LOG_FIELD("block",         (uint64_t)(block))          \
    LOG_FIELD("missing_count", (uint32_t)(missing_count))  \
  )
