#pragma once
#include <jk/actions.hpp>
#include <boost/fusion/container.hpp>

namespace jk::actions
{
struct handlers {
  std::vector<action_fun> total;
  std::vector<action_fun> current_sequence;
  std::vector<action_fun> current_action;

  void finish_action(auto) {
    current_sequence.push_back([acts = std::move(current_action)] (const value& in) {
      if(acts.empty())
        return action::copy_all(in);
      else
        return action::collapse(in, acts);
    });
  }

  void finish_sequence(auto) {
    total.push_back([acts = std::move(current_sequence)] (const value& in) {
      if(acts.empty())
        return action::copy_all(in);
      else
        return action::collapse(in, acts);
    });
  }

  void access_array(int idx) {
    current_action.push_back([=] (const value& in) {
      return action::access_array(idx, in);
    });
  }

  void access_array_sequence(const std::vector<int>& idx) {
    current_action.push_back([=] (const value& in) {
      return action::access_array_indices(idx, in);
    });
  }

  void access_array_range(boost::fusion::deque<int, int> idx) {
    current_action.push_back([=] (const value& in) {
      return action::access_array_range({at_c<0>(idx), at_c<1>(idx)}, in);
    });
  }

  void access_member(const std::string& idx) {
    current_action.push_back([=] (const value& in) {
      return action::access_member(idx, in);
    });
  }

  void access_empty_array(auto) {
    current_action.push_back([=] (const value& in) {
      return action::iterate_array(in);
    });
  }

  void create_array(auto) {
    auto t = std::move(total);
    total.clear();
    total.push_back([acts = std::move(t)] (const value& in) -> generator<value> {
      return action::as_array(in, acts);
    });
  }

  void clear() {
    current_action.clear();
    current_sequence.clear();
    total.clear();
  }
};
}
