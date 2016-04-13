﻿/*
 * [The "BSD license"]
 *  Copyright (c) 2016 Mike Lischke
 *  Copyright (c) 2013 Terence Parr
 *  Copyright (c) 2013 Dan McLaughlin
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 *  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "WritableToken.h"
#include "Lexer.h"
#include "RuleContext.h"
#include "Interval.h"
#include "Exceptions.h"
#include "CPPUtils.h"

#include "BufferedTokenStream.h"

using namespace org::antlr::v4::runtime;
using namespace antlrcpp;

BufferedTokenStream::BufferedTokenStream(TokenSource *tokenSource) : _tokenSource(tokenSource){
  InitializeInstanceFields();
}

TokenSource* BufferedTokenStream::getTokenSource() const {
  return _tokenSource;
}

size_t BufferedTokenStream::index() {
  return _p;
}

ssize_t BufferedTokenStream::mark() {
  return 0;
}

void BufferedTokenStream::release(ssize_t marker) {
  // no resources to release
}

void BufferedTokenStream::reset() {
  seek(0);
}

void BufferedTokenStream::seek(size_t index) {
  lazyInit();
  _p = adjustSeekIndex(index);
}

size_t BufferedTokenStream::size() {
  return _tokens.size();
}

void BufferedTokenStream::consume() {
  if (LA(1) == EOF) {
    throw IllegalStateException("cannot consume EOF");
  }

  if (sync(_p + 1)) {
    _p = adjustSeekIndex(_p + 1);
  }
}

bool BufferedTokenStream::sync(size_t i) {
  size_t n = i - _tokens.size() + 1; // how many more elements we need?

  if (n > 0) {
    size_t fetched = fetch(n);
    return fetched >= n;
  }

  return true;
}

size_t BufferedTokenStream::fetch(size_t n) {
  if (_fetchedEOF) {
    return 0;
  }

  for (size_t i = 0; i < n; i++) {
    TokenRef t = _tokenSource->nextToken();
    if (is<WritableToken>(t)) {
      (std::dynamic_pointer_cast<WritableToken>(t))->setTokenIndex((int)_tokens.size());
    }
    _tokens.push_back(t);
    if (t->getType() == EOF) {
      _fetchedEOF = true;
      return i + 1;
    }
  }

  return n;
}

TokenRef BufferedTokenStream::get(size_t i) const {
  if (i >= _tokens.size()) {
    throw IndexOutOfBoundsException(std::string("token index ") +
                                    std::to_string(i) +
                                    std::string(" out of range 0..") +
                                    std::to_string(_tokens.size() - 1));
  }
  return _tokens[i];
}

std::vector<TokenRef> BufferedTokenStream::get(size_t start, size_t stop) {
  std::vector<TokenRef> subset;

  lazyInit();

  if (_tokens.empty()) {
    return subset;
  }

  if (stop >= _tokens.size()) {
    stop = _tokens.size() - 1;
  }
  for (size_t i = start; i <= stop; i++) {
    TokenRef t = _tokens[i];
    if (t->getType() == EOF) {
      break;
    }
    subset.push_back(t);
  }
  return subset;
}

ssize_t BufferedTokenStream::LA(ssize_t i) {
  return LT(i)->getType();
}

TokenRef BufferedTokenStream::LB(size_t k) {
  if (k > _p) {
    return nullptr;
  }
  return _tokens[(size_t)(_p - k)];
}

TokenRef BufferedTokenStream::LT(ssize_t k) {
  lazyInit();
  if (k == 0) {
    return nullptr;
  }
  if (k < 0) {
    return LB((size_t)-k);
  }

  size_t i = (size_t)((ssize_t)_p + k - 1);
  sync(i);
  if (i >= _tokens.size()) { // return EOF token
                             // EOF must be last token
    return _tokens.back();
  }

  return _tokens[i];
}

size_t BufferedTokenStream::adjustSeekIndex(size_t i) {
  return i;
}

void BufferedTokenStream::lazyInit() {
  if (_needSetup) {
    setup();
  }
}

void BufferedTokenStream::setup() {
  _needSetup = false;
  sync(0);
  _p = adjustSeekIndex(0);
}

void BufferedTokenStream::setTokenSource(TokenSource *tokenSource) {
  _tokenSource = tokenSource;
  _tokens.clear();
  _needSetup = true;
}

std::vector<TokenRef> BufferedTokenStream::getTokens() {
  return _tokens;
}

std::vector<TokenRef> BufferedTokenStream::getTokens(int start, int stop) {
  return getTokens(start, stop, std::vector<int>());
}

std::vector<TokenRef> BufferedTokenStream::getTokens(int start, int stop, const std::vector<int> &types) {
  lazyInit();
  if (start < 0 || stop >= (int)_tokens.size() || stop < 0 || start >= (int)_tokens.size()) {
    throw IndexOutOfBoundsException(std::string("start ") +
                                    std::to_string(start) +
                                    std::string(" or stop ") +
                                    std::to_string(stop) +
                                    std::string(" not in 0..") +
                                    std::to_string(_tokens.size() - 1));
  }

  std::vector<TokenRef> filteredTokens;

  if (start > stop) {
    return filteredTokens;
  }

  // list = tokens[start:stop]:{T t, t.getType() in types}
  for (size_t i = (size_t)start; i <= (size_t)stop; i++) {
    TokenRef tok = _tokens[i];

    if (types.empty() || std::find(types.begin(), types.end(), tok->getType()) != types.end()) {
      filteredTokens.push_back(tok);
    }
  }
  return filteredTokens;
}

std::vector<TokenRef> BufferedTokenStream::getTokens(int start, int stop, int ttype) {
  std::vector<int> s;
  s.push_back(ttype);
  return getTokens(start, stop, s);
}

ssize_t BufferedTokenStream::nextTokenOnChannel(size_t i, int channel) {
  sync(i);
  if (i >= size()) {
    return -1;
  }

  TokenRef token = _tokens[i];
  while (token->getChannel() != channel) {
    if (token->getType() == EOF) {
      return -1;
    }
    i++;
    sync(i);
    token = _tokens[i];
  }
  return (ssize_t)i;
}

ssize_t BufferedTokenStream::previousTokenOnChannel(size_t i, int channel) const {
  do {
    if (_tokens[i]->getChannel() == channel)
      return (ssize_t)i;
    if (i == 0)
      return -1;
    i--;
  } while (true);
  return -1;
}

std::vector<TokenRef> BufferedTokenStream::getHiddenTokensToRight(size_t tokenIndex, int channel) {
  lazyInit();
  if (tokenIndex >= _tokens.size()) {
    throw IndexOutOfBoundsException(std::to_string(tokenIndex) + " not in 0.." + std::to_string(_tokens.size() - 1));
  }

  ssize_t nextOnChannel = nextTokenOnChannel(tokenIndex + 1, Lexer::DEFAULT_TOKEN_CHANNEL);
  ssize_t to;
  size_t from = tokenIndex + 1;
  // if none onchannel to right, nextOnChannel=-1 so set to = last token
  if (nextOnChannel == -1) {
    to = (ssize_t)size() - 1;
  } else {
    to = nextOnChannel;
  }

  return filterForChannel(from, (size_t)to, channel);
}

std::vector<TokenRef> BufferedTokenStream::getHiddenTokensToRight(size_t tokenIndex) {
  return getHiddenTokensToRight(tokenIndex, -1);
}

std::vector<TokenRef> BufferedTokenStream::getHiddenTokensToLeft(size_t tokenIndex, int channel) {
  lazyInit();
  if (tokenIndex >= _tokens.size()) {
    throw IndexOutOfBoundsException(std::to_string(tokenIndex) + " not in 0.." + std::to_string(_tokens.size() - 1));
  }

  ssize_t prevOnChannel = previousTokenOnChannel(tokenIndex - 1, Lexer::DEFAULT_TOKEN_CHANNEL);
  if (prevOnChannel == (ssize_t)tokenIndex - 1) {
    return std::vector<TokenRef>();
  }
  // if none onchannel to left, prevOnChannel=-1 then from=0
  size_t from = (size_t)(prevOnChannel + 1);
  size_t to = tokenIndex - 1;

  return filterForChannel(from, to, channel);
}

std::vector<TokenRef> BufferedTokenStream::getHiddenTokensToLeft(size_t tokenIndex) {
  return getHiddenTokensToLeft(tokenIndex, -1);
}

std::vector<TokenRef> BufferedTokenStream::filterForChannel(size_t from, size_t to, int channel) {
  std::vector<TokenRef> hidden;
  for (size_t i = from; i <= to; i++) {
    TokenRef t = _tokens[i];
    if (channel == -1) {
      if (t->getChannel() != Lexer::DEFAULT_TOKEN_CHANNEL) {
        hidden.push_back(t);
      }
    } else {
      if (t->getChannel() == channel) {
        hidden.push_back(t);
      }
    }
  }

  return hidden;
}

/**
 * Get the text of all tokens in this buffer.
 */
