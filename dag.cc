#include "dag.h"
#include "metrics.h"

#include <cstdint>
#include <queue>

std::set<uint64_t> Blockchain::GetPast(uint64_t block_id) {
  auto it = past_cache_.find(block_id);
  if (it != past_cache_.end())
    return it->second;

  std::set<uint64_t> &past = past_cache_[block_id];
  std::queue<uint64_t> q;

  for (uint64_t p : blocks[block_id].header.parent_hashes)
    if (!past.count(p)) {
      past.insert(p);
      q.push(p);
    }

  while (!q.empty()) {
    uint64_t cur = q.front();
    q.pop();
    auto bit = blocks.find(cur);
    if (bit == blocks.end())
      continue;
    for (uint64_t p : bit->second.header.parent_hashes)
      if (!past.count(p)) {
        past.insert(p);
        q.push(p);
      }
  }
  return past;
}

std::set<uint64_t> Blockchain::GetFuture(uint64_t block_id) const {
  std::set<uint64_t> future;
  std::queue<uint64_t> q;
  auto ci = children.find(block_id);
  if (ci != children.end())
    for (uint64_t c : ci->second) {
      future.insert(c);
      q.push(c);
    }

  while (!q.empty()) {
    uint64_t cur = q.front();
    q.pop();
    auto ci2 = children.find(cur);
    if (ci2 == children.end())
      continue;
    for (uint64_t c : ci2->second)
      if (!future.count(c)) {
        future.insert(c);
        q.push(c);
      }
  }
  return future;
}

// Kahn over an arbitrary subset
std::vector<uint64_t>
Blockchain::TopologicalSort(const std::set<uint64_t> &subset) {
  std::map<uint64_t, uint64_t> indeg;
  for (uint64_t b : subset) {
    indeg[b] = 0;
    for (uint64_t p : blocks[b].header.parent_hashes)
      if (subset.count(p))
        ++indeg[b];
  }
  std::queue<uint64_t> q;
  for (auto &[b, d] : indeg)
    if (d == 0)
      q.push(b);

  std::vector<uint64_t> result;
  result.reserve(subset.size());
  while (!q.empty()) {
    uint64_t cur = q.front();
    q.pop();
    result.push_back(cur);
    auto ci = children.find(cur);
    if (ci != children.end())
      for (uint64_t ch : ci->second)
        if (subset.count(ch) && --indeg[ch] == 0)
          q.push(ch);
  }
  return result;
}

std::set<uint64_t> Blockchain::CalculateBlueSet(uint64_t block_id) {
  return GreedyBlueSet(block_id);
}

// GreedyBlueSet – GHOSTDAG algorithm
//
// Steps:
//   1. Selected parent (sp) = parent with highest blue_score (lower id wins
//   ties).
//   2. Inherit sp.blue_set as the starting blue set.
//   3. Merge set = past(block_id) \ ( past(sp) ∪ {sp} ).
//   4. Process merge set in topological order.  For candidate C:
//        anticone_blues = |{ b ∈ blue : neither b∈past(C) nor C∈past(b) }|
//        If anticone_blues ≤ k  →  add C to blue.
//   5. block_id is always blue (all blue blocks are in its past → anticone =
//   0).
std::set<uint64_t> Blockchain::GreedyBlueSet(uint64_t block_id) {
  if (blocks[block_id].header.parent_hashes.empty())
    return {block_id};

  std::optional<uint64_t> sp_u;
  std::optional<uint64_t> max_score_u;
  for (uint64_t p : blocks[block_id].header.parent_hashes) {
    uint64_t s = blocks[p].blue_score;
    if (!max_score_u.has_value() || s > max_score_u ||
        (s == max_score_u && p < sp_u)) {
      max_score_u = s;
      sp_u = p;
    }
  }
  if (!sp_u.has_value()) {
    return {block_id}; // genesis
  }
  uint64_t sp = sp_u.value();

  // 2. Inherit sp's blue set
  std::set<uint64_t> blue = blocks[sp].blue_set;
  std::set<uint64_t> past_sp = GetPast(sp);
  std::set<uint64_t> past_block = GetPast(block_id);

  // 3. Merge set
  std::set<uint64_t> merge_set;
  for (uint64_t b : past_block)
    if (b != sp && !past_sp.count(b))
      merge_set.insert(b);

  // 4. Process merge set in topological order
  std::map<uint64_t, std::set<uint64_t>> blue_pasts;
  for (uint64_t b : blue)
    blue_pasts[b] = GetPast(b);

  for (uint64_t candidate : TopologicalSort(merge_set)) {
    auto past_cand = GetPast(candidate);
    uint64_t anticone_blues = 0;
    for (uint64_t b : blue) {
      bool b_in_past_cand = past_cand.count(b);
      bool cand_in_past_b = blue_pasts[b].count(candidate);
      if (!b_in_past_cand && !cand_in_past_b)
        if (++anticone_blues > (uint64_t)ghostdag_k)
          break;
    }
    if (anticone_blues <= (uint64_t)ghostdag_k)
      blue.insert(candidate);
  }

  // 5. block_id is always blue
  blue.insert(block_id);
  return blue;
}

