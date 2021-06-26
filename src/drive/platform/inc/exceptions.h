#pragma once

#include <exception>
#include <string>

// schi #include "core/inc/hsa_internal.h"

namespace hcs {

/// @brief Exception type which carries an error code to return to the user.
class hsa_exception : public std::exception {
 public:
  hsa_exception(status_t error, const char* description) : err_(error), desc_(description) {}
  status_t error_code() const noexcept { return err_; }
  const char* what() const noexcept override { return desc_.c_str(); }

 private:
  status_t err_;
  std::string desc_;
};

/// @brief Holds and invokes callbacks, capturing any execptions and forwarding those to the user
/// after unwinding the runtime stack.
template <class F> class callback_t;
template <class R, class... Args> class callback_t<R (*)(Args...)> {
 public:
  typedef R (*func_t)(Args...);

  callback_t() : function(nullptr) {}

  // Should not be marked explicit.
  callback_t(func_t function_ptr) : function(function_ptr) {}
  callback_t& operator=(func_t function_ptr) { function = function_ptr; return *this; }

  bool operator==(func_t function_ptr) { return function == function_ptr; }
  bool operator!=(func_t function_ptr) { return function != function_ptr; }

  // Allows common function pointer idioms, such as if( func != nullptr )...
  // without allowing silent reversion to the original function pointer type.
  operator void*() { return reinterpret_cast<void*>(function); }

  R operator()(Args... args) {
    try {
      return function(args...);
    } catch (...) {
      throw std::nested_exception();
      return R();
    }
  }

 private:
  func_t function;
};

}  // namespace hcs
