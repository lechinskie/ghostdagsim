// Invariants under test:
//   (I1) block.blue_set is immutable after insertion
//   (I2) block.blue_score = |past(B) ∩ B.blue_set|  (immutable after insertion)
//   (I3) block.is_blue    = B ∈ SelectTip().blue_set  (can change as DAG grows)
//   (I4) B.blue_set is always a k-cluster
//   (I5) B is always in B.blue_set
//   (I6) selected_parent is the parent with the highest blue_score
//   (I7) For every block B: B.blue_score ≥ B.selected_parent.blue_score
//   (I8) GetPast / GetFuture / GetAnticone are topologically correct
//   (I9) Orphans are held until all parents exist, then inserted correctly
//  (I10) Linear chain → every block is blue
//  (I11) With N≥k+2 parallel blocks from genesis, at most k+1 are in the
//        tip's blue_set (dominant tip + at most k anticone siblings)
//  (I12) Dominant chain beats competing chain of equal or lesser length
//  (I13) Merge block: inherits the selected parent's blue_set; additional
//        blocks from the merge set enter only if anticone constraint is met
//  (I14) ComputeGHOSTDAGOrdering is a valid topological ordering
//  (I15) SelectTip returns highest blue_score tip; lower id breaks ties

#include "../dag.h"
#include "../metrics.h"

#include <gtest/gtest.h>
#include <set>
#include <vector>

// ─── helpers
// ──────────────────────────────────────────────────────────────────

// Build a Block with the given id and parents (no other fields matter for
// GHOSTDAG).
static Block MakeBlock(int id, std::vector<int> parents = {}) {
  Block b;
  b.header.block_id = id;
  b.header.parent_hashes = std::move(parents);
  return b;
}

// Verify I4 for every block in the DAG.
static bool AllBlueSetsAreKClusters(Blockchain &dag) {
  for (auto &[id, blk] : dag.blocks)
    if (!dag.IsKCluster(blk.blue_set))
      return false;
  return true;
}

// Verify I2 for every block in the DAG.
// blue_score = |past(B) ∩ B.blue_set| + 1   (B always counts itself)
// Genesis: past={}, score = 0 + 1 = 1  (matches constructor special-case)
static bool AllBlueScoresCorrect(Blockchain &dag) {
  for (auto &[id, blk] : dag.blocks) {
    std::set<int> past = dag.GetPast(id);
    int expected = 1; // +1 for block itself (always in its own blue_set)
    for (int p : past)
      if (blk.blue_set.count(p))
        ++expected;
    if (blk.blue_score != expected)
      return false;
  }
  return true;
}

// ─── fixture
// ──────────────────────────────────────────────────────────────────

class GHOSTDAGTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

// ═════════════════════════════════════════════════════════════════════════════
// I – Genesis
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, Genesis_Invariants) {
  Blockchain dag(3);

  // blue_score = 1 (special-cased in constructor)
  EXPECT_EQ(dag.blocks[0].blue_score, 1);
  // is_blue = true
  EXPECT_TRUE(dag.blocks[0].is_blue);
  // blue_set = {0}
  EXPECT_EQ(dag.blocks[0].blue_set, (std::set<int>{0}));
  // genesis has no parents
  EXPECT_TRUE(dag.blocks[0].header.parent_hashes.empty());
  // genesis is the only tip initially
  EXPECT_EQ(dag.tips, (std::set<int>{0}));
  // SelectTip = 0
  EXPECT_EQ(dag.SelectTip(), 0);
}

TEST_F(GHOSTDAGTest, Genesis_RemainsBlueAfterManyBlocksAdded) {
  Blockchain dag(3);
  for (int i = 1; i <= 20; i++)
    dag.AddBlock(MakeBlock(i, {0}));

  // genesis must stay in the tip's blue_set forever
  int tip = dag.SelectTip();
  EXPECT_TRUE(dag.blocks[tip].blue_set.count(0))
      << "Genesis must always be in the selected tip's blue_set";
}

