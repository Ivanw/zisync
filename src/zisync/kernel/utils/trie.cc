// Copyright 2014, zisync.com

#include <cassert>
#include <algorithm>

#include "zisync/kernel/utils/trie.h"

namespace zs {

using std::unique_ptr;
using std::vector;
using std::string;

// all path is start with '/'

void TrieNode::Search(const string &word, TrieSearchResult *result) {
  if (children.size() == 0) {
    if (result->word_iter == word.end()) {
      result->type = TRIE_SEARCH_RESULT_TYPE_SELF;
      return;
    } else {
      result->type = TRIE_SEARCH_RESULT_TYPE_CHILD;
      return;
    }
  } else {
    if (result->word_iter == word.end()) {
      result->type = TRIE_SEARCH_RESULT_TYPE_PARENT;
    } else {
      for (auto iter = children.begin(); iter != children.end(); iter ++) {
        if (*(result->word_iter) != *(*iter)->key.begin()) {
          continue;
        }
        result->key_iter = (*iter)->key.begin() + 1; 
        result->word_iter ++;
        result->node = iter;
        for (; (result->key_iter) != (*iter)->key.end(); 
             result->key_iter ++) {
          if ((result->word_iter) == word.end()) {
            result->type = TRIE_SEARCH_RESULT_TYPE_PARENT;
            return;
          } else {
            if (*(result->word_iter) == *(result->key_iter)) {
              (result->word_iter) ++;
            } else {
              result->type = TRIE_SEARCH_RESULT_TYPE_STRANGER;
              return;
            }
          }
        }
        return (*iter)->Search(word, result);
      }
      result->type = TRIE_SEARCH_RESULT_TYPE_STRANGER;
      return;
    }
  }
}

TrieSearchResult::TrieSearchResult(Trie *trie, const string &word) {
  assert(!trie->root.empty());
  assert(*trie->root.begin());
  node = trie->root.begin();
  key_iter = (*node)->key.end();
  word_iter = word.cbegin() + 1;
}


bool Trie::Add(const string& word) {
  assert(word.length() > 0);
  assert(word[0] == '/');
  if (root.empty()) {
    root.emplace_back(new TrieNode("/"));
    TrieNode *node = root.begin()->get();
    if (word.length() > 1) {
      node->children.emplace_back(new TrieNode(
              string(word.begin() + 1, word.end()), node));
    }
    return true;
  }
  TrieSearchResult result(this, word);
  (*result.node)->Search(word, &result);
  if (result.type == TRIE_SEARCH_RESULT_TYPE_PARENT) {
    if (result.key_iter == (*result.node)->key.end()) {
      assert(!(*result.node)->children.empty());
    } else {
      (*result.node)->key.erase(result.key_iter, (*result.node)->key.end());
    }
    (*result.node)->children.clear();
    assert(root.size() == 1);
    return true;
  } else if (result.type == TRIE_SEARCH_RESULT_TYPE_SELF) {
    assert(root.size() == 1);
    return false;
  } else if (result.type == TRIE_SEARCH_RESULT_TYPE_CHILD) {
    assert(root.size() == 1);
    return false;
  } else {
    assert(result.type == TRIE_SEARCH_RESULT_TYPE_STRANGER);
    if (result.key_iter == (*result.node)->key.end()) {
      // not find in child
      assert(result.word_iter != word.end());
      (*result.node)->children.emplace_back(new TrieNode(
              string(result.word_iter, word.end()), result.node->get()));
    } else {
      TrieNode *left_child = new TrieNode(
          string(result.key_iter, (*result.node)->key.end()), 
          result.node->get());
      left_child->children.swap((*result.node)->children);
      std::for_each(left_child->children.begin(), left_child->children.end(),
                    [ left_child ] (const unique_ptr<TrieNode> &child) 
                    { child->father = left_child; });
      TrieNode *right_child = new TrieNode(
          string(result.word_iter, word.end()), result.node->get());
      (*result.node)->children.emplace_back(left_child);
      (*result.node)->children.emplace_back(right_child);
      (*result.node)->key.erase(result.key_iter, (*result.node)->key.end());
    }
    assert(root.size() == 1);
    return true;
  }
}

bool Trie::Del(const string& word) {
  TrieSearchResult result(this, word);
  (*result.node)->Search(word, &result);
  if (result.type == TRIE_SEARCH_RESULT_TYPE_SELF) {
    TrieNode *father = (*result.node)->father;
    if (father == NULL) {
      root.erase(result.node);
    } else {
      father->children.erase(result.node);
      if (father->children.size() == 1 && father != root.begin()->get()) {
        unique_ptr<TrieNode> child(father->children.begin()->release());
        assert(child != NULL);
        father->children.swap(child->children);
        father->key.append(child->key);
        std::for_each(father->children.begin(), father->children.end(),
                      [ father ] (const unique_ptr<TrieNode> &child) 
                      { child->father = father; });
        assert(root.size() == 1);
      } else if (father->children.size() == 0) {
        assert(father == root.begin()->get());
        root.clear();
      }
    }
    return true;
  } else {
    assert(root.size() == 1);
    return false;
  }
}
  
TrieSearchResultType Trie::Find(const string& word) {
    if (root.empty()) {
      return TRIE_SEARCH_RESULT_TYPE_STRANGER;
    }
    TrieSearchResult result(this, word);
    (*result.node)->Search(word, &result);
    return result.type;
  }

}  // namespace zs
