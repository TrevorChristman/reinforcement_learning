#include <boost/test/unit_test.hpp>

#include "joiners/example_joiner.h"
#include "test_common.h"
#include "vw/config/options_cli.h"
#include "vw/core/ccb_reduction_features.h"
#include "vw/core/parse_primitives.h"
namespace v2 = reinforcement_learning::messages::flatbuff::v2;

const int DEFAULT_REWARD = -1000;
const int DEFAULT_REWARD_APPRENTICE = 0;

std::vector<float> get_float_rewards(const std::string& int_file_name, const std::string& obs_file_name,
    v2::RewardFunctionType reward_function_type, v2::LearningModeType learning_mode = v2::LearningModeType_Online,
    v2::ProblemType problem_type = v2::ProblemType_CB, float default_reward = DEFAULT_REWARD)
{
  std::string command;
  switch (problem_type)
  {
    case v2::ProblemType_CB:
      command = "--quiet --binary_parser --cb_explore_adf";
      break;
    case v2::ProblemType_CCB:
      command = "--quiet --binary_parser --ccb_explore_adf";
      break;
    case v2::ProblemType_CA:
      command =
          "--quiet --binary_parser --cats 4 --min_value 1 --max_value "
          "100 --bandwidth 1";
      break;
    case v2::ProblemType_SLATES:
      command = "--quiet --binary_parser --ccb_explore_adf --slates";
      break;
    default:
      assert(false);
      break;
  }

  auto options = VW::make_unique<VW::config::options_cli>(VW::split_command_line(command));
  auto vw = VW::external::initialize_with_binary_parser(std::move(options));

  VW::multi_ex examples;
  example_joiner joiner(vw.get());

  joiner.set_problem_type_config(problem_type);
  joiner.set_learning_mode_config(learning_mode);
  joiner.set_reward_function(reward_function_type);
  joiner.set_default_reward(default_reward);

  std::string input_files = get_test_files_location();
  auto interaction_buffer = read_file(input_files + "/reward_functions/" + int_file_name);
  auto observation_buffer = read_file(input_files + "/reward_functions/" + obs_file_name);

  // need to keep the fb buffer around in order to process the event
  std::vector<flatbuffers::DetachedBuffer> int_detached_buffers;
  auto joined_int_events = wrap_into_joined_events(interaction_buffer, int_detached_buffers);

  for (auto& je : joined_int_events)
  {
    joiner.process_event(*je);
    examples.push_back(VW::new_unused_example(*vw));
  }

  // need to keep the fb buffer around in order to process the event
  std::vector<flatbuffers::DetachedBuffer> obs_detached_buffers;
  auto joined_obs_events = wrap_into_joined_events(observation_buffer, obs_detached_buffers);

  for (auto& je : joined_obs_events) { joiner.process_event(*je); }

  joiner.process_joined(examples);

  std::vector<float> rewards;

  switch (problem_type)
  {
    case v2::ProblemType_CB:
    {
      for (auto* example : examples)
      {
        if (!CB::ec_is_example_header(*example) && example->l.cb.costs.size() > 0)
        { rewards.push_back(-1.f * example->l.cb.costs[0].cost); }
      }
    }
    break;
    case v2::ProblemType_CCB:
    {
      // learn/predict isn't called in the unit test but cleanup examples
      // expects shared pred to be set
      examples[0]->pred.decision_scores.resize(1);
      examples[0]->pred.decision_scores[0].push_back({0, 0.f});

      for (auto* example : examples)
      {
        if (example->l.conditional_contextual_bandit.type == VW::ccb_example_type::SLOT)
        { rewards.push_back(-1.f * example->l.conditional_contextual_bandit.outcome->cost); }
      }
    }
    break;
    case v2::ProblemType_CA:
    {
      if (examples.size() == 1) { rewards.push_back(-1.f * examples[0]->l.cb_cont.costs[0].cost); }
    }
    break;
    case v2::ProblemType_SLATES:
    {
      set_slates_label(examples);

      for (auto* example : examples)
      {
        if (example->l.slates.type == VW::slates::example_type::SHARED)
        { rewards.push_back(-1.f * example->l.slates.cost); }
      }
    }
    break;
    default:
      assert(false);
      break;
  }

  clear_examples(examples, vw.get());
  VW::finish(*vw, false);
  return rewards;
}

BOOST_AUTO_TEST_SUITE(reward_functions_with_cb_format_and_float_reward)

