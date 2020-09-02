// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental-interpreter.h"

#include "src/base/optional.h"
#include "src/base/small-vector.h"

namespace v8 {
namespace internal {

using MatchRange = ExperimentalRegExpInterpreter::MatchRange;

namespace {

template <class Character>
class NfaInterpreter {
  // Executes a bytecode program in breadth-first mode, without backtracking.
  // `Character` can be instantiated with `uint8_t` or `uc16` for one byte or
  // two byte input strings.
  //
  // In contrast to the backtracking implementation, this has linear time
  // complexity in the length of the input string. Breadth-first mode means
  // that threads are executed in lockstep with respect to their input
  // position, i.e. the threads share a common input index.  This is similar
  // to breadth-first simulation of a non-deterministic finite automaton (nfa),
  // hence the name of the class.
  //
  // To follow the semantics of a backtracking VM implementation, we have to be
  // careful about whether we stop execution when a thread executes ACCEPT.
  // For example, consider execution of the bytecode generated by the regexp
  //
  //   r = /abc|..|[a-c]{10,}/
  //
  // on input "abcccccccccccccc".  Clearly the three alternatives
  // - /abc/
  // - /../
  // - /[a-c]{10,}/
  // all match this input.  A backtracking implementation will report "abc" as
  // match, because it explores the first alternative before the others.
  //
  // However, if we execute breadth first, then we execute the 3 threads
  // - t1, which tries to match /abc/
  // - t2, which tries to match /../
  // - t3, which tries to match /[a-c]{10,}/
  // in lockstep i.e. by iterating over the input and feeding all threads one
  // character at a time.  t2 will execute an ACCEPT after two characters,
  // while t1 will only execute ACCEPT after three characters. Thus we find a
  // match for the second alternative before a match of the first alternative.
  //
  // This shows that we cannot always stop searching as soon as some thread t
  // executes ACCEPT:  If there is a thread u with higher priority than t, then
  // it must be finished first.  If u produces a match, then we can discard the
  // match of t because matches produced by threads with higher priority are
  // preferred over matches of threads with lower priority.  On the other hand,
  // we are allowed to abort all threads with lower priority than t if t
  // produces a match: Such threads can only produce worse matches.  In the
  // example above, we can abort t3 after two characters because of t2's match.
  //
  // Thus the interpreter keeps track of a priority-ordered list of threads.
  // If a thread ACCEPTs, all threads with lower priority are discarded, and
  // the search continues with the threads with higher priority.  If no threads
  // with high priority are left, we return the match that was produced by the
  // ACCEPTing thread with highest priority.
 public:
  NfaInterpreter(Vector<const RegExpInstruction> bytecode,
                 Vector<const Character> input, int32_t input_index)
      : bytecode_(bytecode),
        input_(input),
        input_index_(input_index),
        pc_last_input_index_(bytecode.size()),
        active_threads_(),
        blocked_threads_(),
        best_match_(base::nullopt) {
    DCHECK(!bytecode_.empty());
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(), -1);
  }

  // Finds up to `max_match_num` matches and writes their boundaries to
  // `matches_out`.  The search begins at the current input index.  Returns the
  // number of matches found.
  int FindMatches(MatchRange* matches_out, int max_match_num) {
    int match_num;
    for (match_num = 0; match_num != max_match_num; ++match_num) {
      base::Optional<MatchRange> match = FindNextMatch();
      if (!match.has_value()) {
        break;
      }

      matches_out[match_num] = *match;
      SetInputIndex(match->end);
    }
    return match_num;
  }

 private:
  // The state of a "thread" executing experimental regexp bytecode.  (Not to
  // be confused with an OS thread.)
  struct InterpreterThread {
    // This thread's program counter, i.e. the index within `bytecode_` of the
    // next instruction to be executed.
    int32_t pc;
    // The index in the input string where this thread started executing.
    int32_t match_begin;
  };

  // Change the current input index for future calls to `FindNextMatch`.
  void SetInputIndex(int new_input_index) {
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    input_index_ = new_input_index;
  }