// ═════════════════════════════════════════════════════════════════════════════
// II – Linear chain (all blocks blue)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, LinearChain_AllBlocksBlue) {
  // 0 → 1 → 2 → 3 → 4 → 5  (k=0 or any k)
  Blockchain dag(0);
  for (int i = 1; i <= 5; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  // Every block should be in the selected tip's blue_set (no anticone at all)
  int tip = dag.SelectTip();
  EXPECT_EQ(tip, 5);
  for (int i = 0; i <= 5; i++)
    EXPECT_TRUE(dag.blocks[tip].blue_set.count(i))
        << "Block " << i << " should be blue in a linear chain";
}

TEST_F(GHOSTDAGTest, LinearChain_BlueScoresAreMonotonicallyIncreasing) {
  Blockchain dag(2);
  for (int i = 1; i <= 6; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  // blue_score must strictly increase along the chain
  for (int i = 1; i <= 6; i++)
    EXPECT_GT(dag.blocks[i].blue_score, dag.blocks[i - 1].blue_score)
        << "blue_score should strictly increase along a linear chain";
}

TEST_F(GHOSTDAGTest, LinearChain_SelectedParentIsDirectParent) {
  Blockchain dag(2);
  for (int i = 1; i <= 5; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  for (int i = 1; i <= 5; i++)
    EXPECT_EQ(dag.blocks[i].selected_parent, i - 1)
        << "selected_parent should be the direct parent in a chain";
}

// ═════════════════════════════════════════════════════════════════════════════
// III – Parallel blocks (anticone constraint)
// ═════════════════════════════════════════════════════════════════════════════

//
// k=0: two parallel blocks – only one can be blue (from tip's perspective)
//
TEST_F(GHOSTDAGTest, ParallelBlocks_K0_OnlyOneBlue) {
  Blockchain dag(0);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));

  // Both have blue_score=1; lower id wins → tip=1
  EXPECT_EQ(dag.SelectTip(), 1);
  EXPECT_TRUE(dag.blocks[1].is_blue);
  EXPECT_FALSE(dag.blocks[2].is_blue)
      << "With k=0, second parallel block must be red";
}

//
// k=1: two parallel blocks – AFTER a merge block both are blue
//
TEST_F(GHOSTDAGTest, ParallelBlocks_K1_BothBlueAfterMerge) {
  Blockchain dag(1);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  // Merge block whose past includes both 1 and 2
  dag.AddBlock(MakeBlock(3, {1, 2}));

  // block 3's blue_set must contain both 1 and 2
  EXPECT_TRUE(dag.blocks[3].blue_set.count(1))
      << "Block 1 should be in blue_set of merge block (k=1)";
  EXPECT_TRUE(dag.blocks[3].blue_set.count(2))
      << "Block 2 should be in blue_set of merge block (k=1)";
  // Both should be is_blue=true since 3 is the tip
  EXPECT_TRUE(dag.blocks[1].is_blue);
  EXPECT_TRUE(dag.blocks[2].is_blue);
}

//
// k=0: two parallel blocks – merge block can only inherit ONE chain
//
TEST_F(GHOSTDAGTest, ParallelBlocks_K0_MergeBlockOnlyInheritsOneChain) {
  Blockchain dag(0);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1, 2}));

  // blue_set(3) must NOT contain both 1 and 2 (they're in anticone, k=0)
  bool has1 = dag.blocks[3].blue_set.count(1) > 0;
  bool has2 = dag.blocks[3].blue_set.count(2) > 0;
  EXPECT_FALSE(has1 && has2) << "With k=0 both parallel blocks cannot both be "
                                "in merge block's blue_set";
  // block 3 itself is always in its own blue_set
  EXPECT_TRUE(dag.blocks[3].blue_set.count(3));
}

