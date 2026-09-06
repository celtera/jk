#pragma once
#include <jk/action_fun.hpp>

#include <vector>

namespace jk::actions
{
//! A successfully compiled program; parsing never exposes partial actions.
struct handlers
{
  std::vector<action_fun> current_seq;
};
}
