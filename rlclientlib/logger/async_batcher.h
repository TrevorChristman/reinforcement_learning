#pragma once

#include "api_status.h"
#include "constants.h"
#include "data_buffer.h"
#include "err_constants.h"
#include "error_callback_fn.h"
#include "event_queue.h"
#include "message_sender.h"
#include "rl_string_view.h"
#include "serialization/fb_serializer.h"
#include "serialization/json_serializer.h"
#include "utility/config_helper.h"
#include "utility/object_pool.h"
#include "utility/periodic_background_proc.h"
#include "vw/common/hash.h"
#include "vw/explore/explore.h"

// float comparisons
#include "vw/core/vw_math.h"

#include <functional>

namespace reinforcement_learning
{
class error_callback_fn;
};

namespace reinforcement_learning
{
namespace logger
{
template <typename TEvent>
class i_async_batcher
{
public:
  using TFunc = std::function<int(TEvent&, api_status*)>;
  virtual ~i_async_batcher() = default;

  virtual int init(api_status* status) = 0;

  virtual int append(TFunc&& func, TEvent* event, api_status* status = nullptr) = 0;
  virtual int append(TFunc& func, TEvent* event, api_status* status = nullptr) = 0;

  virtual int run_iteration(api_status* status) = 0;
};

// This class takes uses a queue and a background thread to accumulate events, and send them by batch asynchronously.
// A batch is shipped with TSender::send(data)
// TFunc : a function with the prototype int f(Event& evt, api_status* status)
//       return value: Error code
//       evt    : Event type out-param
//       status : Optional status object
template <typename TEvent, template <typename> class TSerializer = json_collection_serializer>
class async_batcher : public i_async_batcher<TEvent>
{
public:
  using shared_state_t = typename TSerializer<TEvent>::shared_state_t;
  using TFunc = std::function<int(TEvent&, api_status*)>;

  int init(api_status* status) override;

  int append(TFunc&& func, TEvent* event, api_status* status = nullptr) override;
  int append(TFunc& func, TEvent* event, api_status* status = nullptr) override;

  int run_iteration(api_status* status) override;

private:
  int fill_buffer(std::shared_ptr<utility::data_buffer>& retbuffer, size_t& remaining, api_status* status);

  void flush();  // flush all batches

public:
  async_batcher(i_message_sender* sender, utility::watchdog& watchdog, shared_state_t& shared_state,
      error_callback_fn* perror_cb, const utility::async_batcher_config& config);
  ~async_batcher();

private:
  std::unique_ptr<i_message_sender> _sender;

  event_queue<TEvent> _queue;  // A queue to accumulate batch of events.
  size_t _send_high_water_mark;
  error_callback_fn* _perror_cb;
  shared_state_t& _shared_state;