//
// N parallel blocks from genesis (no merge): at most k+1 are in ANY block's
// blue_set
//
TEST_F(GHOSTDAGTest, ManyParallelBlocks_AtMostKPlus1BlueFromMerge) {
  const int K = 3;
  const int N = 20;
  Blockchain dag(K);

  for (int i = 1; i <= N; i++)
    dag.AddBlock(MakeBlock(i, {0}));

  // Add a merge block that references all N parallel blocks
  std::vector<int> all_parents;
  for (int i = 1; i <= N; i++)
    all_parents.push_back(i);
  dag.AddBlock(MakeBlock(N + 1, all_parents));

  int merge_tip = N + 1;
  int blue_in_merge = 0;
  for (int i = 1; i <= N; i++)
    if (dag.blocks[merge_tip].blue_set.count(i))
      ++blue_in_merge;

  EXPECT_LE(blue_in_merge, K + 1) << "At most k+1 parallel blocks can be blue "
                                     "from a merge block's perspective";
  EXPECT_GE(blue_in_merge, 1) << "At least one parallel block must be blue "
                                 "(the selected parent's chain)";

  // The merge block's blue_set must be a valid k-cluster
  EXPECT_TRUE(dag.IsKCluster(dag.blocks[merge_tip].blue_set));
}

// ═════════════════════════════════════════════════════════════════════════════
// IV – Competing chains
// ═════════════════════════════════════════════════════════════════════════════

//
// Chain A (length 4) beats chain B (length 2) with k=0
//
//  Chain A: 0 → 1 → 2 → 3 → 4     blue_score(4) = 4
//  Chain B: 0 → 5 → 6              blue_score(6) = 2
//
TEST_F(GHOSTDAGTest, CompetingChains_LongerChainWins) {
  Blockchain dag(0);
  // Chain A
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(4, {3}));
  // Chain B
  dag.AddBlock(MakeBlock(5, {0}));
  dag.AddBlock(MakeBlock(6, {5}));

  EXPECT_EQ(dag.SelectTip(), 4) << "Longer chain A should win";

  // Chain A blocks are all blue from tip 4's perspective
  for (int i = 0; i <= 4; i++)
    EXPECT_TRUE(dag.blocks[4].blue_set.count(i))
        << "Chain A block " << i << " should be blue";

  // Chain B blocks (5, 6) must NOT be blue (k=0, full anticone with chain A)
  EXPECT_FALSE(dag.blocks[5].is_blue);
  EXPECT_FALSE(dag.blocks[6].is_blue);
}

//
// Equal-length chains: lower id wins as tiebreak
//
TEST_F(GHOSTDAGTest, CompetingChains_EqualLength_LowerIdWins) {
  Blockchain dag(0);
  // Chain A: 0→1→2   (tip=2, blue_score=2)
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  // Chain B: 0→3→4   (tip=4, blue_score=2)
  dag.AddBlock(MakeBlock(3, {0}));
  dag.AddBlock(MakeBlock(4, {3}));

  // Both tips have blue_score=2; lower id = 2 wins
  EXPECT_EQ(dag.SelectTip(), 2);
  EXPECT_TRUE(dag.blocks[2].is_blue);
  EXPECT_FALSE(dag.blocks[4].is_blue);
}

// ═════════════════════════════════════════════════════════════════════════════
// V – blue_set is immutable after insertion  (I1)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, BlueSet_ImmutableAfterInsertion) {
  Blockchain dag(5);

  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));

  std::set<int> blue_set_1_snapshot = dag.blocks[1].blue_set;
  std::set<int> blue_set_2_snapshot = dag.blocks[2].blue_set;

  // Add many more blocks; blue_set of 1 and 2 must not change
  for (int i = 3; i <= 15; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  EXPECT_EQ(dag.blocks[1].blue_set, blue_set_1_snapshot)
      << "blue_set of block 1 must not change after new blocks are added";
  EXPECT_EQ(dag.blocks[2].blue_set, blue_set_2_snapshot)
      << "blue_set of block 2 must not change after new blocks are added";
}

TEST_F(GHOSTDAGTest, BlueScore_ImmutableAfterInsertion) {
  Blockchain dag(5);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {2}));

  int score1 = dag.blocks[1].blue_score;
  int score2 = dag.blocks[2].blue_score;
  int score3 = dag.blocks[3].blue_score;

  for (int i = 4; i <= 20; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  EXPECT_EQ(dag.blocks[1].blue_score, score1);
  EXPECT_EQ(dag.blocks[2].blue_score, score2);
  EXPECT_EQ(dag.blocks[3].blue_score, score3);
}

