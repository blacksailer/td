//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/invoke.h"
#include "td/utils/logging.h"

#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

//
// Essentially we have:
// (ActorT::func, arg1, arg2, ..., argn)
// We want to call:
// actor->func(arg1, arg2, ..., argn)
// And in some cases we would like to delay this call.
//
// First attempt would be
// [a1=arg1, a2=arg2, ..., an=argn](ActorT *actor) {
//   actor->func(a1, a2, ..., an)
// }
//
// But there are some difficulties with elimitation on unnecessary copies.
// We want to use move constructor when it is possible
//
// We may pass
// Tmp. Temporary / rvalue reference
// Var. Variable / reference
// CnstRef. const reference
//
//
// Function may expect
// Val. Value
// CnstRef. const reference
// Ref. rvalue reverence / reference
//
// TODO:
//    Immediate call / Delayed call
// Tmp->Val       move / move->move
// Tmp->CnstRef      + / move->+
// Tmp->Ref          + / move->+
// Var->Val       copy / copy->move
// Var->CnstRef      + / copy->
// Var->Ref          + / copy->+   // khm. It will complile, but won't work
//
// So I will use common idiom: forward references
// If delay is needed, just std::forward data to temporary storage, and std::move them when call is executed.
//
//
// create_immediate_closure(&ActorT::func, arg1, arg2, ..., argn).run(actor)
// to_delayed_closure(std::move(immediate)).run(actor)

namespace td {
template <class ActorT, class FunctionT, class... ArgsT>
class DelayedClosure;

template <class ActorT, class FunctionT, class... ArgsT>
class ImmediateClosure {
 public:
  using Delayed = DelayedClosure<ActorT, FunctionT, ArgsT...>;
  friend Delayed;
  using ActorType = ActorT;

  auto run(ActorT *actor) {
    return mem_call_tuple(actor, std::move(args));
  }

  // no &&. just save references as references.
  explicit ImmediateClosure(FunctionT func, ArgsT... args) : args(func, std::forward<ArgsT>(args)...) {
  }

 private:
  std::tuple<FunctionT, ArgsT...> args;
};

template <class ActorT, class ResultT, class... DestArgsT, class... SrcArgsT>
ImmediateClosure<ActorT, ResultT (ActorT::*)(DestArgsT...), SrcArgsT &&...> create_immediate_closure(
    ResultT (ActorT::*func)(DestArgsT...), SrcArgsT &&... args) {
  return ImmediateClosure<ActorT, ResultT (ActorT::*)(DestArgsT...), SrcArgsT &&...>(func,
                                                                                     std::forward<SrcArgsT>(args)...);
}

template <class ActorT, class FunctionT, class... ArgsT>
class DelayedClosure {
 public:
  using ActorType = ActorT;
  using Delayed = DelayedClosure<ActorT, FunctionT, ArgsT...>;

  auto run(ActorT *actor) {
    return mem_call_tuple(actor, std::move(args));
  }

  DelayedClosure clone() const {
    return do_clone(*this);
  }

  explicit DelayedClosure(ImmediateClosure<ActorT, FunctionT, ArgsT...> &&other) : args(std::move(other.args)) {
  }

  explicit DelayedClosure(FunctionT func, ArgsT... args) : args(func, std::forward<ArgsT>(args)...) {
  }

  template <class F>
  void for_each(const F &f) {
    tuple_for_each(args, f);
  }

 private:
  using ArgsStorageT = std::tuple<FunctionT, typename std::decay<ArgsT>::type...>;

  ArgsStorageT args;

  template <class FromActorT, class FromFunctionT, class... FromArgsT>
  explicit DelayedClosure(const DelayedClosure<FromActorT, FromFunctionT, FromArgsT...> &other,
                          std::enable_if_t<LogicAnd<std::is_copy_constructible<FromArgsT>::value...>::value, int> = 0)
      : args(other.args) {
  }

  template <class FromActorT, class FromFunctionT, class... FromArgsT>
  explicit DelayedClosure(
      const DelayedClosure<FromActorT, FromFunctionT, FromArgsT...> &other,
      std::enable_if_t<!LogicAnd<std::is_copy_constructible<FromArgsT>::value...>::value, int> = 0) {
    LOG(FATAL) << "Deleted constructor";
    std::abort();
  }

  template <class FromActorT, class FromFunctionT, class... FromArgsT>
  std::enable_if_t<!LogicAnd<std::is_copy_constructible<FromArgsT>::value...>::value,
                   DelayedClosure<FromActorT, FromFunctionT, FromArgsT...>>
  do_clone(const DelayedClosure<FromActorT, FromFunctionT, FromArgsT...> &value) const {
    LOG(FATAL) << "Trying to clone DelayedClosure that contains noncopyable elements";
    std::abort();
  }

  template <class FromActorT, class FromFunctionT, class... FromArgsT>
  std::enable_if_t<LogicAnd<std::is_copy_constructible<FromArgsT>::value...>::value,
                   DelayedClosure<FromActorT, FromFunctionT, FromArgsT...>>
  do_clone(const DelayedClosure<FromActorT, FromFunctionT, FromArgsT...> &value) const {
    return DelayedClosure<FromActorT, FromFunctionT, FromArgsT...>(value);
  }
};

template <class... ArgsT>
typename ImmediateClosure<ArgsT...>::Delayed to_delayed_closure(ImmediateClosure<ArgsT...> &&other) {
  return typename ImmediateClosure<ArgsT...>::Delayed(std::move(other));
}

template <class... ArgsT>
DelayedClosure<ArgsT...> to_delayed_closure(DelayedClosure<ArgsT...> &&other) {
  return std::move(other);
}

template <class ActorT, class ResultT, class... DestArgsT, class... SrcArgsT>
auto create_delayed_closure(ResultT (ActorT::*func)(DestArgsT...), SrcArgsT &&... args) {
  return DelayedClosure<ActorT, ResultT (ActorT::*)(DestArgsT...), SrcArgsT &&...>(func,
                                                                                   std::forward<SrcArgsT>(args)...);
}

}  // namespace td