  // Find the next match, begin search at input_index_;
  base::Optional<MatchRange> FindNextMatch() {
    DCHECK(active_threads_.empty());
    // TODO(mbid,v8:10765): Can we get around resetting `pc_last_input_index_`
    // here? As long as
    //
    //   pc_last_input_index_[pc] < input_index_
    //
    // for all possible program counters pc that are reachable without input
    // from pc = 0 and
    //
    //   pc_last_input_index_[k] <= input_index_
    //
    // for all k > 0 hold I think everything should be fine.  Maybe we can do
    // something about this in `SetInputIndex`.
    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(), -1);

    DCHECK(blocked_threads_.empty());
    DCHECK(active_threads_.empty());
    DCHECK_EQ(best_match_, base::nullopt);

    // All threads start at bytecode 0.
    active_threads_.emplace_back(InterpreterThread{0, input_index_});
    // Run the initial thread, potentially forking new threads, until every
    // thread is blocked without further input.
    RunActiveThreads();

    // We stop if one of the following conditions hold:
    // - We have exhausted the entire input.
    // - We have found a match at some point, and there are no remaining
    //   threads with higher priority than the thread that produced the match.
    //   Threads with low priority have been aborted earlier, and the remaining
    //   threads are blocked here, so the latter simply means that
    //   `blocked_threads_` is empty.
    while (input_index_ != input_.length() &&
           !(best_match_.has_value() && blocked_threads_.empty())) {
      DCHECK(active_threads_.empty());
      uc16 input_char = input_[input_index_];
      ++input_index_;

      // If we haven't found a match yet, we add a thread with least priority
      // that attempts a match starting after `input_char`.
      if (!best_match_.has_value()) {
        active_threads_.emplace_back(InterpreterThread{0, input_index_});
      }

      // We unblock all blocked_threads_ by feeding them the input char.
      FlushBlockedThreads(input_char);

      // Run all threads until they block or accept.
      RunActiveThreads();
    }

    // Clean up the data structures we used.
    base::Optional<MatchRange> result = best_match_;
    best_match_ = base::nullopt;
    blocked_threads_.clear();
    active_threads_.clear();

    return result;
  }

  // Run an active thread `t` until it executes a CONSUME_RANGE or ACCEPT
  // instruction, or its PC value was already processed.
  // - If processing of `t` can't continue because of CONSUME_RANGE, it is
  //   pushed on `blocked_threads_`.
  // - If `t` executes ACCEPT, set `best_match` according to `t.match_begin` and
  //   the current input index. All remaining `active_threads_` are discarded.
  void RunActiveThread(InterpreterThread t) {
    while (true) {
      if (IsPcProcessed(t.pc)) return;
      MarkPcProcessed(t.pc);

      RegExpInstruction inst = bytecode_[t.pc];
      switch (inst.opcode) {
        case RegExpInstruction::CONSUME_RANGE: {
          blocked_threads_.emplace_back(t);
          return;
        }
        case RegExpInstruction::FORK: {
          InterpreterThread fork = t;
          fork.pc = inst.payload.pc;
          active_threads_.emplace_back(fork);
          ++t.pc;
          break;
        }
        case RegExpInstruction::JMP:
          t.pc = inst.payload.pc;
          break;
        case RegExpInstruction::ACCEPT:
          best_match_ = MatchRange{t.match_begin, input_index_};
          active_threads_.clear();
          return;
      }
    }
  }

  // Run each active thread until it can't continue without further input.
  // `active_threads_` is empty afterwards.  `blocked_threads_` are sorted from
  // low to high priority.
  void RunActiveThreads() {
    while (!active_threads_.empty()) {
      InterpreterThread t = active_threads_.back();
      active_threads_.pop_back();
      RunActiveThread(t);
    }
  }