  utility::periodic_background_proc<async_batcher> _periodic_background_proc;
  float _pass_prob;
  queue_mode_enum _queue_mode;
  std::condition_variable _cv;
  std::mutex _m;
  utility::object_pool<utility::data_buffer> _buffer_pool;
  const char* _batch_content_encoding;
  float _subsample_rate;
  events_counter_status _events_counter_status;
  uint64_t _buffer_end_event_index = 0;
};

template <typename TEvent, template <typename> class TSerializer>
int async_batcher<TEvent, TSerializer>::init(api_status* status)
{
  RETURN_IF_FAIL(_periodic_background_proc.init(this, status));
  bool subsample_lte_zero = _subsample_rate < 0.f || VW::math::are_same(_subsample_rate, 0.f);
  bool subsample_gt_one = _subsample_rate > 1.f && !VW::math::are_same(_subsample_rate, 1.f);
  if (subsample_lte_zero || subsample_gt_one)
  {
    // invalid subsample rate
    RETURN_ERROR_ARG(nullptr, status, invalid_argument, "subsampling rate must be within (0, 1]");
  }
  return error_code::success;
}

template <typename TEvent, template <typename> class TSerializer>
int async_batcher<TEvent, TSerializer>::append(TFunc&& func, TEvent* event, api_status* status)
{
  // If subsampling rate is < 1, then run subsampling logic
  if (_subsample_rate < 1.f)
  {
    if (event->try_drop(_subsample_rate, constants::SUBSAMPLE_RATE_DROP_PASS))
    {
      // If the event is dropped, just get out of here
      return error_code::success;
    }
  }

  _queue.push(std::move(func), TSerializer<TEvent>::serializer_t::size_estimate(*event), event);

  // block or drop events if the queue if full
  if (_queue.is_full())
  {
    if (queue_mode_enum::BLOCK == _queue_mode)
    {
      std::unique_lock<std::mutex> lk(_m);
      _cv.wait(lk, [this] { return !_queue.is_full(); });
    }
    else if (queue_mode_enum::DROP == _queue_mode)
    {
      _queue.prune(_pass_prob);
    }
  }

  return error_code::success;
}

template <typename TEvent, template <typename> class TSerializer>
int async_batcher<TEvent, TSerializer>::append(TFunc& func, TEvent* event, api_status* status)
{
  return append(std::move(func), event, status);
}

template <typename TEvent, template <typename> class TSerializer>
int async_batcher<TEvent, TSerializer>::run_iteration(api_status* status)
{
  flush();
  return error_code::success;
}

template <typename TEvent, template <typename> class TSerializer>
int async_batcher<TEvent, TSerializer>::fill_buffer(
    std::shared_ptr<utility::data_buffer>& buffer, size_t& remaining, api_status* status)
{
  TFunc f_evt;
  TEvent evt;
  TSerializer<TEvent> collection_serializer(*buffer.get(), _batch_content_encoding, _shared_state);

  while (remaining > 0 && collection_serializer.size() < _send_high_water_mark)
  {
    if (_queue.pop(&f_evt))
    {
      if (queue_mode_enum::BLOCK == _queue_mode) { _cv.notify_one(); }
      RETURN_IF_FAIL(f_evt(evt, status));
      RETURN_IF_FAIL(collection_serializer.add(evt, status));
      --remaining;
    }
  }

  if (_events_counter_status == events_counter_status::ENABLE)
  {
    uint64_t buffer_start_event_index = _buffer_end_event_index;
    _buffer_end_event_index = evt.get_event_index();
    uint64_t original_event_count = (_buffer_end_event_index - buffer_start_event_index);
    RETURN_IF_FAIL(collection_serializer.finalize(status, original_event_count));
  }
  else
  {
    RETURN_IF_FAIL(collection_serializer.finalize(status));
  }
  return error_code::success;
}

template <typename TEvent, template <typename> class TSerializer>
void async_batcher<TEvent, TSerializer>::flush()
{
  const auto queue_size = _queue.size();

  // Early exit if queue is empty.
  if (queue_size == 0) { return; }

  auto remaining = queue_size;
  // Handle batching
  while (remaining > 0)
  {
    api_status status;

    auto buffer = _buffer_pool.acquire();
    if (fill_buffer(buffer, remaining, &status) != error_code::success) { ERROR_CALLBACK(_perror_cb, status); }
    if (_sender->send(TSerializer<TEvent>::message_id(), buffer, &status) != error_code::success)
    { ERROR_CALLBACK(_perror_cb, status); }
  }
}

template <typename TEvent, template <typename> class TSerializer>
async_batcher<TEvent, TSerializer>::async_batcher(i_message_sender* sender, utility::watchdog& watchdog,
    typename TSerializer<TEvent>::shared_state_t& shared_state, error_callback_fn* perror_cb,
    const utility::async_batcher_config& config)
    : _sender(sender)
    , _queue(config.send_queue_max_capacity, config.event_counter_status, config.subsample_rate)
    , _send_high_water_mark(config.send_high_water_mark)
    , _perror_cb(perror_cb)
    , _shared_state(shared_state)
    , _periodic_background_proc(
          static_cast<int>(config.send_batch_interval_ms), watchdog, "Async batcher thread", perror_cb)
    , _pass_prob(0.5)
    , _queue_mode(config.queue_mode)
    , _batch_content_encoding(config.batch_content_encoding)
    , _subsample_rate(config.subsample_rate)
    , _events_counter_status(config.event_counter_status)
{
}

template <typename TEvent, template <typename> class TSerializer>
async_batcher<TEvent, TSerializer>::~async_batcher()
{
  // Stop the background procedure the queue before exiting
  _periodic_background_proc.stop();
  if (_queue.size() > 0) { flush(); }
}
}  // namespace logger
}  // namespace reinforcement_learning