// 3 rewards in f-reward_3obs_v2.fb are: 5, 4, 3 and timestamps are in
// descending order added in test_common.cc
BOOST_AUTO_TEST_CASE(earliest)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Earliest);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3);
}

BOOST_AUTO_TEST_CASE(average)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Average);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), (3.0 + 4 + 5) / 3);
}

BOOST_AUTO_TEST_CASE(min)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Min);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3);
}

BOOST_AUTO_TEST_CASE(max)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Max);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 5);
}

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Median);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 4);
}

BOOST_AUTO_TEST_CASE(sum)
{
  auto rewards = get_float_rewards("cb/cb_v2.fb", "cb/f-reward_3obs_v2.fb", v2::RewardFunctionType_Sum);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3 + 4 + 5);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_ca_format_and_float_reward)

// 3 rewards in f-reward_3obs_v2.fb are: 5, 4, 3 and timestamps are in
// descending order added in test_common.cc
BOOST_AUTO_TEST_CASE(earliest)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Earliest,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3);
}

BOOST_AUTO_TEST_CASE(average)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Average,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), (3.0 + 4 + 5) / 3);
}

BOOST_AUTO_TEST_CASE(min)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Min,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3);
}

BOOST_AUTO_TEST_CASE(max)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Max,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 5);
}

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Median,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 4);
}

BOOST_AUTO_TEST_CASE(sum)
{
  auto rewards = get_float_rewards("ca/ca_v2.fb", "ca/f-reward_3obs_v2.fb", v2::RewardFunctionType_Sum,
      v2::LearningModeType_Online, v2::ProblemType_CA);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 3 + 4 + 5);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_cb_format_and_appentice_mode)
BOOST_AUTO_TEST_CASE(apprentice_with_first_action_matching_baseline_action_returns_real_reward)
{
  // 3 rewards in f-reward_3obs_v2.fb are: 5, 4, 3
  // Apprentice mode assigns reward of 1.0/0.0 for match/no match.
  auto rewards = get_float_rewards("cb/cb_apprentice_match_baseline_v2.fb", "cb/f-reward_3obs_v2.fb",
      v2::RewardFunctionType_Sum, v2::LearningModeType_Apprentice);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.front(), 1);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_ccb_format_with_slot_index)
// fi-reward-v2.fb contains outcome events for two slots:
// for slot 0, rewards are: 2, 5, 2, 4
// for slot 1, rewards are: 2, 2, 5, 1
// test_common.cc added outcome event timestamps in descending order
BOOST_AUTO_TEST_CASE(earliest)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Earliest,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), 4);
  BOOST_CHECK_EQUAL(rewards.at(1), 1);
}

BOOST_AUTO_TEST_CASE(average)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Average,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 5.0 + 2.0 + 4.0) / 4);
  BOOST_CHECK_EQUAL(rewards.at(1), (2.0 + 2.0 + 5.0 + 1.0) / 4);
}

BOOST_AUTO_TEST_CASE(min)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Min,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), 2.0);
  BOOST_CHECK_EQUAL(rewards.at(1), 1.0);
}

BOOST_AUTO_TEST_CASE(max)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Max,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), 5.0);
  BOOST_CHECK_EQUAL(rewards.at(1), 5.0);
}

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Median,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 4.0) / 2);
  BOOST_CHECK_EQUAL(rewards.at(1), (2.0 + 2.0) / 2);
}

BOOST_AUTO_TEST_CASE(sum)
{
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-reward_v2.fb", v2::RewardFunctionType_Sum,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), 2 + 5 + 2 + 4);
  BOOST_CHECK_EQUAL(rewards.at(1), 2 + 2 + 5 + 1);
}

BOOST_AUTO_TEST_CASE(set_default_reward_to_slot_with_no_matching_outcomes)
{
  // This test case is for outcome event slot index out of bound
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fi-out-of-bound-reward_v2.fb", v2::RewardFunctionType_Earliest,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), DEFAULT_REWARD);
  BOOST_CHECK_EQUAL(rewards.at(1), DEFAULT_REWARD);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_ccb_format_with_slot_id)
// ccb-with-slot-id contains 1 interaction with 2 slots with _id equals to
// slot_0 and slot_1
// fs-reward-v2.fb contains outcome events with slot id for two slots:
// for slot_0, rewards are: 2, 5, 2, 4
// for slot_1, rewards are: 2, 2, 5, 1
// test_common.cc added outcome event timestamps in descending order

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("ccb/ccb-with-slot-id_v2.fb", "ccb/fs-reward_v2.fb", v2::RewardFunctionType_Median,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 4.0) / 2);
  BOOST_CHECK_EQUAL(rewards.at(1), (2.0 + 2.0) / 2);
}