  // Unblock all blocked_threads_ by feeding them an `input_char`.  Should only
  // be called with `input_index_` pointing to the character *after*
  // `input_char` so that `pc_last_input_index_` is updated correctly.
  void FlushBlockedThreads(uc16 input_char) {
    // The threads in blocked_threads_ are sorted from high to low priority,
    // but active_threads_ needs to be sorted from low to high priority, so we
    // need to activate blocked threads in reverse order.
    //
    // TODO(mbid,v8:10765): base::SmallVector doesn't support `rbegin()` and
    // `rend()`, should we implement that instead of this awkward iteration?
    // Maybe we could at least use an int i and check for i >= 0, but
    // SmallVectors don't have length() methods.
    for (size_t i = blocked_threads_.size(); i > 0; --i) {
      InterpreterThread t = blocked_threads_[i - 1];
      RegExpInstruction inst = bytecode_[t.pc];
      DCHECK_EQ(inst.opcode, RegExpInstruction::CONSUME_RANGE);
      RegExpInstruction::Uc16Range range = inst.payload.consume_range;
      if (input_char >= range.min && input_char <= range.max) {
        ++t.pc;
        active_threads_.emplace_back(t);
      }
    }
    blocked_threads_.clear();
  }

  // It is redundant to have two threads t, t0 execute at the same PC value,
  // because one of t, t0 matches iff the other does.  We can thus discard
  // the one with lower priority.  We check whether a thread executed at some
  // PC value by recording for every possible value of PC what the value of
  // input_index_ was the last time a thread executed at PC. If a thread
  // tries to continue execution at a PC value that we have seen before at
  // the current input index, we abort it. (We execute threads with higher
  // priority first, so the second thread is guaranteed to have lower
  // priority.)
  //
  // Check whether we've seen an active thread with a given pc value since the
  // last increment of `input_index_`.
  bool IsPcProcessed(int pc) {
    DCHECK_LE(pc_last_input_index_[pc], input_index_);
    return pc_last_input_index_[pc] == input_index_;
  }

  // Mark a pc as having been processed since the last increment of
  // `input_index_`.
  void MarkPcProcessed(int pc) {
    DCHECK_LE(pc_last_input_index_[pc], input_index_);
    pc_last_input_index_[pc] = input_index_;
  }

  Vector<const RegExpInstruction> bytecode_;
  Vector<const Character> input_;
  int input_index_;

  // TODO(mbid,v8:10765): The following `SmallVector`s have somehwat
  // arbitrarily chosen small capacity sizes; should benchmark to find a good
  // value.

  // pc_last_input_index_[k] records the value of input_index_ the last
  // time a thread t such that t.pc == k was activated, i.e. put on
  // active_threads_.  Thus pc_last_input_index.size() == bytecode.size().  See
  // also `RunActiveThread`.
  base::SmallVector<int, 64> pc_last_input_index_;

  // Active threads can potentially (but not necessarily) continue without
  // input.  Sorted from low to high priority.
  base::SmallVector<InterpreterThread, 64> active_threads_;

  // The pc of a blocked thread points to an instruction that consumes a
  // character. Sorted from high to low priority (so the opposite of
  // `active_threads_`).
  base::SmallVector<InterpreterThread, 64> blocked_threads_;

  // The best match found so far during the current search.  If several threads
  // ACCEPTed, then this will be the match of the accepting thread with highest
  // priority.
  base::Optional<MatchRange> best_match_;
};

}  // namespace

int ExperimentalRegExpInterpreter::FindMatchesNfaOneByte(
    Vector<const RegExpInstruction> bytecode, Vector<const uint8_t> input,
    int start_index, MatchRange* matches_out, int max_match_num) {
  NfaInterpreter<uint8_t> interpreter(bytecode, input, start_index);
  return interpreter.FindMatches(matches_out, max_match_num);
}

int ExperimentalRegExpInterpreter::FindMatchesNfaTwoByte(
    Vector<const RegExpInstruction> bytecode, Vector<const uc16> input,
    int start_index, MatchRange* matches_out, int max_match_num) {
  NfaInterpreter<uc16> interpreter(bytecode, input, start_index);
  return interpreter.FindMatches(matches_out, max_match_num);
}

}  // namespace internal
}  // namespace v8
