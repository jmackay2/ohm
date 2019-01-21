// Copyright (c) 2017
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "gpuApiException.h"

#include <sstream>
#include <utility>

using namespace gputil;

#define DEFAULT_MSG "Attempting write access to read only memory."

ApiException::ApiException(int error_code, const char *msg)
  : Exception(msg)
  , error_code_(error_code)
{
  if (!msg)
  {
    std::ostringstream str;
    str << "API error " << errorCodeString(error_code) << " (" << error_code << ")";
    setMessage(str.str().c_str());
  }
}


ApiException::ApiException(ApiException &&other) noexcept
  : Exception(std::move(other))
  , error_code_(other.error_code_)
{}