// GreedyBlueSetFromTip
//
// Same as GreedyBlueSet but uses a caller-supplied past_set (avoids
// recomputing it when the caller already has it).
std::set<uint64_t>
Blockchain::GreedyBlueSetFromTip(uint64_t tip_id,
                                 const std::set<uint64_t> &past_set) {
  if (past_set.empty())
    return GreedyBlueSet(tip_id);
  if (blocks[tip_id].header.parent_hashes.empty())
    return {tip_id};

  std::optional<uint64_t> sp_u;
  std::optional<uint64_t> max_score_u;

  for (uint64_t p : blocks[tip_id].header.parent_hashes) {
    uint64_t s = blocks[p].blue_score;
    if (!max_score_u.has_value() || s > max_score_u ||
        (s == max_score_u && p < sp_u)) {
      max_score_u = s;
      sp_u = p;
    }
  }

  if (!sp_u.has_value()) {
    return {}; // no parents — shouldn't happen outside genesis
  }
  uint64_t sp = sp_u.value();

  std::set<uint64_t> blue = blocks[sp].blue_set;
  std::set<uint64_t> past_sp = GetPast(sp);

  std::set<uint64_t> merge_set;
  for (uint64_t b : past_set)
    if (b != sp && !past_sp.count(b))
      merge_set.insert(b);

  for (uint64_t candidate : TopologicalSort(merge_set)) {
    std::set<uint64_t> past_cand = GetPast(candidate);
    uint64_t anticone_blues = 0;
    for (uint64_t b : blue) {
      bool b_is_anc = past_cand.count(b) > 0;
      bool cand_is_anc = GetPast(b).count(candidate) > 0;
      if (!b_is_anc && !cand_is_anc)
        if (++anticone_blues > (uint64_t)ghostdag_k)
          break;
    }
    if (anticone_blues <= (uint64_t)ghostdag_k)
      blue.insert(candidate);
  }

  blue.insert(tip_id);
  return blue;
}

// CalculateBlueScore
// = |{ x ∈ past(block_id) : x ∈ blue_set }| + 1
//
// The +1 counts block_id itself, which is always in its own blue_set.
// Matches the Kaspa convention: blue_score = total blue blocks up to and
// including this block. Genesis constructor sets score=1 = 0 (empty past) + 1.
int Blockchain::CalculateBlueScore(uint64_t block_id,
                                   const std::set<uint64_t> &blue_set) {
  uint64_t score = 0;
  for (uint64_t b : GetPast(block_id))
    if (blue_set.count(b))
      ++score;
  return score + 1; // +1 for block_id itself (always in its own blue_set)
}

void Blockchain::AddBlock(const Block &new_block) {
  uint64_t block_id = new_block.header.block_id;

  // Orphan check
  for (uint64_t p : new_block.header.parent_hashes) {
    if (!blocks.count(p)) {
      orphans[block_id] = new_block;

      METRIC_BLOCK_ORPHANED(node_id_metric, block_id,
                            new_block.header.parent_hashes, NOW);
      return;
    }
  }

  // Insert
  blocks[block_id] = new_block;
  past_cache_.erase(block_id);

  for (uint64_t p : new_block.header.parent_hashes) {
    children[p].insert(block_id);
    tips.erase(p);
  }
  tips.insert(block_id);

  // GHOSTDAG data
  std::set<uint64_t> blue_set = GreedyBlueSet(block_id);
  uint64_t blue_score = CalculateBlueScore(block_id, blue_set);

  std::optional<uint64_t> sp_u;
  std::optional<uint64_t> max_sp_u;
  for (uint64_t p : new_block.header.parent_hashes) {
    uint64_t s = blocks[p].blue_score;
    if (!max_sp_u.has_value() || s > max_sp_u || (s == max_sp_u && p < sp_u)) {
      max_sp_u = s;
      sp_u = p;
    }
  }
  if (!sp_u.has_value()) {
    return; // genesis — no parents
  }
  uint64_t sp = sp_u.value();

  blocks[block_id].blue_set = blue_set;
  blocks[block_id].blue_score = blue_score;
  blocks[block_id].selected_parent = sp;

  // is_blue = appears in selected tip's blue_set
  std::optional<uint64_t> sel_tip = SelectTip();
  bool is_blue = sel_tip.has_value() &&
                 (sel_tip.value() == block_id ||
                  blocks[sel_tip.value()].blue_set.count(block_id) > 0);

  blocks[block_id].is_blue = is_blue;

  // Re-evaluate is_blue for ALL accepted blocks.
  // is_blue = "this block is in the current selected tip's blue_set".
  // We must update every block – not just current tips – because when a merge
  // block is added, previously non-tip blocks (now merged) need updating too.
  sel_tip = SelectTip();
  if (sel_tip.has_value()) {
    for (auto &[id, blk] : blocks)
      blk.is_blue = blocks[sel_tip.value()].blue_set.count(id) > 0;
  }

  ProcessOrphans();
}

