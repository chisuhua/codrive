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

#include <algorithm>
#include <deque>
#include <forward_list>
#include <iomanip>
#include <map>
#include <numeric>
#include <unordered_set>
#include "utils/util.h"
// #include "utils/log.h"

// #include "ngraph/coordinate_diff.hpp"
// #include "ngraph/function.hpp"
// #include "ngraph/graph_util.hpp"
// #include "ngraph/node.hpp"
// #include "ngraph/result_vector.hpp"
// #include "ngraph/runtime/backend.hpp"
// #include "ngraph/shape.hpp"

#include <iostream>

using namespace std;
// using namespace logger;
using namespace util;

std::string util::to_cplusplus_sourcecode_literal(bool val)
{
    return val ? "true" : "false";
}

void util::dump(ostream& out, const void* _data, size_t _size)
{
    auto flags = out.flags();
    const uint8_t* data = reinterpret_cast<const uint8_t*>(_data);
    size_t len = _size;
    size_t index = 0;
    while (index < len)
    {
        out << std::hex << std::setw(8) << std::setfill('0') << index;
        for (int i = 0; i < 8; i++)
        {
            if (index + i < len)
            {
                out << " " << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<uint32_t>(data[i]);
            }
            else
            {
                out << "   ";
            }
        }
        out << "  ";
        for (int i = 8; i < 16; i++)
        {
            if (index + i < len)
            {
                out << " " << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<uint32_t>(data[i]);
            }
            else
            {
                out << "   ";
            }
        }
        out << "  ";
        for (int i = 0; i < 16; i++)
        {
            char ch = (index + i < len ? data[i] : ' ');
            out << ((ch < 32) ? '.' : ch);
        }
        out << "\n";
        data += 16;
        index += 16;
    }
    out.flags(flags);
}

std::string util::to_lower(const std::string& s)
{
    std::string rc = s;
    std::transform(rc.begin(), rc.end(), rc.begin(), ::tolower);
    return rc;
}

std::string util::to_upper(const std::string& s)
{
    std::string rc = s;
    std::transform(rc.begin(), rc.end(), rc.begin(), ::toupper);
    return rc;
}

string util::trim(const string& s)
{
    string rc = s;
    // trim trailing spaces
    size_t pos = rc.find_last_not_of(" \t");
    if (string::npos != pos)
    {
        rc = rc.substr(0, pos + 1);
    }

    // trim leading spaces
    pos = rc.find_first_not_of(" \t");
    if (string::npos != pos)
    {
        rc = rc.substr(pos);
    }
    return rc;
}

vector<string> util::split(const string& src, char delimiter, bool do_trim)
{
    size_t pos;
    string token;
    size_t start = 0;
    vector<string> rc;
    while ((pos = src.find(delimiter, start)) != std::string::npos)
    {
        token = src.substr(start, pos - start);
        start = pos + 1;
        if (do_trim)
        {
            token = trim(token);
        }
        rc.push_back(token);
    }
    if (start <= src.size())
    {
        token = src.substr(start);
        if (do_trim)
        {
            token = trim(token);
        }
        rc.push_back(token);
    }
    return rc;
}

size_t util::hash_combine(const std::vector<size_t>& list)
{
    size_t seed = 0;
    for (size_t v : list)
    {
        seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}


size_t util::round_up(size_t size, size_t alignment)
{
    if (alignment == 0)
    {
        return size;
    }

    size_t remainder = size % alignment;
    if (remainder == 0)
    {
        return size;
    }

    return size + alignment - remainder;
}


size_t stopwatch::get_call_count() const
{
    return m_total_count;
}

size_t stopwatch::get_seconds() const
{
    return chrono::duration_cast<chrono::seconds>(get_timer_value()).count();
}

size_t stopwatch::get_milliseconds() const
{
    return chrono::duration_cast<chrono::milliseconds>(get_timer_value()).count();
}

size_t stopwatch::get_microseconds() const
{
    return chrono::duration_cast<chrono::microseconds>(get_timer_value()).count();
}

size_t stopwatch::get_nanoseconds() const
{
    return get_timer_value().count();
}

chrono::nanoseconds stopwatch::get_timer_value() const
{
    if (m_active)
    {
        return (m_clock.now() - m_start_time);
    }
    else
    {
        return m_last_time;
    }
}

size_t stopwatch::get_total_seconds() const
{
    return chrono::duration_cast<chrono::seconds>(m_total_time).count();
}

size_t stopwatch::get_total_milliseconds() const
{
    return chrono::duration_cast<chrono::milliseconds>(m_total_time).count();
}

size_t stopwatch::get_total_microseconds() const
{
    return chrono::duration_cast<chrono::microseconds>(m_total_time).count();
}

size_t stopwatch::get_total_nanoseconds() const
{
    return m_total_time.count();
}

namespace util
{
    template <>
    float parse_string<float>(const std::string& s)
    {
        const char* tmp = s.c_str();
        char* end;
        float result = strtof(tmp, &end);
        if (*end != 0)
        {
            throw std::runtime_error("Could not parse literal '" + s + "'");
        }
        return result;
    }

    template <>
    double parse_string<double>(const std::string& s)
    {
        const char* tmp = s.c_str();
        char* end;
        double result = strtod(tmp, &end);
        if (*end != 0)
        {
            throw std::runtime_error("Could not parse literal '" + s + "'");
        }
        return result;
    }
}

void util::check_fp_values_isinf(const char* name, const float* array, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (std::isinf(array[i]))
        {
            throw std::runtime_error("Discovered Inf in '" + string(name) + "'");
        }
    }
}

void util::check_fp_values_isinf(const char* name, const double* array, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (std::isinf(array[i]))
        {
            throw std::runtime_error("Discovered Inf in '" + string(name) + "'");
        }
    }
}

void util::check_fp_values_isnan(const char* name, const float* array, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (std::isnan(array[i]))
        {
            throw std::runtime_error("Discovered NaN in '" + string(name) + "'");
        }
    }
}

void util::check_fp_values_isnan(const char* name, const double* array, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (std::isnan(array[i]))
        {
            throw std::runtime_error("Discovered NaN in '" + string(name) + "'");
        }
    }
}

