#pragma once

#include <string>
#include <vector>

namespace hmi_nexus::ui::widgets {

struct ChartSeries {
    std::string name;
    std::vector<int> points;
};

}  // namespace hmi_nexus::ui::widgets