BOOST_AUTO_TEST_CASE(set_default_reward_if_slot_has_no_outcome_events)
{
  // This test case is for outcome slot id does not match interaction
  // slot id
  auto rewards = get_float_rewards("ccb/ccb_v2.fb", "ccb/fs-reward_v2.fb", v2::RewardFunctionType_Earliest,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), DEFAULT_REWARD);
  BOOST_CHECK_EQUAL(rewards.at(1), DEFAULT_REWARD);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_ccb_format_with_mixed_slot_index_and_slot_id)
// ccb-with-slot-id contains 1 interaction with 2 slots with _id equals to
// slot_0 and slot_1
// fmix-reward-v2.fb contains outcome events with mixed slot index and slot id for two slots:
// with first slot: (0, 2.0), ("slot_0", 2.0), (0, 5.0), ("slot_0", 2.0)
// with second slot: (1, 2.0), ("slot_1", 5.0), (1, 4.0), ("slot_1", 1.0)
// test_common.cc added outcome event timestamps in descending order

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("ccb/ccb-with-slot-id_v2.fb", "ccb/fmix-reward_v2.fb", v2::RewardFunctionType_Median,
      v2::LearningModeType_Online, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 2.0) / 2);
  BOOST_CHECK_EQUAL(rewards.at(1), (2.0 + 4.0) / 2);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ccb_reward_with_apprentice_mode)
// actions for slot 0: [0, 1], actions for slot 1: [1]
// fi-reward-v2.fb contains outcome events for two slots:
// for slot 0, rewards are: 2, 5, 2, 4
// for slot 1, rewards are: 2, 2, 5, 1
BOOST_AUTO_TEST_CASE(first_action_matching_baseline_action_returns_real_reward)
{  // baseline actions: [0, 1] (set in example_gen.cc)
  // in apprentice mode rewards are 1.0/0.0 for match/no match
  auto rewards = get_float_rewards("ccb/ccb-apprentice-baseline-match_v2.fb", "ccb/fi-reward_v2.fb",
      v2::RewardFunctionType_Sum, v2::LearningModeType_Apprentice, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), 1);
  BOOST_CHECK_EQUAL(rewards.at(1), 1);
}

BOOST_AUTO_TEST_CASE(first_action_not_matching_baseline_action_returns_default_reward)
{  // baseline actions: [1, 0] (set in example_gen.cc)
  auto rewards = get_float_rewards("ccb/ccb-apprentice-baseline-not-match_v2.fb", "ccb/fi-reward_v2.fb",
      v2::RewardFunctionType_Sum, v2::LearningModeType_Apprentice, v2::ProblemType_CCB);

  BOOST_CHECK_EQUAL(rewards.size(), 2);
  BOOST_CHECK_EQUAL(rewards.at(0), DEFAULT_REWARD_APPRENTICE);
  BOOST_CHECK_EQUAL(rewards.at(1), DEFAULT_REWARD_APPRENTICE);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(reward_functions_with_slates_format)
// rewards in fi-reward_v2.fb are 2, 2, 5, 2
// test_common.cc added outcome event timestamps in descending order
BOOST_AUTO_TEST_CASE(earliest)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Earliest,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), 2);
}

BOOST_AUTO_TEST_CASE(average)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Average,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 2.0 + 5.0 + 2.0) / 4);
}

BOOST_AUTO_TEST_CASE(min)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Min,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), 2.0);
}

BOOST_AUTO_TEST_CASE(max)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Max,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), 5.0);
}

BOOST_AUTO_TEST_CASE(median)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Median,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), (2.0 + 2.0) / 2);
}

BOOST_AUTO_TEST_CASE(sum)
{
  auto rewards = get_float_rewards("slates/slates_v2.fb", "slates/fi-reward_v2.fb", v2::RewardFunctionType_Sum,
      v2::LearningModeType_Online, v2::ProblemType_SLATES);

  BOOST_CHECK_EQUAL(rewards.size(), 1);
  BOOST_CHECK_EQUAL(rewards.at(0), 2 + 2 + 5 + 2);
}
BOOST_AUTO_TEST_SUITE_END()