void Blockchain::ProcessOrphans() {
  bool progress = true;
  while (progress) {
    progress = false;
    std::vector<uint64_t> ready;
    for (auto &[oid, oblk] : orphans) {
      bool can_add = true;
      for (uint64_t p : oblk.header.parent_hashes)
        if (!blocks.count(p)) {
          can_add = false;
          break;
        }
      if (can_add)
        ready.push_back(oid);
    }
    for (uint64_t oid : ready) {
      auto it = orphans.find(oid);
      if (it == orphans.end())
        continue;

      Block ob = it->second;
      orphans.erase(it);
      AddBlock(ob);

      if (HasBlock(ob.header.block_id))
        METRIC_BLOCK_UNORPHANED(node_id_metric, ob.header.block_id, NOW);
      progress = true;
    }
  }
}

std::optional<uint64_t> Blockchain::SelectTip() {
  if (tips.empty())
    return std::nullopt;

  std::optional<uint64_t> best;
  std::optional<uint64_t> best_score;
  for (uint64_t t : tips) {
    uint64_t s = blocks.at(t).blue_score;
    if (!best_score.has_value() || s > best_score ||
        (s == best_score && t < best)) {
      best_score = s;
      best = t;
    }
  }
  return best;
}

// ComputeGHOSTDAGOrdering
// Primary:   higher blue_score first
// Secondary: earlier time_created first
// Tertiary:  lower block_id first
std::vector<uint64_t> Blockchain::ComputeGHOSTDAGOrdering() {
  std::map<uint64_t, uint64_t> indeg;
  for (auto &[id, blk] : blocks) {
    indeg[id] = 0;
    for (uint64_t p : blk.header.parent_hashes)
      if (blocks.count(p))
        ++indeg[id];
  }

  auto cmp = [this](uint64_t a, uint64_t b) {
    const Block &ba = blocks.at(a);
    const Block &bb = blocks.at(b);
    if (ba.blue_score != bb.blue_score)
      return ba.blue_score < bb.blue_score;
    if (ba.header.time_created != bb.header.time_created)
      return ba.header.time_created > bb.header.time_created;
    return a > b;
  };
  std::priority_queue<uint64_t, std::vector<uint64_t>, decltype(cmp)> pq(cmp);
  for (auto &[id, d] : indeg)
    if (d == 0)
      pq.push(id);

  std::vector<uint64_t> ordering;
  ordering.reserve(blocks.size());
  while (!pq.empty()) {
    uint64_t cur = pq.top();
    pq.pop();
    ordering.push_back(cur);
    auto ci = children.find(cur);
    if (ci != children.end())
      for (uint64_t ch : ci->second)
        if (--indeg[ch] == 0)
          pq.push(ch);
  }
  return ordering;
}

bool Blockchain::IsKCluster(const std::set<uint64_t> &blue_set) {
  for (uint64_t b : blue_set) {
    std::set<uint64_t> past_b = GetPast(b);
    std::set<uint64_t> fut_b = GetFuture(b);
    uint64_t ac = 0;
    for (uint64_t x : blue_set) {
      if (x == b)
        continue;
      if (!past_b.count(x) && !fut_b.count(x))
        if (++ac > (uint64_t)ghostdag_k)
          return false;
    }
  }
  return true;
}

bool Blockchain::IsKClusterSubset(const std::set<uint64_t> &blue_set) {
  std::set<uint64_t> existing;
  for (uint64_t b : blue_set)
    if (blocks.count(b))
      existing.insert(b);
  return IsKCluster(existing);
}

uint64_t Blockchain::GetDagWidth() const { return tips.size(); }

bool Blockchain::HasBlock(uint64_t id) const { return blocks.count(id) > 0; }
bool Blockchain::IsRed(uint64_t id) const {
  auto it = blocks.find(id);
  return it != blocks.end() && !it->second.is_blue;
}
bool Blockchain::IsOrphan(uint64_t id) const { return orphans.count(id) > 0; }

std::vector<const Block *> Blockchain::GetChildrenPointers(const Block &block) {
  std::vector<const Block *> out;
  auto ci = children.find(block.header.block_id);
  if (ci == children.end())
    return out;
  for (uint64_t ch : ci->second) {
    auto bit = blocks.find(ch);
    if (bit != blocks.end())
      out.push_back(&bit->second);
  }
  return out;
}

std::vector<const Block *> Blockchain::GetParentsPointers(const Block &block) {
  std::vector<const Block *> out;
  for (uint64_t p : block.header.parent_hashes) {
    auto bit = blocks.find(p);
    if (bit != blocks.end())
      out.push_back(&bit->second);
  }
  return out;
}
