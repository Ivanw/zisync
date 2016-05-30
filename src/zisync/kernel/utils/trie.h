// Copyright 2014, zisync.com

#ifndef ZISYNC_KERNEL_UTILS_TRIE_H_
#define ZISYNC_KERNEL_UTILS_TRIE_H_

#include <string>
#include <vector>
#include <memory>

namespace zs {

class TrieNode;
class Trie;

enum TrieSearchResultType {
  TRIE_SEARCH_RESULT_TYPE_NULL      = 0,
  TRIE_SEARCH_RESULT_TYPE_PARENT    = 1,
  TRIE_SEARCH_RESULT_TYPE_CHILD     = 2,
  TRIE_SEARCH_RESULT_TYPE_SELF      = 3,
  TRIE_SEARCH_RESULT_TYPE_STRANGER  = 4,
};

class TrieSearchResult {
 public:
  TrieSearchResult(Trie *trie, const std::string &word);
  TrieSearchResultType type;
  std::vector<std::unique_ptr<TrieNode>>::iterator node;
  std::string::const_iterator word_iter;
  std::string::iterator key_iter;
};

class TrieNode {
  friend class Trie;
 public:
  TrieNode(const std::string& key_, TrieNode *father_ = NULL):
      key(key_),father(father_) {}
  void Search(const std::string &word, TrieSearchResult *result);
  std::string key;
  std::vector<std::unique_ptr<TrieNode>> children;
  TrieNode *father;
};

class Trie {
  friend class TrieSearchResult;
 public:
  /*  Exits return false */
  bool Add(const std::string& word);
  TrieSearchResultType Find(const std::string& word);
  bool Del(const std::string &word);
       
 private:
  std::vector<std::unique_ptr<TrieNode>> root;
};
  

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_TRIE_H_
