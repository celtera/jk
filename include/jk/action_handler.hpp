#pragma once
#include <boost/fusion/container.hpp>
#include <boost/variant.hpp>

#include <jk/actions.hpp>

#include <iostream>
namespace jk::actions
{
struct handlers
{
  std::vector<action_fun> total;
  std::vector<std::pair<std::string, action_fun>> current_object;
  std::vector<action_fun> current_pipe;
  std::vector<action_fun> current_seq;
  std::vector<action_fun> current_action;

  action_fun collapsed_actions()
  {
    return {
        [acts = std::move(current_action)](const value& in)
        {
          if (acts.empty())
          {
            return action::copy_all(in);
          }
          else
          {
            return action::collapse_pipe(in, acts);
          }
        },
        "collapsed_actions"};
  }

  void create_member(const std::string& alnum)
  {
    current_object.emplace_back(std::make_pair(alnum, collapsed_actions()));
  }

  void read_member(const std::string& alnum)
  {
    // FIXME likely we have to use the current collapsed action?
    auto act = action_fun{
        {[=](const value& in) { return action::access_member(alnum, in); }},
        "read_member"};
    current_object.emplace_back(std::make_pair(alnum, act));
  }

  void create_object(auto)
  {
    auto act = [mems = std::move(current_object)](const value& in)
    { return action::as_object(in, mems); };
    current_action.push_back(action_fun{act, "create_object"});
  }
  void finish_pipe_action(auto)
  {
    current_pipe.push_back(collapsed_actions());
  }

  void finish_pipe(auto)
  {
    // Same than finish_seq_action
  }

  void finish_seq_action(auto)
  {
    // FIXME this should put stuff in current_seq
    // And current_seq should then iterate them
    current_seq.push_back(
        {{[acts = std::move(current_pipe)](const value& in)
          {
            if (acts.empty())
            {
              return action::copy_all(in);
            }
            else
            {
              return action::collapse_pipe(in, acts);
            }
          }},
         "finish_seq_action"});
  }

  void finish_seq(auto)
  {
    auto act = action_fun{
        [acts = std::move(current_seq)](const value& in) -> generator<value>
        {
          if (acts.empty())
          {
            return action::copy_all(in);
          }
          else
          {
            return action::process_sequence(in, acts);
          }
        },
        "run_finish_seq"};
    current_seq.clear();
    current_seq.push_back(std::move(act));
  }

  void access_array(int idx)
  {
    current_action.push_back(
        {{[=](const value& in) { return action::access_array(idx, in); }},
         "access_array"});
  }

  void access_array_pipe(const std::vector<int>& idx)
  {
    current_action.push_back(
        {{[=](const value& in)
          { return action::access_array_indices(idx, in); }},
         "access_array_pipe"});
  }

  void access_array_range(boost::fusion::deque<int, int> idx)
  {
    current_action.push_back(
        {{[=](const value& in) {
           return action::access_array_range({at_c<0>(idx), at_c<1>(idx)}, in);
         }},
         "access_array_range"});
  }

  void access_member(const std::string& idx)
  {
    current_action.push_back(
        {{[=](const value& in) { return action::access_member(idx, in); }},
         "access_member"});
  }

  void access_empty_array(auto)
  {
    current_action.push_back(
        {{[=](const value& in) { return action::iterate_array(in); }},
         "access_empty_array"});
  }

  void create_array(auto)
  {
    DEBUG("create_array\n");
    auto t = std::move(current_seq);
    current_seq.clear();
    current_action.push_back(
        {{[acts = std::move(t)](const value& in) -> generator<value>
          { return action::as_array(in, acts); }},
         "create_array"});
  }

  void recurse(auto)
  {
    current_action.push_back(
        {{[=](const value& in) { return action::recurse(in); }}, "recurse"});
  }

  void clear()
  {
    current_action.clear();
    current_pipe.clear();
    current_seq.clear();
    total.clear();
  }
};
}
