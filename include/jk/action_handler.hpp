#pragma once
#include <boost/fusion/container.hpp>

#include <jk/actions.hpp>

namespace jk::actions
{
struct handlers
{
  std::vector<action_fun> total;
  std::vector<action_fun> current_pipe;
  std::vector<action_fun> current_seq;
  std::vector<action_fun> current_action;

  void finish_pipe_action(auto)
  {
    DEBUG("finish_pipe_action\n");
    current_pipe.push_back(
        {{[acts = std::move(current_action)](const value& in)
          {
            DEBUG(" -- run_finish_pipe_action\n");
            if (acts.empty())
            {
              DEBUG(" ---- copy_all! \n");
              return action::copy_all(in);
            }
            else
            {
              DEBUG(" ---- collapse_pipe! \n");
              return action::collapse_pipe(in, acts);
            }
          }},
         "finish_pipe_action"});
  }

  void finish_pipe(auto)
  {

    // Same than finish_seq_action
    // DEBUG("finish_pipe\n");
  }

  void finish_seq_action(auto)
  {
    DEBUG("finish_seq_action\n");
    // FIXME this should put stuff in current_seq
    // And current_seq should then iterate them
    current_seq.push_back(
        {{[acts = std::move(current_pipe)](const value& in)
          {
            DEBUG(" -- run_finish_seq_action\n");
            if (acts.empty())
            {
              DEBUG(" ---- copy_all! \n");
              return action::copy_all(in);
            }
            else
            {
              DEBUG(" ---- collapse_pipe! \n");
              return action::collapse_pipe(in, acts);
            }
          }},
         "finish_seq_action"});
    /*
    current_seq.push_back([acts = std::move(current_action)] (const value& in) {
      if(acts.empty())
        return action::copy_all(in);
      else
        return action::collapse_pipe(in, acts);
    });
    */
  }

  void finish_seq(auto)
  {
    DEBUG("finish_seq\n");
    auto t = std::move(current_seq);
    current_seq.clear();
    current_seq.push_back(
        {{[acts = std::move(t)](const value& in) -> generator<value>
          {
            DEBUG(" -- run_finish_seq\n");
            if (acts.empty())
            {
              DEBUG(" ---- copy_all! \n");
              return action::copy_all(in);
            }
            else
            {
              DEBUG(" ---- process_all! \n");
              return action::process_sequence(in, acts);
            }
          }},
         "run_finish_seq"});
    // total = std::move(current_seq);
    /*
    DEBUG("finish_seq\n");
    auto t = std::move(current_seq);
    current_seq.clear();
    total.push_back([acts = std::move(t)] (const value& in) -> generator<value> {
      return action::copy_all(in);
    });
    */
    /*
    total.push_back([acts = std::move(current_pipe)] (const value& in) {
      if(acts.empty())
        return action::copy_all(in);
      else
        return action::process_all(acts, in);
    });
    */
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

  void clear()
  {
    current_action.clear();
    current_pipe.clear();
    current_seq.clear();
    total.clear();
  }
};
}