// ═════════════════════════════════════════════════════════════════════════════
// VI – is_blue reflects the current selected tip  (I3, can change)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, IsBlue_CanChangeAsDAGGrows) {
  // Start with two parallel blocks; the lower-id one is the tip.
  // Extend the higher-id chain so it overtakes; the other blocks become red.
  Blockchain dag(0);
  dag.AddBlock(MakeBlock(1, {0})); // tip, blue
  dag.AddBlock(MakeBlock(2, {0})); // parallel, red

  EXPECT_TRUE(dag.blocks[1].is_blue);
  EXPECT_FALSE(dag.blocks[2].is_blue);

  // Extend chain B to length 3: 0→2→3→4 (blue_score=3 > 1)
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(4, {3}));

  // Now tip = 4; chain B dominates
  EXPECT_EQ(dag.SelectTip(), 4);
  EXPECT_TRUE(dag.blocks[4].is_blue);
  // Block 1 is now red (not in chain B's blue_set under k=0)
  EXPECT_FALSE(dag.blocks[1].is_blue)
      << "Block 1 should become red once a longer competing chain overtakes";
}

// ═════════════════════════════════════════════════════════════════════════════
// VII – Every block is in its own blue_set  (I5)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, EveryBlock_InItsOwnBlueSet) {
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1}));
  dag.AddBlock(MakeBlock(4, {2}));
  dag.AddBlock(MakeBlock(5, {3, 4}));

  for (auto &[id, blk] : dag.blocks)
    EXPECT_TRUE(blk.blue_set.count(id))
        << "Block " << id << " must be in its own blue_set";
}

// ═════════════════════════════════════════════════════════════════════════════
// VIII – blue_set is a k-cluster for every block  (I4)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, BlueSets_AreAlwaysKClusters) {
  Blockchain dag(2);
  // Build a moderately complex DAG
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1}));
  dag.AddBlock(MakeBlock(4, {2}));
  dag.AddBlock(MakeBlock(5, {1, 2}));
  dag.AddBlock(MakeBlock(6, {3, 4}));
  dag.AddBlock(MakeBlock(7, {5, 6}));

  EXPECT_TRUE(AllBlueSetsAreKClusters(dag))
      << "Every block's blue_set must be a valid k-cluster";
}

// ═════════════════════════════════════════════════════════════════════════════
// IX – blue_score correctness  (I2)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, BlueScore_EqualsBlueBlocksInPast) {
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(4, {0}));
  dag.AddBlock(MakeBlock(5, {3, 4}));

  EXPECT_TRUE(AllBlueScoresCorrect(dag))
      << "blue_score must equal |past(B) ∩ B.blue_set| for every block";
}

// ═════════════════════════════════════════════════════════════════════════════
// X – selected_parent has the maximum blue_score among parents  (I6)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, SelectedParent_HasMaxBlueScore) {
  Blockchain dag(3);
  // 0→1→2→3 (chain A, blue_score=3)
  // 0→4      (single block, blue_score=1)
  // 5 merges both
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(4, {0}));
  dag.AddBlock(MakeBlock(5, {3, 4}));

  // selected_parent of 5 must be 3 (blue_score=3 > blue_score of 4=1)
  EXPECT_EQ(dag.blocks[5].selected_parent, 3)
      << "selected_parent must be the parent with the highest blue_score";
}

