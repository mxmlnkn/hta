// Copyright (c) 2018, ZIH,
// Technische Universitaet Dresden,
// Federal Republic of Germany
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//     * Neither the name of metricq nor the names of its contributors
//       may be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <hta/exception.hpp>
#include <hta/meta.hpp>
#include <hta/types.hpp>

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

namespace hta
{
using json = nlohmann::json;

namespace storage
{
    class Metric;
    class Directory;
} // namespace storage

class Level;

/**
 * This is a messy diamond of death
 *         BaseMetric
 *        /          \
 * ReadMetric      WriteMetric
 *   .   \           /   .
 *   .  ReadWriteMetric  .
 *   .         .         .
 * variant<Read,Write,ReadWrite>
 *         unique_ptr
 *       VariantMetric
 *
 * VariantMetric such that we can store one thing in the Directory
 * it must be movable because otherwise we cannot efficiently lock
 * in-place-map construction
 *
 * Some time we really need to clean this up
 * Either just scrap the concept, or make fully virtual interfaces and stuff.
 */

class BaseMetric
{
protected:
    BaseMetric();
    explicit BaseMetric(std::unique_ptr<storage::Metric> storage_metric);
    virtual ~BaseMetric();

    // These classes should not be visible outside
    class IntervalFactor
    {
    public:
        IntervalFactor()
        {
            // needed for linking only
            throw_exception("uninitialized IntervalFactor");
        }

        explicit IntervalFactor(int64_t factor) : factor_(factor)
        {
        }

        Duration operator*(Duration duration) const
        {
            Duration::rep result;
            if (__builtin_mul_overflow(factor_, duration.count(), &result))
            {
                throw_exception("integer overflow during interval multiplication");
            }
            return Duration(result);
        }

        Duration divide_by(Duration duration) const
        {
            auto result = duration / factor_;
            if (result.count() == 0)
            {
                throw_exception("interval division yields 0");
            }
            return Duration(result);
        }

        friend Duration operator*(Duration duration, IntervalFactor factor)
        {
            return factor * duration;
        }

        friend Duration operator/(Duration duration, IntervalFactor factor)
        {
            return factor.divide_by(duration);
        }

    private:
        int64_t factor_;
    };

public:
    const Meta meta() const;

protected:
    std::unique_ptr<storage::Metric> storage_metric_;

    Duration interval_min_;
    Duration interval_max_;
    IntervalFactor interval_factor_;

    TimePoint previous_time_;
};

class ReadMetric : public virtual BaseMetric
{
public:
    explicit ReadMetric(std::unique_ptr<storage::Metric> storage_metric);

protected:
    ReadMetric();

public:
    std::vector<Row> retrieve(TimePoint begin, TimePoint end, uint64_t min_samples,
                              IntervalScope scope = IntervalScope{ Scope::extended, Scope::open });
    std::vector<Row> retrieve(TimePoint begin, TimePoint end, Duration interval_max,
                              IntervalScope scope = IntervalScope{ Scope::extended, Scope::open });
    std::vector<TimeValue> retrieve(TimePoint begin, TimePoint end,
                                    IntervalScope scope = { Scope::closed, Scope::extended });
    size_t count(TimePoint begin, TimePoint end,
                 IntervalScope scope = { Scope::closed, Scope::extended });
    size_t count();
    std::pair<TimePoint, TimePoint> range();

private:
    std::vector<Row> retrieve_raw_row(TimePoint begin, TimePoint end,
                                      IntervalScope scope = IntervalScope{ Scope::closed,
                                                                           Scope::extended });
};

class WriteMetric : public virtual BaseMetric
{
public:
    explicit WriteMetric(std::unique_ptr<storage::Metric> storage_metric);
    ~WriteMetric();

protected:
    WriteMetric();

public:
    void insert(TimeValue tv);
    void flush();

private:
    void insert(Row row);
    Level& get_level(Duration interval);
    Level restore_level(Duration interval);

    std::map<Duration, Level> levels_;
};

class ReadWriteMetric : public ReadMetric, public WriteMetric
{
public:
    explicit ReadWriteMetric(std::unique_ptr<storage::Metric> storage_metric);
};

class VariantMetric
{
public:
    using Variant = std::variant<ReadMetric, WriteMetric, ReadWriteMetric>;

    VariantMetric(const std::string& name, const hta::json& config, storage::Directory& storage);

    template <class M>
    M* get()
    {
        return std::visit(
            [](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (!std::is_convertible_v<T*, M*>)
                {
                    throw Exception("Invalid metric type (read/write) conversion.");
                }
                return (M*)(&arg);
            },
            *metric_);
    }

private:
    std::unique_ptr<Variant> metric_;
};

static_assert(std::is_move_constructible_v<VariantMetric>, "VariantMetric is not movable.");
} // namespace hta
