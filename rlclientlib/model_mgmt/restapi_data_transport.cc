#include "restapi_data_transport.h"

#include "api_status.h"
#include "factory_resolver.h"
#include "trace_logger.h"
#include "utility/header_authorization.h"

#include <cpprest/asyncrt_utils.h>
#include <cpprest/http_client.h>
#include <cpprest/rawptrstream.h>

#include <utility>

using namespace web;        // Common features like URIs.
using namespace web::http;  // Common HTTP functionality
using namespace std::chrono;

namespace u = reinforcement_learning::utility;
namespace e = reinforcement_learning::error_code;

namespace reinforcement_learning
{
namespace model_management
{
restapi_data_transport::restapi_data_transport(i_http_client* httpcli, i_trace* trace)
    : _httpcli(httpcli), _datasz{0}, _trace{trace}
{
}
restapi_data_transport::restapi_data_transport(
    std::unique_ptr<i_http_client>&& httpcli, utility::configuration cfg, model_source model_source, i_trace* trace)
    : _httpcli(std::move(httpcli)), _cfg(std::move(cfg)), _model_source(model_source), _datasz{0}, _trace{trace}
{
}

/*
 * Example successful response
 *
 * Received response status code:200
 * Accept-Ranges = bytes
 * Content-Length = 7666
 * Content-MD5 = VuJg8VgcBQwevGhJR2Yehw==
 * Content-Type = application/octet-stream
 * Date = Mon, 28 May 2018 14:41:02 GMT
 * ETag = "0x8D5C03A2AEC2189"
 * Last-Modified = Tue, 22 May 2018 23:17:20 GMT
 * Server = Windows-Azure-Blob/1.0 Microsoft-HTTPAPI/2.0
 * x-ms-blob-type = BlockBlob
 * x-ms-lease-state = available
 * x-ms-lease-status = unlocked
 * x-ms-request-id = 241f3513-801e-0041-0991-f6893e000000
 * x-ms-server-encrypted = true
 * x-ms-version = 2017-04-17
 */

int restapi_data_transport::get_data_info(
    ::utility::datetime& last_modified, ::utility::size64_t& sz, api_status* status)
{
  // Get request URI and start the request.
  http_request request(_method_type);
  RETURN_IF_FAIL(add_authentiction_header(request.headers(), status));
  // Build request URI and start the request.
  auto request_task = _httpcli->request(request).then([&](http_response response) {
    if (response.status_code() != 200)
    {
      // if the call using HEAD fails, try with GET only once and return the results of GET request call
      if (_retry_get_data)
      {
        _retry_get_data = false;
        _method_type = methods::GET;
        RETURN_IF_FAIL(get_data_info(last_modified, sz, status));
        return error_code::success;
      }

      RETURN_ERROR_ARG(_trace, status, http_bad_status_code, "Found: ", response.status_code(), _httpcli->get_url());
    }
    const auto iter = response.headers().find(U("Last-Modified"));
    if (iter == response.headers().end())
    { RETURN_ERROR_ARG(_trace, status, last_modified_not_found, _httpcli->get_url()); }

    last_modified = ::utility::datetime::from_string(iter->second);
    if (last_modified.to_interval() == 0)
    { RETURN_ERROR_ARG(_trace, status, last_modified_invalid, _httpcli->get_url()); }

    sz = response.headers().content_length();

    return error_code::success;
  });

  // Wait for all the outstanding I/O to complete and handle any exceptions
  try
  {
    return request_task.get();
  }
  catch (const std::exception& e)
  {
    RETURN_ERROR_LS(_trace, status, exception_during_http_req) << e.what() << "\n URL: " << _httpcli->get_url();
  }
}

int restapi_data_transport::add_authentiction_header(http_headers& header, api_status* status)
{
  if (_model_source != model_source::AZURE)
  {
    RETURN_IF_FAIL(_headerimpl->init(_cfg, status, _trace));
    RETURN_IF_FAIL(_headerimpl->insert_authorization_header(header, status, _trace));
  }
  return error_code::success;
}

int restapi_data_transport::get_data(model_data& ret, api_status* status)
{
  ::utility::datetime curr_last_modified;
  ::utility::size64_t curr_datasz = 0;
  _method_type = methods::HEAD;
  _retry_get_data = true;
  RETURN_IF_FAIL(get_data_info(curr_last_modified, curr_datasz, status));

  if (curr_last_modified == _last_modified && curr_datasz == _datasz) { return error_code::success; }
  _method_type = methods::GET;
  http_request request(_method_type);
  RETURN_IF_FAIL(add_authentiction_header(request.headers(), status));
  // Build request URI and start the request.
  auto request_task =
      _httpcli
          ->request(request)
          // Handle response headers arriving.
          .then([&](const pplx::task<http_response>& resp_task) {
            auto response = resp_task.get();
            if (response.status_code() != 200)
            {
              RETURN_ERROR_ARG(
                  _trace, status, http_bad_status_code, "Found: ", response.status_code(), _httpcli->get_url());
            }

            const auto iter = response.headers().find(U("Last-Modified"));
            if (iter == response.headers().end())
            { RETURN_ERROR_ARG(_trace, status, last_modified_not_found, _httpcli->get_url()); }

            curr_last_modified = ::utility::datetime::from_string(iter->second);
            if (curr_last_modified.to_interval() == 0)
            {
              RETURN_ERROR_ARG(_trace, status, last_modified_invalid,
                  "Found: ", ::utility::conversions::to_utf8string(curr_last_modified.to_string()),
                  _httpcli->get_url());
            }

            curr_datasz = response.headers().content_length();
            if (curr_datasz > 0)
            {
              auto* const buff = ret.alloc(curr_datasz);
              const Concurrency::streams::rawptr_buffer<char> rb(buff, curr_datasz, std::ios::out);

              // Write response body into the file.
              const auto readval =
                  response.body().read_to_end(rb).get();  // need to use task.get to throw exceptions properly

              ret.data_sz(readval);
              ret.increment_refresh_count();
              _datasz = readval;
            }
            else
            {
              ret.data_sz(0);
            }

            _last_modified = curr_last_modified;
            return error_code::success;
          });

  // Wait for all the outstanding I/O to complete and handle any exceptions
  try
  {
    request_task.wait();
  }
  catch (const std::exception& e)
  {
    ret.free();
    RETURN_ERROR_LS(_trace, status, exception_during_http_req) << e.what();
  }
  catch (...)
  {
    ret.free();
    RETURN_ERROR_LS(_trace, status, exception_during_http_req) << error_code::unknown_s;
  }

  return request_task.get();
}
}  // namespace model_management
}  // namespace reinforcement_learning