TEST_F(GHOSTDAGTest, SelectedParent_BlueScoreMonotonicity) {
  // I7: blue_score(B) >= blue_score(selected_parent(B))  (always)
  Blockchain dag(2);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1}));
  dag.AddBlock(MakeBlock(4, {2}));
  dag.AddBlock(MakeBlock(5, {3, 4}));
  dag.AddBlock(MakeBlock(6, {5}));

  for (auto &[id, blk] : dag.blocks) {
    if (blk.selected_parent == -1)
      continue; // genesis
    EXPECT_GE(blk.blue_score, dag.blocks[blk.selected_parent].blue_score)
        << "blue_score must be >= selected_parent's blue_score for block "
        << id;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// XI – GetPast, GetFuture, GetAnticone  (I8)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, GetPast_CorrectAncestors) {
  // 0 → 1 → 2 → 3
  //       ↘ 4
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(4, {1}));

  EXPECT_EQ(dag.GetPast(0), (std::set<int>{}));
  EXPECT_EQ(dag.GetPast(1), (std::set<int>{0}));
  EXPECT_EQ(dag.GetPast(2), (std::set<int>{0, 1}));
  EXPECT_EQ(dag.GetPast(3), (std::set<int>{0, 1, 2}));
  EXPECT_EQ(dag.GetPast(4), (std::set<int>{0, 1}));
}

TEST_F(GHOSTDAGTest, GetFuture_CorrectDescendants) {
  // 0 → 1 → 3
  // 0 → 2 → 3
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1, 2}));

  EXPECT_EQ(dag.GetFuture(0), (std::set<int>{1, 2, 3}));
  EXPECT_EQ(dag.GetFuture(1), (std::set<int>{3}));
  EXPECT_EQ(dag.GetFuture(2), (std::set<int>{3}));
  EXPECT_EQ(dag.GetFuture(3), (std::set<int>{}));
}

TEST_F(GHOSTDAGTest, GetAnticone_SiblingBlocksHaveEmptyAnticone) {
  // Only 3 blocks: 0, 1, 2. Both 1 and 2 from genesis.
  // GetAnticone(1,2) = blocks unrelated to BOTH 1 and 2. Only block 0
  // remains but 0 is ancestor of both → result is empty.
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));

  EXPECT_TRUE(dag.GetAnticone(1, 2).empty())
      << "With only genesis + 2 siblings, anticone(1,2) must be empty";
}

TEST_F(GHOSTDAGTest, GetAnticone_ThirdBlockInAnticoneOfSiblings) {
  // 0→1, 0→2, 0→3.  GetAnticone(1,2) = {3} (3 is unrelated to both 1 and 2)
  Blockchain dag(5);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {0}));

  auto ac = dag.GetAnticone(1, 2);
  EXPECT_TRUE(ac.count(3))
      << "Block 3 should be in anticone(1,2): it is unrelated to both";
  EXPECT_FALSE(ac.count(0))
      << "Genesis is ancestor of 1 and 2, not in their anticone";
}

TEST_F(GHOSTDAGTest, GetAnticone_AncestorIsNotInAnticone) {
  // 0→1→2.  block 0 is ancestor of 1. GetAnticone(0, 2) should NOT contain 1
  // (1 is in past(2) so it IS related to 2).
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));

  auto ac = dag.GetAnticone(0, 2);
  EXPECT_TRUE(ac.empty()) << "In a linear chain 0→1→2 there are no blocks "
                             "unrelated to both 0 and 2";
}

// ═════════════════════════════════════════════════════════════════════════════
// XII – Orphan handling  (I9)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, Orphan_HeldUntilParentArrives) {
  Blockchain dag(3);

  // Add block 2 (parent=1) before block 1 exists → orphan
  dag.AddBlock(MakeBlock(2, {1}));
  EXPECT_TRUE(dag.IsOrphan(2));
  EXPECT_FALSE(dag.HasBlock(2));

  // Now add block 1; block 2 should be unorphaned automatically
  dag.AddBlock(MakeBlock(1, {0}));
  EXPECT_FALSE(dag.IsOrphan(2));
  EXPECT_TRUE(dag.HasBlock(2));
}

