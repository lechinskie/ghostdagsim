#include "dag.h"
#include "metrics.h"

#include <queue>

std::set<int> Blockchain::GetPast(int block_id) {
  auto it = past_cache_.find(block_id);
  if (it != past_cache_.end())
    return it->second;

  std::set<int> &past = past_cache_[block_id];
  std::queue<int> q;

  for (int p : blocks[block_id].header.parent_hashes)
    if (!past.count(p)) {
      past.insert(p);
      q.push(p);
    }

  while (!q.empty()) {
    int cur = q.front();
    q.pop();
    auto bit = blocks.find(cur);
    if (bit == blocks.end())
      continue;
    for (int p : bit->second.header.parent_hashes)
      if (!past.count(p)) {
        past.insert(p);
        q.push(p);
      }
  }
  return past;
}

std::set<int> Blockchain::GetFuture(int block_id) const {
  std::set<int> future;
  std::queue<int> q;
  auto ci = children.find(block_id);
  if (ci != children.end())
    for (int c : ci->second) {
      future.insert(c);
      q.push(c);
    }

  while (!q.empty()) {
    int cur = q.front();
    q.pop();
    auto ci2 = children.find(cur);
    if (ci2 == children.end())
      continue;
    for (int c : ci2->second)
      if (!future.count(c)) {
        future.insert(c);
        q.push(c);
      }
  }
  return future;
}

// Kahn over an arbitrary subset
std::vector<int> Blockchain::TopologicalSort(const std::set<int> &subset) {
  std::map<int, int> indeg;
  for (int b : subset) {
    indeg[b] = 0;
    for (int p : blocks[b].header.parent_hashes)
      if (subset.count(p))
        ++indeg[b];
  }
  std::queue<int> q;
  for (auto &[b, d] : indeg)
    if (d == 0)
      q.push(b);

  std::vector<int> result;
  result.reserve(subset.size());
  while (!q.empty()) {
    int cur = q.front();
    q.pop();
    result.push_back(cur);
    auto ci = children.find(cur);
    if (ci != children.end())
      for (int ch : ci->second)
        if (subset.count(ch) && --indeg[ch] == 0)
          q.push(ch);
  }
  return result;
}

