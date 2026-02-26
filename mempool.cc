/**
 * @file Mempool.cpp
 * @brief Data structure unique for each miner that holds his transactions that
 * can be included in a block
 * @author Tomas Hladky <xhladk15@stud.fit.vutbr.cz>
 * @author Martin Peresini <iperesini@fit.vut.cz>
 * @author Eramoss <eduardo_ramos@edu.univali.br> revised
 * @date 2026
 */

#include "mempool.h"

HtabItem::HtabItem(std::string &_key, uint64_t _txId, uint32_t _fee)
    : key(_key), txId(_txId), fee(_fee) {}

HtabIterator::HtabIterator(std::list<HtabItem>::iterator _iterator,
                           size_t _index)
    : iterator(_iterator), index(_index) {}

void HtabItem::setIterator(
    std::multimap<uint32_t,
                  std::pair<std::list<HtabItem>::iterator, uint32_t>>::iterator
        _multimapIterator) {
  multimapIterator = _multimapIterator;
}

Mempool::Mempool(size_t n)
    : htabItems(n, std::list<HtabItem>{}),
      randomMempoolIndexGenerator(0, int(n) - 1) {
  arrSize = n;
  itemCount = 0;
}

HtabIterator Mempool::begin() {
  for (size_t i = 0; i < arrSize; i++) {
    if (!htabItems[i].empty()) {
      return {htabItems[i].begin(), i};
    }
  }

  // No item was found
  return end();
}

size_t Mempool::size() const { return itemCount; }

size_t Mempool::bucketCount() const { return arrSize; }

HtabIterator Mempool::getRandomTransaction(std::mt19937 randomGen) {
  size_t index = randomMempoolIndexGenerator(randomGen);

  size_t max = (arrSize / 2) + 1;
  size_t down_i = index;
  size_t up_i = index == arrSize - 1 ? 0 : index + 1;

  for (size_t i = 0; i < max; i++) {
    // Test positions on and below index
    if (down_i ==
        UINT64_MAX) { // Because of unsigned value, this is equal to == -1
      down_i = arrSize - 1;
    }
    if (!htabItems[down_i].empty()) {
      if (htabItems[down_i].size() == 1) {
        return {htabItems[down_i].begin(), down_i};
      } else {
        // Randomly remove element in bucket
        std::uniform_int_distribution<> bucketIndex(
            0, int(htabItems[down_i].size()) - 1);
        auto it = htabItems[down_i].begin();
        std::advance(it, bucketIndex(randomGen));
        return {it, down_i};
      }
    } else {
      down_i--;
    }

    // Test positions above index
    if (up_i == this->arrSize) {
      up_i = 0;
    }
    if (!htabItems[up_i].empty()) {
      if (htabItems[up_i].size() == 1) {
        // Directly remove element if bucket size == 1
        return {htabItems[up_i].begin(), up_i};
      } else {
        // Randomly remove element in bucket
        std::uniform_int_distribution<> bucketIndex(
            0, int(htabItems[up_i].size()) - 1);
        auto it = htabItems[up_i].begin();
        std::advance(it, bucketIndex(randomGen));
        return {it, up_i};
      }
    } else {
      up_i++;
    }
  }

  return end();
}

HtabIterator Mempool::getSortedTransactionDescending() {
  auto it = multimapItems.rbegin();
  return {it->second.first, it->second.second};
}

void Mempool::eraseTransaction(HtabIterator &iterator) {
  if (!iterator.isValid()) {
    return;
  }

  multimapItems.erase(iterator.iterator->multimapIterator);
  htabItems[iterator.index].erase(iterator.iterator);
  itemCount--;
}

void Mempool::eraseTransactionsAscending(const uint32_t size) {
  uint32_t i = 0;
  for (auto it = multimapItems.begin(); it != multimapItems.end();) {
    if (i == size) {
      break;
    }

    i++;

    htabItems[it->second.second].erase(it->second.first);
    multimapItems.erase(it++); // Remove and increase iterator

    itemCount--;
  }
}

void Mempool::eraseRandomTransactions(std::mt19937 randomGen,
                                      const uint32_t size) {
  for (int i = 0; i < (int)size; i++) {
    // Take random index in hash table
    size_t index = randomMempoolIndexGenerator(randomGen);

    size_t max = (arrSize / 2) + 1;
    size_t down_idx = index;
    size_t up_idx = index == arrSize - 1 ? 0 : index + 1;

    for (size_t j = 0; j < max; j++) {
      // Test positions on and below index
      if (down_idx == UINT64_MAX) {
        down_idx = arrSize - 1;
      }
      if (!htabItems[down_idx].empty()) {
        if (htabItems[down_idx].size() == 1) {
          // Directly remove element if bucket size == 1
          multimapItems.erase(htabItems[down_idx].begin()->multimapIterator);
          htabItems[down_idx].pop_front();
          itemCount--;
        } else {
          // Randomly remove element in bucket
          std::uniform_int_distribution<> bucketIndex(
              0, int(htabItems[down_idx].size()) - 1);
          auto it = htabItems[down_idx].begin();
          std::advance(it, bucketIndex(randomGen));
          multimapItems.erase(it->multimapIterator);
          htabItems[down_idx].erase(it);
          itemCount--;
        }
        break;
      } else {
        down_idx--;
      }

      // Test positions above index
      if (up_idx > this->arrSize - 1) {
        up_idx = 0;
      }
      if (!htabItems[up_idx].empty()) {
        if (htabItems[up_idx].size() == 1) {
          // Directly remove element if bucket size == 1
          multimapItems.erase(htabItems[up_idx].begin()->multimapIterator);
          htabItems[up_idx].pop_front();
          itemCount--;
        } else {
          // Randomly remove element in bucket
          std::uniform_int_distribution<> bucketIndex(
              0, int(htabItems[up_idx].size()) - 1);
          auto it = htabItems[up_idx].begin();
          std::advance(it, bucketIndex(randomGen));
          multimapItems.erase(it->multimapIterator);
          htabItems[up_idx].erase(it);
          itemCount--;
        }
        break;
      } else {
        up_idx++;
      }
    }
  }
}

HtabIterator Mempool::find(uint32_t minerId, uint64_t txId) {
  std::string key =
      std::to_string(minerId).append(".").append(std::to_string(txId));

  //	if (key.empty()) {
  //		return end();
  //	}

  // Get index from hash function
  size_t index = (hashFun(key) % arrSize);

  auto &vectIt = htabItems[index];

  for (auto it = vectIt.begin(); it != vectIt.end(); ++it) {
    if (it->txId == txId) {
      return {it, index};
    }
  }

  return end();
}

size_t Mempool::hashFun(const std::string &key) {
  return std::hash<std::string>{}(key);
}

void Mempool::clear() {
  for (auto &item : htabItems) {
    item.clear();
  }
  multimapItems.clear();
  itemCount = 0;
}

HtabIterator Mempool::insert(uint32_t minerId, uint64_t txId, uint32_t fee) {
  std::string key =
      std::to_string(minerId).append(".").append(std::to_string(txId));
  size_t index = (hashFun(key) % arrSize);

  // Create new entry
  htabItems[index].emplace_front(key, txId, fee);
  auto multimapIterator =
      multimapItems.insert({fee, {htabItems[index].begin(), index}});
  htabItems[index].begin()->setIterator(multimapIterator);

  itemCount++;

  return {htabItems[index].begin(), index};
}