TEST_F(GHOSTDAGTest, Orphan_ChainOfOrphansAllResolve) {
  Blockchain dag(3);

  // Add 5→4→3→2→1 (all parents missing when added)
  dag.AddBlock(MakeBlock(5, {4}));
  dag.AddBlock(MakeBlock(4, {3}));
  dag.AddBlock(MakeBlock(3, {2}));
  dag.AddBlock(MakeBlock(2, {1}));

  EXPECT_TRUE(dag.IsOrphan(2));
  EXPECT_TRUE(dag.IsOrphan(3));
  EXPECT_TRUE(dag.IsOrphan(4));
  EXPECT_TRUE(dag.IsOrphan(5));

  // Provide the root of the chain
  dag.AddBlock(MakeBlock(1, {0}));

  // All should now be accepted
  for (int i = 1; i <= 5; i++) {
    EXPECT_TRUE(dag.HasBlock(i)) << "Block " << i << " should be accepted";
    EXPECT_FALSE(dag.IsOrphan(i)) << "Block " << i << " should not be orphan";
  }
}

TEST_F(GHOSTDAGTest, Orphan_DataIsCorrectAfterUnorphaning) {
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(2, {1})); // orphan
  dag.AddBlock(MakeBlock(1, {0})); // triggers unorphaning

  // After resolution, GHOSTDAG data for block 2 must be valid
  EXPECT_TRUE(dag.blocks[2].blue_set.count(2))
      << "Block 2 must be in its own blue_set after being unorphaned";
  EXPECT_GE(dag.blocks[2].blue_score, 0);
  EXPECT_EQ(dag.blocks[2].selected_parent, 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// XIII – SelectTip  (I15)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, SelectTip_OnlyGenesis) {
  Blockchain dag(3);
  EXPECT_EQ(dag.SelectTip(), 0);
}

TEST_F(GHOSTDAGTest, SelectTip_PrefersHigherBlueScore) {
  Blockchain dag(2);
  // Short chain: tip at 2 (score=2)
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  // Single block: tip at 3 (score=1)
  dag.AddBlock(MakeBlock(3, {0}));

  EXPECT_EQ(dag.SelectTip(), 2)
      << "Tip with higher blue_score (2) should win over tip with score (1)";
}

TEST_F(GHOSTDAGTest, SelectTip_LowerIdBreaksTie) {
  Blockchain dag(3);
  dag.AddBlock(MakeBlock(1, {0})); // score=1
  dag.AddBlock(MakeBlock(2, {0})); // score=1, higher id

  // Both have equal blue_score; lower id (1) wins
  EXPECT_EQ(dag.SelectTip(), 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// XIV – ComputeGHOSTDAGOrdering  (I14)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, GHOSTDAGOrdering_IsTopologicallyValid) {
  Blockchain dag(2);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1}));
  dag.AddBlock(MakeBlock(4, {2}));
  dag.AddBlock(MakeBlock(5, {3, 4}));

  std::vector<int> order = dag.ComputeGHOSTDAGOrdering();

  // Every block must appear exactly once
  EXPECT_EQ(order.size(), dag.blocks.size());
  std::set<int> seen(order.begin(), order.end());
  EXPECT_EQ(seen.size(), dag.blocks.size());

  // For each block, all its parents must appear before it
  std::map<int, int> position;
  for (int i = 0; i < (int)order.size(); i++)
    position[order[i]] = i;

  for (auto &[id, blk] : dag.blocks)
    for (int p : blk.header.parent_hashes)
      EXPECT_LT(position[p], position[id])
          << "Parent " << p << " must appear before block " << id;
}

TEST_F(GHOSTDAGTest, GHOSTDAGOrdering_BlueBlocksBeforeRedAtSameLevel) {
  // With k=0, chain A (0→1→2) has higher blue_score than orphan chain B
  // (0→3→4). In the ordering, chain A blocks should come before chain B at the
  // same depth.
  Blockchain dag(0);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {1}));
  dag.AddBlock(MakeBlock(3, {0}));
  dag.AddBlock(MakeBlock(4, {3}));

  auto order = dag.ComputeGHOSTDAGOrdering();
  std::map<int, int> pos;
  for (int i = 0; i < (int)order.size(); i++)
    pos[order[i]] = i;

  // Block 2 (blue_score=2) should appear before block 4 (blue_score=2? let's
  // check) Actually 2 and 4 both have blue_score=2; lower id tiebreak → 2
  // before 4.
  EXPECT_LT(pos[2], pos[4]) << "Block 2 (lower id, equal score) should appear "
                               "before block 4 in ordering";
}

