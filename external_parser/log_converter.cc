#include "log_converter.h"

#include "date.h"

#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/reader.h>
#include <rapidjson/writer.h>

namespace log_converter
{
namespace rj = rapidjson;

void build_json(std::ofstream& outfile, joined_event::joined_event& je, VW::io::logger& logger)
{
  switch (je.interaction_metadata.payload_type)
  {
    case v2::PayloadType_CB:
      build_cb_json(outfile, je, logger);
      break;
    case v2::PayloadType_CCB:
      build_ccb_json(outfile, je, logger);
      break;
    case v2::PayloadType_CA:
      build_ca_json(outfile, je, logger);
      break;
    case v2::PayloadType_Slates:
      build_slates_json(outfile, je, logger);
      break;
    default:
      break;
  }
}

void build_cb_json(std::ofstream& outfile, joined_event::joined_event& je, VW::io::logger& logger)
{
  auto cb_je = reinterpret_cast<const joined_event::cb_joined_event*>(je.get_hold_of_typed_data());
  float cost = -1.f * cb_je->reward;
  float original_cost = -1.f * cb_je->original_reward;
  const auto& interaction_data = cb_je->interaction_data;
  const auto& probabilities = interaction_data.probabilities;
  const auto& actions = interaction_data.actions;
  try
  {
    rj::StringBuffer out_buffer;
    rj::Writer<rj::StringBuffer> writer(out_buffer);

    writer.StartObject();

    writer.Key("_label_cost", static_cast<rapidjson::SizeType>(strlen("_label_cost")), true);
    writer.Double(cost);

    float label_p = probabilities.size() > 0 ? probabilities[0] * je.interaction_metadata.pass_probability : 0.f;
    writer.Key("_label_probability", static_cast<rapidjson::SizeType>(strlen("_label_probability")), true);
    writer.Double(label_p);

    writer.Key("_label_Action", static_cast<rapidjson::SizeType>(strlen("_label_Action")), true);
    writer.Uint(actions[0]);

    writer.Key("_labelIndex", static_cast<rapidjson::SizeType>(strlen("_labelIndex")), true);
    writer.Uint(actions[0] - 1);

    bool skip_learn = !je.is_joined_event_learnable();
    if (skip_learn)
    {
      writer.Key("_skipLearn", static_cast<rapidjson::SizeType>(strlen("_skipLearn")), true);
      writer.Bool(skip_learn);
    }

    writer.Key("o", static_cast<rapidjson::SizeType>(strlen("o")), true);

    writer.StartArray();
    for (auto& o : je.outcome_events)
    {
      writer.StartObject();
      if (!o.action_taken)
      {
        writer.Key("v", static_cast<rapidjson::SizeType>(strlen("v")), true);
        writer.Double(o.value);
      }
      writer.Key("EventId", static_cast<rapidjson::SizeType>(strlen("EventId")), true);
      writer.String(o.metadata.event_id.c_str(), static_cast<rapidjson::SizeType>(o.metadata.event_id.length()), true);

      writer.Key("ActionTaken", static_cast<rapidjson::SizeType>(strlen("ActionTaken")), true);
      writer.Bool(o.action_taken);

      writer.EndObject();
    }

    writer.EndArray();

    std::string ts_str = date::format("%FT%TZ", date::floor<std::chrono::microseconds>(je.joined_event_timestamp));

    writer.Key("Timestamp", static_cast<rapidjson::SizeType>(strlen("Timestamp")), true);
    writer.String(ts_str.c_str(), static_cast<rapidjson::SizeType>(ts_str.length()), true);

    writer.Key("Version", static_cast<rapidjson::SizeType>(strlen("Version")), true);
    writer.String("1", static_cast<rapidjson::SizeType>(strlen("1")), true);

    writer.Key("EventId", static_cast<rapidjson::SizeType>(strlen("EventId")), true);
    writer.String(
        interaction_data.event_id.c_str(), static_cast<rapidjson::SizeType>(interaction_data.event_id.length()), true);

    writer.Key("a", static_cast<rapidjson::SizeType>(strlen("a")), true);
    writer.StartArray();

    for (auto& action_id : actions) { writer.Uint(action_id); }

    writer.EndArray();

    writer.Key("c");
    std::replace(je.context.begin(), je.context.end(), '\n', ' ');
    writer.RawValue(je.context.c_str(), je.context.length(), rj::kObjectType);

    writer.Key("p", static_cast<rapidjson::SizeType>(strlen("p")), true);
    writer.StartArray();
    for (auto& p : probabilities) { writer.Double(p); }

    writer.EndArray();

    writer.Key("VWState", static_cast<rapidjson::SizeType>(strlen("VWState")), true);
    writer.StartObject();
    writer.Key("m", static_cast<rapidjson::SizeType>(strlen("m")), true);
    writer.String(je.model_id.c_str(), static_cast<rapidjson::SizeType>(je.model_id.length()), true);
    writer.EndObject();

    if (interaction_data.probability_of_drop != 0.f)
    {
      writer.Key("pdrop");
      writer.Double(interaction_data.probability_of_drop);
    }

    writer.Key("_original_label_cost", static_cast<rapidjson::SizeType>(strlen("_original_label_cost")), true);
    writer.Double(original_cost);

    writer.EndObject();

    outfile << out_buffer.GetString() << std::endl;
  }
  catch (const std::exception& e)
  {
    logger.out_error(
        "convert event: [{}] from binary to json format failed: [{}].", interaction_data.event_id, e.what());
  }
}

void build_ccb_json(std::ofstream& outfile, joined_event::joined_event& je, VW::io::logger& logger)
{
  const std::string& event_id = je.interaction_metadata.event_id;

  auto ccb_joined_event = reinterpret_cast<const joined_event::ccb_joined_event*>(je.get_hold_of_typed_data());
  const auto& interaction_data = ccb_joined_event->multi_slot_interaction.interaction_data;
  const auto& outcomes_map = ccb_joined_event->outcomes_map;
  const auto& rewards = ccb_joined_event->rewards;
  const auto& original_rewards = ccb_joined_event->original_rewards;
  const auto& baseline_actions = ccb_joined_event->multi_slot_interaction.baseline_actions;

  bool skip_learn = !je.is_joined_event_learnable();

  try
  {
    rj::StringBuffer out_buffer;
    rj::Writer<rj::StringBuffer> writer(out_buffer);

    writer.StartObject();

    std::string ts_str = date::format("%FT%TZ", date::floor<std::chrono::microseconds>(je.joined_event_timestamp));

    writer.Key("Timestamp");
    writer.String(ts_str.c_str(), static_cast<rapidjson::SizeType>(ts_str.length()), true);

    if (skip_learn)
    {
      writer.Key("_skipLearn");
      writer.Bool(skip_learn);
    }

    writer.Key("Version");
    writer.String("1");

    if (!event_id.empty())
    {
      writer.Key("EventId");
      writer.String(event_id.c_str());
    }

    writer.Key("c");
    std::replace(je.context.begin(), je.context.end(), '\n', ' ');
    writer.RawValue(je.context.c_str(), je.context.length(), rj::kObjectType);

    writer.Key("_outcomes");
    writer.StartArray();
    for (int i = 0; i < static_cast<int>(interaction_data.size()); i++)
    {
      writer.StartObject();
      writer.Key("_label_cost");
      writer.Double(-1.f * rewards[i]);

      writer.Key("_id");  // slot id
      writer.String(interaction_data[i].event_id.c_str(),
          static_cast<rapidjson::SizeType>(interaction_data[i].event_id.length()), true);

      writer.Key("_a");
      writer.StartArray();
      for (auto& action_id : interaction_data[i].actions) { writer.Uint(action_id); }
      writer.EndArray();

      writer.Key("_p");
      writer.StartArray();

      for (auto& p : interaction_data[i].probabilities) { writer.Double(p); }
      writer.EndArray();

      if (outcomes_map.find(i) != outcomes_map.end())
      {
        writer.Key("_o");
        writer.StartArray();

        for (auto& o : outcomes_map.at(i))
        {
          writer.StartObject();

          if (!o.action_taken)
          {
            writer.Key("v");
            writer.Double(o.value);
          }

          writer.Key("EventId");
          writer.String(
              o.metadata.event_id.c_str(), static_cast<rapidjson::SizeType>(o.metadata.event_id.length()), true);

          writer.Key("Index");

          if (o.index_type == v2::IndexValue_literal)
          { writer.String(o.s_index.c_str(), static_cast<rapidjson::SizeType>(o.s_index.length()), true); }
          else
          {
            writer.String(std::to_string(o.index).c_str(),
                static_cast<rapidjson::SizeType>(std::to_string(o.index).length()), true);
          }

          writer.Key("ActionTaken");
          writer.Bool(o.action_taken);

          writer.EndObject();
        }

        writer.EndArray();
      }

      writer.Key("_original_label_cost");
      writer.Double(-1.f * original_rewards[i]);
      writer.EndObject();
    }
    writer.EndArray();

    if (baseline_actions.size() > 0)
    {
      writer.Key("_ba");
      writer.StartArray();
      for (auto& ba : baseline_actions) { writer.Uint(ba); }
      writer.EndArray();
    }

    writer.Key("VWState");
    writer.StartObject();
    writer.Key("m");
    writer.String(je.model_id.c_str(), static_cast<rapidjson::SizeType>(je.model_id.length()), true);
    writer.EndObject();

    if (ccb_joined_event->multi_slot_interaction.probability_of_drop != 0.f)
    {
      writer.Key("pdrop");
      writer.Double(ccb_joined_event->multi_slot_interaction.probability_of_drop);
    }

    writer.EndObject();
    outfile << out_buffer.GetString() << std::endl;
  }
  catch (const std::exception& e)
  {
    logger.out_error("convert event: [{}] from binary to json format failed: [{}].", event_id, e.what());
  }
}

void build_ca_json(std::ofstream& outfile, joined_event::joined_event& je, VW::io::logger& logger)
{
  auto ca_je = reinterpret_cast<const joined_event::ca_joined_event*>(je.get_hold_of_typed_data());
  float cost = -1.f * ca_je->reward;

  const auto& interaction_data = ca_je->interaction_data;
  try
  {
    rj::StringBuffer out_buffer;
    rj::Writer<rj::StringBuffer> writer(out_buffer);

    writer.StartObject();

    writer.Key("_label_ca", static_cast<rapidjson::SizeType>(strlen("_label_ca")), true);
    writer.StartObject();
    writer.Key("cost", static_cast<rapidjson::SizeType>(strlen("cost")), true);
    writer.Double(cost);
    writer.Key("pdf_value", static_cast<rapidjson::SizeType>(strlen("pdf_value")), true);
    writer.Double(interaction_data.pdf_value);
    writer.Key("action", static_cast<rapidjson::SizeType>(strlen("action")), true);
    writer.Double(interaction_data.action);
    writer.EndObject();

    std::string ts_str = date::format("%FT%TZ", date::floor<std::chrono::microseconds>(je.joined_event_timestamp));

    writer.Key("Timestamp", static_cast<rapidjson::SizeType>(strlen("Timestamp")), true);
    writer.String(ts_str.c_str(), static_cast<rapidjson::SizeType>(ts_str.length()), true);

    writer.Key("Version", static_cast<rapidjson::SizeType>(strlen("Version")), true);
    writer.String("1", static_cast<rapidjson::SizeType>(strlen("1")), true);

    writer.Key("EventId", static_cast<rapidjson::SizeType>(strlen("EventId")), true);
    writer.String(
        interaction_data.event_id.c_str(), static_cast<rapidjson::SizeType>(interaction_data.event_id.length()), true);

    writer.Key("c", static_cast<rapidjson::SizeType>(strlen("c")), true);
    std::replace(je.context.begin(), je.context.end(), '\n', ' ');
    writer.RawValue(je.context.c_str(), je.context.length(), rj::kObjectType);

    writer.Key("VWState", static_cast<rapidjson::SizeType>(strlen("VWState")), true);
    writer.StartObject();
    writer.Key("m", static_cast<rapidjson::SizeType>(strlen("m")), true);
    writer.String(je.model_id.c_str(), static_cast<rapidjson::SizeType>(je.model_id.length()), true);
    writer.EndObject();

    if (interaction_data.probability_of_drop != 0.f)
    {
      writer.Key("pdrop", static_cast<rapidjson::SizeType>(strlen("pdrop")), true);
      writer.Double(interaction_data.probability_of_drop);
    }

    bool skip_learn = !je.is_joined_event_learnable();
    if (skip_learn)
    {
      writer.Key("_skipLearn", static_cast<rapidjson::SizeType>(strlen("_skipLearn")), true);
      writer.Bool(skip_learn);
    }

    writer.EndObject();

    outfile << out_buffer.GetString() << std::endl;
  }
  catch (const std::exception& e)
  {
    logger.out_error(
        "convert event: [{}] from binary to json format failed: [{}].", interaction_data.event_id, e.what());
  }
}

void build_slates_json(std::ofstream& outfile, joined_event::joined_event& je, VW::io::logger& logger)
{
  const std::string& event_id = je.interaction_metadata.event_id;

  auto slates_je = reinterpret_cast<const joined_event::slates_joined_event*>(je.get_hold_of_typed_data());
  const auto& interaction_data = slates_je->multi_slot_interaction.interaction_data;
  float cost = -1.f * slates_je->reward;
  bool skip_learn = !je.is_joined_event_learnable();

  try
  {
    rj::StringBuffer out_buffer;
    rj::Writer<rj::StringBuffer> writer(out_buffer);

    writer.StartObject();

    std::string ts_str = date::format("%FT%TZ", date::floor<std::chrono::microseconds>(je.joined_event_timestamp));

    writer.Key("Timestamp");
    writer.String(ts_str.c_str(), static_cast<rapidjson::SizeType>(ts_str.length()), true);

    writer.Key("Version");
    writer.String("1");

    if (!event_id.empty())
    {
      writer.Key("EventId");
      writer.String(event_id.c_str());
    }

    writer.Key("_label_cost", static_cast<rapidjson::SizeType>(strlen("_label_cost")), true);
    writer.Double(cost);

    writer.Key("o", static_cast<rapidjson::SizeType>(strlen("o")), true);

    writer.StartArray();
    for (auto& o : je.outcome_events)
    {
      writer.StartObject();
      if (!o.action_taken)
      {
        writer.Key("v", static_cast<rapidjson::SizeType>(strlen("v")), true);
        writer.Double(o.value);
      }
      writer.Key("EventId", static_cast<rapidjson::SizeType>(strlen("EventId")), true);
      writer.String(o.metadata.event_id.c_str(), static_cast<rapidjson::SizeType>(o.metadata.event_id.length()), true);

      writer.Key("ActionTaken", static_cast<rapidjson::SizeType>(strlen("ActionTaken")), true);
      writer.Bool(o.action_taken);

      writer.EndObject();
    }

    writer.EndArray();

    writer.Key("_outcomes", static_cast<rapidjson::SizeType>(strlen("_outcomes")), true);

    writer.StartArray();
    for (auto& interaction : interaction_data)
    {
      writer.StartObject();

      writer.Key("_a");
      writer.StartArray();
      for (auto& action_id : interaction.actions) { writer.Uint(action_id); }
      writer.EndArray();

      writer.Key("_p");
      writer.StartArray();

      for (auto& p : interaction.probabilities) { writer.Double(p); }
      writer.EndArray();
      writer.EndObject();
    }

    writer.EndArray();

    writer.Key("c");
    std::replace(je.context.begin(), je.context.end(), '\n', ' ');
    writer.RawValue(je.context.c_str(), je.context.length(), rj::kObjectType);

    writer.Key("VWState");
    writer.StartObject();
    writer.Key("m");
    writer.String(je.model_id.c_str(), static_cast<rapidjson::SizeType>(je.model_id.length()), true);
    writer.EndObject();

    if (slates_je->multi_slot_interaction.probability_of_drop != 0.f)
    {
      writer.Key("pdrop");
      writer.Double(slates_je->multi_slot_interaction.probability_of_drop);
    }

    if (skip_learn)
    {
      writer.Key("_skipLearn", static_cast<rapidjson::SizeType>(strlen("_skipLearn")), true);
      writer.Bool(skip_learn);
    }

    writer.EndObject();
    outfile << out_buffer.GetString() << std::endl;
  }
  catch (const std::exception& e)
  {
    logger.out_error("convert event: [{}] from binary to json format failed: [{}].", event_id, e.what());
  }
}
}  // namespace log_converter
