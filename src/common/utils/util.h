//*****************************************************************************
// Copyright 2017-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#pragma once

#include <chrono>
#include <cmath>
#include <cstdlib> // llvm 8.1 gets confused about `malloc` otherwise
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

// #include "ngraph/axis_vector.hpp"
// #include "ngraph/node_vector.hpp"
// #include "ngraph/shape.hpp"

namespace util {
class Node;
class Function;
class NodeMap;
class stopwatch;

namespace runtime {
    class Backend;
    class Value;
}

std::string to_cplusplus_sourcecode_literal(bool val);

template <typename T>
std::string join(const T& v, const std::string& sep = ", ")
{
    std::ostringstream ss;
    size_t count = 0;
    for (const auto& x : v) {
        if (count++ > 0) {
            ss << sep;
        }
        ss << x;
    }
    return ss.str();
}

template <typename T>
std::string vector_to_string(const T& v)
{
    std::ostringstream os;
    os << "[ " << util::join(v) << " ]";
    return os.str();
}

size_t hash_combine(const std::vector<size_t>& list);
void dump(std::ostream& out, const void*, size_t);

std::string to_lower(const std::string& s);
std::string to_upper(const std::string& s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char delimiter, bool trim = false);
template <typename T>
std::string locale_string(T x)
{
    std::stringstream ss;
    ss.imbue(std::locale(""));
    ss << x;
    return ss.str();
}

class stopwatch {
public:
    void start()
    {
        if (m_active == false) {
            m_total_count++;
            m_active = true;
            m_start_time = m_clock.now();
        }
    }

    void stop()
    {
        if (m_active == true) {
            auto end_time = m_clock.now();
            m_last_time = end_time - m_start_time;
            m_total_time += m_last_time;
            m_active = false;
        }
    }

    size_t get_call_count() const;
    size_t get_seconds() const;
    size_t get_milliseconds() const;
    size_t get_microseconds() const;
    std::chrono::nanoseconds get_timer_value() const;
    size_t get_nanoseconds() const;

    size_t get_total_seconds() const;
    size_t get_total_milliseconds() const;
    size_t get_total_microseconds() const;
    size_t get_total_nanoseconds() const;

private:
    std::chrono::high_resolution_clock m_clock;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time;
    bool m_active = false;
    std::chrono::nanoseconds m_total_time = std::chrono::high_resolution_clock::duration::zero();
    std::chrono::nanoseconds m_last_time = std::chrono::high_resolution_clock::duration::zero();
    size_t m_total_count = 0;
};

/// Parses a string containing a literal of the underlying type.
template <typename T>
T parse_string(const std::string& s)
{
    T result;
    std::stringstream ss;

    ss << s;
    ss >> result;

    // Check that (1) parsing succeeded and (2) the entire string was used.
    if (ss.fail() || ss.rdbuf()->in_avail() != 0) {
        throw std::runtime_error("Could not parse literal '" + s + "'");
    }

    return result;
}

/// template specializations for float and double to handle INFINITY, -INFINITY
/// and NaN values.
template <>
float parse_string<float>(const std::string& s);
template <>
double parse_string<double>(const std::string& s);

/// Parses a list of strings containing literals of the underlying type.
template <typename T>
std::vector<T> parse_string(const std::vector<std::string>& ss)
{
    std::vector<T> result;

    for (auto s : ss) {
        result.push_back(parse_string<T>(s));
    }

    return result;
}

template <typename T>
T ceil_div(const T& x, const T& y)
{
    return (x == 0 ? 0 : (1 + (x - 1) / y));
}

template <typename T>
T subtract_or_zero(T x, T y)
{
    return y > x ? 0 : x - y;
}

void check_fp_values_isinf(const char* name, const float* array, size_t n);
void check_fp_values_isinf(const char* name, const double* array, size_t n);
void check_fp_values_isnan(const char* name, const float* array, size_t n);
void check_fp_values_isnan(const char* name, const double* array, size_t n);

size_t round_up(size_t size, size_t alignment);



/**
     * EnumMask is intended to work with a scoped enum type. It's used to store
     * a combination of enum values and provides easy access and manipulation
     * of these enum values as a mask.
     *
     * EnumMask does not provide a set_all() or invert() operator because they
     * could do things unexpected by the user, i.e. for enum with 4 bit values,
     * invert(001000...) != 110100..., due to the extra bits.
     */
template <typename T>
class EnumMask {
public:
    /// Make sure the template type is an enum.
    static_assert(std::is_enum<T>::value, "EnumMask template type must be an enum");
    /// Extract the underlying type of the enum.
    typedef typename std::underlying_type<T>::type value_type;
    /// Some bit operations are not safe for signed values, we require enum
    /// type to use unsigned underlying type.
    static_assert(std::is_unsigned<value_type>::value, "EnumMask enum must use unsigned type.");

    EnumMask()
        : m_value { 0 }
    {
    }
    EnumMask(const T& enum_value)
        : m_value { static_cast<value_type>(enum_value) }
    {
    }
    EnumMask(const EnumMask& other)
        : m_value { other.m_value }
    {
    }
    EnumMask(std::initializer_list<T> enum_values)
        : m_value { 0 }
    {
        for (auto& v : enum_values) {
            m_value |= static_cast<value_type>(v);
        }
    }
    value_type value() const { return m_value; }
    /// Check if any of the enum bit mask match
    bool is_any_set(const EnumMask& p) const { return m_value & p.m_value; }
    /// Check if all of the enum bit mask match
    bool is_set(const EnumMask& p) const { return (m_value & p.m_value) == p.m_value; }
    /// Check if any of the enum bit mask does not match
    bool is_any_clear(const EnumMask& p) const { return !is_set(p); }
    /// Check if all of the enum bit mask do not match
    bool is_clear(const EnumMask& p) const { return !is_any_set(p); }
    void set(const EnumMask& p) { m_value |= p.m_value; }
    void clear(const EnumMask& p) { m_value &= ~p.m_value; }
    void clear_all() { m_value = 0; }
    bool operator[](const EnumMask& p) const { return is_set(p); }
    bool operator==(const EnumMask& other) const { return m_value == other.m_value; }
    bool operator!=(const EnumMask& other) const { return m_value != other.m_value; }
    EnumMask& operator=(const EnumMask& other)
    {
        m_value = other.m_value;
        return *this;
    }
    EnumMask& operator&=(const EnumMask& other)
    {
        m_value &= other.m_value;
        return *this;
    }

    EnumMask& operator|=(const EnumMask& other)
    {
        m_value |= other.m_value;
        return *this;
    }

    EnumMask operator&(const EnumMask& other) const
    {
        return EnumMask(m_value & other.m_value);
    }

    EnumMask operator|(const EnumMask& other) const
    {
        return EnumMask(m_value | other.m_value);
    }

    friend std::ostream& operator<<(std::ostream& os, const EnumMask& m)
    {
        os << m.m_value;
        return os;
    }

private:
    /// Only used internally
    explicit EnumMask(const value_type& value)
        : m_value { value }
    {
    }

    value_type m_value;
};
} // end namespace util

