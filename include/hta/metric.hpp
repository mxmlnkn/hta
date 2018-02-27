#pragma once

#include <hta/types.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace hta
{
namespace storage
{
    class Metric;
}

class Level;

class BaseMetric
{
protected:
    BaseMetric();
    BaseMetric(std::unique_ptr<storage::Metric> storage_metric);
    ~BaseMetric();

protected:
    // TODO make configurable
    Duration interval_min_ = duration_cast(std::chrono::seconds(10));
    uint64_t interval_factor_ = 10;
    std::unique_ptr<storage::Metric> storage_metric_;
};

class ReadMetric : protected virtual BaseMetric
{
public:
    ReadMetric(std::unique_ptr<storage::Metric> storage_metric);

protected:
    ReadMetric();

public:
    std::vector<TimeAggregate> retrieve(TimePoint begin, TimePoint end, uint64_t min_samples,
                                        IntervalScope scope = IntervalScope{ Scope::extended,
                                                                             Scope::open });
    std::vector<TimeAggregate> retrieve(TimePoint begin, TimePoint end, Duration interval_max,
                                        IntervalScope scope = IntervalScope{ Scope::extended,
                                                                             Scope::open });
    std::vector<TimeValue> retrieve(TimePoint begin, TimePoint end,
                                    IntervalScope scope = { Scope::closed, Scope::extended });
    std::pair<TimePoint, TimePoint> range();

private:
    std::vector<TimeAggregate> retrieve_raw_time_aggregate(TimePoint begin, TimePoint end,
                                                           IntervalScope scope = IntervalScope{
                                                               Scope::closed, Scope::extended });
};

class WriteMetric : protected virtual BaseMetric
{
public:
    WriteMetric(std::unique_ptr<storage::Metric> storage_metric);

protected:
    WriteMetric();
    ~WriteMetric();

public:
    void insert(TimeValue tv);

private:
    void insert(Row row);
    Level& get_level(Duration interval);
    Level restore_level(Duration interval);

    std::map<Duration, Level> levels_;
};

class ReadWriteMetric : public ReadMetric, public WriteMetric
{
public:
    ReadWriteMetric(std::unique_ptr<storage::Metric> storage_metric);
};
}