std::string BufferedTokenStream::getSourceName() const
{
  return _tokenSource->getSourceName();
}

std::wstring BufferedTokenStream::getText() {
  lazyInit();
  fill();
  return getText(misc::Interval(0, (int)size() - 1));
}

std::wstring BufferedTokenStream::getText(const misc::Interval &interval) {
  int start = interval.a;
  int stop = interval.b;
  if (start < 0 || stop < 0) {
    return L"";
  }
  lazyInit();
  if (stop >= (int)_tokens.size()) {
    stop = (int)_tokens.size() - 1;
  }

  std::wstringstream ss;
  for (size_t i = (size_t)start; i <= (size_t)stop; i++) {
    TokenRef t = _tokens[i];
    if (t->getType() == EOF) {
      break;
    }
    ss << t->getText();
  }
  return ss.str();
}

std::wstring BufferedTokenStream::getText(RuleContext *ctx) {
  return getText(ctx->getSourceInterval());
}

std::wstring BufferedTokenStream::getText(TokenRef start, TokenRef stop) {
  if (start != nullptr && stop != nullptr) {
    return getText(misc::Interval(start->getTokenIndex(), stop->getTokenIndex()));
  }

  return L"";
}

void BufferedTokenStream::fill() {
  lazyInit();
  const size_t blockSize = 1000;
  while (true) {
    size_t fetched = fetch(blockSize);
    if (fetched < blockSize) {
      return;
    }
  }
}

void BufferedTokenStream::InitializeInstanceFields() {
  _needSetup = true;
  _fetchedEOF = false;
}
