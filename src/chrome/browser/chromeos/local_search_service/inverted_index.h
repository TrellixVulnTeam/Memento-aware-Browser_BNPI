// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOCAL_SEARCH_SERVICE_INVERTED_INDEX_H_
#define CHROME_BROWSER_CHROMEOS_LOCAL_SEARCH_SERVICE_INVERTED_INDEX_H_

#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/strings/string16.h"

namespace local_search_service {
// Stores the content id, start position and length of the token in original
// documents.
struct TokenPosition {
  TokenPosition() = default;
  TokenPosition(const std::string& id,
                uint32_t start_value,
                uint32_t length_value);
  TokenPosition(const TokenPosition& token_position) = default;
  std::string content_id;
  uint32_t start;
  uint32_t length;
};

// Stores the token (after processed). |positions| represents the token's
// positions in one document.
struct Token {
  Token();
  Token(const Token& token);
  Token(const base::string16& text, const std::vector<TokenPosition>& pos);
  ~Token();
  base::string16 content;
  std::vector<TokenPosition> positions;
};

// A posting is a list of TokenPosition.
using Posting = std::vector<TokenPosition>;

// A map from document id to posting.
using PostingList = std::unordered_map<std::string, Posting>;

// A tuple that stores a document ID, token's positions and token's TF-IDF
// score.
using TfidfResult = std::tuple<std::string, Posting, float>;

// InvertedIndex stores the inverted index for local search. It provides the
// abilities to add/remove documents, find term, etc. Before this class can be
// used to return tf-idf scores of a term, the client should build the index
// first (using BuildInvertedIndex).
class InvertedIndex {
 public:
  InvertedIndex();
  ~InvertedIndex();
  InvertedIndex(const InvertedIndex&) = delete;
  InvertedIndex& operator=(const InvertedIndex&) = delete;

  // Returns document ID and positions of a term.
  PostingList FindTerm(const base::string16& term) const;

  // Adds a new document to the inverted index. If the document ID is already in
  // the index, remove the existing and add the new one. All tokens must be
  // unique (have unique content). This function doesn't modify any cache. It
  // only adds documents and tokens to the index.
  void AddDocument(const std::string& document_id,
                   const std::vector<Token>& tokens);

  // Removes a document from the inverted index. Do nothing if document_id is
  // not in the index. This function doesn't modify any cache. It only removes
  // documents and tokens from the index.
  void RemoveDocument(const std::string& document_id);

  // Gets TF-IDF scores for a term. This function returns the TF-IDF score from
  // the cache.
  // Note: client of this function should call BuildInvertedIndex before using
  // this function to have up-to-date score.
  std::vector<TfidfResult> GetTfidf(const base::string16& term) const;

  // Builds the inverted index.
  void BuildInvertedIndex();

  // Checks if the inverted index has been built: returns |true| if the inverted
  // index is up to date, returns |false| if there are some modified document
  // since the last time the index has been built.
  bool IsInvertedIndexBuilt() const { return terms_to_be_updated_.empty(); }

 private:
  friend class InvertedIndexTest;

  // Calculates TF-IDF scores for a term.
  std::vector<TfidfResult> CalculateTfidf(const base::string16& term);

  // Set of the terms that are needed to be update in |tfidf_cache_|.
  std::unordered_set<base::string16> terms_to_be_updated_;
  // Contains the length of the document (the number of terms in the document).
  // The size of this map will always equal to the number of documents in the
  // index.
  std::unordered_map<std::string, int> doc_length_;
  // A map from term to PostingList.
  std::unordered_map<base::string16, PostingList> dictionary_;
  // Contains the TF-IDF scores for all the term in the index.
  std::unordered_map<base::string16, std::vector<TfidfResult>> tfidf_cache_;
  // Number of documents when the index was built.
  uint32_t num_docs_from_last_update_ = 0;
};

}  // namespace local_search_service

#endif  // CHROME_BROWSER_CHROMEOS_LOCAL_SEARCH_SERVICE_INVERTED_INDEX_H_