std::set<int> Blockchain::CalculateBlueSet(int block_id) {
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
std::set<int> Blockchain::GreedyBlueSet(int block_id) {
  if (blocks[block_id].header.parent_hashes.empty())
    return {block_id};

  // 1. Selected parent
  int sp = -1;
  int max_score = -1;
  for (int p : blocks[block_id].header.parent_hashes) {
    int s = blocks[p].blue_score;
    if (s > max_score || (s == max_score && (sp == -1 || p < sp))) {
      max_score = s;
      sp = p;
    }
  }

  // 2. Inherit sp's blue set
  std::set<int> blue = blocks[sp].blue_set;
  std::set<int> past_sp = GetPast(sp);
  std::set<int> past_block = GetPast(block_id);

  // 3. Merge set
  std::set<int> merge_set;
  for (int b : past_block)
    if (b != sp && !past_sp.count(b))
      merge_set.insert(b);

  // 4. Process merge set in topological order
  for (int candidate : TopologicalSort(merge_set)) {
    std::set<int> past_cand = GetPast(candidate);

    int anticone_blues = 0;
    for (int b : blue) {
      bool b_is_anc = past_cand.count(b) > 0;
      bool cand_is_anc = GetPast(b).count(candidate) > 0;
      if (!b_is_anc && !cand_is_anc)
        if (++anticone_blues > ghostdag_k)
          break;
    }
    if (anticone_blues <= ghostdag_k)
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
std::set<int> Blockchain::GreedyBlueSetFromTip(int tip_id,
                                               const std::set<int> &past_set) {
  if (past_set.empty())
    return GreedyBlueSet(tip_id);
  if (blocks[tip_id].header.parent_hashes.empty())
    return {tip_id};

  int sp = -1;
  int max_score = -1;
  for (int p : blocks[tip_id].header.parent_hashes) {
    int s = blocks[p].blue_score;
    if (s > max_score || (s == max_score && (sp == -1 || p < sp))) {
      max_score = s;
      sp = p;
    }
  }

  std::set<int> blue = blocks[sp].blue_set;
  std::set<int> past_sp = GetPast(sp);

  std::set<int> merge_set;
  for (int b : past_set)
    if (b != sp && !past_sp.count(b))
      merge_set.insert(b);

  for (int candidate : TopologicalSort(merge_set)) {
    std::set<int> past_cand = GetPast(candidate);
    int anticone_blues = 0;
    for (int b : blue) {
      bool b_is_anc = past_cand.count(b) > 0;
      bool cand_is_anc = GetPast(b).count(candidate) > 0;
      if (!b_is_anc && !cand_is_anc)
        if (++anticone_blues > ghostdag_k)
          break;
    }
    if (anticone_blues <= ghostdag_k)
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
int Blockchain::CalculateBlueScore(int block_id,
                                   const std::set<int> &blue_set) {
  int score = 0;
  for (int b : GetPast(block_id))
    if (blue_set.count(b))
      ++score;
  return score + 1; // +1 for block_id itself (always in its own blue_set)
}

void Blockchain::AddBlock(const Block &new_block) {
  int block_id = new_block.header.block_id;

  // Orphan check
  for (int p : new_block.header.parent_hashes) {
    if (!blocks.count(p)) {
      orphans[block_id] = new_block;
      return;
    }
  }

  // Insert
  blocks[block_id] = new_block;
  past_cache_.erase(block_id);

  for (int p : new_block.header.parent_hashes) {
    children[p].insert(block_id);
    tips.erase(p);
  }
  tips.insert(block_id);

  // GHOSTDAG data
  std::set<int> blue_set = GreedyBlueSet(block_id);
  int blue_score = CalculateBlueScore(block_id, blue_set);

  int sp = -1;
  int max_sp = -1;
  for (int p : new_block.header.parent_hashes) {
    int s = blocks[p].blue_score;
    if (s > max_sp || (s == max_sp && (sp == -1 || p < sp))) {
      max_sp = s;
      sp = p;
    }
  }

  blocks[block_id].blue_set = blue_set;
  blocks[block_id].blue_score = blue_score;
  blocks[block_id].selected_parent = sp;

  // is_blue = appears in selected tip's blue_set
  int sel_tip = SelectTip();
  bool is_blue =
      (sel_tip == block_id) ||
      (sel_tip != -1 && blocks[sel_tip].blue_set.count(block_id) > 0);
  blocks[block_id].is_blue = is_blue;

  // Re-evaluate is_blue for ALL accepted blocks.
  // is_blue = "this block is in the current selected tip's blue_set".
  // We must update every block – not just current tips – because when a merge
  // block is added, previously non-tip blocks (now merged) need updating too.
  sel_tip = SelectTip();
  for (auto &[id, blk] : blocks)
    blk.is_blue = (sel_tip != -1 && blocks[sel_tip].blue_set.count(id) > 0);

  ProcessOrphans();
}

void Blockchain::ProcessOrphans() {
  bool progress = true;
  while (progress) {
    progress = false;
    std::vector<int> ready;
    for (auto &[oid, oblk] : orphans) {
      bool can_add = true;
      for (int p : oblk.header.parent_hashes)
        if (!blocks.count(p)) {
          can_add = false;
          break;
        }
      if (can_add)
        ready.push_back(oid);
    }
    for (int oid : ready) {
      Block ob = orphans[oid];
      orphans.erase(oid);
      AddBlock(ob);
      progress = true;
    }
  }
}

int Blockchain::SelectTip() {
  if (tips.empty())
    return -1;
  int best = -1;
  int best_score = -1;
  for (int t : tips) {
    int s = blocks.at(t).blue_score;
    if (s > best_score || (s == best_score && (best == -1 || t < best))) {
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
std::vector<int> Blockchain::ComputeGHOSTDAGOrdering() {
  std::map<int, int> indeg;
  for (auto &[id, blk] : blocks) {
    indeg[id] = 0;
    for (int p : blk.header.parent_hashes)
      if (blocks.count(p))
        ++indeg[id];
  }

  auto cmp = [this](int a, int b) {
    const Block &ba = blocks.at(a);
    const Block &bb = blocks.at(b);
    if (ba.blue_score != bb.blue_score)
      return ba.blue_score < bb.blue_score;
    if (ba.header.time_created != bb.header.time_created)
      return ba.header.time_created > bb.header.time_created;
    return a > b;
  };
  std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);
  for (auto &[id, d] : indeg)
    if (d == 0)
      pq.push(id);

  std::vector<int> ordering;
  ordering.reserve(blocks.size());
  while (!pq.empty()) {
    int cur = pq.top();
    pq.pop();
    ordering.push_back(cur);
    auto ci = children.find(cur);
    if (ci != children.end())
      for (int ch : ci->second)
        if (--indeg[ch] == 0)
          pq.push(ch);
  }
  return ordering;
}

bool Blockchain::IsKCluster(const std::set<int> &blue_set) {
  for (int b : blue_set) {
    std::set<int> past_b = GetPast(b);
    std::set<int> fut_b = GetFuture(b);
    int ac = 0;
    for (int x : blue_set) {
      if (x == b)
        continue;
      if (!past_b.count(x) && !fut_b.count(x))
        if (++ac > ghostdag_k)
          return false;
    }
  }
  return true;
}

bool Blockchain::IsKClusterSubset(const std::set<int> &blue_set) {
  std::set<int> existing;
  for (int b : blue_set)
    if (blocks.count(b))
      existing.insert(b);
  return IsKCluster(existing);
}

int Blockchain::GetDagWidth() const { return static_cast<int>(tips.size()); }
bool Blockchain::HasBlock(int id) const { return blocks.count(id) > 0; }
bool Blockchain::IsRed(int id) const {
  auto it = blocks.find(id);
  return it != blocks.end() && !it->second.is_blue;
}
bool Blockchain::IsOrphan(int id) const { return orphans.count(id) > 0; }

std::vector<const Block *> Blockchain::GetChildrenPointers(const Block &block) {
  std::vector<const Block *> out;
  auto ci = children.find(block.header.block_id);
  if (ci == children.end())
    return out;
  for (int ch : ci->second) {
    auto bit = blocks.find(ch);
    if (bit != blocks.end())
      out.push_back(&bit->second);
  }
  return out;
}

std::vector<const Block *> Blockchain::GetParentsPointers(const Block &block) {
  std::vector<const Block *> out;
  for (int p : block.header.parent_hashes) {
    auto bit = blocks.find(p);
    if (bit != blocks.end())
      out.push_back(&bit->second);
  }
  return out;
}