// ═════════════════════════════════════════════════════════════════════════════
// XV – IsKCluster / IsKClusterSubset
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, IsKCluster_LinearChainIsAlwaysKCluster) {
  Blockchain dag(0); // even k=0
  for (int i = 1; i <= 5; i++)
    dag.AddBlock(MakeBlock(i, {i - 1}));

  // In a linear chain there are no anticone relations → trivially a k-cluster
  for (auto &[id, blk] : dag.blocks)
    EXPECT_TRUE(dag.IsKCluster(blk.blue_set))
        << "Linear chain blue_set should always be a k-cluster";
}

TEST_F(GHOSTDAGTest, IsKCluster_TooManyAnticoneFails) {
  Blockchain dag(1);
  // Build a set with 3 mutually unrelated blocks; k=1 means max anticone=1
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {0}));

  // {1, 2, 3} are pairwise in anticone; each has anticone size 2 > k=1
  std::set<int> bad_set = {1, 2, 3};
  EXPECT_FALSE(dag.IsKCluster(bad_set))
      << "Three mutually unrelated blocks should NOT form a k=1 cluster";
}

// ═════════════════════════════════════════════════════════════════════════════
// XVI – Global invariant sweep over a complex DAG
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(GHOSTDAGTest, GlobalInvariants_ComplexDAG) {
  //
  //         1   2
  //          \ /
  //    0      3   4
  //     \    / \ /
  //      5  6   7
  //       \ |  /
  //         8
  //
  Blockchain dag(2);
  dag.AddBlock(MakeBlock(1, {0}));
  dag.AddBlock(MakeBlock(2, {0}));
  dag.AddBlock(MakeBlock(3, {1, 2}));
  dag.AddBlock(MakeBlock(4, {0}));
  dag.AddBlock(MakeBlock(5, {0}));
  dag.AddBlock(MakeBlock(6, {3}));
  dag.AddBlock(MakeBlock(7, {3, 4}));
  dag.AddBlock(MakeBlock(8, {5, 6, 7}));

  // I1 + I2: blue_sets and blue_scores are correct
  EXPECT_TRUE(AllBlueScoresCorrect(dag));

  // I4: every blue_set is a k-cluster
  EXPECT_TRUE(AllBlueSetsAreKClusters(dag));

  // I5: every block is in its own blue_set
  for (auto &[id, blk] : dag.blocks)
    EXPECT_TRUE(blk.blue_set.count(id))
        << "Block " << id << " must be in its own blue_set";

  // I6/I7: selected_parent has the max blue_score among parents
  for (auto &[id, blk] : dag.blocks) {
    if (blk.selected_parent == -1)
      continue;
    for (int p : blk.header.parent_hashes)
      EXPECT_GE(dag.blocks[blk.selected_parent].blue_score,
                dag.blocks[p].blue_score)
          << "selected_parent of block " << id
          << " must have the highest blue_score among parents";
  }

  // I3: is_blue consistency – block i is blue iff it's in SelectTip().blue_set
  int tip = dag.SelectTip();
  for (auto &[id, blk] : dag.blocks) {
    bool expected_blue = dag.blocks[tip].blue_set.count(id) > 0;
    EXPECT_EQ(blk.is_blue, expected_blue)
        << "is_blue mismatch for block " << id;
  }

  // I14: ordering is a valid topological sort
  auto order = dag.ComputeGHOSTDAGOrdering();
  EXPECT_EQ(order.size(), dag.blocks.size());
  std::map<int, int> pos;
  for (int i = 0; i < (int)order.size(); i++)
    pos[order[i]] = i;
  for (auto &[id, blk] : dag.blocks)
    for (int p : blk.header.parent_hashes)
      EXPECT_LT(pos[p], pos[id]);
}

// ═════════════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════════════
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
