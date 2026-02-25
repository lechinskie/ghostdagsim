/**
 * @file Mempool.h
 * @brief Data structure unique for each miner that holds his transactions that
 * can be included in a block
 * @author Tomas Hladky <xhladk15@stud.fit.vutbr.cz>
 * @author Martin Peresini <iperesini@fit.vut.cz>
 * @author Eramoss <eduardo_ramos@edu.univali.br>
 * @date 2026
 */

#pragma once

#include <list>
#include <map>
#include <random>
#include <string>
#include <vector>

class HtabItem {
public:
  std::string &key;
  uint64_t txId;
  uint32_t fee;
  std::multimap<uint32_t,
                std::pair<std::list<HtabItem>::iterator, uint32_t>>::iterator
      multimapIterator;

  HtabItem(std::string &_key, uint64_t _txId, uint32_t _fee);

  void setIterator(
      std::multimap<uint32_t, std::pair<std::list<HtabItem>::iterator,
                                        uint32_t>>::iterator _multimapIterator);

  /**
   *
   * @param item Item stored in hashtable
   * @return State if items are equal
   */
  inline bool isEqual(HtabItem *item) const { return txId == item->txId; }
};

class HtabIterator {
public:
  std::list<HtabItem>::iterator iterator;
  size_t index;

  HtabIterator(std::list<HtabItem>::iterator _iterator, size_t _index);

  /**
   * @brief SIZE_MAX is special value for mempool hashtable to indicate invalid
   * value
   * @return Validity state
   */
  inline bool isValid() const { return index != SIZE_MAX; }
};

class Mempool {
  std::vector<std::list<HtabItem>> htabItems;
  std::multimap<uint32_t, std::pair<std::list<HtabItem>::iterator, uint32_t>>
      multimapItems;

  std::uniform_int_distribution<> randomMempoolIndexGenerator;

  size_t itemCount;
  size_t arrSize;

  /**
   * @brief Used when item does not exists in hashtable
   * @return Invalid element.
   */
  inline HtabIterator end() { return {htabItems[0].end(), SIZE_MAX}; }

public:
  /**
   *
   * @param n maximum number of transactions that can stored in mempool
   */
  Mempool(size_t n);

  /**
   *
   * @return First element in hashtable
   */
  HtabIterator begin();

  /**
   *
   * @return Number of elements stored in mempool
   */
  size_t size() const;

  /**
   *
   * @return Maximum number of transactions that can be stored in mempool
   */
  size_t bucketCount() const;

  /**
   *
   * @param randomGen Random generator
   * @return Random transaction from mempool
   */
  HtabIterator getRandomTransaction(std::mt19937 randomGen);

  /**
   *
   * @return Transaction with the highest fee
   */
  HtabIterator getSortedTransactionDescending();

  /**
   *
   * @param iterator Iterator with item to erase
   */
  void eraseTransaction(HtabIterator &iterator);

  /**
   *
   * @param randomGen
   * @param size Number of transactions to be randomly erased from mempool
   */
  void eraseRandomTransactions(std::mt19937 randomGen, uint32_t size);

  /**
   *
   * @param size Number of transactions with lowest fee to be erased from
   * mempool
   */
  void eraseTransactionsAscending(uint32_t size);

  /**
   *
   * @param key string to hash
   * @return hash value of key
   */
  static size_t hashFun(const std::string &key);

  /**
   *
   * @param minerId Id of miner
   * @param txId transaction id
   * @return Iterator to item
   */
  HtabIterator find(uint32_t minerId, uint64_t txId);

  /**
   * @brief Erase all items in mempool
   */
  void clear();

  /**
   *
   * @param minerId Id of miner
   * @param txId transaction id
   * @param fee transaction fee
   * @return Iterator to inserted item
   */
  HtabIterator insert(uint32_t minerId, uint64_t txId, uint32_t fee);
};
