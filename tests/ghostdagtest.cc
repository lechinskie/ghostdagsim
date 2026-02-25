#include "../dag.h"

#include <gtest/gtest.h>

class BlockchainTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(BlockchainTest, GenesisBlock) {
  Blockchain dag(10);
  EXPECT_TRUE(dag.HasBlock(0));
  EXPECT_EQ(dag.tips.size(), 1);
  EXPECT_TRUE(dag.tips.count(0));
  EXPECT_TRUE(dag.blocks[0].is_blue);
  EXPECT_EQ(dag.blocks[0].blue_score, 1);
}

TEST_F(BlockchainTest, AddBlockWithMultipleParents) {
  Blockchain dag(10);
  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  Block block2;
  block2.header.block_id = 2;
  block2.header.parent_hashes.push_back(0);
  dag.AddBlock(block2);

  Block block3;
  block3.header.block_id = 3;
  block3.header.parent_hashes.push_back(1);
  block3.header.parent_hashes.push_back(2);
  dag.AddBlock(block3);

  EXPECT_EQ(dag.tips.size(), 1);
  EXPECT_TRUE(dag.tips.count(3));
}

TEST_F(BlockchainTest, OrphanBlockHandling) {
  Blockchain dag(10);
  Block block;
  block.header.block_id = 1;
  block.header.parent_hashes.push_back(999);
  dag.AddBlock(block);

  EXPECT_TRUE(dag.IsOrphan(1));
  EXPECT_FALSE(dag.HasBlock(1));
  EXPECT_EQ(dag.tips.size(), 1);
  EXPECT_TRUE(dag.tips.count(0));
}

TEST_F(BlockchainTest, OrphanResolution) {
  Blockchain dag(10);
  Block orphan;
  orphan.header.block_id = 1;
  orphan.header.parent_hashes.push_back(999);
  dag.AddBlock(orphan);
  EXPECT_TRUE(dag.IsOrphan(1));

  Block parent;
  parent.header.block_id = 999;
  parent.header.parent_hashes.push_back(0);
  dag.AddBlock(parent);

  EXPECT_FALSE(dag.IsOrphan(1));
  EXPECT_TRUE(dag.HasBlock(1));
}

TEST_F(BlockchainTest, MultipleOrphansResolve) {
  Blockchain dag(10);
  Block orphan1;
  orphan1.header.block_id = 1;
  orphan1.header.parent_hashes.push_back(100);
  dag.AddBlock(orphan1);

  Block orphan2;
  orphan2.header.block_id = 2;
  orphan2.header.parent_hashes.push_back(100);
  dag.AddBlock(orphan2);

  EXPECT_TRUE(dag.IsOrphan(1));
  EXPECT_TRUE(dag.IsOrphan(2));

  Block parent;
  parent.header.block_id = 100;
  parent.header.parent_hashes.push_back(0);
  dag.AddBlock(parent);

  EXPECT_FALSE(dag.IsOrphan(1));
  EXPECT_FALSE(dag.IsOrphan(2));
}

TEST_F(BlockchainTest, DAGWidthIncreases) {
  Blockchain dag(10);
  EXPECT_EQ(dag.GetDagWidth(), 1);

  for (int i = 1; i <= 5; i++) {
    Block block;
    block.header.block_id = i;
    block.header.parent_hashes.push_back(0);
    dag.AddBlock(block);
  }

  EXPECT_EQ(dag.GetDagWidth(), 5);
}

TEST_F(BlockchainTest, TipSelectionByBlueScore) {
  Blockchain dag(10);
  for (int i = 1; i <= 3; i++) {
    Block block;
    block.header.block_id = i;
    block.header.parent_hashes.push_back(0);
    dag.AddBlock(block);
  }

  int selected = dag.SelectTip();
  EXPECT_TRUE(selected >= 1 && selected <= 3);
}

TEST_F(BlockchainTest, BlueSetContainsGenesisWithSingleBlock) {
  Blockchain dag(10);
  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  auto blue_set = dag.CalculateBlueSet(1);
  EXPECT_TRUE(blue_set.count(0));
  EXPECT_TRUE(blue_set.count(1));
}

TEST_F(BlockchainTest, BlueScoreEqualsBlueSetSize) {
  Blockchain dag(10);

  for (int i = 1; i <= 3; i++) {
    Block block;
    block.header.block_id = i;
    block.header.parent_hashes.push_back(0);
    dag.AddBlock(block);
  }

  for (int i = 1; i <= 3; i++) {
    auto blue_set = dag.CalculateBlueSet(i);
    EXPECT_EQ(dag.blocks[i].blue_score, (int)blue_set.size());
  }
}

TEST_F(BlockchainTest, ParallelBlocksBothBlue) {
  Blockchain dag(10);

  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  Block block2;
  block2.header.block_id = 2;
  block2.header.parent_hashes.push_back(0);
  dag.AddBlock(block2);

  EXPECT_TRUE(dag.blocks[1].is_blue);
  EXPECT_TRUE(dag.blocks[2].is_blue);
}

TEST_F(BlockchainTest, K0CollapsesToChain) {
  Blockchain dag(0);

  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  Block block2;
  block2.header.block_id = 2;
  block2.header.parent_hashes.push_back(0);
  dag.AddBlock(block2);

  std::set<int> set1 = {0, 1};
  EXPECT_TRUE(dag.IsKCluster(set1));

  std::set<int> set2 = {0, 1, 2};
  EXPECT_FALSE(dag.IsKCluster(set2));
}

TEST_F(BlockchainTest, GHOSTDAGOrdering) {
  Blockchain dag(10);
  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  auto ordering = dag.ComputeGHOSTDAGOrdering();
  EXPECT_GE(ordering.size(), 2);
}

TEST_F(BlockchainTest, DAGMergedTips) {
  Blockchain dag(10);

  Block block1;
  block1.header.block_id = 1;
  block1.header.parent_hashes.push_back(0);
  dag.AddBlock(block1);

  Block block2;
  block2.header.block_id = 2;
  block2.header.parent_hashes.push_back(0);
  dag.AddBlock(block2);

  Block block3;
  block3.header.block_id = 3;
  block3.header.parent_hashes.push_back(1);
  dag.AddBlock(block3);

  Block block4;
  block4.header.block_id = 4;
  block4.header.parent_hashes.push_back(2);
  dag.AddBlock(block4);

  Block block5;
  block5.header.block_id = 5;
  block5.header.parent_hashes.push_back(3);
  block5.header.parent_hashes.push_back(4);
  dag.AddBlock(block5);

  EXPECT_EQ(dag.tips.size(), 1);
  EXPECT_TRUE(dag.tips.count(5));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
