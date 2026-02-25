#include "../mempool.h"

#include <gtest/gtest.h>

class MempoolTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(MempoolTest, EmptyMempool) {
  Mempool pool(100);
  EXPECT_EQ(pool.size(), 0);
  EXPECT_EQ(pool.bucketCount(), 100);
}

TEST_F(MempoolTest, InsertTransaction) {
  Mempool pool(100);
  auto it = pool.insert(1, 100, 50);
  EXPECT_EQ(pool.size(), 1);
  EXPECT_TRUE(it.isValid());
}

TEST_F(MempoolTest, InsertMultipleTransactions) {
  Mempool pool(100);
  pool.insert(1, 100, 50);
  pool.insert(1, 101, 30);
  pool.insert(1, 102, 70);
  EXPECT_EQ(pool.size(), 3);
}

TEST_F(MempoolTest, FindTransaction) {
  Mempool pool(100);
  pool.insert(1, 100, 50);

  auto it = pool.find(1, 100);
  EXPECT_TRUE(it.isValid());
  EXPECT_EQ(it.iterator->txId, 100);
  EXPECT_EQ(it.iterator->fee, 50);
}

TEST_F(MempoolTest, FindNonExistentTransaction) {
  Mempool pool(100);
  pool.insert(1, 100, 50);

  auto it = pool.find(1, 999);
  EXPECT_FALSE(it.isValid());
}

TEST_F(MempoolTest, EraseTransaction) {
  Mempool pool(100);
  auto it = pool.insert(1, 100, 50);
  EXPECT_EQ(pool.size(), 1);

  pool.eraseTransaction(it);
  EXPECT_EQ(pool.size(), 0);

  auto found = pool.find(1, 100);
  EXPECT_FALSE(found.isValid());
}

TEST_F(MempoolTest, GetHighestFeeTransaction) {
  Mempool pool(100);
  pool.insert(1, 100, 50);
  pool.insert(1, 101, 30);
  pool.insert(1, 102, 70);

  auto it = pool.getSortedTransactionDescending();
  EXPECT_TRUE(it.isValid());
  EXPECT_EQ(it.iterator->fee, 70);
}

TEST_F(MempoolTest, EraseLowestFeeTransactions) {
  Mempool pool(100);
  pool.insert(1, 100, 50);
  pool.insert(1, 101, 30);
  pool.insert(1, 102, 70);
  pool.insert(1, 103, 20);
  EXPECT_EQ(pool.size(), 4);

  pool.eraseTransactionsAscending(2);
  EXPECT_EQ(pool.size(), 2);

  auto it = pool.getSortedTransactionDescending();
  EXPECT_EQ(it.iterator->fee, 70);
}

TEST_F(MempoolTest, ClearMempool) {
  Mempool pool(100);
  pool.insert(1, 100, 50);
  pool.insert(1, 101, 30);
  EXPECT_EQ(pool.size(), 2);

  pool.clear();
  EXPECT_EQ(pool.size(), 0);
}

TEST_F(MempoolTest, DuplicateInsertionAllowed) {
  Mempool pool(100);
  pool.insert(1, 100, 50);
  pool.insert(1, 100, 100);

  EXPECT_EQ(pool.size(), 2);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
